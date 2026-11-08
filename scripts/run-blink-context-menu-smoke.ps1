param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Ordinary user required'}
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public static class BlinkMenuProbeInput {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr window,uint msg,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
 public static IntPtr OwnedWindow;
 static IntPtr InputSite(IntPtr parent){IntPtr child=IntPtr.Zero;while((child=FindWindowEx(parent,child,null,null))!=IntPtr.Zero){var name=new StringBuilder(128);GetClassName(child,name,128);if(name.ToString()=="InputSiteWindowClass")return child;var nested=InputSite(child);if(nested!=IntPtr.Zero)return nested;}return IntPtr.Zero;}
 public static void Key(int pid,uint key){var input=InputSite(OwnedWindow);uint owner;GetWindowThreadProcessId(input,out owner);if(input==IntPtr.Zero||owner!=(uint)pid)throw new Exception("Owned input site required");UIntPtr result;if(SendMessageTimeout(input,0x100,new UIntPtr(key),new IntPtr(1),2,3000,out result)==IntPtr.Zero)throw new Exception("Key down failed");if(SendMessageTimeout(input,0x101,new UIntPtr(key),new IntPtr(unchecked((int)0xC0000001)),2,3000,out result)==IntPtr.Zero)throw new Exception("Key up failed");}

}
'@
$project=Split-Path -Parent $PSScriptRoot
$runRoot=Join-Path (Join-Path $project '.build\phase1b-slice7') ('ui-'+$Configuration.ToLowerInvariant()+'-'+(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+$PID)
$fixture=Join-Path $runRoot '中文菜单'
New-Item -ItemType Directory -Path (Join-Path $fixture '000-folder') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'aaa.txt'),'owned menu fixture')
New-Item -ItemType HardLink -Path (Join-Path $fixture 'bbb.txt') -Target (Join-Path $fixture 'aaa.txt') | Out-Null
$process=$null;$failure=$null;$forced=$false;$exitCode=$null
$actions=[Collections.Generic.List[object]]::new()
function Window { $process.Refresh();if($process.HasExited){throw 'Blink exited'};if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null};return [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle) }
function ById([string]$id){$window=Window;if(-not $window){return $null};return $window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))}
function OfType($parent,$type){if(-not $parent){return @()};return @($parent.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,$type)))}
function WaitFor([string]$name,[scriptblock]$predicate){$deadline=[DateTime]::UtcNow.AddSeconds(15);$last='';do{try{if(& $predicate){$actions.Add([pscustomobject]@{action=$name;passed=$true});return}}catch{$last=$_.Exception.Message};[Threading.Thread]::Sleep(150)}while([DateTime]::UtcNow -lt $deadline);throw ('Timed out: '+$name+'; '+$last)}
function Address {return @(OfType (Window) ([Windows.Automation.ControlType]::Edit) | Where-Object {$_.Current.IsEnabled})[0]}
function AddressValue {return (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function StatusContains([string]$pattern){return @(OfType (Window) ([Windows.Automation.ControlType]::Text) | Where-Object {$_.Current.Name -match $pattern}).Count -gt 0}
function Navigate([string]$path,[int]$count){$address=Address;$address.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($path);$address.SetFocus();[BlinkMenuProbeInput]::Key($process.Id,13);WaitFor 'owned navigation completed' {(AddressValue) -eq $path -and (StatusContains ('(?:Ready|就绪).*\b'+$count+'\b|(?:empty folder|空文件夹)'))}}
function Rows {return @(OfType (ById 'FolderEntries') ([Windows.Automation.ControlType]::ListItem))}
function ItemMenu([int]$index){$row=@(Rows)[$index];$row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$row.SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x5D);WaitFor 'Application key opens owned row menu' {$null -ne (ById 'ContextOpen')}}
function Invoke([string]$id){
 $element=ById $id;$pattern=$null
 if($element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}else{$element.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()}
}
function Submenu([string]$id){
 (ById $id).GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 $child=$(if($id -eq 'ContextView'){'ContextViewDetails'}else{'ContextSortDescending'})
 WaitFor ($id+' submenu realized') {$null -ne (ById $child)}
}
function Dismiss { [BlinkMenuProbeInput]::Key($process.Id,27) }
function EmptyMenu { (ById 'FolderEntries').SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x5D);WaitFor 'Application key opens empty-folder menu' {$null -ne (ById 'ContextNewFolder')} }
function SelectedCount {return (ById 'FolderEntries').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection().Count}
function ComboValue([string]$id){if($id -eq 'FolderViewMode'){return (ById $id).Current.HelpText};$selected=(ById $id).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection();return $selected[0].Current.Name}

try {
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe');$psi.WorkingDirectory=Split-Path -Parent $psi.FileName;$psi.UseShellExecute=$false;$psi.EnvironmentVariables['BLINK_STARTUP_TELEMETRY']=Join-Path $runRoot 'startup.log'
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Start failed'}
 WaitFor 'production browse controls loaded' {$null -ne (ById 'FolderViewMode')}
 [BlinkMenuProbeInput]::OwnedWindow=$process.MainWindowHandle
 Navigate $fixture 3

 ItemMenu 1
 WaitFor 'item menu exposes all baseline commands' {$null -ne (ById 'ContextCut') -and $null -ne (ById 'ContextCopy') -and $null -ne (ById 'ContextRename') -and $null -ne (ById 'ContextDelete') -and $null -ne (ById 'ContextProperties') -and $null -ne (ById 'ContextView') -and $null -ne (ById 'ContextSort')}
 Invoke 'ContextCopy'
 WaitFor 'copy reports unavailable without mutating fixture' {(StatusContains '(?:Copy|复制).*(?:not available|暂未开放)') -and (Test-Path (Join-Path $fixture 'aaa.txt')) -and (Test-Path (Join-Path $fixture 'bbb.txt'))}
 ItemMenu 0
 Invoke 'ContextOpen'
 WaitFor 'context Open navigates into the selected directory' {(AddressValue) -eq (Join-Path $fixture '000-folder') -and (StatusContains 'empty folder|空文件夹')}
 EmptyMenu
 WaitFor 'background menu exposes Paste NewFolder Refresh View Sort' { $null -ne (ById 'ContextPaste') -and $null -ne (ById 'ContextRefresh') -and $null -ne (ById 'ContextView') -and $null -ne (ById 'ContextSort') -and $null -eq (ById 'ContextOpen') }
 Invoke 'ContextNewFolder'
 WaitFor 'NewFolder reports unavailable and keeps folder empty' {(StatusContains '(?:New folder|新建文件夹).*(?:not available|暂未开放)') -and @(Get-ChildItem -LiteralPath (Join-Path $fixture '000-folder') -Force).Count -eq 0}
 EmptyMenu
 Invoke 'ContextPaste'
 WaitFor 'Paste reports unavailable and keeps folder empty' {(StatusContains '(?:Paste|粘贴).*(?:not available|暂未开放)') -and @(Get-ChildItem -LiteralPath (Join-Path $fixture '000-folder') -Force).Count -eq 0}
 EmptyMenu
 Submenu 'ContextView'
 WaitFor 'view submenu exposes three modes' {$null -ne (ById 'ContextViewGrid') -and $null -ne (ById 'ContextViewDetails') -and $null -ne (ById 'ContextViewList')}
 Invoke 'ContextViewGrid'
 WaitFor 'menu switches empty folder to Grid without navigation' {(ComboValue 'FolderViewMode') -match 'Grid|网格' -and (AddressValue) -eq (Join-Path $fixture '000-folder')}
 Navigate $fixture 3
 ItemMenu 1
 Submenu 'ContextView'
 Invoke 'ContextViewList'
 WaitFor 'menu switches to List and preserves selected row' {(ComboValue 'FolderViewMode') -match 'List|列表' -and (SelectedCount) -eq 1 -and (AddressValue) -eq $fixture}
 ItemMenu 1
 Submenu 'ContextSort'
 WaitFor 'sort submenu exposes all fields and directions' {$null -ne (ById 'ContextSortName') -and $null -ne (ById 'ContextSortType') -and $null -ne (ById 'ContextSortSize') -and $null -ne (ById 'ContextSortModified') -and $null -ne (ById 'ContextSortAscending') -and $null -ne (ById 'ContextSortDescending')}
 Invoke 'ContextSortType'
 WaitFor 'menu changes authoritative sort field' {(ComboValue 'FolderSortField') -match 'Type|类型' -and (SelectedCount) -eq 1 -and (AddressValue) -eq $fixture}
 ItemMenu 1
 Submenu 'ContextSort'
 Invoke 'ContextSortDescending'
 WaitFor 'menu changes authoritative sort direction' {(ById 'FolderSortDirection').Current.Name -match 'Descending|降序' -and (SelectedCount) -eq 1}
 $rows=@(Rows)
 $files=@($rows | Where-Object {$_.Current.Name -match '^[ab]{3}\.txt$'})
 $files[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 $files[1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).AddToSelection()
 $files[1].SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x5D)
 WaitFor 'menu on selected hard-link rows preserves multi-selection' {(SelectedCount) -eq 2 -and $null -ne (ById 'ContextCopy')}
 WaitFor 'multi-selection keeps copy intent and disables single-item commands' {(ById 'ContextCopy').Current.IsEnabled -and -not (ById 'ContextOpen').Current.IsEnabled -and -not (ById 'ContextRename').Current.IsEnabled -and -not (ById 'ContextProperties').Current.IsEnabled}
 Invoke 'ContextDelete'
 WaitFor 'Delete intent keeps both hard-link rows and selection' {(StatusContains '(?:Delete|删除).*(?:not available|暂未开放)') -and (SelectedCount) -eq 2 -and (Test-Path (Join-Path $fixture 'aaa.txt')) -and (Test-Path (Join-Path $fixture 'bbb.txt'))}
 $row=@(Rows | Where-Object {$_.Current.Name -eq 'aaa.txt'})[0]
 $row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$row.SetFocus()
 [BlinkMenuProbeInput]::Key($process.Id,0x71)
 WaitFor 'F2 follows the unavailable rename command route' {(StatusContains '(?:Rename|重命名).*(?:not available|暂未开放)') -and (Test-Path (Join-Path $fixture 'aaa.txt'))}
 $row.SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x2E)
 WaitFor 'Delete key keeps source files and reports unavailable' {(StatusContains '(?:Delete|删除).*(?:not available|暂未开放)') -and (Test-Path (Join-Path $fixture 'aaa.txt'))}
 (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('menu address draft')
 foreach($command in @(@('ContextCut','Cut|剪切'),@('ContextRename','Rename|重命名'),@('ContextProperties','Properties|属性'))) {
  $row=@(Rows | Where-Object {$_.Current.Name -eq 'aaa.txt'})[0];$row.SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x5D)
  WaitFor ('menu ready for '+$command[0]) {$null -ne (ById $command[0])}
  Invoke $command[0]
  WaitFor ($command[0]+' unavailable preserves address draft') {(StatusContains ('(?:'+$command[1]+').*(?:not available|暂未开放)')) -and (AddressValue) -eq 'menu address draft'}
 }
 (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($fixture)
 [IO.File]::WriteAllText((Join-Path $fixture 'ccc.txt'),'owned refresh fixture')
 ItemMenu 1
 Invoke 'ContextRefresh'
 WaitFor 'context Refresh reenumerates an externally added file' {(AddressValue) -eq $fixture -and (StatusContains '(?:Ready|就绪).*\b4\b') -and @(Rows | Where-Object {$_.Current.Name -eq 'ccc.txt'}).Count -eq 1}
 Remove-Item -LiteralPath (Join-Path $fixture 'ccc.txt')
 ItemMenu 1
 Invoke 'ContextRefresh'
 WaitFor 'context Refresh reconciles an externally deleted file' {(AddressValue) -eq $fixture -and (StatusContains '(?:Ready|就绪).*\b3\b') -and @(Rows | Where-Object {$_.Current.Name -eq 'ccc.txt'}).Count -eq 0}

 $row=@(Rows | Where-Object {$_.Current.Name -eq 'aaa.txt'})[0]
 $row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 $row=@(Rows | Where-Object {$_.Current.Name -eq 'aaa.txt'})[0]
 $row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 $row.SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x5D)
 WaitFor 'menu opens before tab switch' {$null -ne (ById 'ContextRefresh')}
 $oldRefresh=ById 'ContextRefresh'
 Invoke 'AddButton'
 WaitFor 'new tab becomes ready at ThisPC' {(AddressValue) -match '^(?:This PC|此电脑)$' -and (StatusContains '(?:locations|位置)')}
 $addressBefore=AddressValue
 $stalePeerUnavailable=$false
 try{$oldRefresh.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}catch [Windows.Automation.ElementNotAvailableException] {$stalePeerUnavailable=$true}
 if(-not $stalePeerUnavailable){WaitFor 'stale command reports page changed' {StatusContains 'page has changed|页面已变化'}}
 WaitFor 'stale menu cannot navigate the new active tab' {(AddressValue) -eq $addressBefore -and (ComboValue 'FolderViewMode') -match 'Details|详细信息'}
 $thisPcRow=@(Rows)[0];$thisPcRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$thisPcRow.SetFocus();[BlinkMenuProbeInput]::Key($process.Id,0x5D)
 WaitFor 'ThisPC opens locations and disables filesystem-only commands' {(ById 'ContextOpen').Current.IsEnabled -and -not (ById 'ContextSort').Current.IsEnabled -and -not (ById 'ContextCut').Current.IsEnabled}
 Dismiss
 WaitFor 'fixture contents unchanged after unavailable commands' {[IO.File]::ReadAllText((Join-Path $fixture 'aaa.txt')) -eq 'owned menu fixture' -and [IO.File]::ReadAllText((Join-Path $fixture 'bbb.txt')) -eq 'owned menu fixture' -and @(Get-ChildItem -LiteralPath $fixture -Force).Count -eq 3}

}catch{$failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace}
finally {
 if($process){$process.Refresh();if(-not $process.HasExited){$process.CloseMainWindow()|Out-Null;if(-not $process.WaitForExit(5000)){$forced=$true;$process.Kill();$process.WaitForExit(5000)|Out-Null}};$process.Refresh();if($process.HasExited){$exitCode=$process.ExitCode}}
 if(-not $failure -and ($forced -or $exitCode -ne 0)){$failure='Abnormal test close'}
 $report=[ordered]@{configuration=$Configuration;input_mode='owned window messages and UIA';actions=@($actions.ToArray());passed=($null -eq $failure);failure=$failure;exit_code=$exitCode;forced_termination=$forced;process_id=$(if($process){$process.Id}else{$null});binary_sha256=(Get-FileHash -LiteralPath $psi.FileName -Algorithm SHA256).Hash}
 $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding UTF8
 $report | ConvertTo-Json -Depth 6
}
if($failure){exit 1}
