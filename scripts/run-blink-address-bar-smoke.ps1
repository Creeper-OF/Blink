[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$Binary='',
    [ValidateRange(5,90)][int]$StepTimeoutSeconds=30,
    # Supply only an AutomationId or exact UIA Name confirmed by an owned-window
    # probe. A missing identity records diagnostics and fails without guessing.
    [string]$NativeOverflowId='',
    [string]$NativeOverflowName=''
)
# G03a integration smoke: only the newly launched PID/HWND and exact GUID files.
# Does not build, touch service/configuration/Clipboard, use global input, force
# an exit, or claim native model tests / unexercised IME and UNC coverage.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$stage=Join-Path $project '.build\explorer-address-bar'
$integration=Join-Path $stage 'integration'
$fixtureParent=Join-Path $integration 'ui-address-fixtures'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-address-bar-report.json'
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $fixtureParent ('owned-'+$guid)
$ancestor=Join-Path $fixture ('a-'+$token)
$unicode=Join-Path $ancestor ('资料-中文-é-'+$token)
$middle=Join-Path $unicode ('m-'+$token+'-level')
$deep=Join-Path $middle ('d-'+$token+'-level')
$sibling=Join-Path $fixture ('s-'+$token)
$empty=Join-Path $fixture ('e-'+$token)
$missing=Join-Path $fixture ('missing-'+$token)
$allowedDirectories=@($ancestor,$unicode,$middle,$deep,$sibling,$empty)
$marker='addr-'+$token+'.txt';$refreshName='refresh-'+$token+'.txt'
$allowedFiles=@{}
foreach($folderPath in @($ancestor,$unicode,$middle,$deep,$sibling)){$allowedFiles[(Join-Path $folderPath $marker)]='Blink address '+$guid+' '+[IO.Path]::GetFileName($folderPath)}
$refreshPath=Join-Path $deep $refreshName;$allowedFiles[$refreshPath]='Blink address '+$guid+' REFRESH'
$initialFiles=@($allowedFiles.Keys|Where-Object{$_ -ine $refreshPath})
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid)
$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$tempChanged=$false
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new()
$process=$null;$fixtureCreated=$false;$script:refreshCreated=$false;$originalSize=$null;$script:windowCache=$null;$script:currentStep='preflight'
$report=[ordered]@{
 status='RUNNING';passed=$false;skipped=$false;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null
 configuration=$Configuration;binary=$Binary;binary_sha256=$null;process_id=$null;process_exited=$false;exit_code=$null;exit_code_hex=$null;forced_termination=$false;before_close_alive=$null;close_requested=$false;close_request_utc=$null;close_request_result=$null;process_exit_observed_utc=$null
 environment=$null;window_dpi=$null;desktop_preflight=$null;current_step=$script:currentStep;failure=$null;checks=@();notes=@()
 fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_ownership_failures=@();fixture_bytes_removed=0
 compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0
 snapshots=@();history_snapshots=@();keyboard_evidence=@();focus_preconditions=@();overflow_evidence=$null;automatic_refresh=$null;search_focus_evidence=$null;stale_peer=$null;draft_close_evidence=$null;last_ui_snapshot=$null
 human_acceptance='NOT RUN';model_test_results='NOT RUN by this UI smoke'
 not_run=@('Windows 11 24H2 Explorer comparison','theme/DPI/language/Narrator matrix','real IME composition and candidate selection','UNC/network and disconnected shares','child-directory breadcrumb menus (deferred)')
 input_mode='New owned PID/HWND UIA and InputSite keyboard messages only; no global SendInput or Clipboard'
}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($true))}
function Record([string]$Label,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Label;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('ADDRESS_BAR_UI_SKIP: '+$Why)}
function Check([string]$Label,[scriptblock]$Body){$script:currentStep=$Label;Save-Report;try{$null=&$Body;Record $Label}catch{if($_.Exception.Message.StartsWith('ADDRESS_BAR_UI_SKIP: ')){Record $Label 'SKIP' $_.Exception.Message}else{Record $Label 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace)};throw}}
function Window{
 $process.Refresh();if($process.HasExited){throw ('Owned Blink exited: '+$process.ExitCode)}
 if(-not $script:windowCache){if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null};$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)}
 if($script:windowCache.Current.ProcessId -ne $process.Id){throw 'Owned window PID changed'};return $script:windowCache
}
function Condition([string]$Id){return [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)}
function Visible($Element){return $null -ne $Element -and -not $Element.Current.IsOffscreen}
function Descendants($Root){if(-not $Root){return @()};return @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.ProcessId -eq $process.Id})}
function Find-In($Root,[string]$Id,[bool]$VisibleOnly=$false){if(-not $Root){return $null};foreach($item in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,(Condition $Id)))){if($item.Current.ProcessId -eq $process.Id -and (-not $VisibleOnly -or (Visible $item))){return $item}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){
 $found=Find-In (Window) $Id $true;if($found){return $found}
 foreach($ownedHwnd in [BlinkAddressBarUiNative]::OwnedTopWindows($process.Id)){
  try{$rootPeer=[Windows.Automation.AutomationElement]::FromHandle($ownedHwnd);if($rootPeer.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'};if($rootPeer.Current.AutomationId -ceq $Id -and (Visible $rootPeer)){return $rootPeer};$found=Find-In $rootPeer $Id $true;if($found){return $found}}
  catch{if([BlinkAddressBarUiNative]::WindowExists($ownedHwnd)){throw}}
 };return $null
}
function Address{ $addressHost=By-Id 'AddressBar';if(-not $addressHost){throw 'AddressBar host peer missing'};return $addressHost }
function Address-Text{return (Address).Current.HelpText}
function Editor{return Find-In (Window) 'AddressEditor' $true}
function Editor-Text{return (Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Set-Draft([string]$Draft){$editorPeer=Editor;if(-not $editorPeer){throw 'Visible owned AddressEditor required'};$editorPeer.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Draft);Wait-Ui 'draft is reflected in editor and passive host HelpText' {(Editor-Text) -ceq $Draft -and (Address-Text) -ceq $Draft}}
function Entries{return By-Id 'FolderEntries'}
function Rows{$listing=Entries;if(-not $listing){return @()};return @($listing.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and -not $_.Current.IsOffscreen})}
function Row-Names{return @(Rows|ForEach-Object{$_.Current.Name})}
function Selected-Names{$listing=Entries;if(-not $listing){return @()};return @($listing.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}
function Same-Names([string[]]$First,[string[]]$Second){return $First.Count -eq $Second.Count -and ($First -join "`n") -ceq ($Second -join "`n")}
function Peer-Evidence($Element){if(-not $Element){return [pscustomobject]@{found=$false}};$c=$Element.Current;return [pscustomobject]@{found=$true;id=$c.AutomationId;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;name=$c.Name;help_text=$c.HelpText;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;pid=$c.ProcessId;runtime_id=($Element.GetRuntimeId()-join ',');rect=[pscustomobject]@{x=$c.BoundingRectangle.X;y=$c.BoundingRectangle.Y;width=$c.BoundingRectangle.Width;height=$c.BoundingRectangle.Height}}}
function Focus-In([string]$Id){$focused=[Windows.Automation.AutomationElement]::FocusedElement;for($depth=0;$focused -and $depth -lt 15;$depth++){if($focused.Current.ProcessId -ne $process.Id){return $false};if($focused.Current.AutomationId -ceq $Id){return $true};$focused=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($focused)};return $false}
function Act($Element){if(-not $Element -or $Element.Current.ProcessId -ne $process.Id -or -not (Visible $Element) -or -not $Element.Current.IsEnabled){throw 'Visible enabled owned action required'};$pattern=$null;if($Element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}elseif($Element.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$pattern.Expand()}else{throw ('No Invoke/Expand pattern on actual peer '+$Element.Current.AutomationId)}}
function Ready{return (Visible (Entries)) -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}
function Require-Desktop{$desktop=[BlinkAddressBarUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady){Skip ('Unlocked desktop required: '+$desktop.Reason)}}
function Key([uint32]$Code,[int]$Modifiers=0){Require-Desktop;[BlinkAddressBarUiNative]::Key($process.Id,$Code,$Modifiers)}
function Capture-State([string]$Why){
 $state=[ordered]@{reason=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;host=$null;editor=$null;breadcrumbs=@();rows=@();selected=@();focus=$null;visible_owned_surfaces=@();errors=@()}
 try{$state.address=Address-Text;$state.host=Peer-Evidence (Address);$state.editor=Peer-Evidence (Editor)}catch{$state.errors+=@('address: '+$_.Exception.Message)}
 try{$crumbRoot=By-Id 'AddressBreadcrumbs';$state.breadcrumbs=@(@($crumbRoot)+@(Descendants $crumbRoot)|Where-Object{$_}|ForEach-Object{Peer-Evidence $_})}catch{$state.errors+=@('breadcrumbs: '+$_.Exception.Message)}
 try{$state.rows=@(Row-Names);$state.selected=@(Selected-Names)}catch{$state.errors+=@('entries: '+$_.Exception.Message)}
 try{$focused=[Windows.Automation.AutomationElement]::FocusedElement;if($focused.Current.ProcessId -eq $process.Id){$state.focus=Peer-Evidence $focused}}catch{$state.errors+=@('focus: '+$_.Exception.Message)}
 try{foreach($ownedHwnd in [BlinkAddressBarUiNative]::OwnedTopWindows($process.Id)){$rootPeer=[Windows.Automation.AutomationElement]::FromHandle($ownedHwnd);$state.visible_owned_surfaces+=@(@($rootPeer)+@(Descendants $rootPeer)|Where-Object{(Visible $_) -and ($_.Current.ControlType -eq [Windows.Automation.ControlType]::MenuItem -or $_.Current.AutomationId -like 'Address*')}|ForEach-Object{Peer-Evidence $_})}}catch{$state.errors+=@('owned surfaces: '+$_.Exception.Message)}
 $report.last_ui_snapshot=[pscustomobject]$state;Save-Report;return [pscustomobject]$state
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('ADDRESS_BAR_UI_SKIP: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);try{$null=Capture-State $Why}catch{};throw ('Timed out: '+$Why+'; '+$last)}
function Input-Focus-Evidence([string]$Stage){
 $desktop=[BlinkAddressBarUiNative]::DesktopState();$focused=[Windows.Automation.AutomationElement]::FocusedElement;$focusPeer=$null
 if($focused){$focusPeer=[pscustomobject]@{pid=$focused.Current.ProcessId;id=$focused.Current.AutomationId;class=$focused.Current.ClassName;has_keyboard_focus=$focused.Current.HasKeyboardFocus}}
 return [pscustomobject]@{stage=$Stage;utc=[DateTime]::UtcNow.ToString('o');owned_pid=$process.Id;foreground_pid=$desktop.ForegroundPid;foreground_hwnd=$desktop.Foreground;foreground_class=$desktop.ForegroundClass;desktop_ready=$desktop.InputReady;focus=$focusPeer;editor_has_keyboard_focus=$(if(Editor){(Editor).Current.HasKeyboardFocus}else{$null})}
}
function Enter-Edit([uint32]$KeyCode=0x4C,[int]$Modifiers=1){
 try{
  $historyButton=By-Id 'AddressHistoryButton';if(-not (Visible $historyButton) -or -not $historyButton.Current.IsEnabled){throw 'Visible owned address focus anchor unavailable'}
  $historyButton.SetFocus()
  Wait-Ui 'owned foreground and actual UIA focus before address keyboard input' {$desktop=[BlinkAddressBarUiNative]::DesktopState();$focused=[Windows.Automation.AutomationElement]::FocusedElement;$desktop.InputReady -and $desktop.ForegroundPid -eq $process.Id -and $focused -and $focused.Current.ProcessId -eq $process.Id -and $focused.Current.HasKeyboardFocus}
 }catch{$report.focus_preconditions+=@(Input-Focus-Evidence 'ADDRESS_BAR_TEST_PRECONDITION_NOT_MET');Save-Report;throw ('ADDRESS_BAR_TEST_PRECONDITION_NOT_MET: owned foreground and actual UIA focus were not acquired; '+$_.Exception.Message)}
 $report.focus_preconditions+=@(Input-Focus-Evidence 'before address key');Save-Report
 Key $KeyCode $Modifiers
 try{Wait-Ui 'visible focused AddressEditor' {(Visible (Editor)) -and (Focus-In 'AddressEditor') -and (Editor).Current.HasKeyboardFocus}}
 finally{$report.focus_preconditions+=@(Input-Focus-Evidence 'after address key');Save-Report}
}
function Cancel-Edit([string]$Expected){Key 27;Wait-Ui 'Escape restores committed address and entries focus' {-not (Editor) -and (Address-Text) -ieq $Expected -and (Focus-In 'FolderEntries')}}
function Navigate([string]$Target,[string]$ExpectedMarker=$marker){
 if($Target -notin $allowedDirectories){throw 'Navigation outside owned fixture'}
 Close-History;Enter-Edit;Set-Draft $Target;Key 13
 Wait-Ui 'Enter commits exact owned folder and real listing' {-not (Editor) -and (Address-Text) -ieq $Target -and (Ready) -and $(if($ExpectedMarker){@(Row-Names) -ccontains $ExpectedMarker}else{@(Row-Names).Count -eq 0})}
}
function Tabs{return @((By-Id 'WindowTabs').FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TabItem)))}
function History-Rows{
 $historyRows=[Collections.Generic.List[object]]::new()
 # IDs are semantic item indexes, not guessed native popup classes.
 for($index=0;$index -lt 128;$index++){$historyPeer=Owned-Id ('AddressHistory'+$index);if($historyPeer){$historyRows.Add([pscustomobject]@{index=$index;element=$historyPeer;name=$historyPeer.Current.Name;help_text=$historyPeer.Current.HelpText})}elseif($index -gt 0){break}}
 return @($historyRows.ToArray())
}
function History-Open{return $null -ne (Owned-Id 'AddressHistory0')}
function History-Snapshot([string]$Label){$snapshot=[pscustomobject]@{label=$Label;utc=[DateTime]::UtcNow.ToString('o');address=(Address-Text);root=(Peer-Evidence (Owned-Id 'AddressHistoryMenu'));items=@(History-Rows|ForEach-Object{[pscustomobject]@{index=$_.index;name=$_.name;help_text=$_.help_text;peer=(Peer-Evidence $_.element)}})};$report.history_snapshots+=@($snapshot);Save-Report;return $snapshot}
function History-Target([string]$Path){$matches=@(History-Rows|Where-Object{$_.name -ieq $Path -or $_.help_text -ieq $Path});if($matches.Count -ne 1){throw ('Exact owned history target not unique: '+$Path)};return $matches[0]}
function Open-History([bool]$Keyboard=$true){if($Keyboard){Key 0x73}else{Act (By-Id 'AddressHistoryButton')};Wait-Ui 'actual history menu has a real item' {History-Open}}
function Close-History{if(History-Open){Key 27;Wait-Ui 'history leaves close' {-not (History-Open)}}}
function Owned-Crumb([string]$Path){
 if($Path -notin $allowedDirectories){throw 'Breadcrumb target must be an owned directory'}
 $label=[IO.Path]::GetFileName($Path);$matches=@(Descendants (By-Id 'AddressBreadcrumbs')|Where-Object{(Visible $_) -and $_.Current.Name -ceq $label -and $_.Current.IsEnabled}|Where-Object{$action=$null;$_.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$action)})
 if($matches.Count -ne 1){throw ('Expected one invokable native breadcrumb for owned label '+$label+'; found '+$matches.Count)};return $matches[0]
}
function Native-Overflow{
 $crumbRoot=By-Id 'AddressBreadcrumbs'
 if(-not $NativeOverflowId -and -not $NativeOverflowName){$probe=Capture-State 'native overflow identity required';$report.overflow_evidence=[pscustomobject]@{status='ACTUAL PEER PROBE ONLY; NAVIGATION NOT RUN';narrow_probe=$probe};Save-Report;throw 'No confirmed native breadcrumb overflow identity; provide probe-confirmed -NativeOverflowId or -NativeOverflowName'}
 $matches=@(Descendants $crumbRoot|Where-Object{(Visible $_) -and $_.Current.IsEnabled -and $(if($NativeOverflowId){$_.Current.AutomationId -ceq $NativeOverflowId}else{$_.Current.Name -ceq $NativeOverflowName})}|Where-Object{$action=$null;$_.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$action) -or $_.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$action)})
 if($matches.Count -ne 1){throw ('Confirmed native overflow identity did not match one actionable peer; found '+$matches.Count)};return $matches[0]
}
function Overflow-Target([string]$Path){
 if($Path -notin $allowedDirectories){throw 'Overflow target must be owned'};$label=[IO.Path]::GetFileName($Path);$matches=@()
 foreach($ownedHwnd in [BlinkAddressBarUiNative]::OwnedTopWindows($process.Id)){$rootPeer=[Windows.Automation.AutomationElement]::FromHandle($ownedHwnd);foreach($item in @(Descendants $rootPeer)){
  if(-not (Visible $item) -or -not $item.Current.IsEnabled -or $item.Current.Name -cne $label){continue}
  # Accept only actual native popup descendants, never the still-visible crumb.
  $parent=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($item);$insideCrumbs=$false
  for($depth=0;$parent -and $depth -lt 16;$depth++){if($parent.Current.ProcessId -ne $process.Id){break};if($parent.Current.AutomationId -ceq 'AddressBreadcrumbs'){$insideCrumbs=$true;break};$parent=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($parent)}
  $action=$null;if(-not $insideCrumbs -and $item.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$action)){$matches+=@($item)}
 }}
 $unique=@{};foreach($item in $matches){$unique[($item.GetRuntimeId()-join ',')]=$item};if($unique.Count -ne 1){return $null};return @($unique.Values)[0]
}
function Read-Bytes([string]$Path){return [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path))}
function Expected-Bytes([string]$Path){return [Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes([string]$allowedFiles[$Path]))}
function Verify-Bytes([string]$Path){if(-not $allowedFiles.ContainsKey($Path) -or (Read-Bytes $Path) -cne (Expected-Bytes $Path)){throw ('Owned file bytes changed: '+$Path)}}
function Ownership-Failures{
 $bad=[Collections.Generic.List[string]]::new();$pending=[Collections.Generic.Stack[string]]::new();$mask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
 $root=Get-Item -LiteralPath $fixture -Force;if(-not $root.PSIsContainer -or ($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($root.Attributes -band $mask)){return @('Fixture root type or attributes changed')};$pending.Push($fixture)
 while($pending.Count){foreach($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){$bad.Add('Reparse entry: '+$entry.FullName);continue}
  if($entry.PSIsContainer){if($entry.FullName -in $allowedDirectories -and -not ($entry.Attributes -band $mask)){$pending.Push($entry.FullName)}else{$bad.Add('Unknown directory or attributes: '+$entry.FullName)};continue}
  if(-not $allowedFiles.ContainsKey($entry.FullName)){$bad.Add('Unknown file: '+$entry.FullName);continue}
  if($entry.Attributes -band $mask){$bad.Add('Unknown file attributes: '+$entry.FullName)}
  try{Verify-Bytes $entry.FullName}catch{$bad.Add('Unknown or unreadable bytes: '+$entry.FullName)}
 }}
 foreach($directoryPath in $allowedDirectories){if(-not (Test-Path -LiteralPath $directoryPath -PathType Container)){$bad.Add('Missing original directory: '+$directoryPath)}}
 if($script:refreshCreated -and -not (Test-Path -LiteralPath $refreshPath -PathType Leaf)){$bad.Add('Missing refresh marker: '+$refreshPath)}
 foreach($requiredPath in $initialFiles){if(-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)){$bad.Add('Missing original file: '+$requiredPath)}}
 return @($bad.ToArray())
}
Save-Report
try{
 if([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or -not [Environment]::UserInteractive){Skip 'Interactive Windows desktop required'}
 if(-not (Test-Path -LiteralPath $Binary -PathType Leaf)){Skip 'Candidate runtime unavailable'}
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary user required'}
 $version=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
 $themeState=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -ErrorAction SilentlyContinue
 $report.environment=[ordered]@{display_version=$version.DisplayVersion;build=$version.CurrentBuildNumber;ubr=$version.UBR;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;os_64bit=[Environment]::Is64BitOperatingSystem;process_64bit=[Environment]::Is64BitProcess;is_administrator=$false;system_apps_use_light_theme=$themeState.AppsUseLightTheme;blink_actual_theme='not captured';configuration_requested=$Configuration;explorer_reference='NOT RUN'}
 New-Item -ItemType Directory -Path $compilerTemp -Force|Out-Null;$env:TEMP=$compilerTemp;$env:TMP=$compilerTemp;$tempChanged=$true
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 Add-Type @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkAddressBarUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkAddressBarUiNative {
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; public Point(int x,int y){X=x;Y=y;} }
    delegate bool EnumWindowProc(IntPtr window,IntPtr parameter);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll",SetLastError=true)] static extern bool EnumWindows(EnumWindowProc callback,IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int length);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
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
    public static BlinkAddressBarUiDesktop DesktopState() {
        var result=new BlinkAddressBarUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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

    [StructLayout(LayoutKind.Sequential)] struct WindowRect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window,out WindowRect rect);
    [DllImport("user32.dll",SetLastError=true)] static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
    public static int[] WindowSize(int pid) { Own(OwnedWindow,pid);WindowRect rect;if(!GetWindowRect(OwnedWindow,out rect))throw new Win32Exception();return new int[]{rect.Right-rect.Left,rect.Bottom-rect.Top}; }
    public static void Resize(int pid,int width,int height) { Own(OwnedWindow,pid);if(width<420||height<300)throw new ArgumentOutOfRangeException();if(!SetWindowPos(OwnedWindow,IntPtr.Zero,0,0,width,height,0x16))throw new Win32Exception(); }
}
'@
 Require-Desktop
 $report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash
 New-Item -ItemType Directory -Path $fixtureParent -Force|Out-Null
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture already exists'}
 New-Item -ItemType Directory -Path $fixture|Out-Null;$fixtureCreated=$true
 foreach($folderPath in $allowedDirectories){New-Item -ItemType Directory -Path $folderPath|Out-Null}
 foreach($filePath in $initialFiles){[IO.File]::WriteAllText($filePath,$allowedFiles[$filePath],[Text.UTF8Encoding]::new($false))}
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false
 $psi.EnvironmentVariables.Remove('BLINK_INITIAL_PATH');$psi.EnvironmentVariables.Remove('BLINK_PHASE1A_UI_PROBE');$psi.EnvironmentVariables.Remove('BLINK_STARTUP_TELEMETRY')
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned Blink launch failed'}
 $report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)}
 [BlinkAddressBarUiNative]::OwnedWindow=$process.MainWindowHandle
 $report.window_dpi=[BlinkAddressBarUiNative]::GetDpiForWindow($process.MainWindowHandle);$originalSize=[BlinkAddressBarUiNative]::WindowSize($process.Id)
 $thisPcLabels=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq 'Location.ThisPC'){$thisPcLabels+=@([string]$data.value)}}}
 Check '01 ThisPC exposes a passive address host, native breadcrumbs and no fabricated filesystem path' {
  Wait-Ui 'initial real ThisPC address presentation' {(Visible (By-Id 'AddressBreadcrumbs')) -and (Visible (By-Id 'AddressHistoryButton')) -and $thisPcLabels -ccontains (Address-Text) -and -not (Editor)}
  $report.snapshots+=@(Capture-State 'initial ThisPC');Save-Report
 }
 Check '02 Ctrl L selects all virtual address text and Escape restores entries focus' {
  $before=Address-Text;Enter-Edit;Key 0x2E;Wait-Ui 'Delete replaces the automatically selected whole address' {(Editor-Text) -ceq '' -and (Address-Text) -ceq ''}
  Cancel-Edit $before
  $report.keyboard_evidence+=@([pscustomobject]@{sequence='Ctrl+L Delete Escape';initial=$before;automatic_full_selection=$true;entries_focus_return=$true});Save-Report
 }
 Check '03 Unicode path Enter commits the real directory and its file listing' {
  Navigate $deep
  if(-not (Visible (By-Id 'AddressBreadcrumbs'))){throw 'Successful Enter did not return to native breadcrumbs'}
  Verify-Bytes (Join-Path $deep $marker);$report.snapshots+=@(Capture-State 'Unicode deep committed');Save-Report
 }
 Check '04 Alt D selects the committed full path and Escape abandons a draft' {
  $beforeRows=@(Row-Names);Enter-Edit 0x44 4;Key 0x2E;Wait-Ui 'Alt D selected the complete committed path' {(Editor-Text) -ceq ''}
  Set-Draft ('unfinished-'+$token+'-草稿');Enter-Edit;Wait-Ui 'repeated Ctrl L preserves the latest active draft' {(Editor-Text) -ceq ('unfinished-'+$token+'-草稿') -and (Address-Text) -ceq ('unfinished-'+$token+'-草稿')};Key 0x2E;Wait-Ui 'repeated Ctrl L selects all of the preserved latest draft' {(Editor-Text) -ceq ''};Set-Draft ('latest-'+$token+'-草稿');Cancel-Edit $deep
  if(-not (Same-Names @(Row-Names) $beforeRows)){throw 'Cancelling a draft changed the directory listing'}
  $report.keyboard_evidence+=@([pscustomobject]@{sequence='Alt+D Delete draft Ctrl+L Delete latest-draft Escape';restored=$deep;entries_focus_return=$true});Save-Report
 }
 Check '05 Ctrl A Delete in the address editor never deletes selected files or changes the view' {
  $row=@(Rows|Where-Object{$_.Current.Name -ceq $marker});if($row.Count -ne 1){throw 'Owned selectable marker unavailable'};$row[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  $beforeRows=@(Row-Names);$beforeSelection=@(Selected-Names);$beforeMode=(By-Id 'FolderViewMode').Current.HelpText
  Enter-Edit;Set-Draft ('unsent-'+$token);Key 0x41 1;Key 0x2E;Wait-Ui 'Ctrl A Delete clears only the draft' {(Editor-Text) -ceq '' -and (Address-Text) -ceq ''}
  if(-not (Same-Names @(Row-Names) $beforeRows) -or -not (Same-Names @(Selected-Names) $beforeSelection) -or (By-Id 'FolderViewMode').Current.HelpText -cne $beforeMode){throw 'Text editing changed listing, selection or view'}
  foreach($filePath in $initialFiles){Verify-Bytes $filePath};Cancel-Edit $deep
 }
 Check '06 address Escape on the real search page restores its visible results focus and committed location' {
  Key 0x46 1;Wait-Ui 'Ctrl F focuses the owned search query' {Focus-In 'SearchQuery'}
  $searchPeer=By-Id 'SearchQuery';$searchPeer.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($token);$searchPeer.SetFocus();Key 13
  Wait-Ui 'submitted unique query shows the actual search page (zero results allowed)' {(Visible (By-Id 'SearchResults')) -and (Visible (By-Id 'SearchLeave')) -and (By-Id 'SearchQuery').GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -ceq $token}
  $searchDraft='search-draft-'+$token;Enter-Edit 0x44 4;Set-Draft $searchDraft;Key 27
  Wait-Ui 'address Escape restores committed path and visible SearchResults focus' {-not (Editor) -and (Address-Text) -ieq $deep -and (Visible (By-Id 'SearchResults')) -and (Focus-In 'SearchResults')}
  $resultsPeer=By-Id 'SearchResults';$resultItems=@($resultsPeer.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem)))
  $report.search_focus_evidence=[pscustomobject]@{query=$token;committed_path=(Address-Text);results=(Peer-Evidence $resultsPeer);actual_row_count=$resultItems.Count;address_cancel_focus='SearchResults';index_result_requirement='none; search UI/focus behavior only';index_performance='NOT TESTED'};Save-Report
  Act (By-Id 'SearchLeave');Wait-Ui 'real SearchLeave restores the owned directory for subsequent checks' {(Address-Text) -ieq $deep -and (Ready) -and -not (Visible (By-Id 'SearchResults')) -and (Focus-In 'FolderEntries')}
 }
 Check '07 history button opens real navigation history and passive address reads leave it open' {
  Open-History $false;$null=History-Target $deep;$before=History-Snapshot 'button open';$firstId=$before.items[0].peer.runtime_id
  for($index=0;$index -lt 3;$index++){if((Address-Text) -ine $deep){throw 'Passive address read changed committed location'}}
  if(-not (History-Open) -or ((Owned-Id 'AddressHistory0').GetRuntimeId()-join ',') -cne $firstId){throw 'Reading AddressBar dismissed/replaced history'}
  Close-History
 }
 Check '08 F4 opens actual history and invoking its real owned leaf navigates' {
  Navigate $sibling;Open-History;$target=History-Target $deep;$null=History-Snapshot 'F4 from sibling';Act $target.element
  Wait-Ui 'history leaf commits the exact deep owned directory' {-not (History-Open) -and -not (Editor) -and (Address-Text) -ieq $deep -and (Ready) -and @(Row-Names) -ccontains $marker}
 }
 Check '09 a failed path preserves the committed listing and never enters navigation history' {
  $beforeRows=@(Row-Names);Enter-Edit;Set-Draft $missing;Key 13
  Wait-Ui 'failed owned path returns to the actual committed listing' {-not (Editor) -and (Address-Text) -ieq $deep -and (Ready) -and (Same-Names @(Row-Names) $beforeRows)}
  if(Test-Path -LiteralPath $missing){throw 'Failed navigation created a directory'}
  Open-History;$failedSnapshot=History-Snapshot 'after missing path';if(@($failedSnapshot.items|Where-Object{$_.name -ieq $missing -or $_.help_text -ieq $missing}).Count){throw 'Unsuccessful path leaked into real history'};Close-History
 }
 Check '10 native breadcrumb ancestor clicks navigate two real levels with exact listings' {
  foreach($targetPath in @($middle,$unicode)){Wait-Ui 'owned native ancestor breadcrumb is realized' {$null -ne (Owned-Crumb $targetPath)};Act (Owned-Crumb $targetPath);Wait-Ui 'ancestor click commits its exact owned directory' {(Address-Text) -ieq $targetPath -and -not (Editor) -and (Ready) -and @(Row-Names) -ccontains $marker};Verify-Bytes (Join-Path $targetPath $marker)}
  $report.snapshots+=@(Capture-State 'native ancestors clicked');Save-Report
 }
 Check '11 narrow native breadcrumb overflow opens its real popup and navigates a collapsed ancestor' {
  Navigate $deep
  try{
   [BlinkAddressBarUiNative]::Resize($process.Id,620,$originalSize[1]);Wait-Ui 'owned window resized narrow' {([BlinkAddressBarUiNative]::WindowSize($process.Id))[0] -le 640}
   $before=Capture-State 'narrow breadcrumb before native overflow';$report.overflow_evidence=[pscustomobject]@{status='NATIVE OVERFLOW NOT INVOKED';width=620;before=$before};Save-Report;$overflowPeer=Native-Overflow
   $visibleTarget=@(Descendants (By-Id 'AddressBreadcrumbs')|Where-Object{(Visible $_) -and $_.Current.Name -ceq [IO.Path]::GetFileName($ancestor)})
   if($visibleTarget.Count){throw 'Chosen native overflow ancestor was not collapsed at this width'}
   Act $overflowPeer;Wait-Ui 'real native popup exposes the collapsed owned ancestor' {$null -ne (Overflow-Target $ancestor)};$leaf=Overflow-Target $ancestor
   $report.overflow_evidence=[pscustomobject]@{width=620;trigger=(Peer-Evidence $overflowPeer);target=(Peer-Evidence $leaf);before=$before;after_open=(Capture-State 'native collapsed popup open');narrow_committed=$null;window_restored=$false;restored_state=$null};Save-Report
   $beforeCrumbIdentity=(By-Id 'AddressBreadcrumbs').GetRuntimeId()-join ','
   $expectedAncestorRows=@([IO.Path]::GetFileName($unicode),$marker)|Sort-Object
   Act $leaf
   # At this width CommandNew/CommandSort can migrate into native More and
   # leave the main UIA tree. Verify committed navigation from its actual
   # directory, exact contents and freshly presented native breadcrumbs.
   Wait-Ui 'native overflow ancestor really navigates with exact owned contents' {
    $committedCrumbs=By-Id 'AddressBreadcrumbs'
    (Address-Text) -ieq $ancestor -and -not (Editor) -and (Visible (Entries)) -and
     (Same-Names (@(Row-Names)|Sort-Object) @($expectedAncestorRows)) -and
     (Visible $committedCrumbs) -and $committedCrumbs.Current.HelpText -ieq $ancestor -and
     ($committedCrumbs.GetRuntimeId()-join ',') -cne $beforeCrumbIdentity -and -not (Overflow-Target $ancestor)
   }
   Verify-Bytes (Join-Path $ancestor $marker)
   $report.overflow_evidence.narrow_committed=[pscustomobject]@{address=(Address-Text);rows=@(Row-Names);breadcrumbs=(Peer-Evidence (By-Id 'AddressBreadcrumbs'));popup_target_closed=($null -eq (Overflow-Target $ancestor));marker_bytes_verified=$true};Save-Report
  }finally{[BlinkAddressBarUiNative]::Resize($process.Id,$originalSize[0],$originalSize[1])}
  # Subsequent checks require the original full-width command capabilities.
  Wait-Ui 'owned window width and full command readiness restored' {
   ([BlinkAddressBarUiNative]::WindowSize($process.Id))[0] -eq $originalSize[0] -and
    (Address-Text) -ieq $ancestor -and (Ready) -and
    (Same-Names (@(Row-Names)|Sort-Object) @($expectedAncestorRows))
  }
  Verify-Bytes (Join-Path $ancestor $marker)
  $report.overflow_evidence.window_restored=$true;$report.overflow_evidence.restored_state=Capture-State 'native ancestor navigation after full width/readiness restoration';Save-Report
 }
 Check '12 automatic directory refresh retains an active address draft without forcing navigation' {
  Navigate $deep;Enter-Edit;$draft='draft-'+$token+'-正在输入';Set-Draft $draft
  [IO.File]::WriteAllText($refreshPath,$allowedFiles[$refreshPath],[Text.UTF8Encoding]::new($false));$script:refreshCreated=$true
  Wait-Ui 'filesystem watcher refreshes rows while the same address draft remains' {@(Row-Names) -ccontains $refreshName -and (Editor-Text) -ceq $draft -and (Address-Text) -ceq $draft -and (Focus-In 'AddressEditor')}
  Verify-Bytes $refreshPath;$report.automatic_refresh=[pscustomobject]@{source='owned file write and watcher; no F5/manual refresh';draft=$draft;editor=(Peer-Evidence (Editor));rows=@(Row-Names)};Save-Report;Cancel-Edit $deep
 }
 Check '13 switching tabs discards an old unsent draft and restores each committed location' {
  Navigate $deep;Enter-Edit;Set-Draft ('old-tab-'+$token);$tabCount=@(Tabs).Count;Key 0x54 1
  Wait-Ui 'new owned tab is actual ThisPC with no leaked editor/draft' {@(Tabs).Count -eq $tabCount+1 -and $thisPcLabels -ccontains (Address-Text) -and -not (Editor)}
  (By-Id 'FolderViewMode').SetFocus();Key 0x57 1;Wait-Ui 'closing only the temporary tab restores committed deep path' {@(Tabs).Count -eq $tabCount -and (Address-Text) -ieq $deep -and -not (Editor) -and (Ready)}
 }
 Check '14 an open old history menu closes on tab switch and its stale leaf cannot navigate the active tab' {
  Navigate $sibling;Navigate $deep;$tabCount=@(Tabs).Count
  (By-Id 'FolderViewMode').SetFocus();Key 0x54 1;Wait-Ui 'temporary second tab exists before the open-menu switch' {@(Tabs).Count -eq $tabCount+1 -and $thisPcLabels -ccontains (Address-Text)}
  Navigate $empty ''
  $tabPeers=@(Tabs);$tabPeers[$tabCount-1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  Wait-Ui 'original tab restores its committed deep directory' {(Address-Text) -ieq $deep -and (Ready)}
  Open-History;$oldPeer=(History-Target $sibling).element;$oldEvidence=Peer-Evidence $oldPeer
  # Switch with the real menu still open, without Escape or a forced invalidation.
  $tabPeers=@(Tabs);$tabPeers[$tabCount].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  Wait-Ui 'selecting second tab closes old menu and restores its empty directory' {(Address-Text) -ieq $empty -and -not (History-Open) -and (Ready) -and @(Row-Names).Count -eq 0}
  $outcome='Invoke returned';try{$oldPeer.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}catch{$outcome='Old provider rejected Invoke: '+$_.Exception.Message}
  # A fresh UIA round trip waits for any dispatcher callback queued by Invoke.
  $null=(By-Id 'FolderViewMode').Current.HelpText
  if((Address-Text) -ine $empty -or (Editor) -or -not (Ready) -or @(Row-Names).Count -ne 0){throw 'Stale history callback changed the active tab'}
  $report.stale_peer=[pscustomobject]@{old=$oldEvidence;outcome=$outcome;menu_was_open_during_tab_selection=$true;active_address=(Address-Text)};Save-Report
  (By-Id 'FolderViewMode').SetFocus();Key 0x57 1;Wait-Ui 'temporary stale-peer tab closes back to deep' {@(Tabs).Count -eq $tabCount -and (Address-Text) -ieq $deep}
 }
 Check '15 an empty directory keeps its exact path and produces zero inherited rows' {
  Navigate $empty '';if(@(Row-Names).Count -ne 0 -or (Editor) -or -not (Visible (By-Id 'AddressBreadcrumbs'))){throw 'Empty-directory address reused stale content'}
  $report.snapshots+=@(Capture-State 'empty owned directory');Save-Report
 }
 Check '16 F4 keyboard Home Down Enter chooses the actual recorded history item' {
  Navigate $deep;Navigate $sibling;Open-History;$target=History-Target $deep;$null=History-Snapshot 'keyboard selection before Enter';Key 0x24
  for($index=0;$index -lt $target.index;$index++){Key 0x28};Key 13
  Wait-Ui 'keyboard history Enter commits the intended owned directory' {-not (History-Open) -and (Address-Text) -ieq $deep -and (Ready) -and @(Row-Names) -ccontains $marker}
  $report.keyboard_evidence+=@([pscustomobject]@{sequence='F4 Home Down*index Enter';index=$target.index;target=$deep});Save-Report
 }
 Check '17 history Escape restores focus and ThisPC stays virtual after tab round trip' {
  Open-History;Key 27;Wait-Ui 'history Escape returns focus to address/entries without navigating' {-not (History-Open) -and (Address-Text) -ieq $deep -and ((Focus-In 'FolderEntries') -or (Focus-In 'AddressHistoryButton') -or (Focus-In 'AddressBreadcrumbs'))}
  (By-Id 'FolderViewMode').SetFocus();$tabCount=@(Tabs).Count;Key 0x54 1;Wait-Ui 'new ThisPC has no fabricated filesystem location' {@(Tabs).Count -eq $tabCount+1 -and $thisPcLabels -ccontains (Address-Text) -and -not (Editor) -and (Visible (By-Id 'AddressBreadcrumbs'))}
  $report.snapshots+=@(Capture-State 'ThisPC tab round trip');(By-Id 'FolderViewMode').SetFocus();Key 0x57 1;Wait-Ui 'original owned location survives ThisPC round trip' {@(Tabs).Count -eq $tabCount -and (Address-Text) -ieq $deep};Save-Report
 }
 Check '18 closing with an unsubmitted owned address draft retains the live editor until normal close' {
  $committedBeforeClose=Address-Text;if($committedBeforeClose -ine $deep){throw 'Draft-close regression requires the exact committed owned deep directory'}
  $beforeCloseRows=@(Row-Names);$closeDraft=Join-Path $fixture ('unsent-close-'+$token)
  Enter-Edit;Set-Draft $closeDraft
  Wait-Ui 'unsubmitted draft remains visible with actual editor keyboard focus' {(Visible (Editor)) -and (Editor-Text) -ceq $closeDraft -and (Address-Text) -ceq $closeDraft -and (Editor).Current.HasKeyboardFocus -and (Focus-In 'AddressEditor')}
  if(-not (Same-Names @(Row-Names) $beforeCloseRows) -or (Test-Path -LiteralPath $closeDraft)){throw 'Typing the closing draft navigated or created its unsubmitted path'}
  Verify-Bytes (Join-Path $deep $marker)
  $report.draft_close_evidence=[pscustomobject]@{committed_before_edit=$committedBeforeClose;draft_text=(Editor-Text);editor_visible=(Visible (Editor));editor_has_keyboard_focus=(Editor).Current.HasKeyboardFocus;editor=(Peer-Evidence (Editor));actual_focus=(Input-Focus-Evidence 'live unsubmitted editor immediately before normal close');rows=@(Row-Names);escape_sent=$false;enter_sent=$false;normal_close='finally CloseMainWindow requires exit 0';background_scenario='NOT RUN by this foreground smoke; covered separately by the ignored close probe'};Save-Report
  # Deliberately leave this owned draft and focused editor open. The following
  # finally must close normally without Escape, Enter or another navigation.
 }
 $notes.Add('Only this actual Windows host, recorded theme registry state and window DPI were exercised. Real IME, UNC, Windows 11 24H2 Explorer, full theme/DPI/language matrix and child-directory menus were not run.')
 $report.status='PASS'
}catch{
 if($_.Exception.Message.StartsWith('ADDRESS_BAR_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring(21)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{$null=Capture-State 'unexpected failure'}catch{}}
}finally{
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if($report.before_close_alive){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');Save-Report;$report.close_request_result=$process.CloseMainWindow();Save-Report;if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned Blink did not close normally; PID '+$process.Id+' retained. No force termination.')}}
  $process.Refresh();$report.process_exited=$process.HasExited
  if($process.HasExited){$report.exit_code=$process.ExitCode;$report.exit_code_hex='0x{0:X8}' -f ([long]$report.exit_code -band 4294967295);$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o')}
  if($report.exit_code -ne 0){throw ('Owned Blink returned nonzero exit '+$report.exit_code_hex)}
  if($report.status -eq 'PASS'){Record '19 owned Blink exits normally without force termination'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '19 owned process normal close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process still running; fixture retained'}
  $report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
  $report.fixture_bytes_removed=(Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not (Test-Path -LiteralPath $fixture)
  if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '20 exact owned paths and UTF8 bytes verified before fixture cleanup'}
 }catch{$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '20 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=(Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not (Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -in @('SKIP','PARTIAL');$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI ADDRESS BAR '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -ne 'PASS'){exit 2};exit 0
