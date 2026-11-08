[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [ValidateRange(1,1000)][int]$FixtureFiles=600)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)
$OutputEncoding=[Console]::OutputEncoding
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Ordinary user required'}
$project=Split-Path -Parent $PSScriptRoot
$runId=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+$PID
$runRoot=Join-Path $project ('.build\phase1b-slice6\ui-'+$runId)
$fixtureName='可见性_'+$runId
$fixture=Join-Path $runRoot $fixtureName
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
foreach($name in @('000-visible-dir','001-hidden-dir','002-system-dir','003-protected-dir')){
 New-Item -ItemType Directory -Path (Join-Path $fixture $name) | Out-Null
}
foreach($name in @('010-link-a.txt','012-hidden.txt','013-protected.txt','014-system.txt')){
 [IO.File]::WriteAllText((Join-Path $fixture $name),'Blink owned visibility fixture')
}
New-Item -ItemType HardLink -Path (Join-Path $fixture '011-link-b.txt') -Target (Join-Path $fixture '010-link-a.txt') | Out-Null
foreach($i in 1..$FixtureFiles){[IO.File]::WriteAllText((Join-Path $fixture ('item-{0:D5}.txt' -f $i)),'fixture')}
[IO.File]::SetAttributes((Join-Path $fixture '001-hidden-dir'),[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::Hidden)
[IO.File]::SetAttributes((Join-Path $fixture '002-system-dir'),[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::System)
[IO.File]::SetAttributes((Join-Path $fixture '003-protected-dir'),[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::Hidden)
[IO.File]::SetAttributes((Join-Path $fixture '012-hidden.txt'),[IO.FileAttributes]::Hidden)
[IO.File]::SetAttributes((Join-Path $fixture '013-protected.txt'),[IO.FileAttributes]::System -bor [IO.FileAttributes]::Hidden)
[IO.File]::SetAttributes((Join-Path $fixture '014-system.txt'),[IO.FileAttributes]::System)
$hiddenOnly=Join-Path $fixture '000-visible-dir'
[IO.File]::WriteAllText((Join-Path $hiddenOnly 'hidden-only.txt'),'fixture')
[IO.File]::SetAttributes((Join-Path $hiddenOnly 'hidden-only.txt'),[IO.FileAttributes]::Hidden)
$hiddenCount=$FixtureFiles+5
$visibleCount=$FixtureFiles+3
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BlinkVisibilityInput {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string className,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,System.Text.StringBuilder name,int size);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,UIntPtr wParam,IntPtr lParam,uint flags,uint timeout,out UIntPtr result);
 static IntPtr InputSite(IntPtr parent){IntPtr child=IntPtr.Zero;while((child=FindWindowEx(parent,child,null,null))!=IntPtr.Zero){var name=new System.Text.StringBuilder(128);GetClassName(child,name,128);if(name.ToString()=="InputSiteWindowClass")return child;var nested=InputSite(child);if(nested!=IntPtr.Zero)return nested;}return IntPtr.Zero;}
 public static void Key(IntPtr owned,int pid,uint key){uint owner;GetWindowThreadProcessId(owned,out owner);if(owner!=(uint)pid)throw new Exception("Owned Blink HWND required");var site=InputSite(owned);GetWindowThreadProcessId(site,out owner);if(site==IntPtr.Zero || owner!=(uint)pid)throw new Exception("Owned input site required");UIntPtr result;if(SendMessageTimeout(site,0x100,new UIntPtr(key),new IntPtr(1),2,3000,out result)==IntPtr.Zero)throw new Exception("Owned key down failed");if(SendMessageTimeout(site,0x101,new UIntPtr(key),new IntPtr(unchecked((int)0xC0000001)),2,3000,out result)==IntPtr.Zero)throw new Exception("Owned key up failed");}
}
'@
$process=$null;$failure=$null;$forced=$false;$exitCode=$null
$actions=[Collections.Generic.List[object]]::new()
$telemetry=Join-Path $runRoot 'startup.log'
function Window {
 $process.Refresh();if($process.HasExited){throw ('Blink exited: '+$process.ExitCode)}
 if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null}
 return [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
}
function Find($parent,$property,$value){
 if($null -eq $parent){return $null}
 return $parent.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($property,$value))
}
function ById([string]$id){return Find (Window) ([Windows.Automation.AutomationElement]::AutomationIdProperty) $id}
function OfType($parent,$type){
 if($null -eq $parent){return @()}
 return @($parent.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,$type)))
}
function WaitFor([string]$name,[scriptblock]$predicate){
 $deadline=[DateTime]::UtcNow.AddSeconds(25);$last=''
 do {
  $process.Refresh();if($process.HasExited){throw ('Blink exited during '+$name+': '+$process.ExitCode)}
  try{if(& $predicate){$actions.Add([pscustomobject]@{action=$name;passed=$true});return}}catch{$last=$_.Exception.Message}
  [Threading.Thread]::Sleep(150)
 }while([DateTime]::UtcNow -lt $deadline)
 throw ('Timed out: '+$name+'; '+$last)
}
function Address {return @(OfType (Window) ([Windows.Automation.ControlType]::Edit) | Where-Object {$_.Current.IsEnabled})[0]}
function AddressValue {return (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Status([string]$pattern){return @(OfType (Window) ([Windows.Automation.ControlType]::Text) | Where-Object {$_.Current.Name -match $pattern}).Count -gt 0}
function Ready([int]$count){
 if($count -eq 0){return Status 'Empty|empty folder|空目录|目录为空|空文件夹'}
 return Status ('(?:Ready|就绪).*\b'+$count+'\b')
}
function SubmitPath([string]$path){
 $address=Address;$address.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($path);$address.SetFocus()
 [BlinkVisibilityInput]::Key($process.MainWindowHandle,$process.Id,13)
}
function Navigate([string]$path,[int]$count){SubmitPath $path;WaitFor ('navigate '+$path) { (AddressValue) -eq $path -and (Ready $count) }}
# View is now a DropDownButton; flyout peers may be sibling popup roots.
function TopViewElement([string]$automationId){
 $element=ById $automationId
 if($element -and $element.Current.ProcessId -eq $process.Id -and -not $element.Current.IsOffscreen){return $element}
 $roots=@([Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,
  [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$process.Id)))
 foreach($ownedRoot in $roots){
  if($ownedRoot.Current.ProcessId -ne $process.Id){continue}
  if($ownedRoot.Current.AutomationId -eq $automationId -and -not $ownedRoot.Current.IsOffscreen){return $ownedRoot}
  foreach($candidate in @($ownedRoot.FindAll([Windows.Automation.TreeScope]::Descendants,
   [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$automationId)))){
   if($candidate.Current.ProcessId -eq $process.Id -and -not $candidate.Current.IsOffscreen){return $candidate}
  }
 }
 return $null
}
function InvokeTopViewElement($element){
 if(-not $element -or $element.Current.ProcessId -ne $process.Id -or -not $element.Current.IsEnabled){throw 'Enabled owned View control required'}
 $uiaPattern=$null
 if($element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$uiaPattern)){$uiaPattern.Invoke()}
 elseif($element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$uiaPattern)){$uiaPattern.Toggle()}
 elseif($element.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$uiaPattern)){$uiaPattern.Expand()}
 else{throw 'Owned View control has no Invoke/Toggle/Expand pattern'}
}
function OpenTopViewMenu{
 if(-not(TopViewElement 'TopViewMenu')){InvokeTopViewElement (ById 'FolderViewMode')}
 WaitFor 'top View menu realized' {$null -ne (TopViewElement 'TopViewMenu')}
}
function SetTopViewMode([int]$modeIndex){
 if($modeIndex -lt 0 -or $modeIndex -gt 2){throw 'Unknown View mode index'}
 OpenTopViewMenu
 $modeId=@('TopViewDetails','TopViewList','TopViewGrid')[$modeIndex]
 WaitFor ('top View choice '+$modeId) {$null -ne (TopViewElement $modeId)}
 InvokeTopViewElement (TopViewElement $modeId)
 WaitFor 'top View menu closes after mode choice' {$null -eq (TopViewElement 'TopViewMenu')}
}
function TopViewHiddenItem{
 OpenTopViewMenu
 if(-not(TopViewElement 'TopViewShowHidden')){
  $submenu=TopViewElement 'TopViewShow';$uiaPattern=$null
  if($submenu -and $submenu.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$uiaPattern)){$uiaPattern.Expand()}
  else{InvokeTopViewElement $submenu}
 }
 WaitFor 'top View Show hidden-items choice realized' {$null -ne (TopViewElement 'TopViewShowHidden')}
 return (TopViewElement 'TopViewShowHidden')
}
function TopViewChecked($element){
 $uiaPattern=$null
 if($element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$uiaPattern)){return $uiaPattern.Current.ToggleState -eq [Windows.Automation.ToggleState]::On}
 if($element.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$uiaPattern)){return $uiaPattern.Current.IsSelected}
 throw 'Owned View toggle has no readable checked state'
}
function CloseTopViewMenu{
 if(-not(TopViewElement 'TopViewMenu')){return}
 $button=ById 'FolderViewMode';$uiaPattern=$null
 if($button -and $button.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$uiaPattern)){$uiaPattern.Collapse()}
 else{
  for($escapeIndex=0;$escapeIndex -lt 2 -and (TopViewElement 'TopViewMenu');$escapeIndex++){
   [BlinkVisibilityInput]::Key($process.MainWindowHandle,$process.Id,27)
  }
 }
 WaitFor 'top View menu dismissed without policy change' {$null -eq (TopViewElement 'TopViewMenu')}
}
function IsShown{
 $previousFocus=[Windows.Automation.AutomationElement]::FocusedElement
 try{return (TopViewChecked (TopViewHiddenItem))}
 finally{
  CloseTopViewMenu
  if($previousFocus -and $previousFocus.Current.ProcessId -eq $process.Id){$previousFocus.SetFocus()}
 }
}
function SetShown([bool]$show){
 $item=TopViewHiddenItem
 if((TopViewChecked $item) -ne $show){
  InvokeTopViewElement $item
  WaitFor 'top View menu closes after hidden-items choice' {$null -eq (TopViewElement 'TopViewMenu')}
 }else{CloseTopViewMenu}
}
function Rows {return @(OfType (ById 'FolderEntries') ([Windows.Automation.ControlType]::ListItem))}
function RowNames {return @(Rows | ForEach-Object {$_.Current.Name})}
function Selected {return @((ById 'FolderEntries').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection())}
function BrowseCount {return @(Get-Content -LiteralPath $telemetry | Where-Object {$_ -match 'browse_timing='}).Count}
function Tabs {return @(OfType (Window) ([Windows.Automation.ControlType]::TabItem))}
function ExpandFixture {
 WaitFor 'active fixture revealed in tree' { $null -ne (Find (ById 'NavigationPane') ([Windows.Automation.AutomationElement]::NameProperty) $fixtureName) }
 $item=Find (ById 'NavigationPane') ([Windows.Automation.AutomationElement]::NameProperty) $fixtureName
 while($item -and $item.Current.ControlType -ne [Windows.Automation.ControlType]::TreeItem){$item=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($item)}
 if(-not $item){throw 'Fixture TreeItem unavailable'}
 $item.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 WaitFor 'visible directory loaded in tree' { $null -ne (Find (ById 'NavigationPane') ([Windows.Automation.AutomationElement]::NameProperty) '000-visible-dir') }
}
function TreeHas([string]$name){return $null -ne (Find (ById 'NavigationPane') ([Windows.Automation.AutomationElement]::NameProperty) $name)}
function SetView([int]$index){
 SetTopViewMode $index
 $viewPattern=@('Details|详细信息','List|列表','Grid|网格')[$index]
 WaitFor ('top View authoritative mode '+$index) {(ById 'FolderViewMode').Current.HelpText -match $viewPattern}
}
try {
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')
 $psi.WorkingDirectory=Split-Path -Parent $psi.FileName;$psi.UseShellExecute=$false
 $psi.EnvironmentVariables['BLINK_STARTUP_TELEMETRY']=$telemetry
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi
 if(-not $process.Start()){throw 'Blink start failed'}
 WaitFor 'startup ThisPC defaults hidden off' { $null -ne (ById 'FolderViewMode') -and -not (IsShown) -and (Status '(?:Ready|就绪).*locations|(?:就绪).*位置') }
 Navigate $fixture $visibleCount
 WaitFor 'default file view excludes hidden and system entries' { (RowNames) -contains '010-link-a.txt' -and (RowNames) -notcontains '012-hidden.txt' -and (RowNames) -notcontains '001-hidden-dir' -and (RowNames) -notcontains '003-protected-dir' -and (RowNames) -notcontains '014-system.txt' }
 ExpandFixture
 WaitFor 'default tree excludes hidden and system directories' { -not (TreeHas '001-hidden-dir') -and -not (TreeHas '002-system-dir') -and -not (TreeHas '003-protected-dir') }
 $before=BrowseCount
 SetShown $true
 WaitFor 'show hidden completes progressive projection' { (Ready $hiddenCount) -and (RowNames) -contains '012-hidden.txt' -and (RowNames) -contains '001-hidden-dir' -and (RowNames) -notcontains '013-protected.txt' -and (RowNames) -notcontains '014-system.txt' }
 ExpandFixture
 WaitFor 'tree shows hidden directory and keeps system directories hidden' { (TreeHas '001-hidden-dir') -and -not (TreeHas '002-system-dir') -and -not (TreeHas '003-protected-dir') }
 $rows=Rows
 @($rows | Where-Object {$_.Current.Name -eq '010-link-a.txt'})[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 @($rows | Where-Object {$_.Current.Name -eq '011-link-b.txt'})[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).AddToSelection()
 @($rows | Where-Object {$_.Current.Name -eq '012-hidden.txt'})[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).AddToSelection()
 WaitFor 'hidden row and independent hardlinks selected' { (Selected).Count -eq 3 }
 $draft=$fixture+'\unsubmitted-draft';(Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($draft)
 SetShown $false
 WaitFor 'hide prunes hidden selection and keeps address draft' { (Ready $visibleCount) -and (Selected).Count -eq 2 -and (AddressValue) -eq $draft -and (RowNames) -notcontains '012-hidden.txt' }
 SetShown $true
 WaitFor 'reshow preserves hardlinks without resurrecting hidden selection' { (Ready $hiddenCount) -and (Selected).Count -eq 2 -and (AddressValue) -eq $draft }
 WaitFor 'toggle refilters without directory browse' { (BrowseCount) -eq $before }
 (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($fixture)
 foreach($mode in @(1,2,0)){
  SetView $mode
  WaitFor ('view '+$mode+' shares hidden policy and selection') { (IsShown) -and (Selected).Count -eq 2 -and (RowNames) -contains '012-hidden.txt' -and (ById 'FolderEntries').Current.ClassName -match $(if($mode -eq 2){'GridView'}else{'ListView'}) }
 }
 (ById 'AddButton').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
 WaitFor 'new tab inherits window hidden policy' { (Tabs).Count -eq 2 -and (IsShown) -and (Status '(?:Ready|就绪).*locations|(?:就绪).*位置') }
 Navigate $fixture $hiddenCount
 SetShown $false
 WaitFor 'active tab applies hidden off' { (Ready $visibleCount) -and -not (IsShown) }
 (Tabs)[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 WaitFor 'inactive tab refiltered and preserves visible selection' { (Ready $visibleCount) -and -not (IsShown) -and (Selected).Count -eq 2 -and (RowNames) -notcontains '012-hidden.txt' }
 SetShown $true;SetShown $false;SetShown $true
 WaitFor 'rapid policy toggles discard stale projection and tree results' { (Ready $hiddenCount) -and (IsShown) -and (RowNames) -contains '012-hidden.txt' -and (Selected).Count -eq 2 }
 Navigate $hiddenOnly 1
 SetShown $false
 WaitFor 'hidden-only committed directory displays empty' { (Ready 0) -and (Rows).Count -eq 0 -and (AddressValue) -eq $hiddenOnly }
 SetShown $true
 WaitFor 'hidden-only directory shows row without refresh' { (Ready 1) -and (RowNames) -contains 'hidden-only.txt' }
 SubmitPath $fixture
 SetShown $false
 WaitFor 'navigation completion uses latest visibility policy' { (AddressValue) -eq $fixture -and (Ready $visibleCount) -and -not (IsShown) -and (RowNames) -notcontains '012-hidden.txt' }
}catch{$failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace}
finally {
 if($process){
  $process.Refresh();if(-not $process.HasExited){$process.CloseMainWindow() | Out-Null;if(-not $process.WaitForExit(5000)){$forced=$true;$process.Kill();$process.WaitForExit(5000)|Out-Null}}
  $process.Refresh();if($process.HasExited){$exitCode=$process.ExitCode}
  if($null -eq $failure -and ($forced -or $exitCode -ne 0)){$failure='Blink did not close normally'}
 }
 $report=[ordered]@{configuration=$Configuration;fixture=$fixture;ordinary_user=$true;actions=@($actions.ToArray());passed=($null -eq $failure);failure=$failure;exit_code=$exitCode;forced_termination=$forced;process_id=$(if($process){$process.Id}else{$null});binary_sha256=(Get-FileHash -LiteralPath $psi.FileName -Algorithm SHA256).Hash}
 $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding UTF8
 Write-Output ($report | ConvertTo-Json -Depth 6)
}
if($failure){exit 1}
