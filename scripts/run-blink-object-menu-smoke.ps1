[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Binary='',
 [ValidateRange(5,90)][int]$StepTimeoutSeconds=30
)
# G06a: new owned PID/HWND, real UIA and scoped keyboard input, GUID-only fixtures.
# Does not build, use global input/Clipboard, edit services or force any process exit.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$integration=Join-Path $project '.build\explorer-object-menu\integration'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-object-menu-report.json'
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $integration ('ui-object-menu-fixtures\owned-'+$guid)
$source=Join-Path $fixture ('catalog-'+$token);$child=Join-Path $source ('00-child-'+$token)
$markerName='marker-'+$token+'.txt';$childMarkerName='child-'+$token+'.txt'
$allowedDirectories=@($fixture,$source,$child);$allowedFiles=@{};$fileIdentities=@{};$directoryIdentities=@{}
$allowedFiles[(Join-Path $source $markerName)]='Blink object menu root '+$guid
$allowedFiles[(Join-Path $child $childMarkerName)]='Blink object menu child '+$guid
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP
$process=$null;$fixtureCreated=$false;$tempChanged=$false;$script:windowCache=$null;$script:currentStep='preflight'
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new()
$report=[ordered]@{
 status='RUNNING';passed=$false;skipped=$false;configuration=$Configuration;binary=$Binary;binary_sha256=$null
 source_script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
 started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;current_step='preflight';failure=$null;checks=@();notes=@()
 process_id=$null;process_exited=$false;exit_code=$null;exit_code_hex=$null;forced_termination=$false;before_close_alive=$null;close_requested=$false;close_request_utc=$null;close_request_result=$null;process_exit_observed_utc=$null
 environment=$null;window_dpi=$null;desktop_preflight=$null;focus_evidence=@();keyboard_evidence=@();menu_evidence=@();tab_evidence=@();last_ui_snapshot=$null;last_focus_attempt=$null
 fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_ownership_failures=@();fixture_bytes_removed=0
 compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0
 human_acceptance='NOT RUN';model_test_results='NOT RUN by this UI smoke'
 input_mode='Owned PID/HWND UIA and InputSite keyboard messages; Apps/Shift+F10 open real context menus; no global input or Clipboard'
 not_run=@('physical right-click hit testing','actual clipboard writes','actual native Properties sheet interaction','real IME and network/access-denied targets','full theme/DPI/multi-monitor/language/Narrator matrix','Windows 11 24H2 Explorer comparison')
}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($true))}
function Record([string]$Name,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Name;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('OBJECT_MENU_UI_SKIP: '+$Why)}
function Check([string]$Name,[scriptblock]$Body){$script:currentStep=$Name;Save-Report;try{$null=&$Body;Record $Name}catch{Record $Name 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace);throw}}
function Window{$process.Refresh();if($process.HasExited){throw ('Owned Blink exited '+$process.ExitCode)};if(-not $script:windowCache -and $process.MainWindowHandle -ne [IntPtr]::Zero){$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)};if($script:windowCache -and $script:windowCache.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'};return $script:windowCache}
function Visible($Peer){return $null -ne $Peer -and -not $Peer.Current.IsOffscreen -and $Peer.Current.BoundingRectangle.Width -gt 0 -and $Peer.Current.BoundingRectangle.Height -gt 0}
function Find-In($Root,[string]$Id){if($Root){foreach($peer in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)))){if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer)){return $peer}}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){$found=By-Id $Id;if($found){return $found};foreach($h in [BlinkObjectMenuUiNative]::OwnedTopWindows($process.Id)){try{$root=[Windows.Automation.AutomationElement]::FromHandle($h);if($root.Current.ProcessId -ne $process.Id){throw 'Owned root PID mismatch'};if($root.Current.AutomationId -ceq $Id -and (Visible $root)){return $root};$found=Find-In $root $Id;if($found){return $found}}catch{if([BlinkObjectMenuUiNative]::WindowExists($h)){throw}}};return $null}
function Peer-Evidence($Peer){if(-not $Peer){return [pscustomobject]@{found=$false}};$c=$Peer.Current;if(-not $process -or $c.ProcessId -ne $process.Id){return [pscustomobject]@{found=$true;pid=$c.ProcessId;role=$c.ControlType.ProgrammaticName;outside_owned_process=$true}};return [pscustomobject]@{found=$true;id=$c.AutomationId;name=$c.Name;help_text=$c.HelpText;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;pid=$c.ProcessId;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;has_keyboard_focus=$c.HasKeyboardFocus;runtime_id=($Peer.GetRuntimeId()-join ',')}}
function Editor{return By-Id 'AddressEditor'}
function Address-Text{$p=By-Id 'AddressBar';if(-not $p){throw 'Owned AddressBar missing'};return $p.Current.HelpText}
function Entries{return By-Id 'FolderEntries'}
function Rows{$p=Entries;if(-not $p){return @()};return @($p.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_)})}
function Row-Names{return @(Rows|ForEach-Object{$_.Current.Name})}
function Selected-Names{$p=Entries;if(-not $p){return @()};return @($p.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}
function Tabs{return @((By-Id 'WindowTabs').FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TabItem)))}
function Selected-Tab{
 # SyncTabViewItems rebuilds containers after adding a tab. Capture observed
 # position/name, not a UIA peer which will be retired by that rebuild.
 $tabs=@(Tabs);$chosen=@();for($i=0;$i -lt $tabs.Count;$i++){$pattern=$null;if($tabs[$i].TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern) -and $pattern.Current.IsSelected){$chosen+=@([pscustomobject]@{ordinal=$i;name=$tabs[$i].Current.Name;observed_runtime_id=($tabs[$i].GetRuntimeId()-join ',');process_id=$tabs[$i].Current.ProcessId})}}
 if($chosen.Count -ne 1 -or $chosen[0].process_id -ne $process.Id -or -not $chosen[0].name){throw 'One actual named owned selected tab required'};return $chosen[0]
}
function Same-Names([string[]]$Left,[string[]]$Right){return (@($Left|Sort-Object)-join "`n") -ceq (@($Right|Sort-Object)-join "`n")}
function Ready{return (Visible (Entries)) -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}
function Is-ThisPc{return (Address-Text) -in $thisPcLabels -and (Visible (Entries)) -and @(Rows).Count -gt 0 -and -not(Editor)}
function Is-OwnDescendant($Peer,$Ancestor){if(-not $Peer -or -not $Ancestor){return $false};$id=$Ancestor.GetRuntimeId()-join ',';for($p=$Peer;$p;$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)){if($p.Current.ProcessId -ne $process.Id){return $false};if(($p.GetRuntimeId()-join ',') -ceq $id){return $true}};return $false}
function Named-Container([string[]]$Names,$Type){$pane=By-Id 'NavigationPane';if(-not $pane){return $null};foreach($name in $Names){foreach($peer in @($pane.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)))){$p=$peer;while($p -and $p.Current.ProcessId -eq $process.Id){if($p.Current.ControlType -eq $Type -and (Is-OwnDescendant $p $pane)){return $p};if(($p.GetRuntimeId()-join ',') -ceq ($pane.GetRuntimeId()-join ',')){break};$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)}}};return $null}
function Tree-Item([string[]]$Names){return Named-Container $Names ([Windows.Automation.ControlType]::TreeItem)}
function Quick-Home{return Named-Container $homeLabels ([Windows.Automation.ControlType]::ListItem)}
function Capture-State([string]$Why){
 $state=[ordered]@{why=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;rows=@();selected=@();peers=@();navigation_tree=@();tabs=@();focus=$null;desktop=$null;errors=@()}
 try{$state.address=Address-Text;$state.rows=@(Row-Names);$state.selected=@(Selected-Names)}catch{$state.errors+=@($_.Exception.Message)}
 foreach($id in @('NavigationPane','NavigationTree','NavigationQuickList','NavigationContextMenu','LocationContextMenu','TargetContextOpen','TargetContextNewTab','TargetContextExpandCollapse','TargetContextCopyPath','TargetContextProperties','FolderEntries','WindowTabs')){try{$peer=Owned-Id $id;if($peer){$state.peers+=@(Peer-Evidence $peer)}}catch{$state.errors+=@($id+': '+$_.Exception.Message)}}
 try{
  $pane=By-Id 'NavigationPane';if($pane){$items=$pane.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TreeItem));for($i=0;$i -lt [Math]::Min(40,$items.Count);$i++){try{$peer=$items[$i];if($peer.Current.ProcessId -ne $process.Id){continue};$pattern=$null;$expandState='NO PATTERN';if($peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$expandState=$pattern.Current.ExpandCollapseState.ToString()};$state.navigation_tree+=@([pscustomobject]@{ordinal=$i;name=$peer.Current.Name;is_offscreen=$peer.Current.IsOffscreen;expand_collapse=$expandState})}catch{$state.errors+=@('TreeItem '+$i+': '+$_.Exception.Message)}}}
 }catch{$state.errors+=@('Navigation tree snapshot: '+$_.Exception.Message)}
 try{
  $items=@(Tabs);for($i=0;$i -lt [Math]::Min(40,$items.Count);$i++){try{$peer=$items[$i];if($peer.Current.ProcessId -ne $process.Id){continue};$pattern=$null;$isSelected=$null;$hasSelection=$peer.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern);if($hasSelection){$isSelected=$pattern.Current.IsSelected};$state.tabs+=@([pscustomobject]@{ordinal=$i;name=$peer.Current.Name;is_offscreen=$peer.Current.IsOffscreen;selection_pattern=$hasSelection;is_selected=$isSelected})}catch{$state.errors+=@('Tab '+$i+': '+$_.Exception.Message)}}
 }catch{$state.errors+=@('Tab snapshot: '+$_.Exception.Message)}
 try{$state.focus=Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement);$state.desktop=[BlinkObjectMenuUiNative]::DesktopState()}catch{$state.errors+=@($_.Exception.Message)}
 $report.last_ui_snapshot=[pscustomobject]$state;Save-Report
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('OBJECT_MENU_UI_SKIP: ') -or $_.Exception.Message.StartsWith('OBJECT_MENU_TEST_PRECONDITION_NOT_MET: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);Capture-State $Why;throw ('Timed out: '+$Why+'; '+$last)}
function Require-Owned-Desktop([string]$Why){$desktop=[BlinkObjectMenuUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id){throw ('OBJECT_MENU_TEST_PRECONDITION_NOT_MET: '+$Why+'; expected PID '+$process.Id+', actual foreground '+$desktop.ForegroundPid+'; '+$desktop.Reason)};return $desktop}
function Key([uint32]$Code,[int]$Modifiers=0){$desktop=Require-Owned-Desktop 'before scoped key';[BlinkObjectMenuUiNative]::Key($process.Id,$Code,$Modifiers);$report.keyboard_evidence+=@([pscustomobject]@{step=$script:currentStep;key=$Code;modifiers=$Modifiers;foreground_pid=$desktop.ForegroundPid});Save-Report}
function Focus-Peer($Peer,[string]$Why){if(-not $Peer -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw ('Owned enabled focus target required '+$Why)};if(-not(Visible $Peer)){$scroll=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.ScrollItemPattern]::Pattern,[ref]$scroll)){$scroll.ScrollIntoView()}};$runtime=$Peer.GetRuntimeId()-join ',';$Peer.SetFocus();Wait-Ui ('actual owned focus '+$Why) {$focused=[Windows.Automation.AutomationElement]::FocusedElement;$desktop=[BlinkObjectMenuUiNative]::DesktopState();$report.last_focus_attempt=[pscustomobject]@{target=$Why;expected=(Peer-Evidence $Peer);actual=(Peer-Evidence $focused);desktop=$desktop};return $desktop.InputReady -and $desktop.ForegroundPid -eq $process.Id -and (Visible $Peer) -and $focused -and $focused.Current.ProcessId -eq $process.Id -and $focused.Current.HasKeyboardFocus -and ($focused.GetRuntimeId()-join ',') -ceq $runtime};$report.focus_evidence+=@([pscustomobject]@{step=$script:currentStep;target=$Why;actual=(Peer-Evidence $Peer)});Save-Report}
function Act($Peer){if(-not(Visible $Peer) -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw 'Owned visible enabled action required'};$null=Require-Owned-Desktop 'before real UIA action';$pattern=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}elseif($Peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$pattern.Expand()}else{throw ('No real Invoke/Expand pattern '+$Peer.Current.AutomationId)}}
function Select-Tab($Token,[string]$Path){
 # This run only adds tabs; it never closes or reorders them. The captured
 # ordinal remains stable, and its observed name must still match.
 $tabs=@(Tabs);if($null -eq $Token -or $Token.ordinal -lt 0 -or $Token.ordinal -ge $tabs.Count){throw 'Captured tab ordinal is unavailable'};$peer=$tabs[$Token.ordinal];$pattern=$null
 if($peer.Current.ProcessId -ne $process.Id -or $peer.Current.Name -cne $Token.name -or -not $peer.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)){throw 'Recreated owned tab does not match its captured ordinal/name or selection pattern'}
 $pattern.Select();Wait-Ui 'exact previous tab restored' {$current=@(Tabs);$selected=$null;$current.Count -gt $Token.ordinal -and $current[$Token.ordinal].Current.Name -ceq $Token.name -and $current[$Token.ordinal].TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$selected) -and $selected.Current.IsSelected -and -not(Editor) -and $(if($Path -in $thisPcLabels){Is-ThisPc}else{(Address-Text) -ieq $Path -and (Ready)})}
 $currentTabs=@(Tabs);$restored=$currentTabs[$Token.ordinal];$report.tab_evidence+=@([pscustomobject]@{step=$script:currentStep;operation='requery rebuilt tab by observed ordinal/name';ordinal=$Token.ordinal;name=$Token.name;previous_runtime_id=$Token.observed_runtime_id;current=(Peer-Evidence $restored);actual_address=(Address-Text)});Save-Report
}
function Navigate([string]$Path){if($Path -notin $allowedDirectories){throw 'Unowned fixture navigation'};Focus-Peer (By-Id 'AddressHistoryButton') 'address history';Key 0x4C 1;Wait-Ui 'address editor visible' {$null -ne (Editor)};(Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Path);Focus-Peer (Editor) 'address editor';Key 13;Wait-Ui 'exact owned directory committed' {-not(Editor) -and (Address-Text) -ieq $Path -and (Ready) -and $(if($Path -ieq $source){@(Row-Names) -ccontains $markerName}elseif($Path -ieq $child){@(Row-Names) -ccontains $childMarkerName}else{$true})}}
function Menu-Open($Peer,[string]$MenuId,[bool]$ShiftF10=$false){Focus-Peer $Peer 'context target';if($ShiftF10){Key 0x79 2}else{Key 0x5D};Wait-Ui ('real context menu '+$MenuId) {$null -ne (Owned-Id $MenuId) -and $null -ne (Owned-Id 'TargetContextOpen')};$menu=Owned-Id $MenuId;$report.menu_evidence+=@([pscustomobject]@{step=$script:currentStep;method=$(if($ShiftF10){'owned Shift+F10'}else{'owned Apps key'});source=(Peer-Evidence $Peer);menu=(Peer-Evidence $menu);items=@('TargetContextOpen','TargetContextNewTab','TargetContextExpandCollapse','TargetContextCopyPath','TargetContextProperties'|ForEach-Object{Peer-Evidence (Owned-Id $_)})});Save-Report;return $menu}
function Assert-FolderMenu([string]$MenuId,[string]$Target){
 $menu=Owned-Id $MenuId;if(-not $menu -or $menu.Current.HelpText -ine $Target){throw ('Menu target mismatch; expected '+$Target)}
 foreach($id in @('TargetContextOpen','TargetContextNewTab','TargetContextCopyPath','TargetContextProperties','TargetContextNewFolder')){$p=Owned-Id $id;if(-not(Visible $p) -or -not $p.Current.IsEnabled){throw ('Expected enabled target action '+$id)}}
 foreach($id in @('ContextCut','ContextCopy','ContextPaste','ContextRename','ContextDelete','ContextProperties')){if(Owned-Id $id){throw ('File-list command leaked into independent target menu '+$id)}}
 $root=$Target -match '^[A-Za-z]:\\$'
 foreach($id in @('TargetContextCut','TargetContextCopy','TargetContextRename','TargetContextDelete')){$p=Owned-Id $id;if($root){if($p){throw ('Root exposed inapplicable directory command '+$id)}}elseif(-not(Visible $p) -or -not $p.Current.IsEnabled){throw ('Expected ordinary directory target command '+$id)}}
 $paste=Owned-Id 'TargetContextPaste';if(-not(Visible $paste)){throw 'Independent target Paste missing'}
}
function Dismiss-Menu{Key 27;Wait-Ui 'context menu dismissed' {-not(Owned-Id 'NavigationContextMenu') -and -not(Owned-Id 'LocationContextMenu')}}
function Select-Marker{$p=@(Rows|Where-Object{$_.Current.Name -ceq $markerName});if($p.Count -ne 1){throw 'Owned marker row required'};$p[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Wait-Ui 'owned marker remains exactly selected' {(Same-Names @(Selected-Names) @($markerName))}}
function Fixture-Unchanged{foreach($path in $allowedFiles.Keys){if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw ('Owned payload missing '+$path)};$bytes=[Text.UTF8Encoding]::new($false).GetBytes([string]$allowedFiles[$path]);if([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -cne [Convert]::ToBase64String($bytes) -or [BlinkObjectMenuUiNative]::Identity($path) -cne $fileIdentities[$path]){throw ('Owned payload bytes or identity changed '+$path)}}}
function Ownership-Failures{
 $bad=[Collections.Generic.List[string]]::new();$pending=[Collections.Generic.Stack[string]]::new();$pending.Push($fixture);$mask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
 while($pending.Count){$dir=$pending.Pop();$item=Get-Item -LiteralPath $dir -Force;if(-not $item.PSIsContainer -or ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor $mask)) -or [BlinkObjectMenuUiNative]::Identity($dir) -cne $directoryIdentities[$dir]){$bad.Add('Directory identity or attributes changed: '+$dir);continue};foreach($item in @(Get-ChildItem -LiteralPath $dir -Force)){if($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor $mask)){$bad.Add('Unexpected attributes: '+$item.FullName);continue};if($item.PSIsContainer){if($item.FullName -notin $allowedDirectories){$bad.Add('Unknown directory: '+$item.FullName)}else{$pending.Push($item.FullName)}}elseif(-not $allowedFiles.ContainsKey($item.FullName)){$bad.Add('Unknown file: '+$item.FullName)}}}
 foreach($dir in $allowedDirectories){if(-not(Test-Path -LiteralPath $dir -PathType Container)){$bad.Add('Missing directory: '+$dir)}};try{Fixture-Unchanged}catch{$bad.Add($_.Exception.Message)};return @($bad.ToArray())
}
function Resource-Labels([string]$Key){$labels=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq $Key){$labels+=@([string]$data.value)}}};return @($labels|Select-Object -Unique)}
Save-Report
try{
 New-Item -ItemType Directory -Path $compilerTemp -Force|Out-Null;$env:TEMP=$compilerTemp;$env:TMP=$compilerTemp;$tempChanged=$true
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkObjectMenuUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkObjectMenuUiNative {
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; public Point(int x,int y){X=x;Y=y;} }
    delegate bool EnumWindowProc(IntPtr window,IntPtr parameter);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll",SetLastError=true)] static extern bool EnumWindows(EnumWindowProc callback,IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int length);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode,ExactSpelling=true,SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
    [DllImport("user32.dll",SetLastError=true)] static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetUserObjectInformationW(IntPtr handle,int index,StringBuilder value,uint size,out uint needed);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll",SetLastError=true)] static extern bool AttachThreadInput(uint a,uint b,bool attach);
    [DllImport("user32.dll")] static extern bool GetKeyboardState(byte[] state);
    [DllImport("user32.dll")] static extern bool SetKeyboardState(byte[] state);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    public static IntPtr OwnedWindow;
    public static bool WindowExists(IntPtr window) { return window!=IntPtr.Zero&&IsWindow(window); }
    public static bool IsOwnedVisible(IntPtr window,int pid) {
        uint owner;
        return WindowExists(window)&&IsWindowVisible(window)&&GetWindowThreadProcessId(window,out owner)!=0&&owner==(uint)pid;
    }
    public static IntPtr[] OwnedTopWindows(int pid) {
        var windows=new List<IntPtr>();
        if(!EnumWindows(delegate(IntPtr window,IntPtr parameter){if(IsOwnedVisible(window,pid))windows.Add(window);return true;},IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error(),"Owned top-level window enumeration failed");
        return windows.ToArray();
    }
    public static BlinkObjectMenuUiDesktop DesktopState() {
        var result=new BlinkObjectMenuUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
        var foreground=GetForegroundWindow();result.Foreground=foreground.ToInt64();uint foregroundPid;GetWindowThreadProcessId(foreground,out foregroundPid);result.ForegroundPid=foregroundPid;var name=new StringBuilder(256);GetClassName(foreground,name,name.Capacity);result.ForegroundClass=name.ToString();
        var desktop=OpenInputDesktop(0,false,1);
        if(desktop==IntPtr.Zero){result.InputReady=false;result.Reason="Input desktop is inaccessible (locked, disconnected, or non-interactive)";}
        else{try{uint needed;var text=new StringBuilder(256);if(GetUserObjectInformationW(desktop,2,text,(uint)(text.Capacity*2),out needed)){result.DesktopName=text.ToString();if(!String.Equals(result.DesktopName,"Default",StringComparison.OrdinalIgnoreCase)){result.InputReady=false;result.Reason="Input desktop is "+result.DesktopName+", not the user's unlocked desktop";}}}finally{CloseDesktop(desktop);}}
        var previous=SetThreadDpiAwarenessContext(new IntPtr(-4));try{
            Point cursor;if(GetCursorPos(out cursor)){result.CursorX=cursor.X;result.CursorY=cursor.Y;var hit=WindowFromPoint(cursor);uint pid;GetWindowThreadProcessId(hit,out pid);result.CursorWindowPid=pid;name.Length=0;GetClassName(hit,name,name.Capacity);result.CursorWindowClass=name.ToString();}
            result.CenterX=GetSystemMetrics(76)+GetSystemMetrics(78)/2;result.CenterY=GetSystemMetrics(77)+GetSystemMetrics(79)/2;
            var center=WindowFromPoint(new Point(result.CenterX,result.CenterY));uint centerPid;GetWindowThreadProcessId(center,out centerPid);result.CenterWindowPid=centerPid;
            name.Length=0;GetClassName(center,name,name.Capacity);result.CenterWindowClass=name.ToString();
            if(result.ForegroundClass=="LockScreenBackstopFrame"||(foreground==IntPtr.Zero&&(result.CursorWindowClass=="LockScreenBackstopFrame"||result.CenterWindowClass=="LockScreenBackstopFrame"))){result.InputReady=false;result.Reason="No foreground window and Windows lock-screen backstop owns the cursor/desktop center";}
        }finally{if(previous!=IntPtr.Zero)SetThreadDpiAwarenessContext(previous);}
        return result;
    }
    static void Own(IntPtr window,int pid) { uint owner;GetWindowThreadProcessId(window,out owner);if(window==IntPtr.Zero||owner!=(uint)pid)throw new InvalidOperationException("Owned HWND required"); }
    static IntPtr InputSite(IntPtr parent) { IntPtr child=IntPtr.Zero;while((child=FindWindowEx(parent,child,null,null))!=IntPtr.Zero){var name=new StringBuilder(128);GetClassName(child,name,128);if(name.ToString()=="InputSiteWindowClass")return child;var nested=InputSite(child);if(nested!=IntPtr.Zero)return nested;}return IntPtr.Zero; }
    static void Message(IntPtr window,int pid,uint msg,uint key,IntPtr data) { Own(window,pid);UIntPtr result;if(SendMessageTimeout(window,msg,new UIntPtr(key),data,2,3000,out result)==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"Owned message timed out"); }
    public static void Key(int pid,uint key,int modifiers) {
        Own(OwnedWindow,pid);var site=InputSite(OwnedWindow);Own(site,pid);uint ignored;var target=GetWindowThreadProcessId(site,out ignored);var caller=GetCurrentThreadId();
        byte[] saved=new byte[256];if(!GetKeyboardState(saved))throw new InvalidOperationException("Keyboard snapshot unavailable");
        bool attached=false;try {
            if(modifiers!=0&&target!=caller){if(!AttachThreadInput(caller,target,true))throw new InvalidOperationException("Owned keyboard thread attachment unavailable");attached=true;}
            var state=(byte[])saved.Clone();state[0x10]=state[0x11]=state[0x12]=0;
            if((modifiers&1)!=0)state[0x11]=0x80;if((modifiers&2)!=0)state[0x10]=0x80;if((modifiers&4)!=0)state[0x12]=0x80;
            if(modifiers!=0&&!SetKeyboardState(state))throw new InvalidOperationException("Owned modifier state unavailable");
            uint[] keys={0x11,0x10,0x12};int[] masks={1,2,4};
            for(int n=0;n<3;n++)if((modifiers&masks[n])!=0)Message(site,pid,keys[n]==0x12?0x104u:0x100u,keys[n],new IntPtr(1));
            bool alt=(modifiers&4)!=0;Message(site,pid,alt?0x104u:0x100u,key,new IntPtr(alt?0x20000001:1));Message(site,pid,alt?0x105u:0x101u,key,new IntPtr(unchecked((int)(alt?0xE0000001u:0xC0000001u))));
            for(int n=2;n>=0;n--)if((modifiers&masks[n])!=0){state[keys[n]]=0;SetKeyboardState(state);Message(site,pid,keys[n]==0x12?0x105u:0x101u,keys[n],new IntPtr(unchecked((int)0xC0000001)));}
        }finally{if(modifiers!=0)SetKeyboardState(saved);if(attached){AttachThreadInput(caller,target,false);SetKeyboardState(saved);}}
    }

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string p,uint a,uint s,IntPtr z,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileInformationByHandleEx(Microsoft.Win32.SafeHandles.SafeFileHandle f,int k,byte[] b,uint n);
    public static string Identity(string path){using(var f=CreateFileW(path,0x80,7,IntPtr.Zero,3,0x02000000,IntPtr.Zero)){if(f.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());var b=new byte[24];if(!GetFileInformationByHandleEx(f,18,b,24))throw new Win32Exception(Marshal.GetLastWin32Error());return Convert.ToBase64String(b);}}
    public static void Text(int pid,string text){Own(OwnedWindow,pid);var w=InputSite(OwnedWindow);Own(w,pid);foreach(char c in text)Message(w,pid,0x102u,(uint)c,new IntPtr(1));}
}

'@
 $desktop=[BlinkObjectMenuUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady){Skip $desktop.Reason}
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary-user GUI required'}
 if(-not(Test-Path -LiteralPath $Binary -PathType Leaf)){throw 'Candidate binary missing'};$report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash.ToLowerInvariant()
 $os=Get-CimInstance Win32_OperatingSystem;$report.environment=[ordered]@{caption=$os.Caption;version=$os.Version;build=$os.BuildNumber;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;ordinary_user=$true;theme='NOT CAPTURED';ime='NOT RUN'}
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture already exists'};New-Item -ItemType Directory -Path $fixture -Force|Out-Null;$fixtureCreated=$true
 foreach($dir in $allowedDirectories|Where-Object{$_ -ine $fixture}){New-Item -ItemType Directory -Path $dir -Force|Out-Null};foreach($dir in $allowedDirectories){$directoryIdentities[$dir]=[BlinkObjectMenuUiNative]::Identity($dir)}
 foreach($path in $allowedFiles.Keys){[IO.File]::WriteAllText($path,[string]$allowedFiles[$path],[Text.UTF8Encoding]::new($false));$fileIdentities[$path]=[BlinkObjectMenuUiNative]::Identity($path)}
 $thisPcLabels=@(Resource-Labels 'Location.ThisPC');$homeLabels=@(Resource-Labels 'Nav.Home');$quickHomePath=[Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
 if(-not $thisPcLabels.Count -or -not $homeLabels.Count -or -not $quickHomePath){throw 'Existing localized ThisPC/Home labels and real profile path required'}
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false;foreach($name in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')){$psi.EnvironmentVariables.Remove($name)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned launch failed'};$ownedHandle=$process.Handle;$report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)};[BlinkObjectMenuUiNative]::OwnedWindow=$process.MainWindowHandle;$report.window_dpi=[BlinkObjectMenuUiNative]::GetDpiForWindow($process.MainWindowHandle)
 Wait-Ui 'real initial ThisPC locations' {Is-ThisPc};$initialTab=Selected-Tab;$initialAddress=Address-Text
 Check '01 actual Apps key opens a dedicated drive menu without navigating or ordinary file actions' {
  $driveRows=@(Rows|Where-Object{$_.Current.Name -match '(?i)[a-z]:'});if(-not $driveRows.Count){Skip 'No actual local drive row exposed by this host'}
  $preferred=[IO.Path]::GetPathRoot($project).Substring(0,2);$matched=@($driveRows|Where-Object{$_.Current.Name -match [regex]::Escape($preferred)});$script:driveRow=$(if($matched.Count){$matched[0]}else{$driveRows[0]})
  $script:driveRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$before=@(Selected-Names);$menu=Menu-Open $script:driveRow 'LocationContextMenu'
  $script:driveRoot=$menu.Current.HelpText;if($script:driveRoot -notmatch '^[A-Za-z]:\\$' -or -not(Test-Path -LiteralPath $script:driveRoot -PathType Container)){throw 'Actual menu did not capture an available drive root'}
  Assert-FolderMenu 'LocationContextMenu' $script:driveRoot;if(-not(Is-ThisPc) -or -not(Same-Names @(Selected-Names) $before)){throw 'Opening drive menu navigated or changed its selected row'}
 }
 Check '02 drive NewTab and Open use the exact captured root and preserve the previous tab' {
  $count=@(Tabs).Count;Act (Owned-Id 'TargetContextNewTab');Wait-Ui 'drive opens exact root in new tab' {@(Tabs).Count -eq $count+1 -and (Address-Text) -ieq $script:driveRoot -and (Ready)}
  $report.tab_evidence+=@([pscustomobject]@{step=$script:currentStep;target=$script:driveRoot;before=$count;after=@(Tabs).Count;actual_address=(Address-Text)})
  Select-Tab $initialTab $initialAddress;$script:driveRow=@(Rows|Where-Object{$_.Current.Name -match [regex]::Escape($script:driveRoot.Substring(0,2))})[0]
  $script:driveRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$null=Menu-Open $script:driveRow 'LocationContextMenu' $true;Assert-FolderMenu 'LocationContextMenu' $script:driveRoot
  Act (Owned-Id 'TargetContextOpen');Wait-Ui 'drive Open enters exact root in same tab' {@(Tabs).Count -eq $count+1 -and (Address-Text) -ieq $script:driveRoot -and (Ready)}
 }
 Check '03 virtual ThisPC tree target has only applicable commands and NewTab stays virtual' {
  Wait-Ui 'actual ThisPC tree root' {$null -ne (Tree-Item $thisPcLabels)};$before=Address-Text;$count=@(Tabs).Count;$root=Tree-Item $thisPcLabels
  $menu=Menu-Open $root 'NavigationContextMenu';if($menu.Current.HelpText -notin $thisPcLabels -or (Owned-Id 'TargetContextCopyPath') -or (Owned-Id 'TargetContextProperties') -or -not(Owned-Id 'TargetContextExpandCollapse')){throw 'ThisPC virtual target exposed a filesystem path/property or lost tree actions'}
  if((Address-Text) -ine $before){throw 'Virtual tree context navigated before invocation'};Act (Owned-Id 'TargetContextNewTab');Wait-Ui 'virtual root NewTab remains ThisPC' {@(Tabs).Count -eq $count+1 -and (Is-ThisPc)}
 }
 Check '04 directory tree ExpandCollapse changes the tree without navigating or disturbing list selection' {
  Navigate $source;Select-Marker;Wait-Ui 'exact owned source node revealed' {$null -ne (Tree-Item @([IO.Path]::GetFileName($source)))}
  $peer=Tree-Item @([IO.Path]::GetFileName($source));$beforeState=$peer.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Current.ExpandCollapseState
  if($beforeState -eq [Windows.Automation.ExpandCollapseState]::LeafNode){throw 'Owned source with child must be expandable'}
  $null=Menu-Open $peer 'NavigationContextMenu';Assert-FolderMenu 'NavigationContextMenu' $source;if((Address-Text) -ine $source -or -not(Same-Names @(Selected-Names) @($markerName))){throw 'Tree menu changed current folder or file-list selection'}
  Act (Owned-Id 'TargetContextExpandCollapse');Wait-Ui 'actual tree expansion state toggled' {$p=Tree-Item @([IO.Path]::GetFileName($source));$p -and $p.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Current.ExpandCollapseState -ne $beforeState}
  if((Address-Text) -ine $source -or -not(Same-Names @(Selected-Names) @($markerName))){throw 'Tree expansion navigated or changed file selection'}
  $peer=Tree-Item @([IO.Path]::GetFileName($source));if($peer.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Current.ExpandCollapseState -eq [Windows.Automation.ExpandCollapseState]::Collapsed){$null=Menu-Open $peer 'NavigationContextMenu';Act (Owned-Id 'TargetContextExpandCollapse')}
  Wait-Ui 'owned direct child appears in tree' {$null -ne (Tree-Item @([IO.Path]::GetFileName($child)))}
 }
 Check '05 a child tree NewTab enters that child and leaves the original listing and selection intact' {
  $original=Selected-Tab;$count=@(Tabs).Count;$peer=Tree-Item @([IO.Path]::GetFileName($child));$null=Menu-Open $peer 'NavigationContextMenu' $true;Assert-FolderMenu 'NavigationContextMenu' $child
  if((Address-Text) -ine $source -or -not(Same-Names @(Selected-Names) @($markerName))){throw 'Child context borrowed or changed file selection'}
  Act (Owned-Id 'TargetContextNewTab');Wait-Ui 'child NewTab committed actual child' {@(Tabs).Count -eq $count+1 -and (Address-Text) -ieq $child -and (Ready) -and @(Row-Names) -ccontains $childMarkerName}
  Select-Tab $original $source;if(-not(Same-Names @(Selected-Names) @($markerName))){throw 'Original tab lost its exact selected file'};Fixture-Unchanged
 }
 Check '06 directory tree Open uses the target rather than the current selected file row' {
  Wait-Ui 'owned child available after tab restoration' {$null -ne (Tree-Item @([IO.Path]::GetFileName($child)))};$null=Menu-Open (Tree-Item @([IO.Path]::GetFileName($child))) 'NavigationContextMenu';Assert-FolderMenu 'NavigationContextMenu' $child
  $count=@(Tabs).Count;Act (Owned-Id 'TargetContextOpen');Wait-Ui 'tree Open entered captured child in same tab' {@(Tabs).Count -eq $count -and (Address-Text) -ieq $child -and (Ready) -and @(Row-Names) -ccontains $childMarkerName};Fixture-Unchanged
 }
 Check '07 quick Home menu captures the real profile folder and its Open command enters that folder' {
  Wait-Ui 'real quick Home list item' {$null -ne (Quick-Home)};$before=Address-Text;$null=Menu-Open (Quick-Home) 'NavigationContextMenu';Assert-FolderMenu 'NavigationContextMenu' $quickHomePath
  if(Owned-Id 'TargetContextExpandCollapse'){throw 'Quick location has no independent tree expansion state'};if((Address-Text) -ine $before){throw 'Quick context navigated before invoking Open'}
  Act (Owned-Id 'TargetContextOpen');Wait-Ui 'quick Home Open committed real user profile' {(Address-Text) -ieq $quickHomePath -and (Ready)}
 }
 Check '08 Escape dismisses the quick context menu and returns focus to the navigation area' {
  $before=Address-Text;$null=Menu-Open (Quick-Home) 'NavigationContextMenu' $true;Dismiss-Menu
  Wait-Ui 'actual owned navigation focus after Escape' {$focus=[Windows.Automation.AutomationElement]::FocusedElement;$focus -and $focus.Current.ProcessId -eq $process.Id -and $focus.Current.HasKeyboardFocus -and (Is-OwnDescendant $focus (By-Id 'NavigationPane'))}
  if((Address-Text) -ine $before){throw 'Menu dismissal changed current directory'};Fixture-Unchanged
 }
 $notes.Add('Menu opening uses actual scoped Apps/Shift+F10. UIA Invoke proves menu actions, not physical right-click hit testing. Properties/CopyPath are checked for presence, target and enablement; no actual clipboard write or native property-sheet interaction was performed.')
 $report.status='PASS'
}catch{
 if($_.Exception.Message.StartsWith('OBJECT_MENU_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring('OBJECT_MENU_UI_SKIP: '.Length)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{Capture-State 'unexpected failure'}catch{}}
}finally{
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if($report.before_close_alive){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');Save-Report;$report.close_request_result=$process.CloseMainWindow();Save-Report;if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned Blink still running PID '+$process.Id+'; no force termination')}}
  $process.Refresh();$report.process_exited=$process.HasExited;if($process.HasExited){$report.exit_code=$process.ExitCode;$report.exit_code_hex='0x{0:X8}' -f ([long]$report.exit_code -band 4294967295);$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o')}
  if($report.exit_code -ne 0){throw ('Owned Blink abnormal exit '+$report.exit_code_hex)};if($report.status -eq 'PASS'){Record '09 owned object-menu window closes normally and exits zero'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '09 normal owned close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process remains active; retain fixture'};$report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
  $report.fixture_bytes_removed=[long]((Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture);if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '10 exact GUID paths identities bytes and attributes verified before cleanup'}
 }catch{$report.status='FAIL';$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '10 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=[long]((Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -eq 'SKIP';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI OBJECT MENU '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -ne 'PASS'){exit 2};exit 0
