[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Binary='',
 [ValidateRange(5,90)][int]$StepTimeoutSeconds=30
)
# G03c: only a newly launched PID/HWND and exact GUID-owned directories/files.
# No build, global input/Clipboard, service/configuration edits or forced exit.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$integration=Join-Path $project '.build\explorer-address-completion\integration'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-address-completion-report.json'
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $integration ('ui-completion-fixtures\owned-'+$guid)
$source=Join-Path $fixture ('catalog-'+$token)
$anchorName='00-root-'+$token;$anchor=Join-Path $source $anchorName
$alphaA=Join-Path $source ('alpha-A-'+$token);$alphaB=Join-Path $source ('alpha-B-'+$token)
$beta=Join-Path $source ('beta-'+$token);$unicode=Join-Path $source ('资料-é-'+$token)
$hidden=Join-Path $source ('alpha-hidden-'+$token);$inside=Join-Path $alphaA ('inside-'+$token)
$empty=Join-Path $fixture ('empty-'+$token)
$capDirectories=@(0..17|ForEach-Object{Join-Path $source ('cap-{0:D2}-{1}' -f $_,$token)})
$allowedDirectories=@($fixture,$source,$anchor,$alphaA,$alphaB,$beta,$unicode,$hidden,$inside,$empty)+$capDirectories
$allowedFiles=@{};$markerNames=@{};$markerPaths=@{};$fileIdentities=@{}
$markerNames[$source]='root-'+$token+'.txt';$markerPaths[$source]=Join-Path $source $markerNames[$source]
$allowedFiles[$markerPaths[$source]]='Blink completion root '+$guid
$number=0
foreach($directory in @($alphaA,$alphaB,$beta,$unicode,$hidden,$inside)){
 $name='leaf-'+$number+'-'+$token+'.txt';$number++
 $markerNames[$directory]=$name;$markerPaths[$directory]=Join-Path $directory $name
 $allowedFiles[$markerPaths[$directory]]='Blink completion '+$guid+' '+[IO.Path]::GetFileName($directory)
}
$matchingFile=Join-Path $source ('alpha-file-'+$token+'.txt');$allowedFiles[$matchingFile]='Not a directory '+$guid
$refreshName='00-refresh-'+$token;$refreshPath=Join-Path $source $refreshName
$initialFiles=@($allowedFiles.Keys)
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP
$process=$null;$fixtureCreated=$false;$tempChanged=$false;$script:windowCache=$null;$script:currentStep='preflight';$script:refreshCreated=$false
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new()
$report=[ordered]@{
 status='RUNNING';passed=$false;skipped=$false;configuration=$Configuration;binary=$Binary;binary_sha256=$null
 source_script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
 started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;current_step=$script:currentStep;failure=$null;checks=@();notes=@()
 process_id=$null;process_exited=$false;exit_code=$null;exit_code_hex=$null;forced_termination=$false;before_close_alive=$null;close_requested=$false;close_request_utc=$null;close_request_result=$null;process_exit_observed_utc=$null
 environment=$null;window_dpi=$null;desktop_preflight=$null;focus_evidence=@();completion_snapshots=@();keyboard_evidence=@();tab_completion=$null;stale_peer=$null;automatic_refresh=$null;draft_close=$null;last_ui_snapshot=$null;last_focus_attempt=$null
 fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_ownership_failures=@();fixture_bytes_removed=0
 compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0
 human_acceptance='NOT RUN';model_test_results='NOT RUN by this UI smoke'
 input_mode='Owned PID/HWND UIA and InputSite keyboard messages only; no global input or Clipboard'
 not_run=@('real IME composition/candidate interaction','physical mouse hit testing','UNC/network/access-denied and genuinely blocked filesystem cancellation','full theme/DPI/multi-monitor/language/Narrator matrix','Windows 11 24H2 Explorer comparison')
}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($true))}
function Record([string]$Name,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Name;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('COMPLETION_UI_SKIP: '+$Why)}
function Check([string]$Name,[scriptblock]$Body){$script:currentStep=$Name;Save-Report;try{$null=&$Body;Record $Name}catch{Record $Name 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace);throw}}
function Window{$process.Refresh();if($process.HasExited){throw ('Owned Blink exited '+$process.ExitCode)};if(-not $script:windowCache -and $process.MainWindowHandle -ne [IntPtr]::Zero){$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)};if($script:windowCache -and $script:windowCache.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'};return $script:windowCache}
function Visible($Peer){return $null -ne $Peer -and -not $Peer.Current.IsOffscreen -and $Peer.Current.BoundingRectangle.Width -gt 0 -and $Peer.Current.BoundingRectangle.Height -gt 0}
function Find-In($Root,[string]$Id){if($Root){foreach($peer in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)))){if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer)){return $peer}}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){$found=By-Id $Id;if($found){return $found};foreach($h in [BlinkAddressCompletionUiNative]::OwnedTopWindows($process.Id)){try{$root=[Windows.Automation.AutomationElement]::FromHandle($h);if($root.Current.ProcessId -ne $process.Id){throw 'Owned root PID mismatch'};if($root.Current.AutomationId -ceq $Id -and (Visible $root)){return $root};$found=Find-In $root $Id;if($found){return $found}}catch{if([BlinkAddressCompletionUiNative]::WindowExists($h)){throw}}};return $null}
function Peer-Evidence($Peer){if(-not $Peer){return [pscustomobject]@{found=$false}};$c=$Peer.Current;if(-not $process -or $c.ProcessId -ne $process.Id){return [pscustomobject]@{found=$true;pid=$c.ProcessId;role=$c.ControlType.ProgrammaticName;outside_owned_process=$true}};return [pscustomobject]@{found=$true;id=$c.AutomationId;name=$c.Name;help_text=$c.HelpText;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;pid=$c.ProcessId;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;has_keyboard_focus=$c.HasKeyboardFocus;runtime_id=($Peer.GetRuntimeId()-join ',')}}
function Editor{return By-Id 'AddressEditor'}
function Editor-Text{return (Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Address-Text{$peer=By-Id 'AddressBar';if(-not $peer){throw 'Owned AddressBar missing'};return $peer.Current.HelpText}
function Entries{return By-Id 'FolderEntries'}
function Row-Names{$peer=Entries;if(-not $peer){return @()};return @($peer.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_)}|ForEach-Object{$_.Current.Name})}
function Selected-Names{$peer=Entries;if(-not $peer){return @()};return @($peer.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}
function Ready{return (Visible (Entries)) -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}
function Root-Still-Visible{return (Visible (Entries)) -and @(Row-Names) -ccontains $anchorName}
function Suggestions{return Owned-Id 'AddressSuggestions'}
function Suggestion-Items{$list=Suggestions;if(-not $list){return @()};$items=@($list.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_) -and $_.Current.AutomationId -cmatch '^AddressSuggestion[0-9]+$'});return @($items|Sort-Object{[int]$_.Current.AutomationId.Substring('AddressSuggestion'.Length)})}
function Suggestion-Paths{return @(Suggestion-Items|ForEach-Object{$_.Current.HelpText})}
function Selected-Suggestions{$list=Suggestions;if(-not $list){return @()};return @($list.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection())}
function Selected-Suggestion-Paths{return @(Selected-Suggestions|ForEach-Object{$_.Current.HelpText})}
function Exact-Paths([string[]]$Actual,[string[]]$Expected){$a=@($Actual|Sort-Object);$b=@($Expected|Sort-Object);return $a.Count -eq $b.Count -and ($a -join "`n") -ieq ($b -join "`n")}
function Suggestion-Target([string]$Target){if($Target -notin $allowedDirectories){throw 'Unowned suggestion target'};$matches=@(Suggestion-Items|Where-Object{$_.Current.HelpText -ieq $Target});if($matches.Count -ne 1){throw ('Exact suggestion target not unique '+$Target)};return $matches[0]}
function Capture-State([string]$Why){
 $state=[ordered]@{why=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;draft=$null;rows=@();selected=@();suggestions=@();selected_suggestions=@();peers=@();focus=$null;desktop=$null;errors=@()}
 try{$state.address=Address-Text;if(Editor){$state.draft=Editor-Text};$state.rows=@(Row-Names);$state.selected=@(Selected-Names);$state.suggestions=@(Suggestion-Paths);$state.selected_suggestions=@(Selected-Suggestion-Paths)}catch{$state.errors+=@($_.Exception.Message)}
 foreach($id in @('AddressBar','AddressEditor','AddressSuggestions','AddressSuggestion0','AddressSuggestion1','WindowTabs','FolderEntries')){try{$peer=Owned-Id $id;if($peer){$state.peers+=@(Peer-Evidence $peer)}}catch{$state.errors+=@($id+': '+$_.Exception.Message)}}
 try{$state.focus=Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement);$state.desktop=[BlinkAddressCompletionUiNative]::DesktopState()}catch{$state.errors+=@($_.Exception.Message)}
 $report.last_ui_snapshot=[pscustomobject]$state;Save-Report
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('COMPLETION_UI_SKIP: ') -or $_.Exception.Message.StartsWith('COMPLETION_TEST_PRECONDITION_NOT_MET: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);Capture-State $Why;throw ('Timed out: '+$Why+'; '+$last)}
function Require-Owned-Desktop([string]$Why){$desktop=[BlinkAddressCompletionUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id){throw ('COMPLETION_TEST_PRECONDITION_NOT_MET: '+$Why+'; expected PID '+$process.Id+', actual foreground '+$desktop.ForegroundPid+'; '+$desktop.Reason)};return $desktop}
function Key([uint32]$Code,[int]$Modifiers=0){$desktop=Require-Owned-Desktop 'before scoped key';[BlinkAddressCompletionUiNative]::Key($process.Id,$Code,$Modifiers);$report.keyboard_evidence+=@([pscustomobject]@{step=$script:currentStep;key=$Code;modifiers=$Modifiers;foreground_pid=$desktop.ForegroundPid});Save-Report}
function Focus-Owned([string]$Id){$peer=Owned-Id $Id;if(-not(Visible $peer) -or -not $peer.Current.IsEnabled){throw ('Visible owned focus target missing '+$Id)};$runtime=$peer.GetRuntimeId()-join ',';$peer.SetFocus();$proof=@{value=$null};Wait-Ui ('actual focus '+$Id) {$focused=[Windows.Automation.AutomationElement]::FocusedElement;$desktop=[BlinkAddressCompletionUiNative]::DesktopState();$report.last_focus_attempt=[pscustomobject]@{target=$Id;expected=(Peer-Evidence $peer);actual=(Peer-Evidence $focused);desktop=$desktop};if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id -or -not $focused){return $false};$c=$focused.Current;if($c.ProcessId -ne $process.Id -or -not $c.HasKeyboardFocus -or ($focused.GetRuntimeId()-join ',') -cne $runtime){return $false};$proof.value=[pscustomobject]@{step=$script:currentStep;stage='explicit owned focus';focus=(Peer-Evidence $focused);foreground_pid=$desktop.ForegroundPid};return $true};$report.focus_evidence+=@($proof.value);Save-Report}
function Assert-Editor-Focus([string]$Stage){$proof=@{value=$null};Wait-Ui ('actual editor focus '+$Stage) {$desktop=Require-Owned-Desktop $Stage;$focused=[Windows.Automation.AutomationElement]::FocusedElement;if(-not $focused){return $false};$c=$focused.Current;if($c.ProcessId -ne $process.Id -or $c.AutomationId -cne 'AddressEditor' -or -not $c.HasKeyboardFocus){return $false};$proof.value=[pscustomobject]@{step=$script:currentStep;stage=$Stage;focus=(Peer-Evidence $focused);foreground_pid=$desktop.ForegroundPid};return $true};$report.focus_evidence+=@($proof.value);Save-Report}
function Enter-Edit{Focus-Owned 'AddressHistoryButton';Key 0x4C 1;Wait-Ui 'owned address editor visible' {$null -ne (Editor)};Assert-Editor-Focus 'after Ctrl L'}
function Set-Draft([string]$Text){$peer=Editor;if(-not(Visible $peer)){throw 'Owned visible address editor required'};$peer.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Text);Wait-Ui 'draft value reflected without navigation' {(Editor-Text) -ceq $Text -and (Address-Text) -ceq $Text}}
function Type-Draft([string]$Text){Assert-Editor-Focus 'before real scoped text';$null=Require-Owned-Desktop 'before real scoped text';[BlinkAddressCompletionUiNative]::Text($process.Id,$Text);Wait-Ui 'real WM_CHAR text reflected' {(Editor-Text) -ceq $Text};$report.keyboard_evidence+=@([pscustomobject]@{step=$script:currentStep;operation='owned WM_CHAR Unicode text';text=$Text});Save-Report}
function Verify-Bytes([string]$Path){if(-not $allowedFiles.ContainsKey($Path) -or -not(Test-Path -LiteralPath $Path -PathType Leaf)){throw 'Owned payload file missing'};$expected=[Text.UTF8Encoding]::new($false).GetBytes([string]$allowedFiles[$Path]);if([Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) -cne [Convert]::ToBase64String($expected)){throw ('Owned payload bytes changed '+$Path)};if($fileIdentities.ContainsKey($Path) -and [BlinkAddressCompletionUiNative]::Identity($Path) -cne $fileIdentities[$Path]){throw ('Owned file identity changed '+$Path)}}
function Navigate([string]$Target){if($Target -notin $allowedDirectories){throw 'Navigation outside GUID fixture'};Enter-Edit;Set-Draft $Target;Key 13;Wait-Ui 'exact owned folder committed' {-not(Editor) -and (Address-Text) -ieq $Target -and (Ready) -and $(if($Target -ieq $source){Root-Still-Visible}elseif($markerNames.ContainsKey($Target)){@(Row-Names) -ccontains $markerNames[$Target]}else{@(Row-Names).Count -eq 0})}}
function Begin-Prefix([string]$Prefix){Navigate $source;Enter-Edit;Set-Draft $Prefix}
function Wait-Suggestions([string[]]$Expected){Wait-Ui 'exact current prefix suggestions and editor focus' {$null -ne (Suggestions) -and (Exact-Paths @(Suggestion-Paths) $Expected)};Assert-Editor-Focus 'suggestions do not steal focus';if(-not(Root-Still-Visible)){throw 'Suggestions navigated before acceptance'};$snapshot=[pscustomobject]@{step=$script:currentStep;utc=[DateTime]::UtcNow.ToString('o');draft=(Editor-Text);paths=@(Suggestion-Paths);selected=@(Selected-Suggestion-Paths);focus=(Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement))};$report.completion_snapshots+=@($snapshot);Save-Report}
function Expect-NoSuggestions([string]$Draft){$quiet=[Diagnostics.Stopwatch]::StartNew();Wait-Ui 'no popup for unmatched input across debounce interval' {if(Suggestions){$quiet.Restart();return $false};$null=Require-Owned-Desktop 'unmatched input stability';(Editor-Text) -ceq $Draft -and (Root-Still-Visible) -and $quiet.ElapsedMilliseconds -ge 500};Assert-Editor-Focus 'unmatched input retains editor'}
function Tabs{return @((By-Id 'WindowTabs').FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TabItem)))}
function Act($Peer){if(-not(Visible $Peer) -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw 'Owned enabled action required'};$pattern=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}elseif($Peer.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)){$pattern.Toggle()}elseif($Peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$pattern.Expand()}else{throw ('No actual Invoke/Toggle/Expand pattern '+$Peer.Current.AutomationId)}}
function Set-Hidden([bool]$Show){if(Editor){Key 27;Wait-Ui 'editor cancelled before View policy' {-not(Editor)}};Focus-Owned 'FolderViewMode';Act (By-Id 'FolderViewMode');Wait-Ui 'View Show submenu' {$null -ne (Owned-Id 'TopViewShow')};Act (Owned-Id 'TopViewShow');Wait-Ui 'actual Hidden option' {$null -ne (Owned-Id 'TopViewShowHidden')};$option=Owned-Id 'TopViewShowHidden';$pattern=$null;if($option.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)){$current=$pattern.Current.ToggleState -eq [Windows.Automation.ToggleState]::On}elseif($option.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)){$current=$pattern.Current.IsSelected}else{throw 'Hidden option has no actual toggle/selection pattern'};if($current -ne $Show){Act $option}else{Key 27;Key 27};Wait-Ui 'View policy menus dismissed' {-not(Owned-Id 'TopViewMenu') -and -not(Owned-Id 'TopViewShowHidden')};Wait-Ui 'folder commands ready after hidden policy' {Ready}}
function Ownership-Failures{
 $bad=[Collections.Generic.List[string]]::new();$pending=[Collections.Generic.Stack[string]]::new();$mask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
 $entry=Get-Item -LiteralPath $fixture -Force;if(-not $entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($entry.Attributes -band $mask)){return @('Fixture root changed')};$pending.Push($fixture)
 while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){$bad.Add('Reparse entry: '+$item.FullName);continue};if($item.PSIsContainer){$expected=$(if($item.FullName -ieq $hidden){[IO.FileAttributes]::Hidden}else{0});if($item.FullName -notin $allowedDirectories -or ($item.Attributes -band $mask) -ne $expected){$bad.Add('Unknown directory/attributes: '+$item.FullName)}else{$pending.Push($item.FullName)}}elseif(-not $allowedFiles.ContainsKey($item.FullName) -or ($item.Attributes -band $mask)){$bad.Add('Unknown file/attributes: '+$item.FullName)}else{try{Verify-Bytes $item.FullName}catch{$bad.Add($_.Exception.Message)}}}}
 foreach($directory in $allowedDirectories){if(-not(Test-Path -LiteralPath $directory -PathType Container)){$bad.Add('Missing directory: '+$directory)}};foreach($file in $initialFiles){if(-not(Test-Path -LiteralPath $file -PathType Leaf)){$bad.Add('Missing payload: '+$file)}};if($script:refreshCreated -and -not(Test-Path -LiteralPath $refreshPath -PathType Container)){$bad.Add('Missing refresh directory')};return @($bad.ToArray())
}
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
public sealed class BlinkCompletionUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkAddressCompletionUiNative {
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
    public static BlinkCompletionUiDesktop DesktopState() {
        var result=new BlinkCompletionUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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
 $desktop=[BlinkAddressCompletionUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady){Skip $desktop.Reason}
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary-user GUI required'}
 if(-not(Test-Path -LiteralPath $Binary -PathType Leaf)){throw 'Candidate binary missing'};$report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash.ToLowerInvariant()
 $os=Get-CimInstance Win32_OperatingSystem;$report.environment=[ordered]@{caption=$os.Caption;version=$os.Version;build=$os.BuildNumber;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;ordinary_user=$true;theme='NOT CAPTURED';ime='NOT RUN'}
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture already exists'};New-Item -ItemType Directory -Path $fixture -Force|Out-Null;$fixtureCreated=$true
 foreach($directory in $allowedDirectories|Where-Object{$_ -ine $fixture}){New-Item -ItemType Directory -Path $directory -Force|Out-Null}
 foreach($path in $initialFiles){[IO.File]::WriteAllText($path,[string]$allowedFiles[$path],[Text.UTF8Encoding]::new($false));$fileIdentities[$path]=[BlinkAddressCompletionUiNative]::Identity($path)}
 [IO.File]::SetAttributes($hidden,[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::Hidden)
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false;foreach($name in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')){$psi.EnvironmentVariables.Remove($name)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned launch failed'};$ownedHandle=$process.Handle;$report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)};[BlinkAddressCompletionUiNative]::OwnedWindow=$process.MainWindowHandle;$report.window_dpi=[BlinkAddressCompletionUiNative]::GetDpiForWindow($process.MainWindowHandle)
 $thisPcLabels=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq 'Location.ThisPC'){$thisPcLabels+=@([string]$data.value)}}}
 Check '01 absolute directory prefix excludes ordinary files hidden siblings and descendants' {
  Navigate $source;Set-Hidden $false;Enter-Edit;Set-Draft (Join-Path $source 'ALPHA');Wait-Suggestions @($alphaA,$alphaB)
  if(@(Suggestion-Paths) -icontains $matchingFile -or @(Suggestion-Paths) -icontains $hidden -or @(Suggestion-Paths) -icontains $inside){throw 'Completion leaked a file hidden sibling or descendant'}
 }
 Check '02 a broad prefix exposes exactly twelve navigable directory candidates' {
  Set-Draft (Join-Path $source 'cap-');Wait-Ui 'actual bounded cap suggestions' {$paths=@(Suggestion-Paths);$paths.Count -ge 1 -and $paths.Count -le 12 -and @($paths|Where-Object{$_ -notin $capDirectories}).Count -eq 0}
  # A short popup may virtualize/offscreen later rows. Walk real selection
  # rather than treating only its visible viewport as the full candidate count.
  $firstTwelve=@($capDirectories|Select-Object -First 12);$seen=@{}
  for($i=0;$i -lt 18;$i++){
   Key 0x28;Wait-Ui 'cap keyboard choice stays visible and within the first twelve real folders' {$selected=@(Selected-Suggestions);$selected.Count -eq 1 -and $selected[0].Current.HelpText -in $firstTwelve -and (Visible $selected[0])}
   $selected=@(Selected-Suggestion-Paths);$seen[$selected[0]]=$true
  }
  Assert-Editor-Focus 'bounded suggestions';if($seen.Count -ne 12 -or -not(Root-Still-Visible)){throw 'Keyboard could not reach exactly the bounded twelve suggestions without navigation'}
  $report.completion_snapshots+=@([pscustomobject]@{step=$script:currentStep;draft=(Editor-Text);seen_keyboard_targets=@($seen.Keys);max_expected=12});Save-Report
 }
 Check '03 changing the draft retires old results and unmatched input does not navigate' {
  Set-Draft (Join-Path $source 'alpha');Wait-Suggestions @($alphaA,$alphaB);Set-Draft (Join-Path $source 'beta');Wait-Suggestions @($beta)
  $missing=Join-Path $source ('not-present-'+$token);Set-Draft $missing;Expect-NoSuggestions $missing
 }
 Check '04 actual scoped Unicode text completes a relative directory prefix' {
  Set-Draft '';Assert-Editor-Focus 'before Unicode prefix';Type-Draft '资料-é-';Wait-Suggestions @($unicode)
 }
 Check '05 Down and Up select suggestions while the editor retains draft focus and directory' {
  $draft=Join-Path $source 'alpha';Set-Draft $draft;Wait-Suggestions @($alphaA,$alphaB);$paths=@(Suggestion-Paths)
  Key 0x28;Wait-Ui 'Down selected actual first suggestion' {(Exact-Paths @(Selected-Suggestion-Paths) @($paths[0]))};Assert-Editor-Focus 'after Down'
  Key 0x28;Wait-Ui 'second Down selected actual second suggestion' {(Exact-Paths @(Selected-Suggestion-Paths) @($paths[1]))};Assert-Editor-Focus 'after second Down'
  Key 0x26;Wait-Ui 'Up returned to actual first suggestion' {(Exact-Paths @(Selected-Suggestion-Paths) @($paths[0]))};Assert-Editor-Focus 'after Up'
  if((Editor-Text) -cne $draft -or -not(Root-Still-Visible)){throw 'Arrow choice edited or submitted the draft'}
 }
 Check '06 Tab fills the chosen directory without submitting or leaving the editor' {
  $selected=@(Selected-Suggestion-Paths);if($selected.Count -ne 1 -or $selected[0] -notin @($alphaA,$alphaB)){throw 'Actual selected owned directory required'};$target=$selected[0];Key 9
  Wait-Ui 'Tab filled exact target path with optional directory separator' {$text=Editor-Text;($text -ieq $target -or $text -ieq ($target+'\')) -and (Root-Still-Visible)};Assert-Editor-Focus 'after Tab fill'
  $report.tab_completion=[pscustomobject]@{target=$target;actual_draft=(Editor-Text);root_listing_still_visible=(Root-Still-Visible);navigation_submitted=$false};Save-Report
 }
 Check '07 explicit selected Enter and actual candidate Invoke navigate to their own targets' {
  Begin-Prefix (Join-Path $source 'alpha');Wait-Suggestions @($alphaA,$alphaB);$paths=@(Suggestion-Paths);Key 0x28;Key 0x28;Key 13
  $target=$paths[1];Wait-Ui 'selected Enter committed exact target' {-not(Editor) -and (Address-Text) -ieq $target -and (Ready) -and @(Row-Names) -ccontains $markerNames[$target]};Verify-Bytes $markerPaths[$target]
  Begin-Prefix (Join-Path $source 'beta');Wait-Suggestions @($beta);$candidate=Suggestion-Target $beta;$invoke=$null;if(-not $candidate.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$invoke)){throw 'Candidate lacks its real UIA Invoke action; no SelectionItem substitution used'};$invoke.Invoke()
  Wait-Ui 'candidate Invoke committed exact target' {-not(Editor) -and (Address-Text) -ieq $beta -and (Ready) -and @(Row-Names) -ccontains $markerNames[$beta]};Verify-Bytes $markerPaths[$beta]
 }
 Check '08 Enter without a selected suggestion submits the actual typed parent path' {
  Begin-Prefix ($alphaA+'\');Wait-Suggestions @($inside)
  if(@(Selected-Suggestion-Paths).Count){throw 'Suggestions were automatically selected before explicit keyboard choice'};Key 13
  Wait-Ui 'unselected Enter entered typed folder rather than its child suggestion' {-not(Editor) -and (Address-Text).TrimEnd('\') -ieq $alphaA -and (Ready) -and @(Row-Names) -ccontains $markerNames[$alphaA]};Verify-Bytes $markerPaths[$alphaA]
 }
 Check '09 Escape closes suggestions restores committed path and preserves file selection' {
  Navigate $source;$rootRow=@((Entries).FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.Name -ceq $anchorName -and (Visible $_)})
  if($rootRow.Count -ne 1){throw 'Unique owned root anchor row missing'};$rootRow[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  Enter-Edit;Set-Draft (Join-Path $source 'alpha');Wait-Suggestions @($alphaA,$alphaB);Key 27
  Wait-Ui 'Escape restored committed root and dismissed suggestions' {-not(Editor) -and -not(Suggestions) -and (Address-Text) -ieq $source -and (Root-Still-Visible) -and @(Selected-Names) -ccontains $anchorName};Verify-Bytes $markerPaths[$source]
 }
 Check '10 completion follows the real Show Hidden policy and reverts to the old policy' {
  Navigate $source;Set-Hidden $true;Enter-Edit;Set-Draft (Join-Path $source 'alpha');Wait-Suggestions @($alphaA,$alphaB,$hidden)
  Key 27;Wait-Ui 'editor cancelled before restoring Hidden' {-not(Editor)};Set-Hidden $false;Enter-Edit;Set-Draft (Join-Path $source 'alpha');Wait-Suggestions @($alphaA,$alphaB)
 }
 Check '11 switching tabs retires the popup and a stale suggestion cannot navigate the new tab' {
  Key 27;Wait-Ui 'editor cancelled before tab setup' {-not(Editor)};$count=@(Tabs).Count;Focus-Owned 'AddressHistoryButton';Key 0x54 1
  Wait-Ui 'temporary second tab exists' {@(Tabs).Count -eq $count+1 -and $thisPcLabels -ccontains (Address-Text)};Navigate $empty
  $tabs=@(Tabs);$tabs[$count-1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Wait-Ui 'original root tab active' {-not(Editor) -and (Address-Text) -ieq $source -and (Ready)}
  Enter-Edit;Set-Draft (Join-Path $source 'alpha');Wait-Suggestions @($alphaA,$alphaB);$old=Suggestion-Target $alphaA;$oldEvidence=Peer-Evidence $old
  $tabs=@(Tabs);$tabs[$count].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Wait-Ui 'tab switch retired completion popup' {-not(Editor) -and -not(Suggestions) -and (Address-Text) -ieq $empty -and (Ready) -and @(Row-Names).Count -eq 0}
  $outcome='No stale Invoke pattern';try{$invoke=$null;if($old.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$invoke)){$invoke.Invoke();$outcome='Invoke returned'}}catch{$outcome='Old provider rejected: '+$_.Exception.Message}
  $null=(By-Id 'FolderViewMode').Current.HelpText
  if((Address-Text) -ine $empty -or (Editor) -or (Suggestions) -or -not(Ready) -or @(Row-Names).Count){throw 'Stale suggestion changed the new tab'}
  $report.stale_peer=[pscustomobject]@{old=$oldEvidence;outcome=$outcome;new_tab_address=(Address-Text)};Save-Report
  Focus-Owned 'AddressHistoryButton';Key 0x57 1;Wait-Ui 'temporary tab closed back to root' {@(Tabs).Count -eq $count -and (Address-Text) -ieq $source -and (Ready)}
 }
 Check '12 a real watcher directory creation preserves the unsubmitted draft and editor focus' {
  Enter-Edit;$draft=Join-Path $source 'alpha';Set-Draft $draft;Wait-Suggestions @($alphaA,$alphaB);$null=Require-Owned-Desktop 'before real watcher directory creation'
  $null=[IO.Directory]::CreateDirectory($refreshPath);$script:refreshCreated=$true;$script:allowedDirectories+=@($refreshPath)
  Wait-Ui 'actual file-list refresh retained draft and current suggestions' {$null=Require-Owned-Desktop 'during watcher refresh';@(Row-Names) -ccontains $refreshName -and (Editor-Text) -ceq $draft -and (Exact-Paths @(Suggestion-Paths) @($alphaA,$alphaB))}
  Assert-Editor-Focus 'after actual watcher refresh';if(-not(Root-Still-Visible) -or -not(Test-Path -LiteralPath $refreshPath -PathType Container)){throw 'Watcher refresh navigated to a candidate or lost the new directory'}
  $report.automatic_refresh=[pscustomobject]@{source='real GUID directory creation; no manual refresh';draft=(Editor-Text);paths=@(Suggestion-Paths);refresh_directory=$refreshPath;actual_editor_focus=$true};Save-Report
 }
 Check '13 normal-close precondition retains a live editor with an unsubmitted suggestion prefix' {
  if(-not(Editor)){throw 'Actual live editor required before normal close'};Set-Draft '资料-é-';Wait-Suggestions @($unicode);Assert-Editor-Focus 'immediately before normal close'
  $report.draft_close=[pscustomobject]@{draft=(Editor-Text);suggestions=@(Suggestion-Paths);root_listing=(Root-Still-Visible);normal_close_requested_later=$true};Save-Report;Verify-Bytes $markerPaths[$source]
 }
 $notes.Add('Only the recorded host/DPI and ordinary-user account were exercised. UIA Invoke is an accessibility action, not a physical mouse hit-test. The quiet unmatched-input check is bounded to a local fixture and is not proof of cancelling a genuinely blocked filesystem call.')
 $report.status='PASS'
}catch{
 if($_.Exception.Message.StartsWith('COMPLETION_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring('COMPLETION_UI_SKIP: '.Length)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{Capture-State 'unexpected failure'}catch{}}
}finally{
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if($report.before_close_alive){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');Save-Report;$report.close_request_result=$process.CloseMainWindow();Save-Report;if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned Blink still running PID '+$process.Id+'; no force termination')}}
  $process.Refresh();$report.process_exited=$process.HasExited;if($process.HasExited){$report.exit_code=$process.ExitCode;$report.exit_code_hex='0x{0:X8}' -f ([long]$report.exit_code -band 4294967295);$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o')}
  if($report.exit_code -ne 0){throw ('Owned Blink abnormal exit '+$report.exit_code_hex)};if($report.status -eq 'PASS'){Record '14 live completion editor closes normally and the owned process exits zero'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '14 normal owned close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process remains active; retain fixture'};$report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
  $report.fixture_bytes_removed=[long]((Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture);if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '15 exact GUID payload identities bytes and attributes verified before cleanup'}
 }catch{$report.status='FAIL';$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '15 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=[long]((Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -eq 'SKIP';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI ADDRESS COMPLETION '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -ne 'PASS'){exit 2};exit 0
