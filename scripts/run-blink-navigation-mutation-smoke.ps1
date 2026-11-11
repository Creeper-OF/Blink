[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Binary='',
 [ValidateRange(5,90)][int]$StepTimeoutSeconds=30,
 [ValidateSet('All','NewFolder','Rename','Delete')][string]$Feature='All'
)
# G06c: only a new owned PID/HWND, actual scoped navigation commands, and
# identity/byte-checked GUID fixtures. No Clipboard I/O, global input, ordinary
# Recycle deletion, user recycle-bin enumeration, service edits or forced exit.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$integration=Join-Path $project '.build\explorer-navigation-mutation\integration'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$featureCases=@{All=@(1..12);NewFolder=@(1,2,3,4,10);Rename=@(1,5,6,7,11,12);Delete=@(1,8,9)}
$selectedCheckIds=@($featureCases[$Feature]);$expectedChecks=$selectedCheckIds.Count+2
$reportName=$(if($Feature -eq 'All'){'ui-navigation-mutation-report.json'}else{'ui-navigation-mutation-'+$Feature.ToLowerInvariant()+'-report.json'})
$reportPath=Join-Path $integration $reportName
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $integration ('ui-fixtures\owned-'+$guid);$browse=Join-Path $fixture ('browse-'+$token)
$parentTarget=Join-Path $browse ('00-target-'+$token);$decoyName='decoy-'+$token+'.txt'
$renameStart=Join-Path $parentTarget ('00-rename-'+$token+'.v1');$renameAfter=Join-Path $parentTarget ('00-renamed-'+$token+'.v2');$renameFinal=Join-Path $parentTarget ('00-retried-'+$token+'.v3')
$renameNested=Join-Path $renameStart 'nested-é';$renamePayloadName='payload-'+$token+'.txt'
$existing=Join-Path $parentTarget ('01-existing-'+$token);$deleteTarget=Join-Path $parentTarget ('02-delete-'+$token);$deleteNested=Join-Path $deleteTarget 'nested'
$created=Join-Path $parentTarget ('03-created-'+$token);$retryCreated=Join-Path $parentTarget ('04-retry-created-'+$token)
$newCancelName='cancel-new-'+$token;$renameCancelName='cancel-rename-'+$token;$unsubmittedName='unsubmitted-'+$token
$initialDirectories=@($fixture,$browse,$parentTarget,$renameStart,$renameNested,$existing,$deleteTarget,$deleteNested)
$allowedFiles=@{};$fileIdentities=@{};$directoryIdentities=@{}
$allowedFiles[(Join-Path $browse $decoyName)]='Selected decoy must remain unchanged '+$guid
$allowedFiles[(Join-Path $renameStart $renamePayloadName)]='Rename payload '+$guid
$allowedFiles[(Join-Path $renameNested 'nested.txt')]='Nested rename payload '+$guid
$allowedFiles[(Join-Path $existing 'sentinel.txt')]='Existing target must not be overwritten '+$guid
$allowedFiles[(Join-Path $deleteTarget 'root.txt')]='Owned permanent-delete root '+$guid
$allowedFiles[(Join-Path $deleteNested 'payload.txt')]='Owned permanent-delete child '+$guid
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP
$process=$null;$fixtureCreated=$false;$tempChanged=$false;$script:windowCache=$null;$script:currentStep='preflight';$script:browseTab=$null;$script:observerTab=$null
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new()
$report=[ordered]@{
 status='RUNNING';passed=$false;skipped=$false;feature=$Feature;expected_checks=$expectedChecks;configuration=$Configuration;binary=$Binary;binary_sha256=$null
 source_script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
 started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;current_step='preflight';failure=$null;checks=@();notes=@()
 process_id=$null;process_exited=$false;exit_code=$null;exit_code_hex=$null;forced_termination=$false;before_close_alive=$null;close_requested=$false;close_request_utc=$null;close_request_result=$null;process_exit_observed_utc=$null
 environment=$null;window_dpi=$null;desktop_preflight=$null;focus_evidence=@();keyboard_evidence=@();menu_evidence=@();tab_evidence=@();last_ui_snapshot=$null;last_focus_attempt=$null
 name_dialogs=@();retry_evidence=@();mutation_evidence=@();relocation_evidence=@();delete_evidence=@();stale_peer=$null;draft_close=$null;clipboard_io='NONE'
 fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_ownership_failures=@();fixture_bytes_removed=0
 compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0
 human_acceptance='NOT RUN';model_test_results='NOT RUN by this UI smoke'
 input_mode='Owned PID/HWND UIA and InputSite Apps/Ctrl+Shift+N/F2/Shift+Delete only; no global input'
 not_run=@('ordinary recycle-bin deletion or user recycle-bin operations','mutations of real quick locations/user profile or actual drive roots','physical right-click hit testing','Clipboard operations','real IME/UNC/access-denied and partial-I/O failure combinations','full theme/DPI/multi-monitor/language/Narrator matrix')
}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($true))}
function Record([string]$Name,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Name;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('NAV_MUTATION_UI_SKIP: '+$Why)}
function Check([string]$Name,[scriptblock]$Body){
 # Unselected feature cases are not executed or counted as SKIP/PASS.
 $caseId=[int]$Name.Substring(0,2);if($caseId -notin $selectedCheckIds){return}
 $script:currentStep=$Name;Save-Report;try{$null=&$Body;Record $Name}catch{Record $Name 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace);throw}
}
function Window{$process.Refresh();if($process.HasExited){throw ('Owned Blink exited '+$process.ExitCode)};if(-not $script:windowCache -and $process.MainWindowHandle -ne [IntPtr]::Zero){$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)};if($script:windowCache -and $script:windowCache.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'};return $script:windowCache}
function Visible($Peer){return $null -ne $Peer -and -not $Peer.Current.IsOffscreen -and $Peer.Current.BoundingRectangle.Width -gt 0 -and $Peer.Current.BoundingRectangle.Height -gt 0}
function Find-In($Root,[string]$Id){if($Root){foreach($peer in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)))){if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer)){return $peer}}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){$found=By-Id $Id;if($found){return $found};foreach($h in [BlinkNavigationMutationUiNative]::OwnedTopWindows($process.Id)){try{$root=[Windows.Automation.AutomationElement]::FromHandle($h);if($root.Current.ProcessId -ne $process.Id){throw 'Owned root PID mismatch'};if($root.Current.AutomationId -ceq $Id -and (Visible $root)){return $root};$found=Find-In $root $Id;if($found){return $found}}catch{if([BlinkNavigationMutationUiNative]::WindowExists($h)){throw}}};return $null}
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
 $tabs=@(Tabs);$chosen=@();for($i=0;$i -lt $tabs.Count;$i++){$pattern=$null;if($tabs[$i].TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern) -and $pattern.Current.IsSelected){$chosen+=@([pscustomobject]@{ordinal=$i;name=$tabs[$i].Current.Name;observed_runtime_id=($tabs[$i].GetRuntimeId()-join ',');process_id=$tabs[$i].Current.ProcessId;observed_count=$tabs.Count})}}
 if($chosen.Count -ne 1 -or $chosen[0].process_id -ne $process.Id -or -not $chosen[0].name){throw 'One actual named owned selected tab required'};return $chosen[0]
}
function Same-Names([string[]]$Left,[string[]]$Right){return (@($Left|Sort-Object)-join "`n") -ceq (@($Right|Sort-Object)-join "`n")}
function Ready{return (Visible (Entries)) -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}
function Is-ThisPc{return (Address-Text) -in $thisPcLabels -and (Visible (Entries)) -and @(Rows).Count -gt 0 -and -not(Editor)}
function Is-OwnDescendant($Peer,$Ancestor){if(-not $Peer -or -not $Ancestor){return $false};$id=$Ancestor.GetRuntimeId()-join ',';for($p=$Peer;$p;$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)){if($p.Current.ProcessId -ne $process.Id){return $false};if(($p.GetRuntimeId()-join ',') -ceq $id){return $true}};return $false}
function Named-Container([string[]]$Names,$Type){$pane=By-Id 'NavigationPane';if(-not $pane){return $null};foreach($name in $Names){foreach($peer in @($pane.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)))){$p=$peer;while($p -and $p.Current.ProcessId -eq $process.Id){if($p.Current.ControlType -eq $Type -and (Is-OwnDescendant $p $pane)){return $p};if(($p.GetRuntimeId()-join ',') -ceq ($pane.GetRuntimeId()-join ',')){break};$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)}}};return $null}
function Tree-Item([string[]]$Names){return Named-Container $Names ([Windows.Automation.ControlType]::TreeItem)}
function Capture-State([string]$Why){
 $state=[ordered]@{why=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;rows=@();selected=@();peers=@();navigation_tree=@();tabs=@();focus=$null;desktop=$null;errors=@()}
 try{$state.address=Address-Text;$state.rows=@(Row-Names);$state.selected=@(Selected-Names)}catch{$state.errors+=@($_.Exception.Message)}
 foreach($id in @('NavigationPane','NavigationTree','NavigationQuickList','NavigationContextMenu','LocationContextMenu','TargetContextOpen','TargetContextNewTab','TargetContextExpandCollapse','TargetContextCopyPath','TargetContextProperties','TargetContextNewFolder','TargetContextRename','TargetContextDelete','NewFolderName','RenameName','NewFolderRetry','FileOperationStatus','FolderEntries','WindowTabs')){try{$peer=Owned-Id $id;if($peer){$state.peers+=@(Peer-Evidence $peer)}}catch{$state.errors+=@($id+': '+$_.Exception.Message)}}
 try{
  $pane=By-Id 'NavigationPane';if($pane){$items=$pane.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TreeItem));for($i=0;$i -lt [Math]::Min(40,$items.Count);$i++){try{$peer=$items[$i];if($peer.Current.ProcessId -ne $process.Id){continue};$pattern=$null;$expandState='NO PATTERN';if($peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$expandState=$pattern.Current.ExpandCollapseState.ToString()};$state.navigation_tree+=@([pscustomobject]@{ordinal=$i;name=$peer.Current.Name;is_offscreen=$peer.Current.IsOffscreen;expand_collapse=$expandState})}catch{$state.errors+=@('TreeItem '+$i+': '+$_.Exception.Message)}}}
 }catch{$state.errors+=@('Navigation tree snapshot: '+$_.Exception.Message)}
 try{
  $items=@(Tabs);for($i=0;$i -lt [Math]::Min(40,$items.Count);$i++){try{$peer=$items[$i];if($peer.Current.ProcessId -ne $process.Id){continue};$pattern=$null;$isSelected=$null;$hasSelection=$peer.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern);if($hasSelection){$isSelected=$pattern.Current.IsSelected};$state.tabs+=@([pscustomobject]@{ordinal=$i;name=$peer.Current.Name;is_offscreen=$peer.Current.IsOffscreen;selection_pattern=$hasSelection;is_selected=$isSelected})}catch{$state.errors+=@('Tab '+$i+': '+$_.Exception.Message)}}
 }catch{$state.errors+=@('Tab snapshot: '+$_.Exception.Message)}
 try{$state.focus=Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement);$state.desktop=[BlinkNavigationMutationUiNative]::DesktopState()}catch{$state.errors+=@($_.Exception.Message)}
 $report.last_ui_snapshot=[pscustomobject]$state;Save-Report
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('NAV_MUTATION_UI_SKIP: ') -or $_.Exception.Message.StartsWith('NAV_MUTATION_TEST_PRECONDITION_NOT_MET: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);Capture-State $Why;throw ('Timed out: '+$Why+'; '+$last)}
function Require-Owned-Desktop([string]$Why){$desktop=[BlinkNavigationMutationUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id){throw ('NAV_MUTATION_TEST_PRECONDITION_NOT_MET: '+$Why+'; expected PID '+$process.Id+', actual foreground '+$desktop.ForegroundPid+'; '+$desktop.Reason)};return $desktop}
function Key([uint32]$Code,[int]$Modifiers=0){$desktop=Require-Owned-Desktop 'before scoped key';[BlinkNavigationMutationUiNative]::Key($process.Id,$Code,$Modifiers);$report.keyboard_evidence+=@([pscustomobject]@{step=$script:currentStep;key=$Code;modifiers=$Modifiers;foreground_pid=$desktop.ForegroundPid});Save-Report}
function Focus-Peer($Peer,[string]$Why){if(-not $Peer -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw ('Owned enabled focus target required '+$Why)};if(-not(Visible $Peer)){$scroll=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.ScrollItemPattern]::Pattern,[ref]$scroll)){$scroll.ScrollIntoView()}};$runtime=$Peer.GetRuntimeId()-join ',';$Peer.SetFocus();Wait-Ui ('actual owned focus '+$Why) {$focused=[Windows.Automation.AutomationElement]::FocusedElement;$desktop=[BlinkNavigationMutationUiNative]::DesktopState();$report.last_focus_attempt=[pscustomobject]@{target=$Why;expected=(Peer-Evidence $Peer);actual=(Peer-Evidence $focused);desktop=$desktop};return $desktop.InputReady -and $desktop.ForegroundPid -eq $process.Id -and (Visible $Peer) -and $focused -and $focused.Current.ProcessId -eq $process.Id -and $focused.Current.HasKeyboardFocus -and ($focused.GetRuntimeId()-join ',') -ceq $runtime};$report.focus_evidence+=@([pscustomobject]@{step=$script:currentStep;target=$Why;actual=(Peer-Evidence $Peer)});Save-Report}
function Act($Peer){if(-not(Visible $Peer) -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw 'Owned visible enabled action required'};$null=Require-Owned-Desktop 'before real UIA action';$pattern=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}elseif($Peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$pattern.Expand()}else{throw ('No real Invoke/Expand pattern '+$Peer.Current.AutomationId)}}
function Select-Tab($Token,[string]$Path){
 $null=Require-Owned-Desktop 'before selecting the observed owned tab'
 # This run only adds tabs; it never closes or reorders them. The captured
 # ordinal remains stable, and its observed name must still match.
 $tabs=@(Tabs);if($null -eq $Token -or $Token.ordinal -lt 0 -or $Token.ordinal -ge $tabs.Count){throw 'Captured tab ordinal is unavailable'};$peer=$tabs[$Token.ordinal];$pattern=$null
 if($peer.Current.ProcessId -ne $process.Id -or $peer.Current.Name -cne $Token.name -or -not $peer.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)){throw 'Recreated owned tab does not match its captured ordinal/name or selection pattern'}
 $pattern.Select();Wait-Ui 'exact previous tab restored' {$current=@(Tabs);$selected=$null;$current.Count -gt $Token.ordinal -and $current[$Token.ordinal].Current.Name -ceq $Token.name -and $current[$Token.ordinal].TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$selected) -and $selected.Current.IsSelected -and -not(Editor) -and $(if($Path -in $thisPcLabels){Is-ThisPc}else{(Address-Text) -ieq $Path -and (Ready)})}
 $currentTabs=@(Tabs);$restored=$currentTabs[$Token.ordinal];$report.tab_evidence+=@([pscustomobject]@{step=$script:currentStep;operation='requery rebuilt tab by observed ordinal/name';ordinal=$Token.ordinal;name=$Token.name;previous_runtime_id=$Token.observed_runtime_id;current=(Peer-Evidence $restored);actual_address=(Address-Text)});Save-Report
}
function Menu-Open($Peer,[string]$MenuId,[bool]$ShiftF10=$false){Focus-Peer $Peer 'context target';if($ShiftF10){Key 0x79 2}else{Key 0x5D};Wait-Ui ('real context menu '+$MenuId) {$null -ne (Owned-Id $MenuId) -and $null -ne (Owned-Id 'TargetContextOpen')};$menu=Owned-Id $MenuId;$report.menu_evidence+=@([pscustomobject]@{step=$script:currentStep;method=$(if($ShiftF10){'owned Shift+F10'}else{'owned Apps key'});source=(Peer-Evidence $Peer);menu=(Peer-Evidence $menu);items=@('TargetContextOpen','TargetContextNewTab','TargetContextExpandCollapse','TargetContextCopyPath','TargetContextProperties','TargetContextNewFolder','TargetContextRename','TargetContextDelete'|ForEach-Object{Peer-Evidence (Owned-Id $_)})});Save-Report;return $menu}
function Dismiss-Menu{Key 27;Wait-Ui 'context menu dismissed' {-not(Owned-Id 'NavigationContextMenu') -and -not(Owned-Id 'LocationContextMenu')}}
function Resource-Labels([string]$Key){$labels=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq $Key){$labels+=@([string]$data.value)}}};return @($labels|Select-Object -Unique)}

function Value($Peer){if(-not $Peer){throw 'Owned value peer required'};return $Peer.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Dialog-From($Peer){$windowId=(Window).GetRuntimeId()-join ',';$fallback=$null;for($cursor=$Peer;$cursor;$cursor=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($cursor)){if($cursor.Current.ProcessId -ne $process.Id){break};if(($cursor.GetRuntimeId()-join ',') -ceq $windowId){break};if((Visible $cursor) -and (Find-In $cursor 'PrimaryButton') -and (Find-In $cursor 'CloseButton')){if($cursor.Current.ClassName.EndsWith('ContentDialog',[StringComparison]::Ordinal)){return $cursor};if(-not $fallback){$fallback=$cursor}}};return $fallback}
function Name-Dialog([string]$Id){return Dialog-From (Owned-Id $Id)}
function Dialog-Texts($Dialog){if(-not $Dialog){return @()};return @($Dialog.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Text))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_)}|ForEach-Object{$_.Current.Name})}
function Delete-Dialog{
 $labels=@(Resource-Labels 'Delete.ConfirmTitle');$roots=@((Window));foreach($hwnd in [BlinkNavigationMutationUiNative]::OwnedTopWindows($process.Id)){try{$roots+=@([Windows.Automation.AutomationElement]::FromHandle($hwnd))}catch{if([BlinkNavigationMutationUiNative]::WindowExists($hwnd)){throw}}}
 foreach($root in $roots){foreach($label in $labels){$peers=@($root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$label)));if($root.Current.Name -ceq $label){$peers=@($root)+$peers};foreach($peer in $peers){if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer)){$dialog=Dialog-From $peer;if($dialog){return $dialog}}}}};return $null
}
function Focus-In([string]$Id){$focus=[Windows.Automation.AutomationElement]::FocusedElement;return $focus -and $focus.Current.ProcessId -eq $process.Id -and $focus.Current.HasKeyboardFocus -and (Is-OwnDescendant $focus (Owned-Id $Id))}
function Set-Name([string]$Id,[string]$Name){$editor=Owned-Id $Id;if(-not(Visible $editor) -or -not $editor.Current.IsEnabled){throw 'Actual owned name editor required'};$editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Name);Wait-Ui 'exact name draft reflected in real dialog' {(Value (Owned-Id $Id)) -ceq $Name}}
function Assert-NameDialog([string]$Id,[string]$ContextPath,[string]$Initial=''){
 # Existing naming UI shows the destination parent for NewFolder and the
 # complete captured source path for Rename; require its exact visible text.
 Wait-Ui ('real '+$Id+' modal at exact source/target context') {$editor=Owned-Id $Id;$dialog=Name-Dialog $Id;$editor -and $dialog -and $editor.Current.HasKeyboardFocus -and (Focus-In $Id) -and @(Dialog-Texts $dialog) -ccontains $ContextPath -and $(if($Initial){(Value $editor) -ceq $Initial}else{[bool](Value $editor)})}
 $report.name_dialogs+=@([pscustomobject]@{step=$script:currentStep;editor_id=$Id;value=(Value (Owned-Id $Id));context_path=$ContextPath;focus=(Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement))});Save-Report
}
function Submit-Name([string]$Id){$dialog=Name-Dialog $Id;if(-not $dialog){throw 'Current owned name dialog required'};Act (Find-In $dialog 'PrimaryButton');Wait-Ui 'name dialog submitted and closed' {-not(Owned-Id $Id)}}
function Cancel-Name([string]$Id,[string]$FocusArea='NavigationPane'){
 Key 27;Wait-Ui 'Escape cancels naming and restores its actual owner focus' {-not(Owned-Id $Id) -and (Ready) -and (Focus-In $FocusArea)}
 $report.focus_evidence+=@([pscustomobject]@{step=$script:currentStep;operation='actual Escape from naming dialog';expected_area=$FocusArea;focus=(Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement))});Save-Report
}
function Verify-File([string]$Path,[string]$Expected,[string]$Id=''){
 $item=Get-Item -LiteralPath $Path -Force;if($item.PSIsContainer -or ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly))){throw 'Owned payload type/attributes changed'}
 $bytes=[Text.UTF8Encoding]::new($false).GetBytes($Expected);if([Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) -cne [Convert]::ToBase64String($bytes) -or ($Id -and [BlinkNavigationMutationUiNative]::Identity($Path) -cne $Id)){throw ('Owned file identity/bytes changed: '+$Path)}
}
function Assert-Manifest{
 foreach($path in $directoryIdentities.Keys){$item=Get-Item -LiteralPath $path -Force;if(-not $item.PSIsContainer -or ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly)) -or [BlinkNavigationMutationUiNative]::Identity($path) -cne $directoryIdentities[$path]){throw ('Owned directory identity/attributes changed: '+$path)}}
 foreach($path in $allowedFiles.Keys){Verify-File $path ([string]$allowedFiles[$path]) ([string]$fileIdentities[$path])}
 $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($fixture);while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Unexpected reparse point in fixture'};if($item.PSIsContainer){if(-not $directoryIdentities.ContainsKey($item.FullName)){throw ('Unknown directory retained: '+$item.FullName)};$pending.Push($item.FullName)}elseif(-not $allowedFiles.ContainsKey($item.FullName)){throw ('Unknown file retained: '+$item.FullName)}}}
}
function Ownership-Failures{try{Assert-Manifest;return @()}catch{return @($_.Exception.Message)}}
function Register-Created([string]$Path){
 if(-not $Path.StartsWith($parentTarget+'\',[StringComparison]::OrdinalIgnoreCase) -or $directoryIdentities.ContainsKey($Path)){throw 'Unexpected created-directory path'};$item=Get-Item -LiteralPath $Path -Force
 if(-not $item.PSIsContainer -or ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly)) -or @(Get-ChildItem -LiteralPath $Path -Force).Count){throw 'New result must be the exact owned empty directory'}
 $directoryIdentities[$Path]=[BlinkNavigationMutationUiNative]::Identity($Path);Assert-Manifest
}
function Register-Rename([string]$From,[string]$To){
 if(-not $From.StartsWith($parentTarget+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $To.StartsWith($parentTarget+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $From)){throw 'Expected exact renamed directory transition required'}
 $dirs=@($directoryIdentities.Keys|Where-Object{$_ -ieq $From -or $_.StartsWith($From+'\',[StringComparison]::OrdinalIgnoreCase)});$files=@($allowedFiles.Keys|Where-Object{$_.StartsWith($From+'\',[StringComparison]::OrdinalIgnoreCase)});if(-not $dirs.Count){throw 'Original identity graph unavailable'}
 foreach($path in $dirs){$target=$To+$path.Substring($From.Length);if(-not(Test-Path -LiteralPath $target -PathType Container) -or [BlinkNavigationMutationUiNative]::Identity($target) -cne $directoryIdentities[$path]){throw 'Renamed directory/subdirectory identity changed'}}
 foreach($path in $files){Verify-File ($To+$path.Substring($From.Length)) ([string]$allowedFiles[$path]) ([string]$fileIdentities[$path])}
 foreach($path in $dirs){$target=$To+$path.Substring($From.Length);$directoryIdentities[$target]=$directoryIdentities[$path];$directoryIdentities.Remove($path)}
 foreach($path in $files){$target=$To+$path.Substring($From.Length);$allowedFiles[$target]=$allowedFiles[$path];$fileIdentities[$target]=$fileIdentities[$path];$allowedFiles.Remove($path);$fileIdentities.Remove($path)};Assert-Manifest
 $report.mutation_evidence+=@([pscustomobject]@{step=$script:currentStep;kind='actual directory rename';old=$From;new=$To;directory_count=$dirs.Count;file_count=$files.Count;all_native_identities_and_bytes_preserved=$true});Save-Report
}
function Register-Deletion([string]$Path){
 if($Path -cne $deleteTarget -or -not $Path.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Deletion result outside the one authorized test target'}
 $dirs=@($directoryIdentities.Keys|Where-Object{$_ -ieq $Path -or $_.StartsWith($Path+'\',[StringComparison]::OrdinalIgnoreCase)});$files=@($allowedFiles.Keys|Where-Object{$_.StartsWith($Path+'\',[StringComparison]::OrdinalIgnoreCase)})
 if(-not $dirs.Count -or -not $files.Count){throw 'Exact nonempty deletion identity graph required'};foreach($entry in $dirs+$files){if(Test-Path -LiteralPath $entry){throw 'Permanent deletion did not remove the exact captured graph'}}
 foreach($entry in $dirs){$directoryIdentities.Remove($entry)};foreach($entry in $files){$allowedFiles.Remove($entry);$fileIdentities.Remove($entry)};Assert-Manifest
 $report.mutation_evidence+=@([pscustomobject]@{step=$script:currentStep;kind='confirmed permanent directory deletion';target=$Path;directory_count=$dirs.Count;file_count=$files.Count;predelete_graph_verified=$true;all_expected_paths_absent=$true});Save-Report
}
function Assert-Browse([bool]$NeedReady=$true){if((Address-Text) -ine $browse -or ($NeedReady -and -not(Ready)) -or -not(Same-Names @(Selected-Names) @($decoyName))){throw 'Navigation mutation changed active directory or borrowed its selected decoy'}}
function Navigate-Browse{
 Focus-Peer (By-Id 'AddressHistoryButton') 'address history';Key 0x4C 1;Wait-Ui 'real address editor' {$null -ne (Editor)};(Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($browse);Focus-Peer (Editor) 'address editor';Key 13
 Wait-Ui 'exact owned browse folder committed' {-not(Editor) -and (Address-Text) -ieq $browse -and (Ready) -and @(Row-Names) -ccontains $decoyName};$peer=@(Rows|Where-Object{$_.Current.Name -ceq $decoyName})[0];$peer.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 Wait-Ui 'decoy is the exact selected file' {(Same-Names @(Selected-Names) @($decoyName))};Wait-Ui 'owned browse node revealed' {$null -ne (Tree-Item @([IO.Path]::GetFileName($browse)))}
 foreach($path in @($browse,$parentTarget)){Wait-Ui 'real owned parent node available' {$null -ne (Tree-Item @([IO.Path]::GetFileName($path)))};$peer=Tree-Item @([IO.Path]::GetFileName($path));$pattern=$peer.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern);if($pattern.Current.ExpandCollapseState -eq [Windows.Automation.ExpandCollapseState]::Collapsed){$pattern.Expand()}}
 Wait-Ui 'real loaded mutation targets visible to UIA' {$null -ne (Tree-Item @([IO.Path]::GetFileName($renameStart))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($existing))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($deleteTarget)))};$script:browseTab=Selected-Tab
}
function Target-Peer([string]$Path){if(-not $directoryIdentities.ContainsKey($Path) -or [BlinkNavigationMutationUiNative]::Identity($Path) -cne $directoryIdentities[$Path]){throw 'Current known GUID directory identity required'};return Tree-Item @([IO.Path]::GetFileName($Path))}
function Target-Menu([string]$Path,[bool]$ShiftF10=$false){Wait-Ui 'exact real tree target available' {$null -ne (Target-Peer $Path)};$menu=Menu-Open (Target-Peer $Path) 'NavigationContextMenu' $ShiftF10;if($menu.Current.HelpText -ine $Path){throw 'Mutation menu captured a different directory'};Assert-Browse;return $menu}
function Open-NewFolder([bool]$Keyboard=$false){
 if($Keyboard){Focus-Peer (Target-Peer $parentTarget) 'actual target parent for Ctrl Shift N';Key 0x4E 3}else{$null=Target-Menu $parentTarget;Act (Owned-Id 'TargetContextNewFolder')};Assert-NameDialog 'NewFolderName' $parentTarget
}
function Open-Rename([string]$Path,[bool]$Keyboard=$false){
 if($Keyboard){Focus-Peer (Target-Peer $Path) 'actual directory for F2';Key 0x71}else{$null=Target-Menu $Path $true;Act (Owned-Id 'TargetContextRename')};Assert-NameDialog 'RenameName' $Path ([IO.Path]::GetFileName($Path))
}
function Wait-Retry([string]$Kind){
 Wait-Ui 'real failed operation exposes enabled Retry name action' {$peer=Owned-Id 'NewFolderRetry';$peer -and $peer.Current.IsEnabled -and (Ready)};Assert-Manifest;Assert-Browse
 $report.retry_evidence+=@([pscustomobject]@{step=$script:currentStep;kind=$Kind;parent_target=$parentTarget;status=(Peer-Evidence (Owned-Id 'FileOperationStatus'));retry=(Peer-Evidence (Owned-Id 'NewFolderRetry'));source_and_existing_graph_unchanged=$true});Save-Report
}
function Verify-RelocatedObserver([string]$Old,[string]$New){
 # Captions may intentionally remain drive letters. The observed ordinal/PID
 # and unchanged tab count identify this observer; its actual committed path
 # after selection is the authoritative directory-relocation assertion.
 if(-not $script:observerTab -or $script:observerTab.process_id -ne $process.Id -or $script:observerTab.observed_count -lt 1 -or $script:observerTab.ordinal -lt 0 -or $script:observerTab.ordinal -ge $script:observerTab.observed_count){throw 'Captured secondary tab ordinal/PID/count is invalid'}
 $oldName=[IO.Path]::GetFileName($Old);$newName=[IO.Path]::GetFileName($New);$oldCaption=$script:observerTab.name;$mapped=$oldCaption.Contains($oldName);$expectedName=$(if($mapped){$oldCaption.Replace($oldName,$newName)}else{$oldCaption});$expectedCount=$script:observerTab.observed_count
 Wait-Ui 'same observed tab ordinal PID count and actual caption remain valid after relocation' {$tabs=@(Tabs);$pattern=$null;$tabs.Count -eq $expectedCount -and $tabs[$script:observerTab.ordinal].Current.ProcessId -eq $process.Id -and $tabs[$script:observerTab.ordinal].Current.Name -ceq $expectedName -and $tabs[$script:observerTab.ordinal].TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)}
 $tabs=@(Tabs);$peer=$tabs[$script:observerTab.ordinal];$script:observerTab=[pscustomobject]@{ordinal=$script:observerTab.ordinal;name=$expectedName;observed_runtime_id=($peer.GetRuntimeId()-join ',');process_id=$peer.Current.ProcessId;observed_count=$expectedCount}
 Select-Tab $script:observerTab $New;Wait-Ui 'same observer really browses exact relocated directory and unchanged payload' {(Address-Text) -ieq $New -and (Ready) -and @(Row-Names) -ccontains $renamePayloadName};Assert-Manifest
 $report.relocation_evidence+=@([pscustomobject]@{step=$script:currentStep;ordinal=$script:observerTab.ordinal;observed_count=$expectedCount;process_id=$script:observerTab.process_id;old_caption=$oldCaption;actual_caption=$expectedName;caption_leaf_mapping_applied=$mapped;unchanged_drive_caption_accepted=-not $mapped;actual_address=(Address-Text);same_payload_visible=$true;complete_manifest_verified=$true});Save-Report;Select-Tab $script:browseTab $browse;Assert-Browse
}
function Open-Delete{
 Assert-Manifest;Focus-Peer (Target-Peer $deleteTarget) 'exact owned nonempty target for Shift Delete';Key 0x2E 2;Wait-Ui 'real permanent-delete confirmation' {$null -ne (Delete-Dialog)};$dialog=Delete-Dialog;$texts=@(Dialog-Texts $dialog)
 if(-not @($texts|Where-Object{$_.Contains([IO.Path]::GetFileName($deleteTarget))}).Count -or @($texts|Where-Object{$_.Contains($decoyName)}).Count){throw 'Delete confirmation does not identify only the owned target'}
 $primary=Find-In $dialog 'PrimaryButton';$cancel=Find-In $dialog 'CloseButton';if(-not $primary -or -not $cancel -or $primary.Current.Name -notin @(Resource-Labels 'Delete.ConfirmPermanent') -or $cancel.Current.Name -notin @(Resource-Labels 'Operation.Cancel')){throw 'Actual localized permanent-delete/Cancel buttons required'}
 $report.delete_evidence+=@([pscustomobject]@{step=$script:currentStep;target=$deleteTarget;dialog_text=$texts;primary=(Peer-Evidence $primary);cancel=(Peer-Evidence $cancel);initial_focus=(Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement));predelete_native_graph_verified=$true});Save-Report;return $dialog
}
function Focus-Decoy{
 $peers=@(Rows|Where-Object{$_.Current.Name -ceq $decoyName});if($peers.Count -ne 1){throw 'Exact selected decoy row required'};$peer=$peers[0];$id=$peer.GetRuntimeId()-join ',';$peer.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$peer.SetFocus()
 Wait-Ui 'actual file-list row focus rather than a navigation node' {$focus=[Windows.Automation.AutomationElement]::FocusedElement;$inside=$false;for($p=$focus;$p;$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)){if($p.Current.ProcessId -ne $process.Id){break};if(($p.GetRuntimeId()-join ',') -ceq $id){$inside=$true;break}};$inside -and (Focus-In 'FolderEntries')}
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
public sealed class BlinkNavigationMutationUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkNavigationMutationUiNative {
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
    public static BlinkNavigationMutationUiDesktop DesktopState() {
        var result=new BlinkNavigationMutationUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary-user GUI required'}
 $desktop=[BlinkNavigationMutationUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady){Skip $desktop.Reason}
 if(-not(Test-Path -LiteralPath $Binary -PathType Leaf)){throw 'Candidate binary missing'};$report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash.ToLowerInvariant()
 $os=Get-CimInstance Win32_OperatingSystem;$report.environment=[ordered]@{caption=$os.Caption;version=$os.Version;build=$os.BuildNumber;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;ordinary_user=$true;theme='NOT CAPTURED';ime='NOT RUN'}
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture already exists'};New-Item -ItemType Directory -Path $fixture -Force|Out-Null;$fixtureCreated=$true
 foreach($path in $initialDirectories|Where-Object{$_ -ine $fixture}){New-Item -ItemType Directory -Path $path -Force|Out-Null};foreach($path in $initialDirectories){$directoryIdentities[$path]=[BlinkNavigationMutationUiNative]::Identity($path)}
 foreach($path in $allowedFiles.Keys){[IO.File]::WriteAllText($path,[string]$allowedFiles[$path],[Text.UTF8Encoding]::new($false));$fileIdentities[$path]=[BlinkNavigationMutationUiNative]::Identity($path)}
 $thisPcLabels=@(Resource-Labels 'Location.ThisPC');if(-not $thisPcLabels.Count){throw 'Existing ThisPC localization required'}
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false;foreach($name in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')){$psi.EnvironmentVariables.Remove($name)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned launch failed'};$ownedHandle=$process.Handle;$report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)};[BlinkNavigationMutationUiNative]::OwnedWindow=$process.MainWindowHandle;$report.window_dpi=[BlinkNavigationMutationUiNative]::GetDpiForWindow($process.MainWindowHandle)
 Wait-Ui 'initial real ThisPC page' {Is-ThisPc}
 Check '01 directory drive-root and virtual menus expose only applicable mutation commands' {
  $root=Tree-Item $thisPcLabels;if(-not $root){throw 'Actual virtual ThisPC tree root required'};$null=Menu-Open $root 'NavigationContextMenu';foreach($id in @('TargetContextNewFolder','TargetContextRename','TargetContextDelete')){if(Owned-Id $id){throw ('Virtual target exposed filesystem mutation '+$id)}};Dismiss-Menu
  $rows=@(Rows|Where-Object{$_.Current.Name -match '(?i)[a-z]:'});if(-not $rows.Count){throw 'Actual local drive row required'};$preferred=[IO.Path]::GetPathRoot($project).Substring(0,2);$matched=@($rows|Where-Object{$_.Current.Name -match [regex]::Escape($preferred)});$peer=$(if($matched.Count){$matched[0]}else{$rows[0]});$peer.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$menu=Menu-Open $peer 'LocationContextMenu'
  if($menu.Current.HelpText -notmatch '^[A-Za-z]:\\$' -or -not(Owned-Id 'TargetContextNewFolder') -or -not(Owned-Id 'TargetContextNewFolder').Current.IsEnabled -or (Owned-Id 'TargetContextRename') -or (Owned-Id 'TargetContextDelete')){throw 'Drive root mutation applicability is incorrect'};Dismiss-Menu
  Navigate-Browse;$null=Target-Menu $parentTarget;foreach($id in @('TargetContextNewFolder','TargetContextRename','TargetContextDelete')){if(-not(Owned-Id $id) -or -not(Owned-Id $id).Current.IsEnabled){throw ('Ordinary directory action unavailable '+$id)}};Dismiss-Menu;Assert-Manifest;Assert-Browse
 }
 Check '02 navigation NewFolder Escape cancels its draft and restores navigation focus' {
  Open-NewFolder;Set-Name 'NewFolderName' $newCancelName;Cancel-Name 'NewFolderName';if((Test-Path -LiteralPath (Join-Path $parentTarget $newCancelName)) -or (Test-Path -LiteralPath (Join-Path $browse $newCancelName))){throw 'Cancelled NewFolder created a directory'};Assert-Manifest;Assert-Browse
 }
 Check '03 scoped navigation Ctrl Shift N creates in the clicked parent and refreshes its loaded tree automatically' {
  Open-NewFolder $true;Set-Name 'NewFolderName' ([IO.Path]::GetFileName($created));Submit-Name 'NewFolderName';Wait-Ui 'actual target directory created and appears without F5' {(Test-Path -LiteralPath $created -PathType Container) -and (Tree-Item @([IO.Path]::GetFileName($created))) -and (Ready)}
  Register-Created $created;Assert-Browse;if(Test-Path -LiteralPath (Join-Path $browse ([IO.Path]::GetFileName($created)))){throw 'NewFolder borrowed the active browse directory'}
 }
 Check '04 duplicate NewFolder fails without overwriting and Retry preserves its captured target parent' {
  Open-NewFolder;Set-Name 'NewFolderName' ([IO.Path]::GetFileName($existing));Submit-Name 'NewFolderName';Wait-Retry 'NewFolder conflict';Act (Owned-Id 'NewFolderRetry');Assert-NameDialog 'NewFolderName' $parentTarget
  Set-Name 'NewFolderName' ([IO.Path]::GetFileName($retryCreated));Submit-Name 'NewFolderName';Wait-Ui 'NewFolder Retry creates only in the original target and refreshes tree' {(Test-Path -LiteralPath $retryCreated -PathType Container) -and (Tree-Item @([IO.Path]::GetFileName($retryCreated))) -and (Ready)};Register-Created $retryCreated;Assert-Browse
 }
 Check '05 navigation F2 Escape preserves directory and child identity bytes and owner focus' {
  Open-Rename $renameStart $true;Set-Name 'RenameName' $renameCancelName;Cancel-Name 'RenameName';Assert-Manifest;Assert-Browse;if(Test-Path -LiteralPath (Join-Path $parentTarget $renameCancelName)){throw 'Cancelled rename changed its source'}
 }
 Check '06 target Rename preserves the full identity graph and relocates another tab browsing that directory' {
  $count=@(Tabs).Count;$null=Target-Menu $renameStart;Act (Owned-Id 'TargetContextNewTab');Wait-Ui 'observer tab really browses the target before rename' {@(Tabs).Count -eq $count+1 -and (Address-Text) -ieq $renameStart -and (Ready) -and @(Row-Names) -ccontains $renamePayloadName};$script:observerTab=Selected-Tab;Select-Tab $script:browseTab $browse;Assert-Browse
  Open-Rename $renameStart;Set-Name 'RenameName' ([IO.Path]::GetFileName($renameAfter));Submit-Name 'RenameName';Wait-Ui 'rename commits and loaded tree replaces its old label automatically' {-not(Test-Path -LiteralPath $renameStart) -and (Test-Path -LiteralPath $renameAfter -PathType Container) -and -not(Tree-Item @([IO.Path]::GetFileName($renameStart))) -and (Tree-Item @([IO.Path]::GetFileName($renameAfter))) -and (Ready)}
  Register-Rename $renameStart $renameAfter;Assert-Browse;Verify-RelocatedObserver $renameStart $renameAfter
 }
 Check '07 duplicate Rename fails without overwriting and Retry renames the same captured directory' {
  Open-Rename $renameAfter $true;Set-Name 'RenameName' ([IO.Path]::GetFileName($existing));Submit-Name 'RenameName';Wait-Retry 'Rename conflict';Act (Owned-Id 'NewFolderRetry');Assert-NameDialog 'RenameName' $renameAfter
  Set-Name 'RenameName' ([IO.Path]::GetFileName($renameFinal));Submit-Name 'RenameName';Wait-Ui 'rename Retry preserves target and refreshes exact tree names' {-not(Test-Path -LiteralPath $renameAfter) -and (Test-Path -LiteralPath $renameFinal -PathType Container) -and -not(Tree-Item @([IO.Path]::GetFileName($renameAfter))) -and (Tree-Item @([IO.Path]::GetFileName($renameFinal))) -and (Ready)}
  Register-Rename $renameAfter $renameFinal;Assert-Browse;Verify-RelocatedObserver $renameAfter $renameFinal
 }
 Check '08 scoped Shift Delete defaults to Cancel and Enter preserves the nonempty target' {
  $dialog=Open-Delete;$cancel=Find-In $dialog 'CloseButton';$focus=[Windows.Automation.AutomationElement]::FocusedElement;if(-not $focus -or -not $focus.Current.HasKeyboardFocus -or -not(Is-OwnDescendant $focus $cancel)){throw 'Permanent delete did not initially focus the actual default Cancel button'}
  Key 13;Wait-Ui 'default Cancel closes confirmation without deleting' {-not(Delete-Dialog) -and (Test-Path -LiteralPath $deleteTarget -PathType Container) -and (Ready)};Assert-Manifest;Assert-Browse
 }
 Check '09 confirmed Shift Delete removes only the identity-verified owned nonempty directory and refreshes tree' {
  $dialog=Open-Delete;Assert-Manifest;Act (Find-In $dialog 'PrimaryButton');Wait-Ui 'confirmed permanent deletion removes the captured graph and tree row' {-not(Delete-Dialog) -and -not(Test-Path -LiteralPath $deleteTarget) -and -not(Tree-Item @([IO.Path]::GetFileName($deleteTarget))) -and (Ready)}
  Register-Deletion $deleteTarget;Assert-Browse
 }
 Check '10 a retired target mutation menu cannot open a dialog or change another active tab' {
  $null=Target-Menu $parentTarget;$oldAction=Owned-Id 'TargetContextNewFolder';$count=@(Tabs).Count;Act (By-Id 'AddButton');Wait-Ui 'new actual ThisPC tab after menu retirement' {@(Tabs).Count -eq $count+1 -and (Is-ThisPc)};$unavailable=$false
  try{$pattern=$null;if(-not $oldAction.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$unavailable=$true}else{$pattern.Invoke()}}catch [Windows.Automation.ElementNotAvailableException]{$unavailable=$true}catch [InvalidOperationException]{$unavailable=$true}
  $quiet=[Diagnostics.Stopwatch]::StartNew();$violation=@{observed=$false};Wait-Ui 'stale target mutation leaves virtual page and filesystem unchanged' {if((Owned-Id 'NewFolderName') -or (Owned-Id 'RenameName') -or (Delete-Dialog) -or -not(Is-ThisPc)){$violation.observed=$true;throw 'Retired mutation affected the new active tab'};try{Assert-Manifest}catch{$violation.observed=$true;throw};$quiet.ElapsedMilliseconds -ge 500};if($violation.observed){throw 'A retired mutation affected a dialog page or filesystem during observation'}
  $report.stale_peer=[pscustomobject]@{target=$parentTarget;peer_unavailable=$unavailable;new_tab_stays_virtual=$true;no_dialog_or_filesystem_change=$true};Save-Report;Select-Tab $script:browseTab $browse;Assert-Browse
 }
 Check '11 file-list F2 remains scoped to the decoy file rather than the last navigation target' {
  Focus-Decoy;Key 0x71;Assert-NameDialog 'RenameName' (Join-Path $browse $decoyName) $decoyName;Set-Name 'RenameName' ('discard-'+$token+'.txt');Cancel-Name 'RenameName' 'FolderEntries';Assert-Manifest;Assert-Browse
 }
 Check '12 retain an unsubmitted navigation Rename draft for normal-close lifetime validation' {
  Open-Rename $renameFinal $true;Set-Name 'RenameName' $unsubmittedName;Assert-Manifest;if(Test-Path -LiteralPath (Join-Path $parentTarget $unsubmittedName)){throw 'Unsubmitted navigation rename changed the source'}
  $report.draft_close=[pscustomobject]@{target=$renameFinal;editor=(Peer-Evidence (Owned-Id 'RenameName'));draft=(Value (Owned-Id 'RenameName'));source_graph_unchanged=$true;normal_close_without_submit=$true};Save-Report
 }
 $notes.Add('Naming, directory relocation and permanent deletion use actual captured navigation targets and native modal routes. Only the one identity/byte-checked GUID deletion target is confirmed; ordinary Recycle deletion and user recycle-bin enumeration are never invoked. No F5 is sent to satisfy mutation tree-refresh checks.')
 $report.status='PASS'
}catch{
 if($_.Exception.Message.StartsWith('NAV_MUTATION_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring('NAV_MUTATION_UI_SKIP: '.Length)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{if($process){Capture-State 'unexpected failure'}}catch{}}
}finally{
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if($report.before_close_alive){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');Save-Report;$report.close_request_result=$process.CloseMainWindow();Save-Report;if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned Blink still running PID '+$process.Id+'; no force termination')}}
  $process.Refresh();$report.process_exited=$process.HasExited;if($process.HasExited){$report.exit_code=$process.ExitCode;$report.exit_code_hex='0x{0:X8}' -f ([long]$report.exit_code -band 4294967295);$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o')}
  if($report.exit_code -ne 0){throw ('Owned Blink abnormal exit '+$report.exit_code_hex)};if($report.status -eq 'PASS'){Record '13 owned navigation-mutation window closes normally and exits zero'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '13 normal owned close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process remains active; retain fixture'};$report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
  $report.fixture_bytes_removed=[long]((Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture);if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '14 exact GUID mutation graph identities bytes and attributes verified before cleanup'}
 }catch{$report.status='FAIL';$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '14 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=[long]((Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 if($report.status -eq 'PASS' -and $checks.Count -ne $expectedChecks){$report.status='FAIL';$report.failure='Executed check count does not match the selected feature contract'}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -eq 'SKIP';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI NAVIGATION MUTATION '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -ne 'PASS'){exit 2};exit 0
