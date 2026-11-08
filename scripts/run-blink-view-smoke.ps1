[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [ValidateRange(1,1000)][int]$FixtureFiles=600)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)
$OutputEncoding=[Console]::OutputEncoding
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Ordinary user required'}
$project=Split-Path -Parent $PSScriptRoot
$runId=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+$PID
$runRoot=Join-Path $project ('.build\phase1b-slice5\ui-'+$runId)
$fixture=Join-Path $runRoot '中文视图'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $fixture '000-folder') | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture '001-hardlink-a.txt'),'Blink owned fixture')
New-Item -ItemType HardLink -Path (Join-Path $fixture '002-hardlink-b.txt') -Target (Join-Path $fixture '001-hardlink-a.txt') | Out-Null
foreach($i in 1..$FixtureFiles){[IO.File]::WriteAllText((Join-Path $fixture ('item-{0:D5}.txt' -f $i)),'fixture')}
[IO.File]::WriteAllText((Join-Path $fixture '003-image.PNG'),'not opened by this test')
$expectedCount=$FixtureFiles+4
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BlinkViewSmokeInput {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
 [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd,out uint pid);
 public static bool OwnsForeground(int pid){uint owner;GetWindowThreadProcessId(GetForegroundWindow(),out owner);return owner==(uint)pid;}
 [DllImport("user32.dll")] static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
 public static void Key(byte k){keybd_event(k,0,0,UIntPtr.Zero);keybd_event(k,0,2,UIntPtr.Zero);}
 public static void Ctrl(byte k){keybd_event(0x11,0,0,UIntPtr.Zero);Key(k);keybd_event(0x11,0,2,UIntPtr.Zero);}
}
'@
$process=$null
$actions=[Collections.Generic.List[object]]::new()
$failure=$null
$forcedTermination=$false
$exitCode=$null
$telemetry=Join-Path $runRoot 'startup.log'
function Window {
 $process.Refresh()
 if($process.HasExited){throw ('Blink exited: '+$process.ExitCode)}
 if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null}
 return [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
}
function SendKey([byte]$key){
 if(-not [BlinkViewSmokeInput]::OwnsForeground($process.Id)){throw 'Owned Blink window must have foreground focus before keyboard input'}
 [BlinkViewSmokeInput]::Key($key)
}
function SendCtrl([byte]$key){
 if(-not [BlinkViewSmokeInput]::OwnsForeground($process.Id)){throw 'Owned Blink window must have foreground focus before keyboard input'}
 [BlinkViewSmokeInput]::Ctrl($key)
}
function Condition($property,$value){return [Windows.Automation.PropertyCondition]::new($property,$value)}
function ById([string]$id){
 $window=Window
 if($null -eq $window){return $null}
 return $window.FindFirst([Windows.Automation.TreeScope]::Descendants,(Condition ([Windows.Automation.AutomationElement]::AutomationIdProperty) $id))
}
function OfType($parent,$type){
 if($null -eq $parent){return @()}
 return @($parent.FindAll([Windows.Automation.TreeScope]::Descendants,(Condition ([Windows.Automation.AutomationElement]::ControlTypeProperty) $type)))
}
function WaitFor([string]$name,[scriptblock]$predicate){
 $deadline=[DateTime]::UtcNow.AddSeconds(25)
 $lastError=''
 do {
  $process.Refresh()
  if($process.HasExited){throw ('Blink exited while '+$name+': '+$process.ExitCode)}
  try{if(& $predicate){$actions.Add([pscustomobject]@{action=$name;passed=$true;time=[DateTime]::UtcNow.ToString('o')});return}}catch{$lastError=$_.Exception.Message}
  [Threading.Thread]::Sleep(150)
 }while([DateTime]::UtcNow -lt $deadline)
 throw ('Timed out: '+$name+'; '+$lastError)
}
function Address {
 return @(OfType (Window) ([Windows.Automation.ControlType]::Edit) | Where-Object {$_.Current.IsEnabled})[0]
}
function AddressValue {return (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function StatusContains([string]$pattern){
 return (@(OfType (Window) ([Windows.Automation.ControlType]::Text) | Where-Object {$_.Current.Name -match $pattern}).Count -gt 0)
}
function Navigate([string]$path){
 $address=Address
 $address.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($path)
 $address.SetFocus()
 SendKey (0x0D)
 WaitFor ('navigate '+$path) { (AddressValue) -eq $path -and (StatusContains ('(?:Ready|就绪).*\b'+$expectedCount+'\b')) }
}
function ComboValue([string]$id){
 $selection=(ById $id).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()
 return $(if($selection.Count){$selection[0].Current.Name}else{''})
}
function SetCombo([string]$id,[int]$index,[string]$pattern){
 (ById $id).SetFocus()
 SendKey (0x24)
 for($n=0;$n -lt $index;$n++){SendKey (0x28)}
 WaitFor ($id+'='+$index) { (ComboValue $id) -match $pattern }
}
function FocusEntries {
 WaitFor 'focus a realized file-view item' {
  $rows=@(OfType (ById 'FolderEntries') ([Windows.Automation.ControlType]::ListItem) | Where-Object {$_.Current.IsKeyboardFocusable -and -not $_.Current.IsOffscreen})
  if($rows.Count -eq 0){return $false}
  $rows[0].SetFocus()
  return [BlinkViewSmokeInput]::OwnsForeground($process.Id)
 }
}
function SelectedCount {return (ById 'FolderEntries').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection().Count}
function SelectedNames {return @((ById 'FolderEntries').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection() | ForEach-Object {$_.Current.Name})}
function BrowseCount {
 if(-not (Test-Path -LiteralPath $telemetry)){return 0}
 return @(Get-Content -LiteralPath $telemetry | Where-Object {$_ -match 'browse_timing='}).Count
}
function AssertState([string]$name,[int]$count,[bool]$grid=$false){
 WaitFor $name {
  $control=ById 'FolderEntries'
  $class=$control.Current.ClassName
  $correctClass=$(if($grid){$class -match 'GridView'}else{$class -match 'ListView' -and $class -notmatch 'GridView'})
  $correctClass -and (SelectedCount) -eq $count -and (AddressValue) -eq $fixture
 }
}
try {
 $psi=[Diagnostics.ProcessStartInfo]::new()
 $psi.FileName=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')
 $psi.WorkingDirectory=Split-Path -Parent $psi.FileName
 $psi.UseShellExecute=$false
 $psi.EnvironmentVariables['BLINK_STARTUP_TELEMETRY']=$telemetry
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi
 if(-not $process.Start()){throw 'Blink start failed'}
 WaitFor 'initial ThisPC Details ready' { $null -ne (ById 'FolderViewMode') -and (ComboValue 'FolderViewMode') -match 'Details|详细信息' -and (StatusContains '(?:Ready|就绪).*locations|(?:就绪).*位置') }
 [BlinkViewSmokeInput]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
 SetCombo 'FolderViewMode' 2 'Grid|网格'
 WaitFor 'ThisPC supports Grid and disables folder sort' { (ById 'FolderEntries').Current.ClassName -match 'GridView' -and -not (ById 'FolderSortField').Current.IsEnabled }
 SetCombo 'FolderViewMode' 0 'Details|详细信息'
 Navigate $fixture
 $control=ById 'FolderEntries'
 $rows=@(OfType $control ([Windows.Automation.ControlType]::ListItem))
 $a=@($rows | Where-Object {$_.Current.Name -eq '001-hardlink-a.txt'})[0]
 $b=@($rows | Where-Object {$_.Current.Name -eq '002-hardlink-b.txt'})[0]
 if($null -eq $a -or $null -eq $b){throw ('Hardlink rows not exposed: '+($rows | ForEach-Object {$_.Current.Name} | Out-String))}
 $a.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 $b.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).AddToSelection()
 AssertState 'Details hardlinks selected independently' 2
 $before=BrowseCount
 foreach($mode in @(1,2,0,2,1,0)){
  $pattern=@('Details|详细信息','List|列表','Grid|网格')[$mode]
  SetCombo 'FolderViewMode' $mode $pattern
  AssertState ('view switch preserves hardlinks '+$mode) 2 ($mode -eq 2)
  if((BrowseCount) -ne $before){throw 'View switch re-enumerated folder'}
  if((SelectedNames) -notcontains '001-hardlink-a.txt' -or (SelectedNames) -notcontains '002-hardlink-b.txt'){throw 'Hardlink selection changed'}
 }
 $draft=$fixture+'\unsubmitted-address-draft'
 (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($draft)
 SetCombo 'FolderViewMode' 2 'Grid|网格'
 WaitFor 'view change preserves address editor draft' { (AddressValue) -eq $draft -and (SelectedCount) -eq 2 }
 (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($fixture)
 FocusEntries
 SendCtrl (0x41)
 AssertState 'Grid Ctrl+A selects all committed rows' $expectedCount $true
 SetCombo 'FolderViewMode' 1 'List|列表'
 AssertState 'List preserves all selected rows' $expectedCount
 SetCombo 'FolderSortField' 1 'Type|类型'
 WaitFor 'Type sort completed with all selections' { (ById 'FolderSortDirection').Current.IsEnabled -and (SelectedCount) -eq $expectedCount }
 (ById 'FolderSortDirection').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
 WaitFor 'Type descending completed' { (ById 'FolderSortDirection').Current.Name -match 'Descending|降序' -and (ById 'FolderSortDirection').Current.IsEnabled -and (SelectedCount) -eq $expectedCount }
 $top=@(OfType (ById 'FolderEntries') ([Windows.Automation.ControlType]::ListItem))[0]
 if($top.Current.Name -ne '000-folder'){throw ('Directories are not first: '+$top.Current.Name)}
 $tabsA=@(OfType (Window) ([Windows.Automation.ControlType]::TabItem))
 $tabA=$tabsA[0]
 (ById 'AddButton').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
 WaitFor 'new tab has default Details' { @(OfType (Window) ([Windows.Automation.ControlType]::TabItem)).Count -eq 2 -and (ComboValue 'FolderViewMode') -match 'Details|详细信息' }
 SetCombo 'FolderViewMode' 2 'Grid|网格'
 Navigate $fixture
 $tabA=@(OfType (Window) ([Windows.Automation.ControlType]::TabItem))[0]
 $tabA.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 WaitFor 'tab A restores List Type descending and selection' { (ComboValue 'FolderViewMode') -match 'List|列表' -and (ComboValue 'FolderSortField') -match 'Type|类型' -and (ById 'FolderSortDirection').Current.Name -match 'Descending|降序' -and (SelectedCount) -eq $expectedCount }
 $tabs=@(OfType (Window) ([Windows.Automation.ControlType]::TabItem))
 $tabs[1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 WaitFor 'tab B restores Grid Name ascending without selection leakage' { (ComboValue 'FolderViewMode') -match 'Grid|网格' -and (ComboValue 'FolderSortField') -match 'Name|名称' -and (ById 'FolderSortDirection').Current.Name -match 'Ascending|升序' -and (SelectedCount) -eq 0 }
 FocusEntries
 SendCtrl (0x57)
 WaitFor 'close tab B restores tab A List' { @(OfType (Window) ([Windows.Automation.ControlType]::TabItem)).Count -eq 1 -and (ComboValue 'FolderViewMode') -match 'List|列表' -and (SelectedCount) -eq $expectedCount }
 $actions.Add([pscustomobject]@{action='no source re-enumeration during view switches';passed=$true;count=$before})
}catch{$failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace}
finally {
 if($null -ne $process){
  $process.Refresh()
  if(-not $process.HasExited){$process.CloseMainWindow() | Out-Null;if(-not $process.WaitForExit(5000)){$forcedTermination=$true;$process.Kill();$process.WaitForExit(5000) | Out-Null}}
  $process.Refresh()
  if($process.HasExited){$exitCode=$process.ExitCode}
  if($forcedTermination -and $null -eq $failure){$failure='Owned Blink window did not close within the shutdown deadline'}
  if($null -ne $exitCode -and $exitCode -ne 0 -and $null -eq $failure){$failure='Blink exit code: '+$exitCode}
 }
 $report=[ordered]@{configuration=$Configuration;run_root=$runRoot;fixture=$fixture;ordinary_user=$true;binary_sha256=(Get-FileHash -LiteralPath $psi.FileName -Algorithm SHA256).Hash;expected_rows=$expectedCount;actions=@($actions.ToArray());passed=($null -eq $failure);forced_termination=$forcedTermination;exit_code=$exitCode;failure=$failure;telemetry=$telemetry;process_id=$(if($process){$process.Id}else{$null})}
 $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding UTF8
 Write-Output ($report | ConvertTo-Json -Depth 8)
}
if($null -ne $failure){exit 1}
