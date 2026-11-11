[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$Binary='',
    [ValidateRange(5,90)][int]$StepTimeoutSeconds=30
)

# Historical body below is retained for reference and is never run by this entry.
Write-Warning 'Historical command-bar smoke is disabled: it cannot completely restore arbitrary Clipboard formats. No validation ran. See docs/VALIDATION_GUIDE.md and the current per-feature scripts, including run-blink-navigation-mutation-smoke.ps1 -Feature NewFolder/Rename/Delete.'
exit 2

# Owned CommandBar integration smoke. Uses only this GUID fixture and new Blink PID.
# No global input, service/configuration changes or system Recycle Bin enumeration.
# A PASS means these recorded actions, not the whole Explorer compatibility matrix.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$stage=Join-Path $project '.build\explorer-address-bar'
$integration=Join-Path $stage 'integration'
$fixtureParent=Join-Path $stage 'ui-fixtures'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-command-bar-compat-report.json'
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $fixtureParent ('owned-'+$guid)
$source=Join-Path $fixture 'source';$copyTarget=Join-Path $fixture 'copy-target';$moveTarget=Join-Path $fixture 'move-target';$other=Join-Path $fixture 'other'
$child=Join-Path $source ('cb-'+$token+'-dir');$created=Join-Path $source ('cb-'+$token+'-created')
$copyName='cb-'+$token+'-copy.txt';$moveName='cb-'+$token+'-move.bin';$renameName='cb-'+$token+'-rename.txt';$renamedName='cb-'+$token+'-renamed.txt'
$searchPasteName='cb-'+$token+'-parent-paste.txt'
$sortName='cb-'+$token+'-sort.bin';$refreshName='cb-'+$token+'-refresh.txt'
$deleteName='cb-'+$token+'-delete.txt';$searchName='cb-'+$token+'-search.txt';$hiddenName='cb-'+$token+'-hidden.txt';$systemName='cb-'+$token+'-system.txt';$otherName='cb-'+$token+'-other.txt'
$allowedDirectories=@($source,$copyTarget,$moveTarget,$other,$child,$created)
$bytes=@{};$bytes[(Join-Path $source $copyName)]='Blink toolbar '+$guid+' COPY';$bytes[(Join-Path $copyTarget $copyName)]=$bytes[(Join-Path $source $copyName)]
$bytes[(Join-Path $source $moveName)]=('Blink toolbar '+$guid+' MOVE ')*20;$bytes[(Join-Path $moveTarget $moveName)]=$bytes[(Join-Path $source $moveName)]
$bytes[(Join-Path $source $renameName)]='Blink toolbar '+$guid+' RENAME';$bytes[(Join-Path $source $renamedName)]=$bytes[(Join-Path $source $renameName)]
$bytes[(Join-Path $source $sortName)]=('Blink toolbar '+$guid+' SORT ')*30;$bytes[(Join-Path $source $refreshName)]='Blink toolbar '+$guid+' REFRESH'
$bytes[(Join-Path $source $deleteName)]='Blink toolbar '+$guid+' DELETE';$bytes[(Join-Path $source $searchName)]='Blink toolbar '+$guid+' SEARCH'
$bytes[(Join-Path $source $hiddenName)]='Blink toolbar '+$guid+' HIDDEN';$bytes[(Join-Path $source $systemName)]='Blink toolbar '+$guid+' SYSTEM';$bytes[(Join-Path $other $otherName)]='Blink toolbar '+$guid+' OTHER'
$bytes[(Join-Path $copyTarget $searchPasteName)]='Blink toolbar '+$guid+' SEARCH PARENT PASTE';$bytes[(Join-Path $source $searchPasteName)]=$bytes[(Join-Path $copyTarget $searchPasteName)]
$initialFiles=@((Join-Path $copyTarget $searchPasteName),(Join-Path $source $sortName),(Join-Path $source $copyName),(Join-Path $source $moveName),(Join-Path $source $renameName),(Join-Path $source $deleteName),(Join-Path $source $searchName),(Join-Path $source $hiddenName),(Join-Path $source $systemName),(Join-Path $other $otherName))
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$tempChanged=$false
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new();$process=$null;$fixtureCreated=$false;$originalClipboard=$null;$lastOwnedSequence=[uint32]0
$script:windowCache=$null;$script:barCache=$null;$script:addressCache=$null;$script:entriesCache=$null;$script:barElements=@{};$script:popupCache=@{};$script:queryCache=$null;$currentStep='preflight';$originalSize=$null
$primaryIds=@('CommandNew','CommandCut','CommandCopy','CommandPaste','CommandRename','CommandDelete','CommandSort','FolderViewMode')
$secondaryIds=@('CommandSelectAll','CommandClearSelection','CommandInvertSelection','CommandProperties','CommandRefresh','CommandCopyFullPath','CommandCopyParentPath')
$surfaceIds=@('TopNewMenu','TopSortMenu','TopViewMenu','OverflowPopup','FolderContextMenu','TopViewShowHidden')
$metrics=[ordered]@{window_queries=0;bar_snapshots=0;popup_queries=0;row_snapshots=0;uia_query_ms=0.0}
$report=[ordered]@{status='RUNNING';passed=$false;skipped=$false;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;configuration=$Configuration;binary=$Binary;binary_sha256=$null;process_id=$null;process_exited=$false;exit_code=$null;forced_termination=$false;environment=$null;window_dpi=$null;desktop_preflight=$null;current_step=$currentStep;failure=$null;checks=@();notes=@();fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_ownership_failures=@();fixture_bytes_removed=0;compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0;clipboard_restored=$false;clipboard_evidence=@();bar_snapshots=@();menu_snapshots=@();dialog_snapshots=@();file_effects=@();keyboard_evidence=@();overflow_evidence=$null;stale_peer=$null;search_evidence=$null;search_scope_evidence=@();search_query_steps=@();query_loss_first=$null;last_ui_snapshot=$null;metrics=$metrics;human_acceptance='NOT RUN';not_run=@('Windows 11 24H2 comparison','full theme/DPI/language/IME/Narrator matrix','right-click extensions','system Recycle Bin operation','device and UNC scenarios');input_mode='New owned PID/HWND UIA and InputSite only; clipboard writes are existing Blink commands and conditional own-sequence text restore'}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());$report.metrics=$metrics;[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 11),[Text.UTF8Encoding]::new($true))}
function Record([string]$Label,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Label;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Skip([string]$Why){throw ('COMMAND_BAR_UI_SKIP: '+$Why)}
function Check([string]$Label,[scriptblock]$Body){$script:currentStep=$Label;Save-Report;try{$null=&$Body;Record $Label}catch{if($_.Exception.Message.StartsWith('COMMAND_BAR_UI_SKIP: ')){Record $Label 'SKIP' $_.Exception.Message}else{Record $Label 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace)};throw}}
function Window{
 $process.Refresh();if($process.HasExited){throw ('Owned Blink exited: '+$process.ExitCode)}
 if(-not $script:windowCache){if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null};$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)}
 if($script:windowCache.Current.ProcessId -ne $process.Id){throw 'Window PID changed'};return $script:windowCache
}
function Condition([string]$Id){return [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)}
function Visible($Element){return $null -ne $Element -and -not $Element.Current.IsOffscreen}
function Find-In($Root,[string]$Id,[bool]$VisibleOnly=$false){if(-not $Root){return $null};foreach($item in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,(Condition $Id)))){if($item.Current.ProcessId -eq $process.Id -and (-not $VisibleOnly -or -not $item.Current.IsOffscreen)){return $item}};return $null}
function By-Id([string]$Id){$metrics.window_queries++;return Find-In (Window) $Id}
function Bar{if(-not $script:barCache){$script:barCache=By-Id 'ExplorerCommandBar'};return $script:barCache}
function Entries{if(-not $script:entriesCache){$script:entriesCache=By-Id 'FolderEntries'};return $script:entriesCache}
# Reading the host does not enter edit mode or dismiss an open menu.
function Address{
 if(-not $script:addressCache){$script:addressCache=By-Id 'AddressBar';if(-not $script:addressCache){$script:addressCache=Address-Editor}};return $script:addressCache
}
function Address-Editor{
 $addressEditor=Find-In (Window) 'AddressEditor' $true;if($addressEditor){return $addressEditor}
 $editCondition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Edit);$editors=@((Window).FindAll([Windows.Automation.TreeScope]::Descendants,$editCondition)|Where-Object{-not $_.Current.IsOffscreen -and $_.Current.AutomationId -notin @('SearchQuery','SearchSelectedVolumes','SearchSelectedFolder','RenameName','NewFolderName')});if($editors.Count -ne 1){return $null};return $editors[0]
}
function Value($Element){if($Element.Current.AutomationId -ceq 'AddressBar'){return $Element.Current.HelpText};return $Element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Of-Type($Root,$Type){if(-not $Root){return @()};return @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,$Type)))}
function Cache-Request{
 $request=[Windows.Automation.CacheRequest]::new();$request.TreeScope=[Windows.Automation.TreeScope]::Element
 foreach($prop in @([Windows.Automation.AutomationElement]::AutomationIdProperty,[Windows.Automation.AutomationElement]::NameProperty,[Windows.Automation.AutomationElement]::IsEnabledProperty,[Windows.Automation.AutomationElement]::IsOffscreenProperty,[Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.AutomationElement]::ClassNameProperty,[Windows.Automation.AutomationElement]::BoundingRectangleProperty,[Windows.Automation.AutomationElement]::ProcessIdProperty)){$request.Add($prop)}
 return $request
}
function Rect($Rectangle){return [ordered]@{left=$Rectangle.Left;top=$Rectangle.Top;right=$Rectangle.Right;bottom=$Rectangle.Bottom;width=$Rectangle.Width;height=$Rectangle.Height}}
function Bar-Snapshot{
 $timer=[Diagnostics.Stopwatch]::StartNew();$request=Cache-Request;$scope=$request.Activate();$snapshot=@{}
 try{foreach($element in @((Bar).FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition))){$c=$element.Cached;if($c.ProcessId -ne $process.Id -or -not $c.AutomationId){continue};if($c.AutomationId -in ($primaryIds+$secondaryIds+@('MoreButton'))){$script:barElements[$c.AutomationId]=$element;$snapshot[$c.AutomationId]=[pscustomobject]@{id=$c.AutomationId;name=$c.Name;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;role=$c.ControlType.ProgrammaticName;class=$c.ClassName;rect=(Rect $c.BoundingRectangle)}}}}finally{$scope.Dispose()}
 $metrics.bar_snapshots++;$metrics.uia_query_ms+=$timer.Elapsed.TotalMilliseconds;return $snapshot
}
function Bar-Control([string]$Id){if(-not $script:barElements.ContainsKey($Id)){$null=Bar-Snapshot};if($script:barElements.ContainsKey($Id)){return $script:barElements[$Id]};throw ('CommandBar control missing: '+$Id)}
function Native-Overflow{
 # Real WinUI peer proved by ui-overflow-probe: OverflowPopup -> this Bar.
 # Its own IsOffscreen may remain true while its child commands are visible.
 $ownedBar=Bar;if(-not $ownedBar){return $null};$barIdentity=$ownedBar.GetRuntimeId()-join ','
 foreach($candidate in @($ownedBar.FindAll([Windows.Automation.TreeScope]::Descendants,(Condition 'OverflowPopup')))){
  $c=$candidate.Current
  if($c.ProcessId -ne $process.Id -or $c.ClassName -cne 'Popup' -or $c.ControlType -ne [Windows.Automation.ControlType]::Window){continue}
  $parent=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($candidate)
  if(-not $parent -or $parent.Current.ProcessId -ne $process.Id -or ($parent.GetRuntimeId()-join ',') -cne $barIdentity){continue}
  foreach($id in $secondaryIds){if(Find-In $candidate $id $true){$script:popupCache['OverflowPopup']=$candidate;return $candidate}}
 };$script:popupCache.Remove('OverflowPopup');return $null
}
function Record-Overflow($Root){
 if(-not $Root -or $Root.Current.ProcessId -ne $process.Id -or $Root.Current.AutomationId -cne 'OverflowPopup'){throw 'Real owned OverflowPopup evidence required'}
 $request=Cache-Request;$scope=$request.Activate();$commands=@()
 try{foreach($item in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition))){$c=$item.Cached;if($c.ProcessId -eq $process.Id -and $c.AutomationId -in ($primaryIds+$secondaryIds)){$script:barElements[$c.AutomationId]=$item;$commands+=@([pscustomobject]@{id=$c.AutomationId;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;offscreen=$c.IsOffscreen;enabled=$c.IsEnabled;rect=(Rect $c.BoundingRectangle)})}}}finally{$scope.Dispose()}
 $peer=$Root.Current;$report.menu_snapshots+=@([pscustomobject]@{kind='native-overflow';id=$peer.AutomationId;class=$peer.ClassName;role=$peer.ControlType.ProgrammaticName;peer_offscreen=$peer.IsOffscreen;parent_id=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($Root).Current.AutomationId;commands=$commands;utc=[DateTime]::UtcNow.ToString('o')});Save-Report
}
function Popup([string]$Id){
 $metrics.popup_queries++
 if($Id -ceq 'OverflowPopup'){return Native-Overflow}
 if($script:popupCache.ContainsKey($Id)){try{if(Visible $script:popupCache[$Id]){return $script:popupCache[$Id]}}catch{};$script:popupCache.Remove($Id)}
 $found=Find-In (Window) $Id $true
 if(-not $found){foreach($hwnd in [BlinkCommandBarUiNative]::OwnedTopWindows($process.Id)){if($hwnd -eq $process.MainWindowHandle){continue};try{$root=[Windows.Automation.AutomationElement]::FromHandle($hwnd);if($root.Current.AutomationId -eq $Id -and (Visible $root)){$found=$root}else{$found=Find-In $root $Id $true};if($found){break}}catch{if([BlinkCommandBarUiNative]::WindowExists($hwnd)){throw}}}}
 if($found){$script:popupCache[$Id]=$found};return $found
}
function Menu-Item([string]$SurfaceId,[string]$Id){
 # PowerShell names are case-insensitive: keep the typed ID and UIA peer distinct.
 $presenter=Popup $SurfaceId;if(-not $presenter){return $null};return Find-In $presenter $Id $true
}
function Act($Element){if(-not $Element -or $Element.Current.ProcessId -ne $process.Id -or -not (Visible $Element) -or -not $Element.Current.IsEnabled){throw 'Visible enabled owned action required'};$p=$null;if($Element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$p)){$p.Invoke()}elseif($Element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$p)){$p.Toggle()}elseif($Element.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$p)){$p.Expand()}else{throw ('No action pattern: '+$Element.Current.AutomationId)}}
function Toggle-State($Element){$p=$null;if(-not $Element -or $Element.Current.ProcessId -ne $process.Id){throw 'Owned toggle missing'};if($Element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$p)){return $p.Current.ToggleState -eq [Windows.Automation.ToggleState]::On};if($Element.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$p)){return $p.Current.IsSelected};throw 'Native toggle state unavailable'}
function Require-Desktop{$desktop=[BlinkCommandBarUiNative]::DesktopState();$report.desktop_preflight=$desktop;if(-not $desktop.InputReady){Skip ('Unlocked desktop required: '+$desktop.Reason)}}
function Key([uint32]$Code,[int]$Modifiers=0){Require-Desktop;[BlinkCommandBarUiNative]::Key($process.Id,$Code,$Modifiers)}
function Peer-Evidence($Element){
 if(-not $Element){return [pscustomobject]@{found=$false}}
 $c=$Element.Current;return [pscustomobject]@{found=$true;id=$c.AutomationId;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;name=$c.Name;help_text=$c.HelpText;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;pid=$c.ProcessId;runtime_id=($Element.GetRuntimeId()-join ',')}
}
function Search-Query{
 if(-not $script:queryCache){$script:queryCache=By-Id 'SearchQuery'};return $script:queryCache
}
function Assert-SearchQuery([string]$Stage){
 $query=Search-Query;$actual=$(if($query){Value $query}else{$null});$focus=$null;$focused=[Windows.Automation.AutomationElement]::FocusedElement
 if($focused -and $focused.Current.ProcessId -eq $process.Id){$focus=$focused.Current.AutomationId}
 $step=[pscustomobject]@{stage=$Stage;utc=[DateTime]::UtcNow.ToString('o');expected=$searchName;actual=$actual;matches=($actual -ceq $searchName -and -not [string]::IsNullOrWhiteSpace($actual));focus=$focus}
 $report.search_query_steps+=@($step);if(-not $step.matches -and -not $report.query_loss_first){$report.query_loss_first=$step};Save-Report
 if(-not $step.matches){throw ('COMMAND_BAR_QUERY_CHANGED: exact owned query changed at '+$Stage+'; actual='+$actual)}
}
function Search-Snapshot{
 $state=[ordered]@{query=$null;query_peer=$null;effective_scope=$null;scope_selected=@();selected_folder=$null;include_subfolders=$null;results_found=$false;results_visible=$false;results_peer=$null;rows=@();actual_text=@();errors=@()}
 try{$query=Search-Query;$state.query=$(if($query){Value $query}else{$null});$state.query_peer=Peer-Evidence $query}catch{$state.errors+=@('query: '+$_.Exception.Message)}
 $effective=$null;try{$effective=By-Id 'SearchEffectiveScope';if($effective){$state.effective_scope=Element-Text $effective}}catch{$state.errors+=@('effective scope: '+$_.Exception.Message)}
 try{$scopePeer=By-Id 'SearchScope';if($scopePeer){$state.scope_selected=@($scopePeer.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}}catch{$state.errors+=@('scope: '+$_.Exception.Message)}
 try{$folder=By-Id 'SearchSelectedFolder';if($folder){$state.selected_folder=Value $folder};$include=By-Id 'SearchIncludeSubfolders';if($include){$state.include_subfolders=Toggle-State $include}}catch{$state.errors+=@('scope options: '+$_.Exception.Message)}
 try{
  $results=By-Id 'SearchResults';$state.results_found=$null -ne $results;$state.results_peer=Peer-Evidence $results
  if($results){$state.results_visible=Visible $results;foreach($row in @(Of-Type $results ([Windows.Automation.ControlType]::ListItem))){if($row.Current.ProcessId -eq $process.Id){$state.rows+=@([pscustomobject]@{name=$row.Current.Name;text=(Element-Text $row);offscreen=$row.Current.IsOffscreen;class=$row.Current.ClassName;role=$row.Current.ControlType.ProgrammaticName})}}}
 }catch{$state.errors+=@('results: '+$_.Exception.Message)}
 # Read actual search-pane summaries and volume errors, excluding the left tree.
 if($effective){try{$region=$effective.Current.BoundingRectangle;foreach($text in @(Of-Type (Window) ([Windows.Automation.ControlType]::Text))){$c=$text.Current;if($c.ProcessId -eq $process.Id -and -not $c.IsOffscreen -and $c.Name -and $c.BoundingRectangle.Left -ge $region.Left-2 -and $c.BoundingRectangle.Top -ge $region.Top-2){$state.actual_text+=@([pscustomobject]@{id=$c.AutomationId;text=$c.Name})}}}catch{$state.errors+=@('search text: '+$_.Exception.Message)}}
 return [pscustomobject]$state
}
function Status-Snapshot{
 $listing=Entries;$lower=$(if($listing -and (Visible $listing) -and $listing.Current.BoundingRectangle.Height -gt 0){$listing.Current.BoundingRectangle.Bottom-2}else{(Window).Current.BoundingRectangle.Bottom-150});$request=Cache-Request;$scope=$request.Activate();$texts=@()
 try{foreach($element in @(Of-Type (Window) ([Windows.Automation.ControlType]::Text))){$c=$element.Cached;if($c.ProcessId -eq $process.Id -and -not $c.IsOffscreen -and $c.Name -and $c.BoundingRectangle.Top -ge $lower){$texts+=@([pscustomobject]@{id=$c.AutomationId;text=$c.Name;rect=(Rect $c.BoundingRectangle)})}}}finally{$scope.Dispose()};return $texts
}
function Capture-State([string]$Why){
 $snapshot=[ordered]@{reason=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;bar=@();rows=@();selected=@();entries_class=$null;visible_column_count=$null;view_cached=$null;view_current=$null;status_text=@();search=$null;focus=$null;errors=@()}
 # Capture cached and freshly discoverable peers independently before refreshing
 # the bar map; overflow peers can disappear from UIA while their popup is shut.
 try{$cached=$null;if($script:barElements.ContainsKey('FolderViewMode')){$cached=$script:barElements['FolderViewMode']};$snapshot.view_cached=Peer-Evidence $cached}catch{$snapshot.view_cached=[pscustomobject]@{found=$false;error=$_.Exception.Message};$snapshot.errors+=@('cached View: '+$_.Exception.Message)}
 try{$snapshot.view_current=Peer-Evidence (Find-In (Bar) 'FolderViewMode')}catch{$snapshot.errors+=@('current View: '+$_.Exception.Message)}
 try{$snapshot.address=Value (Address)}catch{$snapshot.errors+=@('address: '+$_.Exception.Message)}
 try{$snapshot.entries_class=(Entries).Current.ClassName}catch{$snapshot.errors+=@('Entries: '+$_.Exception.Message)}
 try{$snapshot.visible_column_count=Visible-ColumnCount}catch{$snapshot.errors+=@('columns: '+$_.Exception.Message)}
 try{$snapshot.rows=@(Row-Names);$snapshot.selected=@(Selected-Names)}catch{$snapshot.errors+=@('rows: '+$_.Exception.Message)}
 try{$snapshot.status_text=@(Status-Snapshot)}catch{$snapshot.errors+=@('status: '+$_.Exception.Message)}
 try{$snapshot.bar=@((Bar-Snapshot).Values)}catch{$snapshot.errors+=@('bar: '+$_.Exception.Message)}
 try{$snapshot.search=Search-Snapshot}catch{$snapshot.errors+=@('search: '+$_.Exception.Message)}
 try{$f=[Windows.Automation.AutomationElement]::FocusedElement;if($f.Current.ProcessId -eq $process.Id){$snapshot.focus=$f.Current.AutomationId}}catch{$snapshot.errors+=@('focus: '+$_.Exception.Message)}
 $report.last_ui_snapshot=$snapshot;Save-Report
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{if($_.Exception.Message.StartsWith('COMMAND_BAR_UI_SKIP: ') -or $_.Exception.Message.StartsWith('COMMAND_BAR_QUERY_CHANGED: ')){throw};$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);try{Capture-State $Why}catch{};throw ('Timed out: '+$Why+'; '+$last)}
function Visible-Surfaces{
 # One combined presenter query plus the Bar-scoped native popup lookup.
 $metrics.popup_queries++;$conditions=[Windows.Automation.Condition[]]@($surfaceIds|Where-Object{$_ -cne 'OverflowPopup'}|ForEach-Object{Condition $_})
 $condition=[Windows.Automation.OrCondition]::new($conditions);$found=@();$request=Cache-Request;$scope=$request.Activate()
 try{
  $roots=@(Window);foreach($hwnd in [BlinkCommandBarUiNative]::OwnedTopWindows($process.Id)){if($hwnd -ne $process.MainWindowHandle){try{$roots+=@([Windows.Automation.AutomationElement]::FromHandle($hwnd))}catch{if([BlinkCommandBarUiNative]::WindowExists($hwnd)){throw}}}}
  foreach($root in $roots){foreach($item in @($root.FindAll([Windows.Automation.TreeScope]::Subtree,$condition))){$c=$item.Cached;if($c.ProcessId -eq $process.Id -and -not $c.IsOffscreen){$script:popupCache[$c.AutomationId]=$item;$found+=@($item)}}}
 }finally{$scope.Dispose()};$nativeOverflow=Native-Overflow;if($nativeOverflow){$found+=@($nativeOverflow)};return $found
}
function Close-Menus{
 for($n=0;$n -lt 4;$n++){if(-not @(Visible-Surfaces).Count){$script:popupCache.Clear();return};Key 27;$null=$process.WaitForExit(100)}
 Wait-Ui 'all owned menu surfaces close' {@(Visible-Surfaces).Count -eq 0};$script:popupCache.Clear()
}
function Open-Menu([string]$Button,[string]$Surface){Close-Menus;Act (Bar-Control $Button);Wait-Ui ('owned '+$Surface) {$null -ne (Popup $Surface)}}
function Open-More{
 Close-Menus;$more=Find-In (Bar) 'MoreButton' $true
 if(-not $more){foreach($b in @(Of-Type (Bar) ([Windows.Automation.ControlType]::Button))){$p=$null;if($b.Current.AutomationId -notin $primaryIds -and $b.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$p)){$more=$b;break}}}
 if(-not $more){throw 'Native CommandBar overflow button unavailable'};Act $more;Wait-Ui 'native More OverflowPopup with visible secondary commands' {$null -ne (Popup 'OverflowPopup')};Record-Overflow (Popup 'OverflowPopup');return $more
}
function More-Action([string]$Id){$null=Open-More;$item=Menu-Item 'OverflowPopup' $Id;if(-not $item){throw ('Missing real More command: '+$Id)};Act $item;$script:popupCache.Clear()}
function Row-Snapshot{
 $request=Cache-Request;$request.Add([Windows.Automation.SelectionItemPattern]::Pattern);$request.Add([Windows.Automation.SelectionItemPattern]::IsSelectedProperty);$scope=$request.Activate();$rows=@()
 try{foreach($element in @(Of-Type (Entries) ([Windows.Automation.ControlType]::ListItem))){$c=$element.Cached;if($c.ProcessId -ne $process.Id){continue};$selected=$element.GetCachedPattern([Windows.Automation.SelectionItemPattern]::Pattern).Cached.IsSelected;$rows+=@([pscustomobject]@{name=$c.Name;selected=$selected;element=$element})}}finally{$scope.Dispose()};$metrics.row_snapshots++;return $rows
}
function Row-Names{return @((Row-Snapshot)|ForEach-Object{$_.name})}
function Selected-Names{return @((Row-Snapshot)|Where-Object{$_.selected}|ForEach-Object{$_.name})}
function Same-Names([string[]]$A,[string[]]$B){return $A.Count -eq $B.Count -and ($A -join "`n") -ceq ($B -join "`n")}
function Navigate([string]$Path,[string]$Marker=''){
 if($Path -notin @($source,$copyTarget,$moveTarget,$other,$child,$created)){throw 'Navigation outside the owned fixture'}
 Close-Menus;Key 0x4C 1;Wait-Ui 'Ctrl L reveals the owned address editor' {$null -ne (Address-Editor)};$editor=Address-Editor;$editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Path);$editor.SetFocus();Key 13;$script:entriesCache=$null
 Wait-Ui 'exact committed fixture location and ready commands' {if((Value (Address)) -ine $Path -or -not (Entries)){return $false};$bar=Bar-Snapshot;if(-not $bar['CommandNew'].enabled -or -not $bar['CommandSort'].enabled){return $false};if($Marker){return @(Row-Names) -ccontains $Marker};return $true}
}
function Select-Rows([string[]]$Names){
 $rows=Row-Snapshot;foreach($name in $Names){$match=@($rows|Where-Object{$_.name -ceq $name});if($match.Count -ne 1){throw ('Owned row not unique: '+$name)};$p=$match[0].element.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern);if($name -ceq $Names[0]){$p.Select()}else{$p.AddToSelection()}}
 Wait-Ui 'exact selected row set' {Same-Names (@(Selected-Names)|Sort-Object) (@($Names)|Sort-Object)}
}
function Resource-Values([string]$Key){$values=@();foreach($culture in @('en-US','zh-CN')){[xml]$xml=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')));foreach($data in $xml.root.data){if($data.name -ceq $Key){$values+=@([string]$data.value)}}};return @($values|Select-Object -Unique)}
$viewLabels=@{};foreach($mode in @('Details','List','Grid')){$viewLabels[$mode]=@(Resource-Values ('View.'+$mode))};$thisPcLabels=@(Resource-Values 'Location.ThisPC');$columnNames=@();foreach($field in @('Name','Modified','Type','Size')){$columnNames+=@(Resource-Values ('Column.'+$field))}
function Element-Text($Root){$texts=[Collections.Generic.List[string]]::new();if(-not $Root){return ''};foreach($item in @($Root)+@(Of-Type $Root ([Windows.Automation.ControlType]::Text))){$texts.Add($item.Current.Name);$p=$null;if($item.TryGetCurrentPattern([Windows.Automation.TextPattern]::Pattern,[ref]$p)){$texts.Add($p.DocumentRange.GetText(-1))}};return $texts.ToArray() -join "`n"}
function Dialog([string]$EditorId,[string]$ExpectedName=''){
 # Native dialogs may live in another HWND of this PID and publish a qualified
 # or framework-specific class. Anchor to real controls, not class spelling.
 $anchorIds=$(if($EditorId){@($EditorId)}else{@('PrimaryButton','CloseButton')})
 if(-not $EditorId -and -not $ExpectedName){throw 'Delete dialog requires its exact owned filename'}
 $windowIdentity=(Window).GetRuntimeId()-join ','
 foreach($anchorId in $anchorIds){
  $anchor=Popup $anchorId;if(-not $anchor -or $anchor.Current.ProcessId -ne $process.Id){continue}
  $ancestor=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($anchor)
  for($depth=0;$ancestor -and $depth -lt 12;$depth++){
   if($ancestor.Current.ProcessId -ne $process.Id -or ($ancestor.GetRuntimeId()-join ',') -ceq $windowIdentity){break}
   if((Visible $ancestor) -and (Find-In $ancestor 'PrimaryButton' $true) -and (Find-In $ancestor 'CloseButton' $true)){
    if(-not $ExpectedName -or @((Element-Text $ancestor) -split "`n"|Where-Object{$_.TrimEnd("`r") -ceq $ExpectedName}).Count -gt 0){return $ancestor}
   }
   $ancestor=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($ancestor)
  }
 };return $null
}
function Record-Dialog([string]$Kind,$Root,[string]$ExpectedName=''){
 if(-not $Root -or $Root.Current.ProcessId -ne $process.Id -or -not (Visible $Root)){throw 'Visible owned dialog evidence required'}
 $chain=@();$peer=$Root
 for($depth=0;$peer -and $depth -lt 10;$depth++){
  $c=$peer.Current;if($c.ProcessId -ne $process.Id){break}
  $chain+=@([pscustomobject]@{id=$c.AutomationId;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;name=$c.Name;pid=$c.ProcessId})
  if(($peer.GetRuntimeId()-join ',') -ceq ((Window).GetRuntimeId()-join ',')){break}
  $peer=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($peer)
 }
 $report.dialog_snapshots+=@([pscustomobject]@{kind=$Kind;expected_name=$ExpectedName;native_peer_ancestors=$chain;utc=[DateTime]::UtcNow.ToString('o')});Save-Report
}
function Dialog-Button($Root,[string]$Id){if(-not $Root -or $Root.Current.ProcessId -ne $process.Id -or -not (Visible $Root)){throw 'Visible owned dialog required'};$item=Find-In $Root $Id $true;if(-not $item){throw ('Owned dialog button missing: '+$Id)};return $item}
function Read-Bytes([string]$Path){if(-not $bytes.ContainsKey($Path)){throw 'Unowned byte read'};return [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path))}
function Expected-Bytes([string]$Path){return [Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes($bytes[$Path]))}
function Verify-Bytes([string]$Path){if(-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Read-Bytes $Path) -cne (Expected-Bytes $Path)){throw ('Missing/changed owned file: '+$Path)}}
function Own-Clipboard([string[]]$Paths,[uint32]$Effect,[uint32]$Before){
 $clip=$null;Wait-Ui 'fresh owned Clipboard payload' {$script:clip=[BlinkCommandBarUiNative]::ReadClipboard($true);$clip=$script:clip;return $clip.Sequence -ne $Before -and $clip.HasFileDrop -and $clip.HasDropEffect -and $clip.DropEffect -eq $Effect -and (Same-Names @($clip.Files) $Paths)}
 $clip=$script:clip;$script:lastOwnedSequence=$clip.Sequence;$report.clipboard_evidence+=@([pscustomobject]@{sequence=$clip.Sequence;paths=@($clip.Files);preferred_effect=$clip.DropEffect});Save-Report
}
function Own-Text([string]$Text,[uint32]$Before){$clip=$null;Wait-Ui 'fresh owned path text Clipboard' {$script:clip=[BlinkCommandBarUiNative]::ReadClipboard($false);$clip=$script:clip;return $clip.Sequence -ne $Before -and -not $clip.HasFileDrop -and $clip.Text -ceq $Text};$script:lastOwnedSequence=$script:clip.Sequence;$report.clipboard_evidence+=@([pscustomobject]@{sequence=$lastOwnedSequence;text_kind='exact owned fixture path'});Save-Report}
function Assert-Enabled($Snapshot,[string[]]$Enabled,[string[]]$Disabled){foreach($id in $Enabled){if(-not $Snapshot.ContainsKey($id) -or -not $Snapshot[$id].enabled){throw ('Expected enabled command: '+$id)}};foreach($id in $Disabled){if(-not $Snapshot.ContainsKey($id) -or $Snapshot[$id].enabled){throw ('Expected disabled command: '+$id)}}}
function Record-Bar([string]$Context){$state=Bar-Snapshot;$report.bar_snapshots+=@([pscustomobject]@{context=$Context;commands=@($state.Values);utc=[DateTime]::UtcNow.ToString('o')});Save-Report;return $state}
function Set-Sort([string]$Choice){Open-Menu 'CommandSort' 'TopSortMenu';$item=Menu-Item 'TopSortMenu' ('TopSort'+$Choice);if(-not $item){throw 'Real Sort choice missing'};Act $item;$script:popupCache.Clear();Wait-Ui 'Sort command completes and file view ready' {return (Bar-Snapshot)['CommandSort'].enabled}}
function Visible-ColumnCount {
 $request=Cache-Request;$scope=$request.Activate();$count=0
 try{$buttons=@((Window).FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Button)));foreach($button in $buttons){$c=$button.Cached;if(-not $c.IsOffscreen -and $c.Name -in $columnNames){$count++}}}finally{$scope.Dispose()};return $count
}
function View-Choice([string]$Mode){
 Open-Menu 'FolderViewMode' 'TopViewMenu';Act (Menu-Item 'TopViewMenu' ('TopView'+$Mode));$script:popupCache.Clear();$script:entriesCache=$null
 Wait-Ui ('actual '+$Mode+' view') {if($viewLabels[$Mode] -cnotcontains (Bar-Control 'FolderViewMode').Current.HelpText){return $false};$name=(Entries).Current.ClassName;return $name -eq $(if($Mode -eq 'Grid'){'GridView'}else{'ListView'})}
 if((Visible-ColumnCount) -ne $(if($Mode -eq 'Details'){4}else{0})){throw ('Actual detail columns do not match '+$Mode)}
 Open-Menu 'FolderViewMode' 'TopViewMenu';$chosen=@();foreach($m in @('Grid','List','Details')){if(Toggle-State (Menu-Item 'TopViewMenu' ('TopView'+$m))){$chosen+=@($m)}};if($chosen.Count -ne 1 -or $chosen[0] -cne $Mode){throw 'View choices are not mutually checked'};$report.menu_snapshots+=@([pscustomobject]@{kind='view';expected=$Mode;checked=$chosen});Close-Menus
}
function Toggle-Hidden([bool]$Show){Open-Menu 'FolderViewMode' 'TopViewMenu';$showItem=Menu-Item 'TopViewMenu' 'TopViewShow';$p=$null;if($showItem.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$p)){$p.Expand()}else{Act $showItem};Wait-Ui 'owned Hidden submenu' {$null -ne (Popup 'TopViewShowHidden')};$item=Popup 'TopViewShowHidden';if((Toggle-State $item) -ne $Show){Act $item;$script:popupCache.Clear()}else{Close-Menus};Wait-Ui 'Hidden affects exact owned rows' {(@(Row-Names) -ccontains $hiddenName) -eq $Show}}
function Set-FixtureSearchScope{
 # Choose a real, bounded advanced scope that contains both sibling folders;
 # record every query transition; never silently refill lost product input.
 Assert-SearchQuery 'scope-before-controls'
 Wait-Ui 'owned advanced-search expander' {$advanced=By-Id 'SearchAdvanced';$advanced -and (Visible $advanced)}
 $advanced=By-Id 'SearchAdvanced';$expansion=$advanced.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
 if($expansion.Current.ExpandCollapseState -ne [Windows.Automation.ExpandCollapseState]::Expanded){$expansion.Expand()};Assert-SearchQuery 'scope-after-advanced-expand'
 Wait-Ui 'visible real search scope picker' {$picker=By-Id 'SearchScope';$picker -and (Visible $picker)}
 $picker=By-Id 'SearchScope';$picker.SetFocus();Assert-SearchQuery 'scope-after-picker-focus'
 $picker.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Assert-SearchQuery 'scope-after-picker-expand'
 Key 0x23;Assert-SearchQuery 'scope-after-picker-End';Key 13;Assert-SearchQuery 'scope-after-picker-Enter'
 $selectedFolderLabels=@(Resource-Values 'Search.SelectedFolder')
 Wait-Ui 'real selected-folder search scope is selected' {$selected=$picker.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection();$selected.Count -eq 1 -and $selectedFolderLabels -ccontains $selected[0].Current.Name};Assert-SearchQuery 'scope-after-selected-folder-choice'
 Wait-Ui 'selected-folder editor and subfolder choice visible' {$folder=By-Id 'SearchSelectedFolder';$include=By-Id 'SearchIncludeSubfolders';$folder -and $include -and (Visible $folder) -and (Visible $include)}
 $folder=By-Id 'SearchSelectedFolder';$folder.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($fixture);Assert-SearchQuery 'scope-after-folder-value'
 $include=By-Id 'SearchIncludeSubfolders';if(-not (Toggle-State $include)){Act $include};Assert-SearchQuery 'scope-after-subfolder-choice'
 Wait-Ui 'owned fixture scope and subfolder state committed in real controls' {(Value $folder) -ieq $fixture -and (Toggle-State $include) -and (Element-Text (By-Id 'SearchEffectiveScope')) -like ('*'+$fixture+'*')}
 $expansion.Collapse();Assert-SearchQuery 'scope-after-advanced-collapse';Assert-SearchQuery 'scope-before-SearchSubmit'
 Act (By-Id 'SearchSubmit');Assert-SearchQuery 'scope-after-SearchSubmit'
 $report.search_scope_evidence+=@([pscustomobject]@{scope='SelectedFolder';folder=$fixture;include_subfolders=$true;query=(Value (Search-Query));selected_via='SearchAdvanced/SearchScope native keyboard and SelectionPattern';submitted_via='SearchSubmit';utc=[DateTime]::UtcNow.ToString('o')});Save-Report
}
function Ownership-Failures{
 $bad=[Collections.Generic.List[string]]::new();$pending=[Collections.Generic.Stack[string]]::new();$mask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
 $root=Get-Item -LiteralPath $fixture -Force;if(-not $root.PSIsContainer -or ($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($root.Attributes -band $mask)){return @('Fixture root type/identity changed')};$pending.Push($fixture)
 while($pending.Count){foreach($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){$bad.Add('Reparse entry: '+$entry.FullName);continue}
  if($entry.PSIsContainer){if($entry.FullName -in $allowedDirectories -and -not ($entry.Attributes -band $mask)){$pending.Push($entry.FullName)}else{$bad.Add('Unknown directory or attributes: '+$entry.FullName)};continue}
  if(-not $bytes.ContainsKey($entry.FullName)){$bad.Add('Unknown file: '+$entry.FullName);continue}
  $expectedAttributes=0;if($entry.FullName -ieq (Join-Path $source $hiddenName)){$expectedAttributes=[IO.FileAttributes]::Hidden}elseif($entry.FullName -ieq (Join-Path $source $systemName)){$expectedAttributes=[IO.FileAttributes]::System}
  if(($entry.Attributes -band $mask) -ne $expectedAttributes){$bad.Add('Unknown file attributes: '+$entry.FullName)}
  try{if((Read-Bytes $entry.FullName) -cne (Expected-Bytes $entry.FullName)){$bad.Add('Unknown bytes: '+$entry.FullName)}}catch{$bad.Add('Unreadable file: '+$entry.FullName)}
 }};return @($bad.ToArray())
}
Save-Report
try{
 if([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or -not [Environment]::UserInteractive){Skip 'Interactive Windows desktop required'}
 if(-not (Test-Path -LiteralPath $Binary -PathType Leaf)){Skip 'Candidate runtime not available'}
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){Skip 'Ordinary user required'}
 $version=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion';$report.environment=[ordered]@{display_version=$version.DisplayVersion;build=$version.CurrentBuildNumber;ubr=$version.UBR;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;is_administrator=$false;theme='not captured';configuration_requested=$Configuration}
 New-Item -ItemType Directory -Path $compilerTemp -Force|Out-Null;$env:TEMP=$compilerTemp;$env:TMP=$compilerTemp;$tempChanged=$true
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 Add-Type @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkCommandBarUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid; public long Foreground; }
public static class BlinkCommandBarUiNative {
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
    public static BlinkCommandBarUiDesktop DesktopState() {
        var result=new BlinkCommandBarUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
        var foreground=GetForegroundWindow();result.Foreground=foreground.ToInt64();var name=new StringBuilder(256);GetClassName(foreground,name,name.Capacity);result.ForegroundClass=name.ToString();
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
    [DllImport("user32.dll")] static extern bool OpenClipboard(IntPtr owner);
    [DllImport("user32.dll")] static extern bool CloseClipboard();
    [DllImport("user32.dll")] static extern bool EmptyClipboard();
    [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
    [DllImport("user32.dll")] static extern bool IsClipboardFormatAvailable(uint format);
    [DllImport("user32.dll")] static extern IntPtr GetClipboardData(uint format);
    [DllImport("user32.dll")] static extern IntPtr SetClipboardData(uint format,IntPtr memory);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern uint RegisterClipboardFormatW(string name);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalLock(IntPtr memory);
    [DllImport("kernel32.dll")] static extern bool GlobalUnlock(IntPtr memory);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalAlloc(uint flags,UIntPtr bytes);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalFree(IntPtr memory);
    [DllImport("kernel32.dll")] static extern UIntPtr GlobalSize(IntPtr memory);
    [DllImport("shell32.dll",CharSet=CharSet.Unicode)] static extern uint DragQueryFileW(IntPtr drop,uint index,StringBuilder name,uint length);
    public static int[] WindowSize(int pid) { Own(OwnedWindow,pid);WindowRect rect;if(!GetWindowRect(OwnedWindow,out rect))throw new Win32Exception();return new int[]{rect.Right-rect.Left,rect.Bottom-rect.Top}; }
    public static void Resize(int pid,int width,int height) { Own(OwnedWindow,pid);if(width<420||height<300)throw new ArgumentOutOfRangeException();if(!SetWindowPos(OwnedWindow,IntPtr.Zero,0,0,width,height,0x16))throw new Win32Exception(); }
    public static BlinkCommandBarClipboard ReadClipboard(bool includePaths) {
        if(!OpenClipboard(IntPtr.Zero))throw new InvalidOperationException("Clipboard busy");
        try {
            var value=new BlinkCommandBarClipboard{Sequence=GetClipboardSequenceNumber(),HasFileDrop=IsClipboardFormatAvailable(15),HasUnicode=IsClipboardFormatAvailable(13)};
            if(value.HasUnicode){var memory=GetClipboardData(13);if(memory!=IntPtr.Zero&&GlobalSize(memory).ToUInt64()<=1048576){var pointer=GlobalLock(memory);if(pointer!=IntPtr.Zero){try{value.Text=Marshal.PtrToStringUni(pointer);}finally{GlobalUnlock(memory);}}}}
            if(includePaths&&value.HasFileDrop){var drop=GetClipboardData(15);uint count=DragQueryFileW(drop,0xffffffffu,null,0);if(count>1024)throw new InvalidOperationException("Unbounded file-drop data");var files=new List<string>();for(uint n=0;n<count;n++){var name=new StringBuilder(32768);if(DragQueryFileW(drop,n,name,(uint)name.Capacity)==0)throw new InvalidOperationException("Invalid file-drop path");files.Add(name.ToString());}value.Files=files.ToArray();}
            var effect=GetClipboardData(RegisterClipboardFormatW("Preferred DropEffect"));if(effect!=IntPtr.Zero&&GlobalSize(effect).ToUInt64()>=4){var pointer=GlobalLock(effect);if(pointer!=IntPtr.Zero){try{value.DropEffect=unchecked((uint)Marshal.ReadInt32(pointer));value.HasDropEffect=true;}finally{GlobalUnlock(effect);}}}
            return value;
        }finally{CloseClipboard();}
    }
    public static bool RestoreText(BlinkCommandBarClipboard previous,uint expected,int pid) {
        Own(OwnedWindow,pid);if(previous==null||previous.Text==null||previous.HasFileDrop||GetClipboardSequenceNumber()!=expected)return false;
        if(!OpenClipboard(OwnedWindow))return false;
        try{if(GetClipboardSequenceNumber()!=expected)return false;var bytes=Encoding.Unicode.GetBytes(previous.Text+"\0");var memory=GlobalAlloc(0x42,new UIntPtr((uint)bytes.Length));if(memory==IntPtr.Zero)return false;var pointer=GlobalLock(memory);if(pointer==IntPtr.Zero){GlobalFree(memory);return false;}Marshal.Copy(bytes,0,pointer,bytes.Length);GlobalUnlock(memory);if(!EmptyClipboard()||SetClipboardData(13,memory)==IntPtr.Zero){GlobalFree(memory);return false;}return true;}finally{CloseClipboard();}
    }
}
public sealed class BlinkCommandBarClipboard { public uint Sequence,DropEffect; public bool HasFileDrop,HasUnicode,HasDropEffect; public string Text; public string[] Files; }
'@

 Require-Desktop
 $report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash
 try{$originalClipboard=[BlinkCommandBarUiNative]::ReadClipboard($false)}catch{$notes.Add('Original Clipboard snapshot unavailable; restore will be conditional only.')}
 New-Item -ItemType Directory -Path $fixtureParent -Force|Out-Null
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture exists'}
 New-Item -ItemType Directory -Path $fixture|Out-Null;$fixtureCreated=$true
 foreach($directory in @($source,$copyTarget,$moveTarget,$other,$child)){New-Item -ItemType Directory -Path $directory|Out-Null}
 foreach($file in $initialFiles){[IO.File]::WriteAllText($file,$bytes[$file],[Text.UTF8Encoding]::new($false))}
 [IO.File]::SetAttributes((Join-Path $source $hiddenName),[IO.FileAttributes]::Hidden)
 [IO.File]::SetAttributes((Join-Path $source $systemName),[IO.FileAttributes]::System)
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false
 $psi.EnvironmentVariables.Remove('BLINK_INITIAL_PATH');$psi.EnvironmentVariables.Remove('BLINK_PHASE1A_UI_PROBE');$psi.EnvironmentVariables.Remove('BLINK_STARTUP_TELEMETRY')
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned Blink launch failed'}
 $report.process_id=$process.Id;Save-Report
 Wait-Ui 'new owned WinUI window' {$null -ne (Window)}
 [BlinkCommandBarUiNative]::OwnedWindow=$process.MainWindowHandle
 $report.window_dpi=[BlinkCommandBarUiNative]::GetDpiForWindow($process.MainWindowHandle);$originalSize=[BlinkCommandBarUiNative]::WindowSize($process.Id)
 Check '01 independent command row is below navigation and above both content panes' {
  Wait-Ui 'real CommandBar and all primary buttons' {if(-not (Bar)){return $false};$state=Bar-Snapshot;return @($primaryIds|Where-Object{-not $state.ContainsKey($_)}).Count -eq 0}
  Navigate $source $copyName
  $state=Record-Bar 'folder-ready';$barRect=(Bar).Current.BoundingRectangle;$addressRect=(Address).Current.BoundingRectangle;$entriesRect=(Entries).Current.BoundingRectangle
  if($barRect.Width -le 0 -or $barRect.Top -lt $addressRect.Bottom-2 -or $barRect.Bottom -gt $entriesRect.Top+2){throw 'CommandBar does not occupy the navigation/body row boundary'}
  if($barRect.Left -gt $entriesRect.Left-100){throw 'CommandBar remains nested in the file pane instead of its own body-wide row'}
  $share=@((Bar).FindAll([Windows.Automation.TreeScope]::Descendants,(Condition 'CommandShare')))
  if($share.Count){throw 'Unsupported Share placeholder is exposed'}
  $report.menu_snapshots+=@([pscustomobject]@{kind='geometry';bar=(Rect $barRect);address=(Rect $addressRect);entries=(Rect $entriesRect);primary_ids=$primaryIds});Save-Report
 }
 Check '02 zero single and mixed file folder selections derive correct primary capabilities' {
  $state=Record-Bar 'zero-selection'
  Assert-Enabled $state @('CommandNew','CommandSort','FolderViewMode') @('CommandCut','CommandCopy','CommandRename','CommandDelete')
  $currentClip=[BlinkCommandBarUiNative]::ReadClipboard($false)
  if($state['CommandPaste'].enabled -ne $currentClip.HasFileDrop){throw 'Paste readiness disagrees with actual standard Clipboard format'}
  Select-Rows @($copyName);Wait-Ui 'single file capabilities' {$st=Bar-Snapshot;return $st['CommandCut'].enabled -and $st['CommandCopy'].enabled -and $st['CommandRename'].enabled -and $st['CommandDelete'].enabled}
  $null=Record-Bar 'single-file'
  Select-Rows @($copyName,[IO.Path]::GetFileName($child));Wait-Ui 'mixed selection capabilities' {$st=Bar-Snapshot;return $st['CommandCut'].enabled -and $st['CommandCopy'].enabled -and -not $st['CommandRename'].enabled -and $st['CommandDelete'].enabled};$null=Record-Bar 'mixed-file-folder'
  Select-Rows @($copyName)
 }
 Check '03 New menu creates the exact owned directory through the existing name dialog' {
  if(Test-Path -LiteralPath $created){throw 'New target unexpectedly exists'}
  Open-Menu 'CommandNew' 'TopNewMenu';Act (Menu-Item 'TopNewMenu' 'TopNewFolder');$script:popupCache.Clear()
  Wait-Ui 'owned new folder editor' {$null -ne (Dialog 'NewFolderName')}
  $dialog=Dialog 'NewFolderName';Record-Dialog 'new-folder' $dialog;(Find-In $dialog 'NewFolderName').GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue([IO.Path]::GetFileName($created))
  Act (Dialog-Button $dialog 'PrimaryButton')
  Wait-Ui 'new folder really exists and ready toolbar resumes' {(Test-Path -LiteralPath $created -PathType Container) -and (Bar-Snapshot)['CommandNew'].enabled}
  if(@(Get-ChildItem -LiteralPath $created -Force).Count){throw 'New owned folder is not empty'}
  $report.file_effects+=@([pscustomobject]@{kind='new';path=$created;empty=$true});Save-Report
 }
 Check '04 Copy and Paste publish COPY and commit exact bytes to the selected destination' {
  Select-Rows @($copyName);$before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();Act (Bar-Control 'CommandCopy')
  Own-Clipboard @((Join-Path $source $copyName)) 1 $before
  Navigate $copyTarget
  Wait-Ui 'folder Paste becomes available for owned file drop' {(Bar-Snapshot)['CommandPaste'].enabled}
  Act (Bar-Control 'CommandPaste');Wait-Ui 'copy destination committed' {(Test-Path -LiteralPath (Join-Path $copyTarget $copyName)) -and (Bar-Snapshot)['CommandNew'].enabled}
  Verify-Bytes (Join-Path $source $copyName);Verify-Bytes (Join-Path $copyTarget $copyName)
  $report.file_effects+=@([pscustomobject]@{kind='copy';source_retained=$true;destination=(Join-Path $copyTarget $copyName);bytes_verified=$true});Save-Report
 }
 Check '05 Cut and Paste publish MOVE and remove only the exact owned source after commit' {
  Navigate $source $moveName;Select-Rows @($moveName);$before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();Act (Bar-Control 'CommandCut')
  Own-Clipboard @((Join-Path $source $moveName)) 2 $before
  Navigate $moveTarget;Wait-Ui 'move Paste ready' {(Bar-Snapshot)['CommandPaste'].enabled};Act (Bar-Control 'CommandPaste')
  Wait-Ui 'move destination committed and source removed' {(Test-Path -LiteralPath (Join-Path $moveTarget $moveName)) -and -not (Test-Path -LiteralPath (Join-Path $source $moveName)) -and (Bar-Snapshot)['CommandNew'].enabled}
  Verify-Bytes (Join-Path $moveTarget $moveName)
  $report.file_effects+=@([pscustomobject]@{kind='move';source_removed=$true;destination=(Join-Path $moveTarget $moveName);bytes_verified=$true});Save-Report
 }
 Check '06 Rename command changes only the owned filename and preserves every byte' {
  Navigate $source $renameName;Select-Rows @($renameName);Act (Bar-Control 'CommandRename');Wait-Ui 'owned Rename editor' {$null -ne (Dialog 'RenameName')}
  $dialog=Dialog 'RenameName';Record-Dialog 'rename' $dialog;$editor=Find-In $dialog 'RenameName'
  if((Value $editor) -cne $renameName){throw 'Rename did not capture the selected original name'}
  $editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($renamedName);Act (Dialog-Button $dialog 'PrimaryButton')
  Wait-Ui 'real owned rename committed' {(Test-Path -LiteralPath (Join-Path $source $renamedName)) -and -not (Test-Path -LiteralPath (Join-Path $source $renameName)) -and (Bar-Snapshot)['CommandNew'].enabled};Verify-Bytes (Join-Path $source $renamedName)
  $report.file_effects+=@([pscustomobject]@{kind='rename';original_name=$renameName;new_name=$renamedName;bytes_verified=$true});Save-Report
 }
 Check '07 Shift toolbar Delete cancels safely then explicitly confirms the exact owned permanent deletion' {
  Select-Rows @($deleteName);(Bar-Control 'CommandDelete').SetFocus();Key 0x20 2
  Wait-Ui 'explicit permanent-delete confirmation' {$null -ne (Dialog '' $deleteName)}
  $dialog=Dialog '' $deleteName;Record-Dialog 'permanent-delete' $dialog $deleteName;if((Element-Text $dialog) -notlike ('*'+$deleteName+'*')){throw 'Delete confirmation does not name the owned file'}
  Act (Dialog-Button $dialog 'CloseButton');Wait-Ui 'cancel closes permanent-delete confirmation' {-not (Dialog '' $deleteName)};Verify-Bytes (Join-Path $source $deleteName)
  Select-Rows @($deleteName);(Bar-Control 'CommandDelete').SetFocus();Key 0x20 2;Wait-Ui 'second explicit confirmation' {$null -ne (Dialog '' $deleteName)}
  $dialog=Dialog '' $deleteName;Record-Dialog 'permanent-delete' $dialog $deleteName;if((Element-Text $dialog) -notlike ('*'+$deleteName+'*')){throw 'Wrong owned confirmation target'};Act (Dialog-Button $dialog 'PrimaryButton')
  Wait-Ui 'confirmed own file deleted and queue ready' {-not (Test-Path -LiteralPath (Join-Path $source $deleteName)) -and (Bar-Snapshot)['CommandNew'].enabled}
  Verify-Bytes (Join-Path $source $copyName);Verify-Bytes (Join-Path $source $renamedName)
  $report.file_effects+=@([pscustomobject]@{kind='permanent-delete';path=(Join-Path $source $deleteName);cancel_retained_source=$true;confirmed_removed=$true;system_recycle_bin_enumerated=$false});Save-Report
 }
 Check '08 Sort menu changes actual order and all three View modes preserve the selected file and ordering' {
  Set-Sort 'Size';Set-Sort 'Descending'
  Wait-Ui 'largest real file sorts ahead of the smaller file leaves' {$leaves=@(Row-Names|Where-Object{$bytes.ContainsKey((Join-Path $source $_))});$leaves.Count -ge 3 -and $leaves[0] -ceq $sortName}
  Open-Menu 'CommandSort' 'TopSortMenu';if(-not (Toggle-State (Menu-Item 'TopSortMenu' 'TopSortSize')) -or -not (Toggle-State (Menu-Item 'TopSortMenu' 'TopSortDescending'))){throw 'Sort has incorrect actual checks'};Close-Menus
  Select-Rows @($copyName);$order=@(Row-Names)
  foreach($mode in @('Grid','List','Details')){
   View-Choice $mode
   if(-not (Same-Names @(Row-Names) $order) -or -not (Same-Names @(Selected-Names) @($copyName)) -or (Value (Address)) -ine $source){throw 'View changed path, source ordering or selection'}
  }
  Toggle-Hidden $true;if(@(Row-Names) -ccontains $systemName){throw 'SYSTEM entry became visible'};Toggle-Hidden $false
 }
 Check '09 More selection and path actions operate on the authoritative current row set' {
  More-Action 'CommandSelectAll';Wait-Ui 'More SelectAll selects all current rows' {$rows=Row-Snapshot;@($rows|Where-Object{$_.selected}).Count -eq $rows.Count -and $rows.Count -gt 1}
  More-Action 'CommandClearSelection';Wait-Ui 'More ClearSelection clears all current rows' {@(Selected-Names).Count -eq 0}
  Select-Rows @($copyName);$all=@(Row-Names);More-Action 'CommandInvertSelection';Wait-Ui 'More InvertSelection selects exactly the complement' {Same-Names (@(Selected-Names)|Sort-Object) (@($all|Where-Object{$_ -cne $copyName})|Sort-Object)}
  More-Action 'CommandInvertSelection';Wait-Ui 'second inversion restores original single selection' {Same-Names @(Selected-Names) @($copyName)}
  $before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();More-Action 'CommandCopyFullPath';Own-Text (Join-Path $source $copyName) $before
  $before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();More-Action 'CommandCopyParentPath';Own-Text $source $before
 }
 Check '10 More Properties reads the owned selection and Refresh updates the same committed folder' {
  Select-Rows @($copyName);More-Action 'CommandProperties';Wait-Ui 'owned Blink Properties dialog' {$null -ne (Popup 'ItemPropertiesDialog') -and (Visible (Popup 'ItemPropertiesDialog'))}
  $dialog=Popup 'ItemPropertiesDialog';Record-Dialog 'properties' $dialog $copyName;Wait-Ui 'Properties contains exact owned filename' {(Element-Text $dialog) -like ('*'+$copyName+'*')};Act (Dialog-Button $dialog 'CloseButton')
  Wait-Ui 'Properties closes normally' {-not (Visible (Popup 'ItemPropertiesDialog'))}
  [IO.File]::WriteAllText((Join-Path $source $refreshName),$bytes[(Join-Path $source $refreshName)],[Text.UTF8Encoding]::new($false))
  Wait-Ui 'folder ready before explicit Refresh' {(Bar-Snapshot)['CommandNew'].enabled};More-Action 'CommandRefresh'
  Wait-Ui 'Refresh reconciles own new file without changing directory' {(Value (Address)) -ieq $source -and @(Row-Names) -ccontains $refreshName -and (Bar-Snapshot)['CommandNew'].enabled};Verify-Bytes (Join-Path $source $refreshName)
 }
 Check '11 a narrow owned window uses native dynamic overflow and its real command remains usable' {
  Select-Rows @($copyName);$beforePath=Value (Address);$beforeSelection=@(Selected-Names)
  $overflowed=@();$foundWidth=0
  foreach($width in @(840,640,520)){
   [BlinkCommandBarUiNative]::Resize($process.Id,$width,[Math]::Max(500,$originalSize[1]));$null=$process.WaitForExit(150)
   $null=Open-More;$surface=Popup 'OverflowPopup';$inside=@();foreach($id in $primaryIds){$item=Find-In $surface $id $true;if($item){$inside+=@($id)}}
   if($inside.Count){$overflowed=$inside;$foundWidth=$width;break};Close-Menus
  }
  if(-not $overflowed.Count){throw 'No primary command entered native dynamic overflow at tested narrow widths'}
  $chosen=$null
  foreach($id in @('FolderViewMode','CommandSort','CommandNew','CommandCopy','CommandCut','CommandRename')){if($id -in $overflowed){$chosen=$id;break}}
  if(-not $chosen){throw 'Overflow has no applicable command for a safe action check'}
  $item=Find-In (Popup 'OverflowPopup') $chosen $true
  if(-not $item){throw 'Chosen primary is not a visible descendant of the real OverflowPopup'}
  $report.overflow_evidence=[ordered]@{width=$foundWidth;primary_commands_in_more=$overflowed;invoked=$chosen;narrow_layout=$null;window_restored=$false;restored_view=$null;list_checked_exclusively=$false};Save-Report
  if($chosen -eq 'FolderViewMode'){
   Act $item;Wait-Ui 'overflowed View opens its real menu' {$null -ne (Popup 'TopViewMenu')};Act (Menu-Item 'TopViewMenu' 'TopViewList');$script:entriesCache=$null;$script:popupCache.Clear()
   Wait-Ui 'overflowed View produces real ListView and zero detail column headers' {(Entries).Current.ClassName -ceq 'ListView' -and (Visible-ColumnCount) -eq 0}
   if((Value (Address)) -ine $beforePath -or -not (Same-Names @(Selected-Names) $beforeSelection)){throw 'Overflowed View changed the committed path or selected fixture'}
   $report.overflow_evidence.narrow_layout=[pscustomobject]@{entries_class=(Entries).Current.ClassName;visible_column_count=(Visible-ColumnCount);path=(Value (Address));selected=@(Selected-Names)};Save-Report
  }elseif($chosen -eq 'CommandSort'){
   Act $item;Wait-Ui 'overflowed Sort opens real choices' {$null -ne (Popup 'TopSortMenu')};Act (Menu-Item 'TopSortMenu' 'TopSortAscending');$script:popupCache.Clear()
  }elseif($chosen -eq 'CommandNew'){
   Act $item;Wait-Ui 'overflowed New opens real menu' {$null -ne (Popup 'TopNewMenu')};Act (Menu-Item 'TopNewMenu' 'TopNewFolder');Wait-Ui 'overflowed New opens owned editor' {$null -ne (Dialog 'NewFolderName')};Act (Dialog-Button (Dialog 'NewFolderName') 'CloseButton')
  }elseif($chosen -in @('CommandCopy','CommandCut')){
   $before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();Act $item;Own-Clipboard @((Join-Path $source $copyName)) $(if($chosen -eq 'CommandCut'){2}else{1}) $before
  }else{
   Act $item;Wait-Ui 'overflowed Rename opens own editor' {$null -ne (Dialog 'RenameName')};if((Value (Find-In (Dialog 'RenameName') 'RenameName')) -cne $copyName){throw 'Overflow Rename selected wrong item'};Act (Dialog-Button (Dialog 'RenameName') 'CloseButton')
  }
  Close-Menus;[BlinkCommandBarUiNative]::Resize($process.Id,$originalSize[0],$originalSize[1]);Wait-Ui 'owned original window size restored' {$size=[BlinkCommandBarUiNative]::WindowSize($process.Id);$size[0] -eq $originalSize[0] -and $size[1] -eq $originalSize[1]}
  $script:barElements.Clear()
  Wait-Ui 'restored-width real View peer returns to the primary command row' {$state=Bar-Snapshot;$state.ContainsKey('FolderViewMode') -and -not $state['FolderViewMode'].offscreen}
  $report.overflow_evidence.window_restored=$true
  if($chosen -eq 'FolderViewMode'){
   Wait-Ui 'restored real View peer reports List and its actual layout remains List' {$fresh=Find-In (Bar) 'FolderViewMode' $true;$fresh -and $viewLabels['List'] -ccontains $fresh.Current.HelpText -and (Entries).Current.ClassName -ceq 'ListView' -and (Visible-ColumnCount) -eq 0}
   $report.overflow_evidence.restored_view=Peer-Evidence (Find-In (Bar) 'FolderViewMode' $true)
   Open-Menu 'FolderViewMode' 'TopViewMenu';$checked=@();foreach($mode in @('Grid','List','Details')){if(Toggle-State (Menu-Item 'TopViewMenu' ('TopView'+$mode))){$checked+=@($mode)}}
   if($checked.Count -ne 1 -or $checked[0] -cne 'List'){throw 'Restored View menu does not exclusively check List after overflow action'}
   $report.overflow_evidence.list_checked_exclusively=$true;Close-Menus
  }
  if((Value (Address)) -ine $beforePath -or -not (Same-Names @(Selected-Names) $beforeSelection)){throw 'Overflow invocation or width restoration changed the committed path or selected fixture'};Save-Report
 }
 Check '12 a stale More selection peer cannot modify another committed directory' {
  More-Action 'CommandClearSelection';Select-Rows @($copyName);$null=Open-More;$old=Menu-Item 'OverflowPopup' 'CommandSelectAll';$pattern=$old.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
  Navigate $other $otherName;Wait-Ui 'new directory starts without stale selection' {@(Selected-Names).Count -eq 0};$outcome='returned'
  try{$pattern.Invoke()}catch{$outcome='rejected: '+$_.Exception.GetType().Name};if($process.WaitForExit(500)){throw 'Blink exited during stale menu callback'}
  if((Value (Address)) -ine $other -or @(Selected-Names).Count -ne 0){throw 'Stale More callback modified the new location'}
  $report.stale_peer=[ordered]@{outcome=$outcome;new_location_unchanged=$true;new_selection_empty=$true};Save-Report
  Navigate $source $copyName;Select-Rows @($copyName)
 }
 Check '13 ThisPC preserves virtual location and disables directory mutation and Sort commands' {
  Close-Menus;(Bar-Control 'FolderViewMode').SetFocus();Key 0x54 1;$script:entriesCache=$null
  Wait-Ui 'new tab commits real ThisPC' {$thisPcLabels -ccontains (Value (Address))}
  $state=Record-Bar 'ThisPC';Assert-Enabled $state @('FolderViewMode') @('CommandNew','CommandCut','CommandCopy','CommandPaste','CommandRename','CommandDelete','CommandSort')
  $null=Open-More;foreach($id in @('CommandSelectAll','CommandClearSelection','CommandInvertSelection')){if((Menu-Item 'OverflowPopup' $id).Current.IsEnabled){throw ('Virtual location unexpectedly supports selection mutation: '+$id)}};if(-not (Menu-Item 'OverflowPopup' 'CommandRefresh').Current.IsEnabled){throw 'ThisPC Refresh unavailable'};Close-Menus
  View-Choice 'Grid';if($thisPcLabels -cnotcontains (Value (Address))){throw 'ThisPC became a fake filesystem location'}
  (Bar-Control 'FolderViewMode').SetFocus();Key 0x57 1;$script:entriesCache=$null;Wait-Ui 'temporary ThisPC tab closes back to fixture' {(Value (Address)) -ieq $source}
 }
 Check '14 real indexed search selection uses original file parent semantics and contextual command capabilities' {
  Navigate $copyTarget $searchPasteName;Select-Rows @($searchPasteName);$before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();Act (Bar-Control 'CommandCopy');Own-Clipboard @((Join-Path $copyTarget $searchPasteName)) 1 $before
  Navigate $other $otherName
  $search=Search-Query;$search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($searchName);Assert-SearchQuery 'input-after-SetValue';$search.SetFocus();Assert-SearchQuery 'input-after-focus';Key 13;Assert-SearchQuery 'input-after-initial-Enter';Set-FixtureSearchScope
  $script:searchList=$null;$script:foundRow=$null
  Wait-Ui 'real unique owned indexed result (no synthetic injection)' {if((Value (Search-Query)) -cne $searchName){Assert-SearchQuery 'waiting-for-real-result-query-changed'};if(-not $script:searchList){$script:searchList=By-Id 'SearchResults'};if(-not $script:searchList -or -not (Visible $script:searchList)){return $false};$rows=@(Of-Type $script:searchList ([Windows.Automation.ControlType]::ListItem)|Where-Object{$_.Current.Name -ceq $searchName});if($rows.Count -eq 1){$script:foundRow=$rows[0];return $true};return $false} ([Math]::Max(45,$StepTimeoutSeconds))
  $state=Record-Bar 'search-zero-selection';Assert-Enabled $state @() @('CommandNew','CommandCut','CommandCopy','CommandPaste','CommandRename','CommandDelete','CommandSort','FolderViewMode')
  $foundRow=$script:foundRow;if((Element-Text $foundRow) -notlike ('*'+(Join-Path $source $searchName)+'*')){throw 'Search row does not represent the exact original fixture path'}
  $foundRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  Wait-Ui 'single real search result capabilities' {$st=Bar-Snapshot;return $st['CommandCut'].enabled -and $st['CommandCopy'].enabled -and $st['CommandPaste'].enabled -and $st['CommandRename'].enabled -and $st['CommandDelete'].enabled}
  $state=Record-Bar 'search-single-result';Assert-Enabled $state @('CommandCut','CommandCopy','CommandPaste','CommandRename','CommandDelete') @('CommandNew','CommandSort','FolderViewMode')
  $labels=@(Resource-Values 'Search.MenuPasteParent');if(-not @($labels|Where-Object{$state['CommandPaste'].name -like ('*'+$_+'*')}).Count){throw 'Search Paste does not explicitly identify the selected result parent'}
  $null=Open-More;foreach($id in @('CommandSelectAll','CommandClearSelection','CommandInvertSelection')){if((Menu-Item 'OverflowPopup' $id).Current.IsEnabled){throw 'Search incorrectly exposes local multi-selection actions'}};Close-Menus
  Act (Bar-Control 'CommandPaste');$script:entriesCache=$null
  Wait-Ui 'search Paste navigates to the result parent and commits owned bytes' {(Value (Address)) -ieq $source -and (Test-Path -LiteralPath (Join-Path $source $searchPasteName)) -and (Bar-Snapshot)['CommandNew'].enabled}
  Verify-Bytes (Join-Path $source $searchPasteName);Verify-Bytes (Join-Path $copyTarget $searchPasteName)
  if(Test-Path -LiteralPath (Join-Path $other $searchPasteName)){throw 'Search Paste incorrectly used the formerly committed folder'}
  $report.file_effects+=@([pscustomobject]@{kind='search-parent-paste';formerly_committed_folder=$other;actual_destination=$source;source_retained=$true;bytes_verified=$true})
  # Search again to exercise the search path command after Paste legitimately left search.
  $search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($searchName);Assert-SearchQuery 'second-input-after-SetValue';$search.SetFocus();Assert-SearchQuery 'second-input-after-focus';Key 13;Assert-SearchQuery 'second-input-after-initial-Enter';Set-FixtureSearchScope;$script:searchList=$null;$script:foundRow=$null
  Wait-Ui 'same exact real result after parent Paste' {if((Value (Search-Query)) -cne $searchName){Assert-SearchQuery 'waiting-for-real-result-query-changed'};if(-not $script:searchList){$script:searchList=By-Id 'SearchResults'};if(-not $script:searchList -or -not (Visible $script:searchList)){return $false};$rows=@(Of-Type $script:searchList ([Windows.Automation.ControlType]::ListItem)|Where-Object{$_.Current.Name -ceq $searchName});if($rows.Count -eq 1){$script:foundRow=$rows[0];return $true};return $false}
  $script:foundRow.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  Wait-Ui 'search path action is ready' {(Bar-Snapshot)['CommandCopy'].enabled}
  $before=[BlinkCommandBarUiNative]::GetClipboardSequenceNumber();More-Action 'CommandCopyFullPath';Own-Text (Join-Path $source $searchName) $before
  $report.search_evidence=[ordered]@{original_path=(Join-Path $source $searchName);formerly_committed_folder=$other;actual_paste_parent=$source;selected_result_actions=$true;paste_explicit_parent=$true;real_index=$true};Save-Report
  Close-Menus;$search.SetFocus();Key 27;$script:entriesCache=$null;Wait-Ui 'Escape leaves search for the committed result parent' {(Value (Address)) -ieq $source -and (Visible (Entries)) -and (Bar-Snapshot)['CommandNew'].enabled}
 }
 Check '15 native More supports Space Escape focus return and keyboard selection command' {
  More-Action 'CommandClearSelection';$more=Find-In (Bar) 'MoreButton' $true;if(-not $more){$more=Open-More;Close-Menus};$moreId=$more.GetRuntimeId()-join ','
  $more.SetFocus();Key 0x20;Wait-Ui 'Space opens native More menu' {$null -ne (Popup 'OverflowPopup')};Key 27
  Wait-Ui 'Escape closes More and returns original anchor focus' {$focused=[Windows.Automation.AutomationElement]::FocusedElement;return -not (Popup 'OverflowPopup') -and $focused.Current.ProcessId -eq $process.Id -and ($focused.GetRuntimeId()-join ',') -ceq $moreId}
  $more.SetFocus();Key 0x20;Wait-Ui 'keyboard More opens for actual selection command' {$null -ne (Popup 'OverflowPopup')};Key 0x24;Key 13
  Wait-Ui 'keyboard SelectAll affects the real current row set' {$rows=Row-Snapshot;@($rows|Where-Object{$_.selected}).Count -eq $rows.Count -and $rows.Count -gt 1}
  $report.keyboard_evidence+=@([pscustomobject]@{sequence='Space Escape; Space Home Enter';anchor_focus_return=$true;real_select_all=$true});Save-Report
 }
 Check '16 address Ctrl A Delete remain text editing and do not delete any selected file' {
  $beforeRows=@(Row-Names);$mode=(Bar-Control 'FolderViewMode').Current.HelpText;Key 0x4C 1;Wait-Ui 'Ctrl L focuses the owned address text' {$f=[Windows.Automation.AutomationElement]::FocusedElement;$f.Current.ProcessId -eq $process.Id -and $f.Current.ControlType -eq [Windows.Automation.ControlType]::Edit}
  Key 0x41 1;Key 0x2E;Wait-Ui 'Delete clears only selected address text' {(Value (Address)) -ceq ''}
  if(-not (Same-Names @(Row-Names) $beforeRows) -or (Bar-Control 'FolderViewMode').Current.HelpText -cne $mode){throw 'Address edit changed listing or view'}
  foreach($path in @((Join-Path $source $copyName),(Join-Path $source $renamedName),(Join-Path $source $searchName),(Join-Path $source $sortName))){Verify-Bytes $path}
  (Address-Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($source);(Bar-Control 'FolderViewMode').SetFocus()
 }
 $notes.Add('System Recycle Bin/default Delete, Explorer comparisons, full theme/DPI/language/IME/Narrator, device/UNC and extension scenarios were not exercised. Permanent deletion used only the named fixture and explicit Shift plus confirmation.')
 $report.status='PASS'
}catch{
 if($_.Exception.Message.StartsWith('COMMAND_BAR_UI_SKIP: ')){$report.status='SKIP';$report.failure=$_.Exception.Message.Substring(21)}else{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{Capture-State 'unexpected failure'}catch{}}
}finally{
 if($process){
  try{
   $process.Refresh()
   if(-not $process.HasExited -and $lastOwnedSequence -ne 0 -and $originalClipboard){try{$report.clipboard_restored=[BlinkCommandBarUiNative]::RestoreText($originalClipboard,$lastOwnedSequence,$process.Id);if(-not $report.clipboard_restored){$notes.Add('Original Clipboard was not a restorable bounded plain-text snapshot or its sequence changed; no unrelated Clipboard overwrite attempted.')}}catch{$notes.Add('Conditional Clipboard restore: '+$_.Exception.Message)}}
   if(-not $process.HasExited){if(-not $process.CloseMainWindow() -or -not $process.WaitForExit(10000)){throw ('Owned Blink did not close normally; PID '+$process.Id+' retained. No force termination.') }}
   $process.Refresh();$report.process_exited=$process.HasExited;if($process.HasExited){$report.exit_code=$process.ExitCode}
   if($report.status -eq 'PASS'){if($report.exit_code -ne 0){throw 'Owned Blink returned nonzero exit'};Record '17 owned Blink exits normally without force termination'}
  }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '17 owned process normal close failed' 'FAIL' $_.Exception.Message}
 }
 if($fixtureCreated){
  try{
   if($process -and -not $process.HasExited){throw 'Owned process still running; fixture retained'}
   $report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Fixture changed; nothing deleted: '+($report.fixture_ownership_failures -join '; '))}
   $report.fixture_bytes_removed=(Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not (Test-Path -LiteralPath $fixture)
   if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '18 exact owned paths and file bytes verified before fixture cleanup'}
  }catch{$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '18 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}
 }
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 if(Test-Path -LiteralPath $compilerTemp){try{$report.compiler_temp_bytes_removed=(Get-ChildItem -LiteralPath $compilerTemp -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not (Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.skipped=$report.status -in @('SKIP','PARTIAL');$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
 Write-Output ('UI COMMAND BAR '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL / '+@($checks|Where-Object{$_.status -eq 'SKIP'}).Count+' SKIP');Write-Output ('Report: '+$reportPath)
}
if($report.status -eq 'FAIL'){exit 1};if($report.status -ne 'PASS'){exit 2};exit 0
