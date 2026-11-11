[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Binary='',
 [ValidateRange(5,90)][int]$StepTimeoutSeconds=30,
 [switch]$ClipboardPreflightOnly
)
# G06b: new PID/HWND, real navigation menus and GUID-owned transfer data.
# Original Clipboard content stays only in memory; unsupported originals SKIP
# before launch/write. Restore only an unchanged test-owned sequence+HWND/PID.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$integration=Join-Path $project '.build\explorer-navigation-transfer\integration'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration $(if($ClipboardPreflightOnly){'clipboard-preflight-report.json'}else{'ui-navigation-transfer-report.json'})
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $integration ('ui-navigation-transfer-fixtures\owned-'+$guid)
$browse=Join-Path $fixture ('browse-'+$token);$decoyName='decoy-'+$token+'.txt'
$copySource=Join-Path $browse ('00-copy-'+$token);$copyTarget=Join-Path $browse ('01-copy-target-'+$token)
$moveSource=Join-Path $browse ('02-move-'+$token);$moveTarget=Join-Path $browse ('03-move-target-'+$token)
$conflictSource=Join-Path $browse ('04-conflict-'+$token);$conflictTarget=Join-Path $browse ('05-conflict-target-'+$token)
$copyNested=Join-Path $copySource '嵌套-é';$moveNested=Join-Path $moveSource 'nested'
$copyResult=Join-Path $copyTarget ([IO.Path]::GetFileName($copySource));$moveResult=Join-Path $moveTarget ([IO.Path]::GetFileName($moveSource));$conflictResult=Join-Path $conflictTarget ([IO.Path]::GetFileName($conflictSource))
$initialDirectories=@($fixture,$browse,$copySource,$copyNested,$copyTarget,$moveSource,$moveNested,$moveTarget,$conflictSource,$conflictTarget,$conflictResult)
$allowedFiles=@{};$fileIdentities=@{};$directoryIdentities=@{}
$allowedFiles[(Join-Path $browse $decoyName)]='Selected decoy must not be copied '+$guid
$allowedFiles[(Join-Path $copySource 'root.txt')]='Copy root '+$guid
$allowedFiles[(Join-Path $copyNested 'payload-一.txt')]='Nested Unicode copy '+$guid
$allowedFiles[(Join-Path $moveSource 'root.txt')]='Move root '+$guid
$allowedFiles[(Join-Path $moveNested 'payload.txt')]='Nested move '+$guid
$allowedFiles[(Join-Path $conflictSource 'same.txt')]='Conflict source remains '+$guid
$allowedFiles[(Join-Path $conflictResult 'same.txt')]='Existing target remains '+$guid
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP
$process=$null;$fixtureCreated=$false;$tempChanged=$false;$script:windowCache=$null;$script:currentStep='preflight'
$originalClipboard=$null;$script:lastOwnedSequence=[uint32]0;$script:lastOwnedOwner=[long]0;$script:lastOwnedPath='';$script:lastOwnedEffect=[uint32]0
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new()
$report=[ordered]@{
 status='RUNNING';passed=$false;skipped=$false;preflight_only=[bool]$ClipboardPreflightOnly;configuration=$Configuration;binary=$Binary;binary_sha256=$null
 source_script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
 started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;current_step='preflight';failure=$null;checks=@();notes=@()
 process_id=$null;process_exited=$false;exit_code=$null;exit_code_hex=$null;forced_termination=$false;before_close_alive=$null;close_requested=$false;close_request_utc=$null;close_request_result=$null;process_exit_observed_utc=$null
 environment=$null;window_dpi=$null;desktop_preflight=$null;focus_evidence=@();keyboard_evidence=@();menu_evidence=@();tab_evidence=@();last_ui_snapshot=$null;last_focus_attempt=$null
 clipboard_original_metadata=$null;clipboard_write_requested=$false;clipboard_evidence=@();clipboard_restoration=$null;copy_result=$null;move_result=$null;conflict_cancel=$null;stale_peer=$null
 fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_ownership_failures=@();fixture_bytes_removed=0
 compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0
 human_acceptance='NOT RUN';model_test_results='NOT RUN by this UI smoke'
 input_mode='Owned PID/HWND UIA and InputSite keyboard messages; actual Apps/Shift+F10; exact GUID CF_HDROP only'
 not_run=@('physical right-click hit testing','unrestorable original Clipboard/OLE/GDI formats','external Explorer performed-effect and cut rendering','Search/busy/clipboard-preparation GUI combinations','cross-volume moves','real IME and network/access-denied targets','full theme/DPI/multi-monitor/language/Narrator matrix')
}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($true))}
function Record([string]$Name,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Name;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('NAV_TRANSFER_UI_SKIP: '+$Why)}
function Check([string]$Name,[scriptblock]$Body){$script:currentStep=$Name;Save-Report;try{$null=&$Body;Record $Name}catch{Record $Name 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace);throw}}
function Window{$process.Refresh();if($process.HasExited){throw ('Owned Blink exited '+$process.ExitCode)};if(-not $script:windowCache -and $process.MainWindowHandle -ne [IntPtr]::Zero){$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)};if($script:windowCache -and $script:windowCache.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA PID mismatch'};return $script:windowCache}
function Visible($Peer){return $null -ne $Peer -and -not $Peer.Current.IsOffscreen -and $Peer.Current.BoundingRectangle.Width -gt 0 -and $Peer.Current.BoundingRectangle.Height -gt 0}
function Find-In($Root,[string]$Id){if($Root){foreach($peer in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)))){if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer)){return $peer}}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){$found=By-Id $Id;if($found){return $found};foreach($h in [BlinkNavigationTransferUiNative]::OwnedTopWindows($process.Id)){try{$root=[Windows.Automation.AutomationElement]::FromHandle($h);if($root.Current.ProcessId -ne $process.Id){throw 'Owned root PID mismatch'};if($root.Current.AutomationId -ceq $Id -and (Visible $root)){return $root};$found=Find-In $root $Id;if($found){return $found}}catch{if([BlinkNavigationTransferUiNative]::WindowExists($h)){throw}}};return $null}
function Peer-Evidence($Peer){if(-not $Peer){return [pscustomobject]@{found=$false}};$c=$Peer.Current;return [pscustomobject]@{found=$true;id=$c.AutomationId;name=$c.Name;help_text=$c.HelpText;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;pid=$c.ProcessId;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;has_keyboard_focus=$c.HasKeyboardFocus;runtime_id=($Peer.GetRuntimeId()-join ',')}}
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
function Capture-State([string]$Why){
 $state=[ordered]@{why=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;rows=@();selected=@();peers=@();navigation_tree=@();tabs=@();focus=$null;desktop=$null;errors=@()}
 try{$state.address=Address-Text;$state.rows=@(Row-Names);$state.selected=@(Selected-Names)}catch{$state.errors+=@($_.Exception.Message)}
 foreach($id in @('NavigationPane','NavigationTree','NavigationQuickList','NavigationContextMenu','LocationContextMenu','TargetContextOpen','TargetContextNewTab','TargetContextExpandCollapse','TargetContextCopyPath','TargetContextProperties','TargetContextCopy','TargetContextCut','TargetContextPaste','FolderEntries','WindowTabs')){try{$peer=Owned-Id $id;if($peer){$state.peers+=@(Peer-Evidence $peer)}}catch{$state.errors+=@($id+': '+$_.Exception.Message)}}
 try{
  $pane=By-Id 'NavigationPane';if($pane){$items=$pane.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::TreeItem));for($i=0;$i -lt [Math]::Min(40,$items.Count);$i++){try{$peer=$items[$i];if($peer.Current.ProcessId -ne $process.Id){continue};$pattern=$null;$expandState='NO PATTERN';if($peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$expandState=$pattern.Current.ExpandCollapseState.ToString()};$state.navigation_tree+=@([pscustomobject]@{ordinal=$i;name=$peer.Current.Name;is_offscreen=$peer.Current.IsOffscreen;expand_collapse=$expandState})}catch{$state.errors+=@('TreeItem '+$i+': '+$_.Exception.Message)}}}
 }catch{$state.errors+=@('Navigation tree snapshot: '+$_.Exception.Message)}
 try{
  $items=@(Tabs);for($i=0;$i -lt [Math]::Min(40,$items.Count);$i++){try{$peer=$items[$i];if($peer.Current.ProcessId -ne $process.Id){continue};$pattern=$null;$isSelected=$null;$hasSelection=$peer.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern);if($hasSelection){$isSelected=$pattern.Current.IsSelected};$state.tabs+=@([pscustomobject]@{ordinal=$i;name=$peer.Current.Name;is_offscreen=$peer.Current.IsOffscreen;selection_pattern=$hasSelection;is_selected=$isSelected})}catch{$state.errors+=@('Tab '+$i+': '+$_.Exception.Message)}}
 }catch{$state.errors+=@('Tab snapshot: '+$_.Exception.Message)}
 try{$state.focus=Peer-Evidence ([Windows.Automation.AutomationElement]::FocusedElement);$state.desktop=[BlinkNavigationTransferUiNative]::DesktopState()}catch{$state.errors+=@($_.Exception.Message)}
 $report.last_ui_snapshot=[pscustomobject]$state;Save-Report
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('NAV_TRANSFER_UI_SKIP: ') -or $_.Exception.Message.StartsWith('NAV_TRANSFER_TEST_PRECONDITION_NOT_MET: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);Capture-State $Why;throw ('Timed out: '+$Why+'; '+$last)}
function Require-Owned-Desktop([string]$Why){$desktop=[BlinkNavigationTransferUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady -or $desktop.ForegroundPid -ne $process.Id){throw ('NAV_TRANSFER_TEST_PRECONDITION_NOT_MET: '+$Why+'; expected PID '+$process.Id+', actual foreground '+$desktop.ForegroundPid+'; '+$desktop.Reason)};return $desktop}
function Key([uint32]$Code,[int]$Modifiers=0){$desktop=Require-Owned-Desktop 'before scoped key';[BlinkNavigationTransferUiNative]::Key($process.Id,$Code,$Modifiers);$report.keyboard_evidence+=@([pscustomobject]@{step=$script:currentStep;key=$Code;modifiers=$Modifiers;foreground_pid=$desktop.ForegroundPid});Save-Report}
function Focus-Peer($Peer,[string]$Why){if(-not $Peer -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw ('Owned enabled focus target required '+$Why)};if(-not(Visible $Peer)){$scroll=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.ScrollItemPattern]::Pattern,[ref]$scroll)){$scroll.ScrollIntoView()}};$runtime=$Peer.GetRuntimeId()-join ',';$Peer.SetFocus();Wait-Ui ('actual owned focus '+$Why) {$focused=[Windows.Automation.AutomationElement]::FocusedElement;$desktop=[BlinkNavigationTransferUiNative]::DesktopState();$report.last_focus_attempt=[pscustomobject]@{target=$Why;expected=(Peer-Evidence $Peer);actual=(Peer-Evidence $focused);desktop=$desktop};return $desktop.InputReady -and $desktop.ForegroundPid -eq $process.Id -and (Visible $Peer) -and $focused -and $focused.Current.ProcessId -eq $process.Id -and $focused.Current.HasKeyboardFocus -and ($focused.GetRuntimeId()-join ',') -ceq $runtime};$report.focus_evidence+=@([pscustomobject]@{step=$script:currentStep;target=$Why;actual=(Peer-Evidence $Peer)});Save-Report}
function Act($Peer){if(-not(Visible $Peer) -or $Peer.Current.ProcessId -ne $process.Id -or -not $Peer.Current.IsEnabled){throw 'Owned visible enabled action required'};$null=Require-Owned-Desktop 'before real UIA action';$pattern=$null;if($Peer.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}elseif($Peer.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)){$pattern.Expand()}else{throw ('No real Invoke/Expand pattern '+$Peer.Current.AutomationId)}}
function Select-Tab($Token,[string]$Path){
 # This run only adds tabs; it never closes or reorders them. The captured
 # ordinal remains stable, and its observed name must still match.
 $tabs=@(Tabs);if($null -eq $Token -or $Token.ordinal -lt 0 -or $Token.ordinal -ge $tabs.Count){throw 'Captured tab ordinal is unavailable'};$peer=$tabs[$Token.ordinal];$pattern=$null
 if($peer.Current.ProcessId -ne $process.Id -or $peer.Current.Name -cne $Token.name -or -not $peer.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)){throw 'Recreated owned tab does not match its captured ordinal/name or selection pattern'}
 $pattern.Select();Wait-Ui 'exact previous tab restored' {$current=@(Tabs);$selected=$null;$current.Count -gt $Token.ordinal -and $current[$Token.ordinal].Current.Name -ceq $Token.name -and $current[$Token.ordinal].TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$selected) -and $selected.Current.IsSelected -and -not(Editor) -and $(if($Path -in $thisPcLabels){Is-ThisPc}else{(Address-Text) -ieq $Path -and (Ready)})}
 $currentTabs=@(Tabs);$restored=$currentTabs[$Token.ordinal];$report.tab_evidence+=@([pscustomobject]@{step=$script:currentStep;operation='requery rebuilt tab by observed ordinal/name';ordinal=$Token.ordinal;name=$Token.name;previous_runtime_id=$Token.observed_runtime_id;current=(Peer-Evidence $restored);actual_address=(Address-Text)});Save-Report
}
function Menu-Open($Peer,[string]$MenuId,[bool]$ShiftF10=$false){Focus-Peer $Peer 'context target';if($ShiftF10){Key 0x79 2}else{Key 0x5D};Wait-Ui ('real context menu '+$MenuId) {$null -ne (Owned-Id $MenuId) -and $null -ne (Owned-Id 'TargetContextOpen')};$menu=Owned-Id $MenuId;$report.menu_evidence+=@([pscustomobject]@{step=$script:currentStep;method=$(if($ShiftF10){'owned Shift+F10'}else{'owned Apps key'});source=(Peer-Evidence $Peer);menu=(Peer-Evidence $menu);items=@('TargetContextOpen','TargetContextNewTab','TargetContextExpandCollapse','TargetContextCopyPath','TargetContextProperties','TargetContextCopy','TargetContextCut','TargetContextPaste'|ForEach-Object{Peer-Evidence (Owned-Id $_)})});Save-Report;return $menu}
function Dismiss-Menu{Key 27;Wait-Ui 'context menu dismissed' {-not(Owned-Id 'NavigationContextMenu') -and -not(Owned-Id 'LocationContextMenu')}}
function Resource-Labels([string]$Key){$labels=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq $Key){$labels+=@([string]$data.value)}}};return @($labels|Select-Object -Unique)}

function Verify-File([string]$Path,[string]$Expected,[string]$Identity=''){
 if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw ('Expected owned file missing: '+$Path)};$item=Get-Item -LiteralPath $Path -Force;if($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly)){throw 'Owned payload attributes changed'}
 $expectedBytes=[Text.UTF8Encoding]::new($false).GetBytes($Expected);if([Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) -cne [Convert]::ToBase64String($expectedBytes)){throw ('Owned payload bytes differ: '+$Path)}
 $actual=[BlinkNavigationTransferUiNative]::Identity($Path);if($Identity -and $actual -cne $Identity){throw ('Owned payload identity changed: '+$Path)};return $actual
}
function Assert-Manifest{
 foreach($path in $directoryIdentities.Keys){$item=Get-Item -LiteralPath $path -Force;if(-not $item.PSIsContainer -or ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly)) -or [BlinkNavigationTransferUiNative]::Identity($path) -cne $directoryIdentities[$path]){throw ('Owned directory identity or attributes changed: '+$path)}}
 foreach($path in $allowedFiles.Keys){$null=Verify-File $path ([string]$allowedFiles[$path]) ([string]$fileIdentities[$path])}
 $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($fixture);while($pending.Count){foreach($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Unexpected reparse point inside owned fixture'};if($entry.PSIsContainer){if(-not $directoryIdentities.ContainsKey($entry.FullName)){throw ('Unknown directory retained: '+$entry.FullName)};$pending.Push($entry.FullName)}elseif(-not $allowedFiles.ContainsKey($entry.FullName)){throw ('Unknown file retained: '+$entry.FullName)}}}
}
function Ownership-Failures{try{Assert-Manifest;return @()}catch{return @($_.Exception.Message)}}
function Register-Transfer([string]$From,[string]$To,[bool]$Moved){
 if(-not $From.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $To.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Transfer result outside exact GUID fixture'}
 $dirs=@($directoryIdentities.Keys|Where-Object{$_ -ieq $From -or $_.StartsWith($From+'\',[StringComparison]::OrdinalIgnoreCase)});$files=@($allowedFiles.Keys|Where-Object{$_.StartsWith($From+'\',[StringComparison]::OrdinalIgnoreCase)});$newDirs=@{};$newFiles=@{};$newIds=@{}
 foreach($path in $dirs){$target=$To+$path.Substring($From.Length);$item=Get-Item -LiteralPath $target -Force;if(-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Expected result directory unavailable'};$identity=[BlinkNavigationTransferUiNative]::Identity($target);if(($Moved -and $identity -cne $directoryIdentities[$path]) -or (-not $Moved -and $identity -ceq $directoryIdentities[$path])){throw 'Transfer result directory identity does not match copy/move semantics'};$newDirs[$target]=$identity}
 foreach($path in $files){$target=$To+$path.Substring($From.Length);$identity=Verify-File $target ([string]$allowedFiles[$path]) $(if($Moved){[string]$fileIdentities[$path]}else{''});if(-not $Moved -and $identity -ceq $fileIdentities[$path]){throw 'Directory copy reused source file identity'};$newFiles[$target]=$allowedFiles[$path];$newIds[$target]=$identity}
 if($Moved){if(Test-Path -LiteralPath $From){throw 'Moved directory source still exists'};foreach($path in $dirs){$directoryIdentities.Remove($path)};foreach($path in $files){$allowedFiles.Remove($path);$fileIdentities.Remove($path)}}
 foreach($path in $newDirs.Keys){$directoryIdentities[$path]=$newDirs[$path]};foreach($path in $newFiles.Keys){$allowedFiles[$path]=$newFiles[$path];$fileIdentities[$path]=$newIds[$path]};Assert-Manifest
}
function Assert-Browse-Preserved{if((Address-Text) -ine $browse -or -not(Ready) -or -not(Same-Names @(Selected-Names) @($decoyName))){throw 'Navigation transfer changed the current address or borrowed its file selection'}}
function Navigate-Browse{
 Focus-Peer (By-Id 'AddressHistoryButton') 'address history';Key 0x4C 1;Wait-Ui 'address editor visible' {$null -ne (Editor)};(Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($browse);Focus-Peer (Editor) 'address editor';Key 13
 Wait-Ui 'exact owned browse directory committed' {-not(Editor) -and (Address-Text) -ieq $browse -and (Ready) -and @(Row-Names) -ccontains $decoyName};$peer=@(Rows|Where-Object{$_.Current.Name -ceq $decoyName})[0];$peer.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 Wait-Ui 'owned decoy selected' {(Same-Names @(Selected-Names) @($decoyName))};Wait-Ui 'browse node revealed in real tree' {$null -ne (Tree-Item @([IO.Path]::GetFileName($browse)))};$peer=Tree-Item @([IO.Path]::GetFileName($browse));$pattern=$peer.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern);if($pattern.Current.ExpandCollapseState -eq [Windows.Automation.ExpandCollapseState]::Collapsed){$pattern.Expand()}
 Wait-Ui 'all real owned transfer targets materialized' {$null -ne (Tree-Item @([IO.Path]::GetFileName($copySource))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($copyTarget))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($moveSource))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($moveTarget))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($conflictSource))) -and $null -ne (Tree-Item @([IO.Path]::GetFileName($conflictTarget)))}
}
function Target-Menu([string]$Path,[bool]$ShiftF10=$false){
 if(-not $directoryIdentities.ContainsKey($Path)){throw 'Unknown target path'};Wait-Ui 'current exact directory tree peer' {$null -ne (Tree-Item @([IO.Path]::GetFileName($Path)))};$menu=Menu-Open (Tree-Item @([IO.Path]::GetFileName($Path))) 'NavigationContextMenu' $ShiftF10
 if($menu.Current.HelpText -ine $Path){throw 'Navigation menu captured another directory'};return $menu
}
function Assert-Clipboard-BeforeWrite{
 $null=Require-Owned-Desktop 'before requested clipboard write';$sequence=$(if($script:lastOwnedSequence){$script:lastOwnedSequence}else{$originalClipboard.Sequence});$owner=$(if($script:lastOwnedSequence){$script:lastOwnedOwner}else{$originalClipboard.Owner})
 if(-not [BlinkTransferClipboard]::Matches($sequence,$owner)){throw 'NAV_TRANSFER_TEST_PRECONDITION_NOT_MET: Clipboard sequence/owner changed before requested write'}
}
function Capture-OwnedClipboard([string]$Path,[uint32]$Effect,[uint32]$Before){
 if(-not $directoryIdentities.ContainsKey($Path)){throw 'Clipboard source must be an exact known GUID directory'};$result=@{value=$null}
 Wait-Ui 'fresh exact test-owned CF_HDROP directory payload' {$value=[BlinkTransferClipboard]::ReadDrop();if($value.Sequence -eq $Before){return $false};if($value.OwnerPid -ne $process.Id -or $value.Owner -eq 0 -or -not $value.HasDrop -or -not $value.HasEffect -or $value.Effect -ne $Effect -or @($value.Paths).Count -ne 1 -or -not [string]::Equals($value.Paths[0],$Path,[StringComparison]::OrdinalIgnoreCase)){throw 'NAV_TRANSFER_TEST_PRECONDITION_NOT_MET: Fresh clipboard payload/effect/owner is not the exact test directory'};$result.value=$value;return $true}
 $value=$result.value;$script:lastOwnedSequence=$value.Sequence;$script:lastOwnedOwner=$value.Owner;$script:lastOwnedPath=$Path;$script:lastOwnedEffect=$Effect
 $report.clipboard_evidence+=@([pscustomobject]@{step=$script:currentStep;sequence=$value.Sequence;owner=$value.Owner;owner_pid=$value.OwnerPid;paths=@($value.Paths);preferred_effect=$value.Effect;format='CF_HDROP + Preferred DropEffect'});Save-Report
 Wait-Ui 'clipboard preparation returns existing command pipeline to idle' {Ready}
}
function Assert-OwnedClipboard{
 $null=Require-Owned-Desktop 'before exact clipboard guard';if(-not $script:lastOwnedSequence){throw 'No test-owned clipboard sequence recorded'};$value=[BlinkTransferClipboard]::ReadDrop()
 if($value.Sequence -ne $script:lastOwnedSequence -or $value.Owner -ne $script:lastOwnedOwner -or $value.OwnerPid -ne $process.Id -or -not $value.HasDrop -or -not $value.HasEffect -or $value.Effect -ne $script:lastOwnedEffect -or @($value.Paths).Count -ne 1 -or -not [string]::Equals($value.Paths[0],$script:lastOwnedPath,[StringComparison]::OrdinalIgnoreCase)){throw 'NAV_TRANSFER_TEST_PRECONDITION_NOT_MET: Clipboard sequence/owner/exact GUID payload/effect changed; no paste attempted'}
}
function Copy-Or-Cut([string]$Path,[bool]$Cut){
 $null=Target-Menu $Path $Cut;Assert-Browse-Preserved;$id=$(if($Cut){'TargetContextCut'}else{'TargetContextCopy'});$peer=Owned-Id $id;if(-not(Visible $peer) -or -not $peer.Current.IsEnabled){throw ('Actual target clipboard action disabled '+$id)}
 Assert-Clipboard-BeforeWrite;$before=[BlinkTransferClipboard]::GetClipboardSequenceNumber();$report.clipboard_write_requested=$true;Save-Report;Act $peer;Capture-OwnedClipboard $Path $(if($Cut){2}else{1}) $before;Assert-Browse-Preserved
}
function Paste-Target([string]$Path){
 if(-not $Path.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $directoryIdentities.ContainsKey($Path) -or [BlinkNavigationTransferUiNative]::Identity($Path) -cne $directoryIdentities[$Path]){throw 'Paste target must be an unchanged exact GUID directory'}
 Assert-OwnedClipboard;$null=Target-Menu $Path $true;Assert-Browse-Preserved;$peer=Owned-Id 'TargetContextPaste';if(-not(Visible $peer) -or -not $peer.Current.IsEnabled){throw 'Actual navigation target Paste is disabled'}
 Assert-OwnedClipboard;Act $peer
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
public sealed class BlinkNavigationTransferUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkNavigationTransferUiNative {
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
    public static BlinkNavigationTransferUiDesktop DesktopState() {
        var result=new BlinkNavigationTransferUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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

public sealed class BlinkTransferClipboardFormatInfo { public uint Id; public string Name; public int Bytes; }
internal sealed class BlinkTransferClipboardFormat { internal uint Id; internal byte[] Bytes; }
public sealed class BlinkTransferClipboardBackup {
 public bool Restorable; public string Reason; public uint Sequence; public long Owner; public uint OwnerPid; public int TotalBytes;
 public BlinkTransferClipboardFormatInfo[] Formats;
 internal System.Collections.Generic.List<BlinkTransferClipboardFormat> Data=new System.Collections.Generic.List<BlinkTransferClipboardFormat>();
}
public sealed class BlinkTransferClipboardDrop { public uint Sequence,OwnerPid,Effect; public long Owner; public bool HasDrop,HasEffect; public string[] Paths=new string[0]; }
public sealed class BlinkTransferClipboardRestore { public string Status,Reason; public bool Restored; public uint ResultSequence; }
public static class BlinkTransferClipboard {
 const int MaxFormatBytes=1048576,MaxTotalBytes=4194304,MaxFormats=16;
 [System.Runtime.InteropServices.DllImport("user32.dll",SetLastError=true)]static extern bool OpenClipboard(System.IntPtr owner);
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern bool CloseClipboard();
 [System.Runtime.InteropServices.DllImport("user32.dll",SetLastError=true)]static extern bool EmptyClipboard();
 [System.Runtime.InteropServices.DllImport("user32.dll")]public static extern uint GetClipboardSequenceNumber();
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern System.IntPtr GetClipboardOwner();
 [System.Runtime.InteropServices.DllImport("user32.dll",SetLastError=true)]static extern uint EnumClipboardFormats(uint previous);
 [System.Runtime.InteropServices.DllImport("user32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode)]static extern int GetClipboardFormatNameW(uint format,System.Text.StringBuilder name,int count);
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern System.IntPtr GetClipboardData(uint format);
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern System.IntPtr SetClipboardData(uint format,System.IntPtr memory);
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern bool IsClipboardFormatAvailable(uint format);
 [System.Runtime.InteropServices.DllImport("user32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode)]static extern uint RegisterClipboardFormatW(string name);
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(System.IntPtr window,out uint pid);
 [System.Runtime.InteropServices.DllImport("kernel32.dll")]static extern System.UIntPtr GlobalSize(System.IntPtr memory);
 [System.Runtime.InteropServices.DllImport("kernel32.dll")]static extern System.IntPtr GlobalLock(System.IntPtr memory);
 [System.Runtime.InteropServices.DllImport("kernel32.dll")]static extern bool GlobalUnlock(System.IntPtr memory);
 [System.Runtime.InteropServices.DllImport("kernel32.dll")]static extern System.IntPtr GlobalAlloc(uint flags,System.UIntPtr size);
 [System.Runtime.InteropServices.DllImport("kernel32.dll")]static extern System.IntPtr GlobalFree(System.IntPtr memory);
 [System.Runtime.InteropServices.DllImport("shell32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode)]static extern uint DragQueryFileW(System.IntPtr drop,uint index,System.Text.StringBuilder name,uint count);
 [System.Runtime.InteropServices.DllImport("kernel32.dll")]static extern void SetLastError(uint error);
 static uint NextFormat(uint previous){SetLastError(0);var next=EnumClipboardFormats(previous);if(next==0&&System.Runtime.InteropServices.Marshal.GetLastWin32Error()!=0)throw new System.InvalidOperationException("Clipboard formats could not be enumerated completely");return next;}
 static uint OwnerPid(System.IntPtr owner){uint pid;GetWindowThreadProcessId(owner,out pid);return pid;}
 static string Name(uint id){switch(id){case 1:return "CF_TEXT";case 2:return "CF_BITMAP";case 8:return "CF_DIB";case 17:return "CF_DIBV5";case 7:return "CF_OEMTEXT";case 13:return "CF_UNICODETEXT";case 15:return "CF_HDROP";case 16:return "CF_LOCALE";}var text=new System.Text.StringBuilder(256);return GetClipboardFormatNameW(id,text,text.Capacity)>0?text.ToString():"Predefined format "+id;}
 static bool KnownGlobal(uint id,string name){return id==1||id==7||id==13||id==16||(id>=0xc000&&(name=="HTML Format"||name=="Rich Text Format"||name=="Rich Text Format Without Objects"));}
 static byte[] Bytes(uint id){var memory=GetClipboardData(id);if(memory==System.IntPtr.Zero)throw new System.InvalidOperationException("Unavailable clipboard HGLOBAL");var size=GlobalSize(memory).ToUInt64();if(size==0||size>MaxFormatBytes)throw new System.InvalidOperationException("Clipboard format exceeds bounded HGLOBAL policy");var pointer=GlobalLock(memory);if(pointer==System.IntPtr.Zero)throw new System.InvalidOperationException("Clipboard HGLOBAL cannot be locked");try{var result=new byte[(int)size];System.Runtime.InteropServices.Marshal.Copy(pointer,result,0,result.Length);return result;}finally{GlobalUnlock(memory);}}
 static bool Equal(byte[] left,byte[] right){if(left.Length!=right.Length)return false;for(int i=0;i<left.Length;i++)if(left[i]!=right[i])return false;return true;}
 public static BlinkTransferClipboardBackup Capture(){
  var value=new BlinkTransferClipboardBackup{Reason="Clipboard busy",Formats=new BlinkTransferClipboardFormatInfo[0]};
  if(!OpenClipboard(System.IntPtr.Zero))return value;
  try{
   value.Sequence=GetClipboardSequenceNumber();var owner=GetClipboardOwner();value.Owner=owner.ToInt64();value.OwnerPid=OwnerPid(owner);
   var infos=new System.Collections.Generic.List<BlinkTransferClipboardFormatInfo>();uint id=0;string unsupported=null;
   while((id=NextFormat(id))!=0){var name=Name(id);infos.Add(new BlinkTransferClipboardFormatInfo{Id=id,Name=name});if(!KnownGlobal(id,name))unsupported="Unrestorable clipboard format: "+name;if(infos.Count>MaxFormats){unsupported="Clipboard has more than sixteen formats";break;}}
   value.Formats=infos.ToArray();if(unsupported!=null){value.Reason=unsupported;return value;}
   foreach(var info in infos){var bytes=Bytes(info.Id);info.Bytes=bytes.Length;value.TotalBytes+=bytes.Length;if(value.TotalBytes>MaxTotalBytes){value.Data.Clear();value.Reason="Clipboard exceeds four MiB snapshot bound";return value;}value.Data.Add(new BlinkTransferClipboardFormat{Id=info.Id,Bytes=bytes});}
   if(GetClipboardSequenceNumber()!=value.Sequence||GetClipboardOwner().ToInt64()!=value.Owner){value.Data.Clear();value.Reason="Clipboard changed during snapshot";return value;}
   value.Restorable=true;value.Reason=infos.Count==0?"Empty clipboard captured":"Every original format captured as bounded known HGLOBAL bytes";return value;
  }catch(System.Exception e){value.Data.Clear();value.Reason=e.Message;return value;}finally{CloseClipboard();}
 }
 public static void Forget(BlinkTransferClipboardBackup backup){if(backup!=null){foreach(var entry in backup.Data)System.Array.Clear(entry.Bytes,0,entry.Bytes.Length);backup.Data.Clear();}}
 public static bool Matches(uint sequence,long owner){if(!OpenClipboard(System.IntPtr.Zero))throw new System.InvalidOperationException("Clipboard busy");try{return GetClipboardSequenceNumber()==sequence&&GetClipboardOwner().ToInt64()==owner;}finally{CloseClipboard();}}
 public static BlinkTransferClipboardDrop ReadDrop(){
  if(!OpenClipboard(System.IntPtr.Zero))throw new System.InvalidOperationException("Clipboard busy");
  try{var owner=GetClipboardOwner();var result=new BlinkTransferClipboardDrop{Sequence=GetClipboardSequenceNumber(),Owner=owner.ToInt64(),OwnerPid=OwnerPid(owner),HasDrop=IsClipboardFormatAvailable(15)};
   if(result.HasDrop){var drop=GetClipboardData(15);var count=DragQueryFileW(drop,0xffffffffu,null,0);if(count>16)throw new System.InvalidOperationException("Clipboard file-drop count exceeds test bound");var paths=new System.Collections.Generic.List<string>();for(uint n=0;n<count;n++){var path=new System.Text.StringBuilder(32768);if(DragQueryFileW(drop,n,path,(uint)path.Capacity)==0)throw new System.InvalidOperationException("Clipboard file-drop path unavailable");paths.Add(path.ToString());}result.Paths=paths.ToArray();}
   var effect=RegisterClipboardFormatW("Preferred DropEffect");if(IsClipboardFormatAvailable(effect)){var memory=GetClipboardData(effect);if(memory!=System.IntPtr.Zero&&GlobalSize(memory).ToUInt64()>=4){var pointer=GlobalLock(memory);if(pointer!=System.IntPtr.Zero){try{result.Effect=unchecked((uint)System.Runtime.InteropServices.Marshal.ReadInt32(pointer));result.HasEffect=true;}finally{GlobalUnlock(memory);}}}}
   if(GetClipboardSequenceNumber()!=result.Sequence||GetClipboardOwner().ToInt64()!=result.Owner)throw new System.InvalidOperationException("Clipboard changed during read");return result;
  }finally{CloseClipboard();}
 }
 public static BlinkTransferClipboardRestore Restore(BlinkTransferClipboardBackup backup,uint sequence,long expectedOwner,int pid,System.IntPtr window){
  var result=new BlinkTransferClipboardRestore{Status="NOT RESTORED",Reason="No restorable snapshot"};if(backup==null||!backup.Restorable)return result;
  if(OwnerPid(window)!=(uint)pid||OwnerPid(new System.IntPtr(expectedOwner))!=(uint)pid){result.Status="CHANGED";result.Reason="Recorded clipboard owner no longer belongs to the owned live process";return result;}
  var memories=new System.Collections.Generic.List<System.IntPtr>();
  try{
   // Allocate every original format before changing clipboard contents.
   foreach(var entry in backup.Data){var memory=GlobalAlloc(0x42,new System.UIntPtr((uint)entry.Bytes.Length));if(memory==System.IntPtr.Zero)throw new System.OutOfMemoryException("Clipboard restore allocation failed");memories.Add(memory);var pointer=GlobalLock(memory);if(pointer==System.IntPtr.Zero)throw new System.InvalidOperationException("Clipboard restore buffer lock failed");try{System.Runtime.InteropServices.Marshal.Copy(entry.Bytes,0,pointer,entry.Bytes.Length);}finally{GlobalUnlock(memory);}}
   if(!OpenClipboard(window)){result.Reason="Clipboard busy during restore";return result;}
   try{
    if(GetClipboardSequenceNumber()!=sequence||GetClipboardOwner().ToInt64()!=expectedOwner){result.Status="CHANGED";result.Reason="Sequence or owner changed; retained current clipboard without writing";return result;}
    if(!EmptyClipboard())throw new System.InvalidOperationException("Original clipboard restore could not empty owned test data");
    for(int i=0;i<backup.Data.Count;i++){if(SetClipboardData(backup.Data[i].Id,memories[i])==System.IntPtr.Zero)throw new System.InvalidOperationException("Original clipboard format restore failed");memories[i]=System.IntPtr.Zero;}
    foreach(var entry in backup.Data)if(!Equal(entry.Bytes,Bytes(entry.Id)))throw new System.InvalidOperationException("Restored clipboard format bytes differ from original snapshot");
    if(backup.Data.Count==0&&NextFormat(0)!=0)throw new System.InvalidOperationException("Original empty clipboard was not restored");
    var expectedFormats=new System.Collections.Generic.HashSet<uint>();foreach(var entry in backup.Data)expectedFormats.Add(entry.Id);var actualFormats=new System.Collections.Generic.HashSet<uint>();uint current=0;while((current=NextFormat(current))!=0){actualFormats.Add(current);if(actualFormats.Count>MaxFormats)throw new System.InvalidOperationException("Restored clipboard format set is unexpectedly large");}if(!actualFormats.SetEquals(expectedFormats))throw new System.InvalidOperationException("Restored clipboard formats differ from original snapshot");
    result.Restored=true;result.Status="RESTORED";result.Reason="Every original format verified byte-for-byte; original bytes were kept only in memory";result.ResultSequence=GetClipboardSequenceNumber();return result;
   }finally{CloseClipboard();}
  }finally{foreach(var memory in memories)if(memory!=System.IntPtr.Zero)GlobalFree(memory);}
 }
}

'@
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary-user GUI required'}
 $originalClipboard=[BlinkTransferClipboard]::Capture();$report.clipboard_original_metadata=[pscustomobject]@{restorable=$originalClipboard.Restorable;reason=$originalClipboard.Reason;sequence=$originalClipboard.Sequence;owner=$originalClipboard.Owner;owner_pid=$originalClipboard.OwnerPid;formats=@($originalClipboard.Formats);total_bytes=$originalClipboard.TotalBytes;content_saved_to_disk=$false};Save-Report
 if(-not $originalClipboard.Restorable){Skip ('Original clipboard cannot be restored completely: '+$originalClipboard.Reason)}
 if($ClipboardPreflightOnly){$report.status='PREFLIGHT_PASS'}else{
 $desktop=[BlinkNavigationTransferUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady){Skip $desktop.Reason}
 if(-not(Test-Path -LiteralPath $Binary -PathType Leaf)){throw 'Candidate binary missing'};$report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash.ToLowerInvariant()
 $os=Get-CimInstance Win32_OperatingSystem;$report.environment=[ordered]@{caption=$os.Caption;version=$os.Version;build=$os.BuildNumber;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;ordinary_user=$true;theme='NOT CAPTURED';ime='NOT RUN'}
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture already exists'};New-Item -ItemType Directory -Path $fixture -Force|Out-Null;$fixtureCreated=$true
 foreach($path in $initialDirectories|Where-Object{$_ -ine $fixture}){New-Item -ItemType Directory -Path $path -Force|Out-Null};foreach($path in $initialDirectories){$directoryIdentities[$path]=[BlinkNavigationTransferUiNative]::Identity($path)}
 foreach($path in $allowedFiles.Keys){[IO.File]::WriteAllText($path,[string]$allowedFiles[$path],[Text.UTF8Encoding]::new($false));$fileIdentities[$path]=[BlinkNavigationTransferUiNative]::Identity($path)}
 $thisPcLabels=@(Resource-Labels 'Location.ThisPC');if(-not $thisPcLabels.Count){throw 'Existing ThisPC labels required'}
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false;foreach($name in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')){$psi.EnvironmentVariables.Remove($name)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned launch failed'};$ownedHandle=$process.Handle;$report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)};[BlinkNavigationTransferUiNative]::OwnedWindow=$process.MainWindowHandle;$report.window_dpi=[BlinkNavigationTransferUiNative]::GetDpiForWindow($process.MainWindowHandle)
 Wait-Ui 'real initial ThisPC locations' {Is-ThisPc}
 Check '01 virtual and drive-root menus expose only applicable clipboard actions' {
  $peer=Tree-Item $thisPcLabels;if(-not $peer){throw 'Actual ThisPC tree root unavailable'};$null=Menu-Open $peer 'NavigationContextMenu'
  foreach($id in @('TargetContextCopy','TargetContextCut','TargetContextPaste')){if(Owned-Id $id){throw ('Virtual ThisPC exposed filesystem clipboard action '+$id)}};Dismiss-Menu
  $driveRows=@(Rows|Where-Object{$_.Current.Name -match '(?i)[a-z]:'});if(-not $driveRows.Count){throw 'Actual local drive row required'};$preferred=[IO.Path]::GetPathRoot($project).Substring(0,2);$matched=@($driveRows|Where-Object{$_.Current.Name -match [regex]::Escape($preferred)});$peer=$(if($matched.Count){$matched[0]}else{$driveRows[0]})
  $peer.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$menu=Menu-Open $peer 'LocationContextMenu';if($menu.Current.HelpText -notmatch '^[A-Za-z]:\\$' -or (Owned-Id 'TargetContextCopy') -or (Owned-Id 'TargetContextCut') -or -not(Owned-Id 'TargetContextPaste')){throw 'Drive root clipboard applicability is incorrect'}
  # Presence is tested without writing Clipboard or pasting into a real root.
  Dismiss-Menu;Navigate-Browse;Assert-Manifest;Assert-Browse-Preserved
 }
 Check '02 tree Copy publishes only the clicked directory as standard CF_HDROP COPY' {
  Copy-Or-Cut $copySource $false;Assert-Manifest;Assert-Browse-Preserved
 }
 Check '03 tree Paste copies the complete nested directory into the clicked destination' {
  Paste-Target $copyTarget;Wait-Ui 'nested directory copy created its exact known payload' {(Test-Path -LiteralPath (Join-Path $copyResult '嵌套-é\payload-一.txt') -PathType Leaf) -and (Ready)}
  Register-Transfer $copySource $copyResult $false;Assert-Browse-Preserved
  $report.copy_result=[pscustomobject]@{source=$copySource;destination=$copyResult;source_retained=$true;nested_bytes_verified=$true;copied_identities_distinct=$true;current_directory_preserved=$true;decoy_selection_preserved=$true};Save-Report
 }
 Check '04 tree Cut publishes MOVE for that directory without moving it at invocation' {
  Copy-Or-Cut $moveSource $true;Assert-Manifest;Assert-Browse-Preserved
 }
 Check '05 target Paste performs a same-volume directory move preserving native identities' {
  if([IO.Path]::GetPathRoot($moveSource) -ine [IO.Path]::GetPathRoot($moveTarget)){throw 'Same-volume move fixture required'};Paste-Target $moveTarget
  Wait-Ui 'exact moved nested payload exists and source is absent' {-not(Test-Path -LiteralPath $moveSource) -and (Test-Path -LiteralPath (Join-Path $moveResult 'nested\payload.txt') -PathType Leaf) -and (Ready)}
  Register-Transfer $moveSource $moveResult $true;Assert-Browse-Preserved
  $report.move_result=[pscustomobject]@{source=$moveSource;destination=$moveResult;source_absent=$true;directory_and_file_identities_preserved=$true;nested_bytes_verified=$true;current_directory_preserved=$true;decoy_selection_preserved=$true};Save-Report
 }
 Check '06 actual directory conflict Cancel preserves both source and existing target bytes' {
  Copy-Or-Cut $conflictSource $false;Paste-Target $conflictTarget;Wait-Ui 'real directory conflict dialog' {$null -ne (Owned-Id 'DirectoryCopyConflictDialog')}
  $sourcePeer=Owned-Id 'CopyConflictSource';$targetPeer=Owned-Id 'CopyConflictDestination';if(-not $sourcePeer -or -not $targetPeer -or $sourcePeer.Current.Name -notlike ('*'+$conflictSource+'*') -or $targetPeer.Current.Name -notlike ('*'+$conflictResult+'*')){throw 'Actual conflict does not identify the owned source/destination'}
  $report.conflict_cancel=[pscustomobject]@{source=$conflictSource;destination=$conflictResult;dialog=(Peer-Evidence (Owned-Id 'DirectoryCopyConflictDialog'));decision='actual scoped Escape invokes the native Close/Cancel route';bytes_unchanged=$false};Save-Report
  Key 27;Wait-Ui 'conflict cancelled and existing operation pipeline idle' {-not(Owned-Id 'DirectoryCopyConflictDialog') -and (Ready)};Assert-Manifest;Assert-Browse-Preserved;$report.conflict_cancel.bytes_unchanged=$true;Save-Report
 }
 Check '07 a retired tree Paste peer cannot paste or navigate after switching tabs' {
  Assert-OwnedClipboard;$null=Target-Menu $moveTarget;$oldPaste=Owned-Id 'TargetContextPaste';if(-not $oldPaste.Current.IsEnabled){throw 'A genuinely enabled owned Paste peer required'};$originalTab=Selected-Tab;$count=@(Tabs).Count
  Act (By-Id 'AddButton');Wait-Ui 'new tab is truly virtual ThisPC' {@(Tabs).Count -eq $count+1 -and (Is-ThisPc)};Assert-OwnedClipboard;$unavailable=$false
  try{$pattern=$null;if(-not $oldPaste.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$unavailable=$true}else{$pattern.Invoke()}}catch [Windows.Automation.ElementNotAvailableException]{$unavailable=$true}catch [InvalidOperationException]{$unavailable=$true}
  $quiet=[Diagnostics.Stopwatch]::StartNew();Wait-Ui 'stale peer leaves virtual tab and owned filesystem unchanged' {if(-not(Is-ThisPc)){throw 'Retired Paste navigated the new active tab'};Assert-Manifest;$quiet.ElapsedMilliseconds -ge 500}
  $report.stale_peer=[pscustomobject]@{old_target=$moveTarget;peer_unavailable=$unavailable;new_tab_stays_virtual=$true;filesystem_manifest_unchanged=$true};Save-Report;Select-Tab $originalTab $browse;Assert-Browse-Preserved
 }
 $notes.Add('Original Clipboard bytes are retained only in this process memory. Apps/Shift+F10 and UIA actions are real accessibility/keyboard paths, not physical right-click hit testing. No real drive root or user profile is used as a paste destination.')
 $report.status='PASS'
 }
}catch{
 if($_.Exception.Message.StartsWith('NAV_TRANSFER_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring('NAV_TRANSFER_UI_SKIP: '.Length)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{if($process){Capture-State 'unexpected failure'}}catch{}}
}finally{
 # Restore while the owned app/window still exists. Never overwrite a newer
 # clipboard sequence/owner, including a copy performed by the user.
 if($originalClipboard -and $script:lastOwnedSequence){try{
  $restore=[BlinkTransferClipboard]::Restore($originalClipboard,$script:lastOwnedSequence,$script:lastOwnedOwner,$process.Id,[BlinkNavigationTransferUiNative]::OwnedWindow);$report.clipboard_restoration=[pscustomobject]@{status=$restore.Status;reason=$restore.Reason;restored=$restore.Restored;result_sequence=$restore.ResultSequence}
  if(-not $restore.Restored){if($restore.Status -eq 'CHANGED'){if($report.status -eq 'PASS'){$report.status='SKIP'};Record '08 original clipboard restoration yields to a changed sequence or owner' 'SKIP' $restore.Reason}else{throw $restore.Reason}}elseif($report.status -eq 'PASS'){Record '08 every original clipboard format restored and verified byte-for-byte'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '08 original clipboard restoration failed' 'FAIL' $_.Exception.Message}}
 if($originalClipboard){[BlinkTransferClipboard]::Forget($originalClipboard)}
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if($report.before_close_alive){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');Save-Report;$report.close_request_result=$process.CloseMainWindow();Save-Report;if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned Blink still running PID '+$process.Id+'; no force termination')}}
  $process.Refresh();$report.process_exited=$process.HasExited;if($process.HasExited){$report.exit_code=$process.ExitCode;$report.exit_code_hex='0x{0:X8}' -f ([long]$report.exit_code -band 4294967295);$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o')}
  if($report.exit_code -ne 0){throw ('Owned Blink abnormal exit '+$report.exit_code_hex)};if($report.status -eq 'PASS'){Record '09 owned navigation-transfer window closes normally and exits zero'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '09 normal owned close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process remains active; retain fixture'};$report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
  $report.fixture_bytes_removed=[long]((Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture);if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '10 exact GUID transfer graph identities bytes and attributes verified before cleanup'}
 }catch{$report.status='FAIL';$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '10 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=[long]((Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum);Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -eq 'SKIP';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI NAVIGATION TRANSFER '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -eq 'PREFLIGHT_PASS'){exit 0};if($report.status -ne 'PASS'){exit 2};exit 0
