[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Binary='',
 [ValidateRange(5,90)][int]$StepTimeoutSeconds=30,
 # Set only from an actual owned-window peer probe; no guessed native ID/name.
 [string]$NativeOverflowId='',
 [string]$NativeOverflowName=''
)
# G03b: newly launched PID/HWND, real UIA peers and exact GUID-owned fixtures.
# No build, service/configuration/Clipboard change, global input or forced exit.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$stage=Join-Path $project '.build\explorer-address-children'
$integration=Join-Path $stage 'integration'
$fixtureParent=Join-Path $integration 'ui-address-children-fixtures'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-address-children-report.json'
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $fixtureParent ('owned-'+$guid)
$ancestor=Join-Path $fixture ('a-'+$token)
$unicode=Join-Path $ancestor ('资料-é-'+$token)
$middle=Join-Path $unicode ('m-'+$token)
$deep=Join-Path $middle ('d-'+$token)
$childA=Join-Path $deep ('A-'+$token)
$childB=Join-Path $deep ('B-'+$token)
$hidden=Join-Path $deep ('hidden-'+$token)
$empty=Join-Path $fixture ('empty-'+$token)
$allowedDirectories=@($ancestor,$unicode,$middle,$deep,$childA,$childB,$hidden,$empty)
$marker='children-'+$token+'.txt';$refreshName='refresh-'+$token+'.txt'
$allowedFiles=@{}
foreach($folderPath in $allowedDirectories|Where-Object{$_ -ine $empty}){$allowedFiles[(Join-Path $folderPath $marker)]='Blink children '+$guid+' '+[IO.Path]::GetFileName($folderPath)}
$refreshPath=Join-Path $deep $refreshName;$allowedFiles[$refreshPath]='Blink children '+$guid+' REFRESH'
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
 menu_snapshots=@();focus_evidence=@();overflow_evidence=$null;automatic_refresh=$null;stale_peer=$null;deleted_directory=$null;last_focus_attempt=$null;last_watcher_state=$null;last_ui_snapshot=$null
 human_acceptance='NOT RUN';model_test_results='NOT RUN by this UI smoke'
 not_run=@('Windows 11 24H2 Explorer comparison','theme/DPI/language/Narrator matrix','real IME composition and candidate selection','UNC/network and disconnected shares','access-denied enumeration and uninterruptible native filesystem calls','deterministic cancellation while a real enumeration is pending','real mouse hit testing (no reusable owned InputSite click helper; human check pending)')
 input_mode='New owned PID/HWND UIA and InputSite keyboard messages only; no global SendInput or Clipboard'
}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($true))}
function Record([string]$Label,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Label;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('ADDRESS_CHILDREN_UI_SKIP: '+$Why)}
function Check([string]$Label,[scriptblock]$Body){$script:currentStep=$Label;Save-Report;try{$null=&$Body;Record $Label}catch{if($_.Exception.Message.StartsWith('ADDRESS_CHILDREN_UI_SKIP: ')){Record $Label 'SKIP' $_.Exception.Message}else{Record $Label 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace)};throw}}
function Window{
 $process.Refresh();if($process.HasExited){throw ('Owned Blink exited: '+$process.ExitCode)}
 if(-not $script:windowCache){if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null};$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)}
 if($script:windowCache.Current.ProcessId -ne $process.Id){throw 'Owned window PID changed'};return $script:windowCache
}
function Condition([string]$Id){return [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)}
function Visible($Element){return $null -ne $Element -and -not $Element.Current.IsOffscreen -and $Element.Current.BoundingRectangle.Width -gt 0 -and $Element.Current.BoundingRectangle.Height -gt 0}
function Descendants($Root){if(-not $Root){return @()};return @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|Where-Object{$_.Current.ProcessId -eq $process.Id})}
function Find-In($Root,[string]$Id,[bool]$VisibleOnly=$false){if(-not $Root){return $null};foreach($item in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,(Condition $Id)))){if($item.Current.ProcessId -eq $process.Id -and (-not $VisibleOnly -or (Visible $item))){return $item}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){
 $found=Find-In (Window) $Id $true;if($found){return $found}
 foreach($ownedHwnd in [BlinkAddressChildrenUiNative]::OwnedTopWindows($process.Id)){
  try{
   $root=[Windows.Automation.AutomationElement]::FromHandle($ownedHwnd)
   if($root.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'}
   if($root.Current.AutomationId -ceq $Id -and (Visible $root)){return $root}
   $found=Find-In $root $Id $true;if($found){return $found}
  }catch{if([BlinkAddressChildrenUiNative]::WindowExists($ownedHwnd)){throw}}
 };return $null
}
function Peer-Evidence($Element){if(-not $Element){return [pscustomobject]@{found=$false}};$c=$Element.Current;return [pscustomobject]@{found=$true;id=$c.AutomationId;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;name=$c.Name;help_text=$c.HelpText;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;pid=$c.ProcessId;runtime_id=($Element.GetRuntimeId()-join ',');rect=[pscustomobject]@{x=$c.BoundingRectangle.X;y=$c.BoundingRectangle.Y;width=$c.BoundingRectangle.Width;height=$c.BoundingRectangle.Height}}}
function Address-Text{$hostPeer=By-Id 'AddressBar';if(-not $hostPeer){throw 'Owned AddressBar missing'};return $hostPeer.Current.HelpText}
function Editor{return Find-In (Window) 'AddressEditor' $true}
function Editor-Text{return (Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Entries{return By-Id 'FolderEntries'}
function Row-Names{$list=Entries;if(-not $list){return @()};return @($list.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_)}|ForEach-Object{$_.Current.Name})}
function Selected-Names{$list=Entries;if(-not $list){return @()};return @($list.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}
function Same-Names([string[]]$First,[string[]]$Second){return $First.Count -eq $Second.Count -and ($First -join "`n") -ceq ($Second -join "`n")}
function Ready{return (Visible (Entries)) -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}
function Focus-In([string]$Id){$peer=[Windows.Automation.AutomationElement]::FocusedElement;for($depth=0;$peer -and $depth -lt 24;$depth++){if($peer.Current.ProcessId -ne $process.Id){return $false};if($peer.Current.AutomationId -ceq $Id){return $true};$peer=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($peer)};return $false}
function Capture-State([string]$Why){
 $snapshot=[ordered]@{why=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;editor=$null;rows=@();selected=@();peers=@();focus=$null;focused_any_pid=$null;desktop=$null;errors=@()}
 try{$snapshot.address=Address-Text;$snapshot.editor=Peer-Evidence (Editor);$snapshot.rows=@(Row-Names);$snapshot.selected=@(Selected-Names)}catch{$snapshot.errors+=@('listing: '+$_.Exception.Message)}
 # Failure evidence stays within fixed semantic targets. Never enumerate all
 # owned XAML peers: expanded trees can make diagnostics outlast the timeout.
 foreach($id in @('AddressBar','AddressEditor','AddressBreadcrumbs','AddressChildrenMenu','AddressChild0','AddressChildrenEmpty','AddressChildrenLoading','AddressChildrenFailed')){
  try{$peer=Owned-Id $id;if($peer){$snapshot.peers+=@(Peer-Evidence $peer)}}catch{$snapshot.errors+=@($id+': '+$_.Exception.Message)}
 }
 try{
  $crumbs=By-Id 'AddressBreadcrumbs'
  if($crumbs){
   $buttonCondition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Button)
   $buttons=@($crumbs.FindAll([Windows.Automation.TreeScope]::Descendants,$buttonCondition)|Where-Object{$_.Current.ProcessId -eq $process.Id -and $_.Current.AutomationId -cmatch '^AddressChildren[0-9]+$'}|Select-Object -First 32)
   foreach($button in $buttons){$snapshot.peers+=@(Peer-Evidence $button)}
  }
 }catch{$snapshot.errors+=@('bounded breadcrumb buttons: '+$_.Exception.Message)}
 try{$snapshot.desktop=[BlinkAddressChildrenUiNative]::DesktopState();$focus=[Windows.Automation.AutomationElement]::FocusedElement;$snapshot.focused_any_pid=Peer-Evidence $focus;if($focus -and $focus.Current.ProcessId -eq $process.Id){$snapshot.focus=$snapshot.focused_any_pid}}catch{$snapshot.errors+=@('desktop/focus: '+$_.Exception.Message)}
 $report.last_ui_snapshot=[pscustomobject]$snapshot;Save-Report;return [pscustomobject]$snapshot
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$clock=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('ADDRESS_CHILDREN_UI_SKIP: ') -or $_.Exception.Message.StartsWith('WATCHER_TEST_PRECONDITION_NOT_MET: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($clock.Elapsed.TotalSeconds -lt $Seconds);try{$null=Capture-State $Why}catch{};throw ('Timed out: '+$Why+'; '+$last)}
function Require-Desktop{$report.desktop_preflight=[BlinkAddressChildrenUiNative]::DesktopState();if(-not $report.desktop_preflight.InputReady){Skip ('Unlocked desktop required: '+$report.desktop_preflight.Reason)}}
function Key([uint32]$Code,[int]$Modifiers=0){Require-Desktop;if($report.desktop_preflight.ForegroundPid -ne $process.Id){throw 'Owned test window lost foreground before keyboard input'};[BlinkAddressChildrenUiNative]::Key($process.Id,$Code,$Modifiers)}
function Act($Element){if(-not(Visible $Element) -or $Element.Current.ProcessId -ne $process.Id -or -not $Element.Current.IsEnabled){throw 'Visible enabled owned action required'};$pattern=$null;if($Element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}elseif($Element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)){$pattern.Toggle()}elseif($Element.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$pattern.Expand()}else{throw ('No actual Invoke/Expand pattern on '+$Element.Current.AutomationId)}}
function Focus-Owned($Peer){
 if(-not(Visible $Peer) -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw 'Visible enabled owned focus target required'}
 $runtimeId=$Peer.GetRuntimeId()-join ',';$expected=Peer-Evidence $Peer;$proof=@{value=$null};$attemptCounter=@{value=0}
 $report.last_focus_attempt=[pscustomobject]@{step=$script:currentStep;utc=[DateTime]::UtcNow.ToString('o');attempt=0;expected=$expected;expected_current=$null;expected_readable=$null;expected_visible=$null;expected_error=$null;actual_focused=$null;actual_has_keyboard_focus=$null;desktop=$null;foreground_pid=$null;matched=$false;error=$null}
 $Peer.SetFocus()
 Wait-Ui 'actual owned focus and foreground' {
  $attemptCounter.value++
  $attempt=[ordered]@{step=$script:currentStep;utc=[DateTime]::UtcNow.ToString('o');attempt=$attemptCounter.value;expected=$expected;expected_current=$null;expected_readable=$false;expected_visible=$null;expected_error=$null;actual_focused=$null;actual_has_keyboard_focus=$null;desktop=$null;foreground_pid=$null;matched=$false;error=$null}
  try{
   $desktop=[BlinkAddressChildrenUiNative]::DesktopState();$attempt.desktop=$desktop;$attempt.foreground_pid=$desktop.ForegroundPid
   $focus=[Windows.Automation.AutomationElement]::FocusedElement
   # Keep the actual peer even when another process owns focus. This is a
   # single replaced diagnostic record, never an unbounded polling history.
   $actual=Peer-Evidence $focus;$attempt.actual_focused=$actual
   if($focus){$attempt.actual_has_keyboard_focus=$focus.Current.HasKeyboardFocus}
   # Re-read the original provider independently: a rebuilt breadcrumb may
   # invalidate it while the foreground window still belongs to this PID.
   try{$attempt.expected_current=Peer-Evidence $Peer;$attempt.expected_readable=$true;$attempt.expected_visible=Visible $Peer}catch{$attempt.expected_error=$_.Exception.Message}
   if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id -or -not $focus -or $actual.pid -ne $process.Id -or -not $attempt.actual_has_keyboard_focus -or $actual.runtime_id -cne $runtimeId){return $false}
   $attempt.matched=$true;$proof.value=[pscustomobject]@{step=$script:currentStep;focus=$actual;foreground_pid=$desktop.ForegroundPid};return $true
  }catch{$attempt.error=$_.Exception.Message;throw}
  finally{$report.last_focus_attempt=[pscustomobject]$attempt}
 }
 $report.focus_evidence+=@($proof.value);Save-Report
}
function Enter-Edit{Focus-Owned (By-Id 'AddressHistoryButton');Key 0x4C 1;Wait-Ui 'visible focused address editor' {(Visible (Editor)) -and (Editor).Current.HasKeyboardFocus -and (Focus-In 'AddressEditor')}}
function Set-Draft([string]$Text){$peer=Editor;if(-not(Visible $peer)){throw 'Visible owned address editor required'};$peer.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Text);Wait-Ui 'address draft reflected' {(Editor-Text) -ceq $Text -and (Address-Text) -ceq $Text}}
function Children-Open{return $null -ne (Owned-Id 'AddressChildrenMenu') -or $null -ne (Owned-Id 'AddressChildrenLoading') -or $null -ne (Owned-Id 'AddressChildrenEmpty') -or $null -ne (Owned-Id 'AddressChildrenFailed') -or $null -ne (Owned-Id 'AddressChild0')}
function Close-Children{if(Children-Open){Key 27;Wait-Ui 'children menu dismissed' {-not(Children-Open)}}}
function Navigate([string]$Target,[string]$ExpectedMarker=$marker){if($Target -notin $allowedDirectories){throw 'Refusing navigation outside this GUID fixture'};Close-Children;Enter-Edit;Set-Draft $Target;Key 13;Wait-Ui 'exact owned directory committed' {-not(Editor) -and (Address-Text) -ieq $Target -and (Ready) -and $(if($ExpectedMarker){@(Row-Names) -ccontains $ExpectedMarker}else{@(Row-Names).Count -eq 0})}}
function In-Crumbs($Peer){for($depth=0;$Peer -and $depth -lt 24;$depth++){if($Peer.Current.ProcessId -ne $process.Id){return $false};if($Peer.Current.AutomationId -ceq 'AddressBreadcrumbs'){return $true};$Peer=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($Peer)};return $false}
function Child-Button([string]$Path,[bool]$PopupOnly=$false){
 $buttonCondition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Button)
 $candidates=@()
 if(-not $PopupOnly){
  $crumbs=By-Id 'AddressBreadcrumbs';if(-not $crumbs){return $null}
  $candidates=@($crumbs.FindAll([Windows.Automation.TreeScope]::Descendants,$buttonCondition))
 }else{
  foreach($ownedHwnd in [BlinkAddressChildrenUiNative]::OwnedTopWindows($process.Id)){
   try{
    $root=[Windows.Automation.AutomationElement]::FromHandle($ownedHwnd)
    if($root.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'}
    if($root.Current.ControlType -eq [Windows.Automation.ControlType]::Button){$candidates+=@($root)}
    $candidates+=@($root.FindAll([Windows.Automation.TreeScope]::Descendants,$buttonCondition))
   }catch{if([BlinkAddressChildrenUiNative]::WindowExists($ownedHwnd)){throw}}
  }
 }
 $unique=@{}
 foreach($peer in $candidates){
  if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer) -and $peer.Current.AutomationId -cmatch '^AddressChildren[0-9]+$' -and $peer.Current.HelpText -ieq $Path -and $peer.Current.IsEnabled -and (-not $PopupOnly -or -not(In-Crumbs $peer))){$unique[($peer.GetRuntimeId()-join ',')]=$peer}
 }
 if($unique.Count -eq 1){return @($unique.Values)[0]};return $null
}
function Breadcrumb-Action([string]$Path){
 $child=Child-Button $Path;if(-not $child){throw 'Actual inline child button required to identify its own native breadcrumb'}
 $peer=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($child)
 for($depth=0;$peer -and $depth -lt 24;$depth++){
  if($peer.Current.ProcessId -ne $process.Id -or $peer.Current.AutomationId -ceq 'AddressBreadcrumbs'){break}
  $pattern=$null;if((Visible $peer) -and $peer.Current.IsEnabled -and $peer.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){return $peer}
  $peer=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($peer)
 };throw ('No actual native breadcrumb action ancestor for '+$Path)
}
function Children-Rows{$rows=[Collections.Generic.List[object]]::new();for($index=0;$index -lt 256;$index++){$peer=Owned-Id ('AddressChild'+$index);if(-not $peer){break};$rows.Add([pscustomobject]@{index=$index;element=$peer;name=$peer.Current.Name;help_text=$peer.Current.HelpText})};return @($rows.ToArray())}
function Children-Snapshot([string]$Label){$snapshot=[pscustomobject]@{label=$Label;utc=[DateTime]::UtcNow.ToString('o');address=(Address-Text);root=(Peer-Evidence (Owned-Id 'AddressChildrenMenu'));items=@(Children-Rows|ForEach-Object{[pscustomobject]@{index=$_.index;name=$_.name;help_text=$_.help_text;peer=(Peer-Evidence $_.element)}});empty=(Peer-Evidence (Owned-Id 'AddressChildrenEmpty'));failed=(Peer-Evidence (Owned-Id 'AddressChildrenFailed'))};$report.menu_snapshots+=@($snapshot);Save-Report;return $snapshot}
function Child-Target([string]$Path){if($Path -notin $allowedDirectories){throw 'Menu action outside owned fixture'};$matches=@(Children-Rows|Where-Object{$_.help_text -ieq $Path});if($matches.Count -ne 1){throw ('Exact owned menu target not unique: '+$Path)};return $matches[0]}
function Open-Children([string]$Path,[bool]$Keyboard=$false,[bool]$PopupOnly=$false,[uint32]$KeyCode=13){Wait-Ui 'actual child-directory button exists' {$null -ne (Child-Button $Path $PopupOnly)};$button=Child-Button $Path $PopupOnly;if($Keyboard){Focus-Owned $button;Key $KeyCode}else{Act $button};Wait-Ui 'children enumeration completed with real rows or status' {$null -ne (Owned-Id 'AddressChild0') -or $null -ne (Owned-Id 'AddressChildrenEmpty') -or $null -ne (Owned-Id 'AddressChildrenFailed')};$null=Children-Snapshot $Path}
function Capture-WatcherState([string]$Phase,$OriginalAnchor,$ExpectedAnchor){
 $state=[ordered]@{phase=$Phase;utc=$null;foreground_pid=$null;input_ready=$null;desktop=$null;focused_any_pid=$null;focus_has_keyboard_focus=$null;menu_open=$null;newrow_present=$null;address=$null;expected_anchor=$ExpectedAnchor;original_anchor=$null;original_anchor_readable=$false;current_anchor=$null;current_anchor_readable=$false;anchor_runtime_changed=$null;errors=@()}
 try{$state.menu_open=Children-Open}catch{$state.errors+=@('menu: '+$_.Exception.Message)}
 try{$state.newrow_present=@(Row-Names) -ccontains $refreshName;$state.address=Address-Text}catch{$state.errors+=@('listing: '+$_.Exception.Message)}
 try{$state.original_anchor=Peer-Evidence $OriginalAnchor;$state.original_anchor_readable=$true}catch{$state.errors+=@('original anchor: '+$_.Exception.Message)}
 try{$currentAnchor=Child-Button $deep;$state.current_anchor=Peer-Evidence $currentAnchor;if($currentAnchor){$state.current_anchor_readable=$true;$state.anchor_runtime_changed=$state.current_anchor.runtime_id -cne $ExpectedAnchor.runtime_id}}catch{$state.errors+=@('current anchor: '+$_.Exception.Message)}
 # Capture foreground and focus after the UI reads so a light-dismiss caused
 # by user activity has contemporaneous evidence, including a foreign PID.
 try{$desktop=[BlinkAddressChildrenUiNative]::DesktopState();$state.desktop=$desktop;$state.foreground_pid=$desktop.ForegroundPid;$state.input_ready=$desktop.InputReady}catch{$state.errors+=@('desktop: '+$_.Exception.Message)}
 try{$focus=[Windows.Automation.AutomationElement]::FocusedElement;$state.focused_any_pid=Peer-Evidence $focus;if($focus){$state.focus_has_keyboard_focus=$focus.Current.HasKeyboardFocus}}catch{$state.errors+=@('focus: '+$_.Exception.Message)}
 $state.utc=[DateTime]::UtcNow.ToString('o');$report.last_watcher_state=[pscustomobject]$state;Save-Report;return [pscustomobject]$state
}
function Require-WatcherForeground($State){
 if(-not $State.input_ready -or $State.foreground_pid -ne $process.Id){throw ('WATCHER_TEST_PRECONDITION_NOT_MET: owned foreground/input desktop lost during '+$State.phase+'; expected PID '+$process.Id+', actual foreground PID '+$State.foreground_pid+'; menu-open='+$State.menu_open+'. External focus changes may normally dismiss the menu; this is not a product-menu failure assertion.')}
}
function Toggle-State($Element){$pattern=$null;if($Element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)){return $pattern.Current.ToggleState -eq [Windows.Automation.ToggleState]::On};if($Element.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)){return $pattern.Current.IsSelected};throw 'Actual native toggle/selection pattern missing'}
function Close-ViewMenus{for($attempt=0;$attempt -lt 3;$attempt++){if(-not(Owned-Id 'TopViewMenu') -and -not(Owned-Id 'TopViewShowHidden')){return};Key 27};Wait-Ui 'View menus dismissed' {-not(Owned-Id 'TopViewMenu') -and -not(Owned-Id 'TopViewShowHidden')}}
function Set-Hidden([bool]$Show){Close-Children;Focus-Owned (By-Id 'FolderViewMode');Act (By-Id 'FolderViewMode');Wait-Ui 'actual View menu' {$null -ne (Owned-Id 'TopViewShow')};Act (Owned-Id 'TopViewShow');Wait-Ui 'actual Hidden submenu' {$null -ne (Owned-Id 'TopViewShowHidden')};$item=Owned-Id 'TopViewShowHidden';if((Toggle-State $item) -ne $Show){Act $item;Wait-Ui 'hidden choice dismissed View menu' {-not(Owned-Id 'TopViewMenu')}}else{Close-ViewMenus};Wait-Ui 'hidden policy reflected in exact listing' {(@(Row-Names) -ccontains [IO.Path]::GetFileName($hidden)) -eq $Show}}
function Tabs{return @((By-Id 'WindowTabs').FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TabItem)))}
function Native-Overflow{
 if(-not $NativeOverflowId -and -not $NativeOverflowName){$report.overflow_evidence=[pscustomobject]@{status='ACTUAL OWNED PEER PROBE ONLY';snapshot=(Capture-State 'native overflow identity required')};Save-Report;throw 'Provide probe-confirmed -NativeOverflowId or -NativeOverflowName; no native overflow action was guessed'}
 $matches=@(Descendants (By-Id 'AddressBreadcrumbs')|Where-Object{(Visible $_) -and $_.Current.IsEnabled -and $(if($NativeOverflowId){$_.Current.AutomationId -ceq $NativeOverflowId}else{$_.Current.Name -ceq $NativeOverflowName})}|Where-Object{$pattern=$null;$_.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern) -or $_.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)})
 if($matches.Count -ne 1){throw ('Confirmed overflow identity did not match one actionable peer: '+$matches.Count)};return $matches[0]
}
function Verify-Bytes([string]$Path){if(-not $allowedFiles.ContainsKey($Path) -or -not(Test-Path -LiteralPath $Path -PathType Leaf) -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) -cne [Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes([string]$allowedFiles[$Path]))){throw ('Owned file bytes changed: '+$Path)}}
function Ownership-Failures{
 $bad=[Collections.Generic.List[string]]::new();$pending=[Collections.Generic.Stack[string]]::new();$mask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
 $root=Get-Item -LiteralPath $fixture -Force;if(-not $root.PSIsContainer -or ($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($root.Attributes -band $mask)){return @('Fixture root type or attributes changed')};$pending.Push($fixture)
 while($pending.Count){foreach($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){$bad.Add('Reparse entry: '+$entry.FullName);continue}
  if($entry.PSIsContainer){$expected=$(if($entry.FullName -ieq $hidden){[IO.FileAttributes]::Hidden}else{0});if($entry.FullName -in $allowedDirectories -and ($entry.Attributes -band $mask) -eq $expected){$pending.Push($entry.FullName)}else{$bad.Add('Unknown directory or attributes: '+$entry.FullName)};continue}
  if(-not $allowedFiles.ContainsKey($entry.FullName)){$bad.Add('Unknown file: '+$entry.FullName);continue};if($entry.Attributes -band $mask){$bad.Add('Unknown file attributes: '+$entry.FullName)};try{Verify-Bytes $entry.FullName}catch{$bad.Add('Unknown or unreadable bytes: '+$entry.FullName)}
 }}
 foreach($directoryPath in $allowedDirectories){if(-not(Test-Path -LiteralPath $directoryPath -PathType Container)){$bad.Add('Missing original directory: '+$directoryPath)}}
 foreach($filePath in $initialFiles){if(-not(Test-Path -LiteralPath $filePath -PathType Leaf)){$bad.Add('Missing original file: '+$filePath)}}
 if($script:refreshCreated -and -not(Test-Path -LiteralPath $refreshPath -PathType Leaf)){$bad.Add('Missing owned refresh marker')};return @($bad.ToArray())
}
Save-Report
try{
 if([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or -not [Environment]::UserInteractive){Skip 'Interactive Windows desktop required'}
 if(-not(Test-Path -LiteralPath $Binary -PathType Leaf)){Skip 'Candidate runtime unavailable'}
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary user required'}
 $version=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
 $report.environment=[ordered]@{display_version=$version.DisplayVersion;build=$version.CurrentBuildNumber;ubr=$version.UBR;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;os_64bit=[Environment]::Is64BitOperatingSystem;process_64bit=[Environment]::Is64BitProcess;is_administrator=$false;explorer_reference='NOT RUN'}
 New-Item -ItemType Directory -Path $compilerTemp -Force|Out-Null;$env:TEMP=$compilerTemp;$env:TMP=$compilerTemp;$tempChanged=$true
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkAddressChildrenUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkAddressChildrenUiNative {
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
    public static BlinkAddressChildrenUiDesktop DesktopState() {
        var result=new BlinkAddressChildrenUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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
 [IO.File]::SetAttributes($hidden,[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::Hidden)
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false
 $psi.EnvironmentVariables.Remove('BLINK_INITIAL_PATH');$psi.EnvironmentVariables.Remove('BLINK_PHASE1A_UI_PROBE');$psi.EnvironmentVariables.Remove('BLINK_STARTUP_TELEMETRY')
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned Blink launch failed'}
 $report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)}
 [BlinkAddressChildrenUiNative]::OwnedWindow=$process.MainWindowHandle
 $report.window_dpi=[BlinkAddressChildrenUiNative]::GetDpiForWindow($process.MainWindowHandle);$originalSize=[BlinkAddressChildrenUiNative]::WindowSize($process.Id)
 $thisPcLabels=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq 'Location.ThisPC'){$thisPcLabels+=@([string]$data.value)}}}
 Check '01 ThisPC child menu lists actual available local volumes without navigation' {
  Wait-Ui 'actual virtual ThisPC presentation' {$thisPcLabels -ccontains (Address-Text) -and (Visible (By-Id 'AddressBreadcrumbs'))}
  Focus-Owned (By-Id 'AddressHistoryButton');$virtual=Address-Text;Open-Children $virtual
  $snapshot=Children-Snapshot 'ThisPC actual volumes';$requiredRoots=@('C:\','D:\'|Where-Object{[IO.Directory]::Exists($_)})
  foreach($rootPath in $requiredRoots){if(@($snapshot.items|Where-Object{$_.help_text -ieq $rootPath}).Count -ne 1){throw ('Accessible local root missing from actual menu: '+$rootPath)}}
  if((Address-Text) -cne $virtual -or (Editor)){throw 'Opening ThisPC child menu navigated or entered edit mode'};Close-Children
 }
 Check '02 current-level menu lists direct visible directories and excludes files and hidden directory' {
  Navigate $deep;Set-Hidden $false;Open-Children $deep;$snapshot=Children-Snapshot 'default visible children'
  $actual=@($snapshot.items|ForEach-Object{$_.help_text}|Sort-Object);$expected=@($childA,$childB)|Sort-Object
  if(-not(Same-Names $actual $expected) -or @($snapshot.items|Where-Object{$_.name -ceq $marker}).Count -or (Address-Text) -ine $deep){throw 'Direct children menu contains files, hidden entries, descendants or changed location'};Close-Children
 }
 Check '03 Enter and Space open the child button and Home Down Enter selects a real directory' {
  Open-Children $deep $true;Close-Children;Open-Children $deep $true $false 0x20;$target=Child-Target $childB
  # The product must establish the keyboard starting point after async loading.
  # Never SetFocus on a menu leaf here; doing so would conceal a focus defect.
  $proof=@{value=$null}
  Wait-Ui 'async menu automatically focused its first real item in the owned foreground window' {
   $focus=[Windows.Automation.AutomationElement]::FocusedElement
   $desktop=[BlinkAddressChildrenUiNative]::DesktopState()
   if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id -or -not $focus){return $false}
   $current=$focus.Current
   if($current.ProcessId -ne $process.Id -or $current.AutomationId -cne 'AddressChild0' -or -not $current.HasKeyboardFocus){return $false}
   $proof.value=[pscustomobject]@{step=$script:currentStep;stage='product async menu focus without test SetFocus';focus=(Peer-Evidence $focus);foreground_pid=$desktop.ForegroundPid};return $true
  }
  $report.focus_evidence+=@($proof.value);Save-Report;Key 0x24
  for($index=0;$index -lt $target.index;$index++){Key 0x28};Key 13
  Wait-Ui 'keyboard child selection navigated to exact owned folder' {-not(Children-Open) -and (Address-Text) -ieq $childB -and (Ready) -and @(Row-Names) -ccontains $marker};Verify-Bytes (Join-Path $childB $marker)
 }
 Check '04 ancestor menu opens without navigation and its child invokes normal navigation' {
  Navigate $childB;Open-Children $deep
  if((Address-Text) -ine $childB){throw 'Opening an ancestor child menu first navigated to the ancestor'}
  $snapshot=Children-Snapshot 'ancestor direct children';$actual=@($snapshot.items|ForEach-Object{$_.help_text}|Sort-Object);$expected=@($childA,$childB)|Sort-Object;if(-not(Same-Names $actual $expected)){throw 'Ancestor menu did not show its exact direct child directories'}
  Act (Child-Target $childA).element;Wait-Ui 'ancestor child choice committed exact owned location' {-not(Children-Open) -and (Address-Text) -ieq $childA -and (Ready) -and @(Row-Names) -ccontains $marker}
 }
 Check '05 empty directory shows one disabled empty status and no actionable inherited children' {
  Navigate $empty '';Open-Children $empty;$snapshot=Children-Snapshot 'empty owned directory'
  if(-not $snapshot.empty.found -or $snapshot.empty.enabled -or $snapshot.failed.found -or $snapshot.items.Count -ne 0 -or (Address-Text) -ine $empty){throw 'Empty directory menu is actionable, failed or reused old children'};Close-Children
 }
 Check '06 Down on the native breadcrumb opens children and Escape preserves path and selection' {
  Navigate $deep;$rows=@((Entries).FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.Name -ceq $marker -and (Visible $_)})
  if($rows.Count -ne 1){throw 'Exact owned marker row missing'};$rows[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$rows[0].SetFocus()
  $crumb=Breadcrumb-Action $deep;$targetCrumb=Peer-Evidence $crumb;$targetRuntime=$targetCrumb.runtime_id
  $barRuntime=(By-Id 'AddressBreadcrumbs').GetRuntimeId()-join ',';$focusHop=@{runtime=$null;proof=$null}
  # Native BreadcrumbBar redirects keyboard entry to its first visible item
  # (including More). Enter the bar, then follow its real Right-key navigation.
  $readNativeFocus={param([string]$Stage,[string]$PreviousRuntime='')
   $focus=[Windows.Automation.AutomationElement]::FocusedElement;$desktop=[BlinkAddressChildrenUiNative]::DesktopState()
   if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id -or -not $focus){return $false}
   $current=$focus.Current
   if($current.ProcessId -ne $process.Id -or -not $current.HasKeyboardFocus -or $current.ClassName -cnotmatch '(^|\.)BreadcrumbBarItem$'){return $false}
   $actual=Peer-Evidence $focus;if($PreviousRuntime -and $actual.runtime_id -ceq $PreviousRuntime){return $false}
   $ancestor=$focus;$inCurrentBar=$false
   for($depth=0;$ancestor -and $depth -lt 24;$depth++){
    if($ancestor.Current.ProcessId -ne $process.Id){break}
    if($ancestor.Current.AutomationId -ceq 'AddressBreadcrumbs'){$inCurrentBar=($ancestor.GetRuntimeId()-join ',') -ceq $barRuntime;break}
    $ancestor=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($ancestor)
   }
   if(-not $inCurrentBar){return $false}
   $focusHop.runtime=$actual.runtime_id;$focusHop.proof=[pscustomobject]@{step=$script:currentStep;stage=$Stage;focus=$actual;has_keyboard_focus=$true;foreground_pid=$desktop.ForegroundPid;bar_runtime_id=$barRuntime;target=$targetCrumb;previous_runtime_id=$PreviousRuntime};return $true
  }
  $crumb.SetFocus()
  Wait-Ui 'actual native breadcrumb focus entered the current bar' {&$readNativeFocus 'native bar entry (More is allowed only as the starting item)'}
  $report.focus_evidence+=@($focusHop.proof);Save-Report
  for($hop=0;$focusHop.runtime -cne $targetRuntime -and $hop -lt 32;$hop++){
   $previousRuntime=$focusHop.runtime;Key 0x27
   Wait-Ui 'Right moved actual native focus to a different breadcrumb in the current bar' {&$readNativeFocus ('native Right key '+($hop+1)) $previousRuntime}
   $report.focus_evidence+=@($focusHop.proof);Save-Report
  }
  if($focusHop.runtime -cne $targetRuntime){throw 'Native breadcrumb target was not reached by at most 32 real Right keys; More is not accepted as the target'}
  Key 0x28;Wait-Ui 'Down from the actual focused native breadcrumb opened its children' {$null -ne (Owned-Id 'AddressChild0') -and (Address-Text) -ieq $deep};$null=Children-Snapshot 'native breadcrumb Down';Key 27;Wait-Ui 'menu closed while committed location and selection stayed exact' {-not(Children-Open) -and (Address-Text) -ieq $deep -and (Same-Names @(Selected-Names) @($marker))};Verify-Bytes (Join-Path $deep $marker)
 }
 Check '07 Ctrl L retires the open menu and Escape abandons an unsent draft' {
  Open-Children $deep;Key 0x4C 1;Wait-Ui 'Ctrl L closed child menu and focused native editor' {-not(Children-Open) -and (Visible (Editor)) -and (Editor).Current.HasKeyboardFocus -and (Editor-Text) -ieq $deep}
  $draft=Join-Path $fixture ('unsent-'+$token);Set-Draft $draft;Key 27;Wait-Ui 'Escape restored the committed folder' {-not(Editor) -and (Address-Text) -ieq $deep -and (Ready)}
  if(Test-Path -LiteralPath $draft){throw 'Unsubmitted draft created a path'}
 }
 Check '08 child menu follows the real Show Hidden policy and excludes files in both states' {
  Set-Hidden $true;Open-Children $deep;$snapshot=Children-Snapshot 'hidden enabled';$actual=@($snapshot.items|ForEach-Object{$_.help_text}|Sort-Object);$expected=@($childA,$childB,$hidden)|Sort-Object
  if(-not(Same-Names $actual $expected)){throw 'Show Hidden policy did not reveal the exact hidden child directory'};Close-Children
  Set-Hidden $false;Open-Children $deep;$snapshot=Children-Snapshot 'hidden disabled again';if(@($snapshot.items|Where-Object{$_.help_text -ieq $hidden}).Count -or $snapshot.items.Count -ne 2){throw 'Child menu retained the old visibility snapshot'};Close-Children
 }
 Check '09 switching tabs closes an open menu and a stale child peer cannot navigate the new tab' {
  $tabCount=@(Tabs).Count;Focus-Owned (By-Id 'AddressHistoryButton');Key 0x54 1;Wait-Ui 'temporary second tab exists' {@(Tabs).Count -eq $tabCount+1 -and $thisPcLabels -ccontains (Address-Text)};Navigate $empty ''
  $tabPeers=@(Tabs);$tabPeers[$tabCount-1].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Wait-Ui 'original deep tab active' {(Address-Text) -ieq $deep -and (Ready)}
  Open-Children $deep;$old=(Child-Target $childA).element;$oldEvidence=Peer-Evidence $old
  $tabPeers=@(Tabs);$tabPeers[$tabCount].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Wait-Ui 'tab switch retired old menu' {(Address-Text) -ieq $empty -and -not(Children-Open) -and (Ready) -and @(Row-Names).Count -eq 0}
  $outcome='Invoke returned';try{$old.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}catch{$outcome='Old provider rejected Invoke: '+$_.Exception.Message}
  $null=(By-Id 'FolderViewMode').Current.HelpText
  if((Address-Text) -ine $empty -or (Editor) -or -not(Ready) -or @(Row-Names).Count){throw 'Stale menu callback changed the active tab'}
  $report.stale_peer=[pscustomobject]@{old=$oldEvidence;outcome=$outcome;menu_was_open_at_tab_switch=$true;active_address=(Address-Text)};Save-Report
  Focus-Owned (By-Id 'AddressHistoryButton');Key 0x57 1;Wait-Ui 'temporary tab closed back to deep' {@(Tabs).Count -eq $tabCount -and (Address-Text) -ieq $deep -and (Ready)}
 }
 Check '10 a real watcher refresh preserves the open menu and its directory choice' {
  Open-Children $deep;$oldMenu=Peer-Evidence (Owned-Id 'AddressChildrenMenu');$watcherAnchor=Child-Button $deep;$anchorBefore=Peer-Evidence $watcherAnchor
  if(-not $watcherAnchor){throw 'Owned current-directory child button required before watcher check'}
  $report.automatic_refresh=[ordered]@{source='owned file write and watcher, no manual refresh';menu_before=$oldMenu;anchor_before=$anchorBefore;menu_after=$null;anchor_after=$null;snapshot=$null};Save-Report
  $proof=@{value=$null}
  Wait-Ui 'watcher precondition is actual first-menu-item focus and owned foreground' {
   $state=Capture-WatcherState 'before owned file write' $watcherAnchor $anchorBefore;Require-WatcherForeground $state
   $focus=$state.focused_any_pid
   if(-not $focus.found -or $focus.pid -ne $process.Id -or $focus.id -cne 'AddressChild0' -or -not $state.focus_has_keyboard_focus -or -not $state.menu_open){return $false}
   $proof.value=[pscustomobject]@{step=$script:currentStep;stage='watcher precondition: actual first menu item without test SetFocus';focus=$focus;foreground_pid=$state.foreground_pid;anchor=$anchorBefore};return $true
  }
  $report.focus_evidence+=@($proof.value);Save-Report
  [IO.File]::WriteAllText($refreshPath,$allowedFiles[$refreshPath],[Text.UTF8Encoding]::new($false));$script:refreshCreated=$true
  Wait-Ui 'owned file write triggered actual listing refresh while menu remained open' {
   $state=Capture-WatcherState 'after owned file write' $watcherAnchor $anchorBefore;Require-WatcherForeground $state
   $state.newrow_present -and $state.menu_open -and $state.address -ieq $deep -and $null -ne (Owned-Id 'AddressChild0')
  }
  Verify-Bytes $refreshPath;$report.automatic_refresh.menu_after=Peer-Evidence (Owned-Id 'AddressChildrenMenu');$report.automatic_refresh.anchor_after=Peer-Evidence (Child-Button $deep);$report.automatic_refresh.snapshot=Children-Snapshot 'after real watcher refresh';Save-Report
  Act (Child-Target $childA).element;Wait-Ui 'preserved menu item navigated after watcher refresh' {(Address-Text) -ieq $childA -and -not(Children-Open) -and (Ready)}
 }
 Check '11 narrow native overflow retains a collapsed ancestors child-directory action' {
  Navigate $deep
  try{
   [BlinkAddressChildrenUiNative]::Resize($process.Id,620,$originalSize[1]);Wait-Ui 'owned window narrowed' {([BlinkAddressChildrenUiNative]::WindowSize($process.Id))[0] -le 640}
   if(Child-Button $ancestor){throw 'Chosen ancestor was still inline; overflow precondition not met'}
   $overflow=Native-Overflow;$overflowEvidence=Peer-Evidence $overflow;Act $overflow;Wait-Ui 'native overflow exposes the collapsed ancestors actual child button' {$null -ne (Child-Button $ancestor $true)}
   $button=Child-Button $ancestor $true;$before=Peer-Evidence $button;Open-Children $ancestor $true $true
   if((Address-Text) -ine $deep){throw 'Opening collapsed ancestors submenu navigated first'}
   $snapshot=Children-Snapshot 'collapsed ancestor child menu';if($snapshot.items.Count -ne 1 -or $snapshot.items[0].help_text -ine $unicode){throw 'Collapsed ancestor submenu target is incorrect'}
   $report.overflow_evidence=[pscustomobject]@{width=620;trigger=$overflowEvidence;child_button=$before;child_menu=$snapshot;committed_after_choice=$null;restored=$false};Save-Report
   Act (Child-Target $unicode).element;Wait-Ui 'collapsed child-menu choice committed exact directory' {(Address-Text) -ieq $unicode -and -not(Editor) -and -not(Children-Open) -and @(Row-Names) -ccontains $marker}
   $report.overflow_evidence.committed_after_choice=Address-Text;Save-Report;Verify-Bytes (Join-Path $unicode $marker)
  }finally{[BlinkAddressChildrenUiNative]::Resize($process.Id,$originalSize[0],$originalSize[1])}
  Wait-Ui 'original window size and command readiness restored' {([BlinkAddressChildrenUiNative]::WindowSize($process.Id))[0] -eq $originalSize[0] -and (Ready) -and (Address-Text) -ieq $unicode}
  $report.overflow_evidence.restored=$true;Save-Report
 }
 Check '12 deleting the owned empty directory produces a real failed status rather than an empty menu' {
  Navigate $empty ''
  $deleted=$false
  $report.deleted_directory=[ordered]@{path=$empty;deleted=$false;failed_snapshot=$null;restored_in_finally=$false};Save-Report
  try{
   $ownedEmpty=Get-Item -LiteralPath $empty -Force
   if(-not $ownedEmpty.PSIsContainer -or ($ownedEmpty.Attributes -band [IO.FileAttributes]::ReparsePoint) -or @(Get-ChildItem -LiteralPath $empty -Force).Count -ne 0){throw 'Only the exact GUID-owned empty directory may be removed for this check'}
   [IO.Directory]::Delete($empty,$false);$deleted=$true;$report.deleted_directory.deleted=$true;Save-Report
   if(Test-Path -LiteralPath $empty){throw 'Owned empty-directory removal was not real'}
   Open-Children $empty
   Wait-Ui 'deleted directory reports actual enumeration failure' {$null -ne (Owned-Id 'AddressChildrenFailed') -and $null -eq (Owned-Id 'AddressChildrenEmpty') -and $null -eq (Owned-Id 'AddressChild0')}
   $snapshot=Children-Snapshot 'deleted committed empty directory'
   if(-not $snapshot.failed.found -or $snapshot.failed.enabled -or [string]::IsNullOrWhiteSpace($snapshot.failed.name) -or $snapshot.empty.found -or $snapshot.items.Count -ne 0 -or (Address-Text) -ine $empty){throw 'Deleted folder failure was replaced by empty success, actionable rows or another location'}
   $report.deleted_directory.failed_snapshot=$snapshot;Save-Report;Close-Children
  }finally{
   if($deleted){
    # Restore the same originally empty GUID fixture even when the UI check fails.
    # Never remove an unexpected replacement or change its attributes/content.
    if(Test-Path -LiteralPath $empty){throw 'Deleted owned path was unexpectedly recreated; replacement retained'}
    $null=[IO.Directory]::CreateDirectory($empty)
    if(-not(Test-Path -LiteralPath $empty -PathType Container) -or @(Get-ChildItem -LiteralPath $empty -Force).Count -ne 0){throw 'Originally empty owned fixture could not be restored'}
    $report.deleted_directory.restored_in_finally=$true;Save-Report
   }
  }
 }
 Check '13 normal-close precondition keeps a real children menu open on the owned directory' {
  Navigate $deep;Open-Children $deep
  if(-not(Children-Open) -or (Address-Text) -ine $deep -or (Editor)){throw 'Normal-close regression requires the actual owned child menu open'}
  $null=Children-Snapshot 'immediately before normal close';Verify-Bytes (Join-Path $deep $marker)
  # Leave it open: finally must close the app normally, with no Escape or kill.
 }
 $notes.Add('Only the recorded ordinary-user Windows host and DPI were exercised. No global input, Clipboard or service changes. Native filesystem cancellation during a genuinely blocked enumeration, access-denied/UNC, IME, real mouse hit testing and complete theme/DPI/language/Narrator coverage remain untested. No reusable owned InputSite pointer-click helper exists; global cursor/input helpers were not used.')
 $report.status='PASS'
}catch{
 if($_.Exception.Message.StartsWith('ADDRESS_CHILDREN_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring('ADDRESS_CHILDREN_UI_SKIP: '.Length)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{$null=Capture-State 'unexpected failure'}catch{}}
}finally{
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if($report.before_close_alive){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');Save-Report;$report.close_request_result=$process.CloseMainWindow();Save-Report;if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned Blink did not close normally; PID '+$process.Id+' retained, no force termination')}}
  $process.Refresh();$report.process_exited=$process.HasExited
  if($process.HasExited){$report.exit_code=$process.ExitCode;$report.exit_code_hex='0x{0:X8}' -f ([long]$report.exit_code -band 4294967295);$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o')}
  if($report.exit_code -ne 0){throw ('Owned Blink returned nonzero exit '+$report.exit_code_hex)}
  if($report.status -eq 'PASS'){Record '14 owned Blink exits normally with its children menu open'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '14 owned process normal close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process still running; fixture retained'}
  $report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
  $report.fixture_bytes_removed=(Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture)
  if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '15 exact GUID paths attributes and UTF8 bytes verified before fixture cleanup'}
 }catch{$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '15 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=(Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -eq 'SKIP';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI ADDRESS CHILDREN '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -ne 'PASS'){exit 2};exit 0
