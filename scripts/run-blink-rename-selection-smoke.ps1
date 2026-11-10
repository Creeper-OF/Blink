[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Binary='',[ValidateRange(5,90)][int]$StepTimeoutSeconds=30)
# Only a new owned Blink PID/HWND and exact GUID fixture. No global SendInput,
# Clipboard/service/user-file changes or forced termination. WM_CHAR is not IME.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$integration=Join-Path $project '.build\explorer-rename-selection\integration'
$fixtureParent=Join-Path $integration 'ui-fixtures';New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-rename-selection-report.json'
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
if(-not [IO.Path]::IsPathRooted($Binary)){$Binary=Join-Path $project $Binary};$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8);$fixture=Join-Path $fixtureParent ('owned-'+$guid);$source=Join-Path $fixture 'source'
$plain='file-'+$token+'.txt';$renamed='renamed-'+$token+'.txt';$full='all-'+$token+'.log'
$multi='archive-'+$token+'.tar.gz';$bare='README-'+$token;$dot='.env-'+$token
$unicode='资料-😀-'+$token+'.txt';$unicodeAfter='新名-😀-'+$token+'.txt';$searchName='search-'+$token+'.txt'
$directoryName='folder-'+$token+'.v1';$directoryAfterName='folder-'+$token+'.v2';$directory=Join-Path $source $directoryName;$directoryAfter=Join-Path $source $directoryAfterName
$child='child-'+$token+'.txt';$created=Join-Path $source ('new-'+$token);$allowedDirectories=@($fixture,$source,$directory,$directoryAfter,$created)
$allowedFiles=@{};foreach($name in @($plain,$renamed,$full)){$allowedFiles[(Join-Path $source $name)]='Blink rename '+$guid+' FILE'}
foreach($name in @($multi,$bare,$dot,$unicode,$unicodeAfter,$searchName)){$allowedFiles[(Join-Path $source $name)]='Blink rename '+$guid+' '+$(if($name -eq $unicodeAfter){$unicode}else{$name})}
foreach($path in @($directory,$directoryAfter)){$allowedFiles[(Join-Path $path $child)]='Blink rename '+$guid+' CHILD'}
$initialFiles=@((Join-Path $source $plain),(Join-Path $source $multi),(Join-Path $source $bare),(Join-Path $source $dot),(Join-Path $source $unicode),(Join-Path $source $searchName),(Join-Path $directory $child))
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$tempChanged=$false;$fixtureCreated=$false;$process=$null;$script:windowCache=$null;$script:currentStep='preflight';$script:lastTextInput=$null;$script:lastExpectedEditorValue=$null
$checks=[Collections.Generic.List[object]]::new();$notes=[Collections.Generic.List[string]]::new()
$report=[ordered]@{status='RUNNING';passed=$false;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;configuration=$Configuration;binary=$Binary;binary_sha256=$null;process_id=$null;process_exited=$false;exit_code=$null;exit_code_hex=$null;forced_termination=$false;before_close_alive=$null;close_requested=$false;close_request_utc=$null;close_request_result=$null;process_exit_observed_utc=$null;checks=@();current_step='preflight';failure=$null;notes=@();environment=$null;window_dpi=$null;desktop_preflight=$null;fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_bytes_removed=0;fixture_ownership_failures=@();compiler_temp_removed=$false;selection_evidence=@();text_inputs=@();editor_value_checks=@();rename_effects=@();focus_evidence=@();row_focus_evidence=@();search_evidence=$null;dialog_close_evidence=$null;last_ui_snapshot=$null;human_acceptance='NOT RUN';model_tests='NOT RUN by UI smoke';input_mode='Owned PID/HWND UIA and InputSite keyboard/WM_CHAR only';not_run=@('real IME composition/candidates','UNC/network','full Windows/theme/DPI/multi-monitor/Narrator matrix','inline rename and multi-selection rename','rename-success selection of new RowIdentity')}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());$report.notes=@($notes.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($true))}
function Record([string]$Label,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Label;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Check([string]$Label,[scriptblock]$Body){$script:currentStep=$Label;Save-Report;try{$null=&$Body;Record $Label}catch{Record $Label 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace);throw}}
function Window{$process.Refresh();if($process.HasExited){throw ('Owned Blink exited '+$process.ExitCode)};if(-not $script:windowCache -and $process.MainWindowHandle -ne [IntPtr]::Zero){$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)};return $script:windowCache}
function Visible($Element){return $null -ne $Element -and -not $Element.Current.IsOffscreen}
function Condition([string]$Id){return [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id)}
function Find-In($Root,[string]$Id){if(-not $Root){return $null};foreach($peer in @($Root.FindAll([Windows.Automation.TreeScope]::Descendants,(Condition $Id)))){if($peer.Current.ProcessId -eq $process.Id -and (Visible $peer)){return $peer}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){$p=By-Id $Id;if($p){return $p};foreach($hwnd in [BlinkRenameUiNative]::OwnedTopWindows($process.Id)){$top=[Windows.Automation.AutomationElement]::FromHandle($hwnd);if($top.Current.ProcessId -ne $process.Id){throw 'Owned HWND/UIA mismatch'};if($top.Current.AutomationId -ceq $Id -and (Visible $top)){return $top};$p=Find-In $top $Id;if($p){return $p}};return $null}
function Value($Element){if(-not $Element){throw 'Owned value peer unavailable'};return $Element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Peer($Element){if(-not $Element){return [pscustomobject]@{found=$false}};$c=$Element.Current;return [pscustomobject]@{found=$true;id=$c.AutomationId;class=$c.ClassName;role=$c.ControlType.ProgrammaticName;name=$c.Name;enabled=$c.IsEnabled;offscreen=$c.IsOffscreen;has_keyboard_focus=$c.HasKeyboardFocus;pid=$c.ProcessId;runtime_id=($Element.GetRuntimeId()-join ',')}}
function Focus-In([string]$Id){$p=[Windows.Automation.AutomationElement]::FocusedElement;for($i=0;$p -and $i -lt 16;$i++){if($p.Current.ProcessId -ne $process.Id){return $false};if($p.Current.AutomationId -ceq $Id){return $true};$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)};return $false}
function Focus-Evidence([string]$Why){$d=[BlinkRenameUiNative]::DesktopState();$f=[Windows.Automation.AutomationElement]::FocusedElement;return [pscustomobject]@{reason=$Why;utc=[DateTime]::UtcNow.ToString('o');foreground_pid=$d.ForegroundPid;foreground_hwnd=$d.Foreground;foreground_class=$d.ForegroundClass;actual_focus=$(if($f){[pscustomobject]@{pid=$f.Current.ProcessId;id=$f.Current.AutomationId;class=$f.Current.ClassName;has_keyboard_focus=$f.Current.HasKeyboardFocus}}else{$null})}}
function Utf16-Evidence([string]$Text){
 $units=@();for($index=0;$index -lt $Text.Length;$index++){$units+=@([pscustomobject]@{index=$index;decimal=[int][char]$Text[$index];hex=('0x{0:X4}' -f [int][char]$Text[$index])})}
 return [pscustomobject]@{text=$Text;code_unit_count=$Text.Length;code_units=$units;encoding='UTF16 code units; no IME claim'}
}
function Capture-State([string]$Why){
 $state=[ordered]@{why=$Why;utc=[DateTime]::UtcNow.ToString('o');address=$null;rows=@();selected=@();rename=$null;new_folder=$null;rename_value=$null;rename_utf16=$null;new_folder_value=$null;new_folder_utf16=$null;last_expected_editor_value=$script:lastExpectedEditorValue;last_text_input=$script:lastTextInput;focus=$null;focus_ancestor_chain=@();commands=@();ready_parts=$null;errors=@()}
 try{
  $state.address=(By-Id 'AddressBar').Current.HelpText;$state.rows=@(Row-Names);$state.selected=@(Selected-Names)
  $state.rename=Peer (Owned-Id 'RenameName');$state.new_folder=Peer (Owned-Id 'NewFolderName');$state.focus=Focus-Evidence $Why
  $renameEditor=Owned-Id 'RenameName';if($renameEditor){$state.rename_value=Value $renameEditor;$state.rename_utf16=Utf16-Evidence $state.rename_value}
  $newEditor=Owned-Id 'NewFolderName';if($newEditor){$state.new_folder_value=Value $newEditor;$state.new_folder_utf16=Utf16-Evidence $state.new_folder_value}
 }catch{$state.errors+=@($_.Exception.Message)}
 try{
  $focus=[Windows.Automation.AutomationElement]::FocusedElement
  for($depth=0;$focus -and $depth -lt 20;$depth++){
   $current=$focus.Current
   if($current.ProcessId -ne $process.Id){$state.focus_ancestor_chain+=@([pscustomobject]@{depth=$depth;pid=$current.ProcessId;outside_owned_process=$true});break}
   $state.focus_ancestor_chain+=@([pscustomobject]@{depth=$depth;id=$current.AutomationId;class=$current.ClassName;role=$current.ControlType.ProgrammaticName;pid=$current.ProcessId;has_keyboard_focus=$current.HasKeyboardFocus;runtime_id=($focus.GetRuntimeId()-join ',')})
   $focus=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($focus)
  }
 }catch{$state.errors+=@('focus ancestors: '+$_.Exception.Message)}
 try{
  foreach($id in @('CommandNew','CommandSort','CommandRename')){
   $peer=By-Id $id
   if(-not $peer){foreach($candidate in @((Window).FindAll([Windows.Automation.TreeScope]::Descendants,(Condition $id)))){if($candidate.Current.ProcessId -eq $process.Id){$peer=$candidate;break}}}
   $state.commands+=@([pscustomobject]@{id=$id;found=($null -ne $peer);enabled=$(if($peer){$peer.Current.IsEnabled}else{$null});visible=(Visible $peer);peer=(Peer $peer)})
  }
  $entriesPeer=Entries;$newPeer=By-Id 'CommandNew';$sortPeer=By-Id 'CommandSort'
  $state.ready_parts=[pscustomobject]@{entries_found=($null -ne $entriesPeer);entries_visible=(Visible $entriesPeer);entries_enabled=$(if($entriesPeer){$entriesPeer.Current.IsEnabled}else{$null});command_new_found=($null -ne $newPeer);command_new_enabled=$(if($newPeer){$newPeer.Current.IsEnabled}else{$null});command_sort_found=($null -ne $sortPeer);command_sort_enabled=$(if($sortPeer){$sortPeer.Current.IsEnabled}else{$null});ready=((Visible $entriesPeer) -and $newPeer.Current.IsEnabled -and $sortPeer.Current.IsEnabled)}
 }catch{$state.errors+=@('ready/commands: '+$_.Exception.Message)}
 $report.last_ui_snapshot=$state;Save-Report;return $state
}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$watch=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why+'; '+$process.ExitCode)}}while($watch.Elapsed.TotalSeconds -lt $Seconds);try{$null=Capture-State $Why}catch{};throw ('Timed out: '+$Why+'; '+$last)}
function Require-Desktop{$d=[BlinkRenameUiNative]::DesktopState();$report.desktop_preflight=$d;if(-not $d.InputReady){throw ('GUI precondition not met: '+$d.Reason)}}
function Require-OwnedFocus([string]$Id){$a=Owned-Id $Id;if(-not (Visible $a) -or -not $a.Current.IsEnabled){throw ('Owned focus anchor unavailable '+$Id)};$a.SetFocus();Wait-Ui 'actual owned foreground and focused region' {$d=[BlinkRenameUiNative]::DesktopState();$f=[Windows.Automation.AutomationElement]::FocusedElement;$d.InputReady -and $d.ForegroundPid -eq $process.Id -and $f -and $f.Current.ProcessId -eq $process.Id -and $f.Current.HasKeyboardFocus -and (Focus-In $Id)};$report.focus_evidence+=@(Focus-Evidence $Id);Save-Report}
function Key([uint32]$Code,[int]$Modifiers=0){Require-Desktop;[BlinkRenameUiNative]::Key($process.Id,$Code,$Modifiers)}
function Type-Text([string]$Text,[string]$Id='RenameName'){
 if(-not (Focus-In $Id) -or -not (Owned-Id $Id).Current.HasKeyboardFocus){throw 'Actual owned editor focus required'}
 $before=Value (Owned-Id $Id);$inputUnits=Utf16-Evidence $Text
 $record=[ordered]@{editor_id=$Id;started_utc=[DateTime]::UtcNow.ToString('o');completed_utc=$null;before=(Utf16-Evidence $before);input=$inputUnits;planned_messages=@($inputUnits.code_units|ForEach-Object{[pscustomobject]@{index=$_.index;message='WM_CHAR 0x0102';wparam_decimal=$_.decimal;wparam_hex=$_.hex;lparam='0x00000001'}});after=$null;status='ABOUT TO SEND';error=$null;ime='NOT RUN'}
 $script:lastTextInput=$record;$report.text_inputs+=@($record);Save-Report
 try{[BlinkRenameUiNative]::Text($process.Id,$Text);$record.status='NATIVE SEND LOOP COMPLETED'}catch{$record.status='NATIVE SEND FAILED';$record.error=$_.Exception.Message;throw}
 finally{$record.completed_utc=[DateTime]::UtcNow.ToString('o');try{$record.after=Utf16-Evidence (Value (Owned-Id $Id))}catch{$record.error=(@($record.error,$_.Exception.Message)|Where-Object{$_})-join '; '};Save-Report}
}
function Act($Element){if(-not (Visible $Element) -or $Element.Current.ProcessId -ne $process.Id -or -not $Element.Current.IsEnabled){throw 'Visible enabled owned action required'};$Element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}
function Entries{return By-Id 'FolderEntries'}
function Rows([string]$Id='FolderEntries'){$l=By-Id $Id;if(-not $l){return @()};return @($l.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_)})}
function Row-Names{return @(Rows|ForEach-Object{$_.Current.Name})}
function Selected-Names{if(-not (Entries)){return @()};return @((Entries).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}
function Same-Names([string[]]$First,[string[]]$Second){return $First.Count -eq $Second.Count -and ($First-join "`n") -ceq ($Second-join "`n")}
function Select-Row([string]$Name,[string]$ListId='FolderEntries'){
 $rowMatches=@(Rows $ListId|Where-Object{$_.Current.Name -ceq $Name})
 if($rowMatches.Count -ne 1){throw ('Exact owned row not unique '+$Name)}
 $row=$rowMatches[0];$rowId=$row.GetRuntimeId()-join ','
 $evidence=[ordered]@{name=$Name;list_id=$ListId;row_runtime_id=$rowId;row=(Peer $row);row_keyboard_focusable=$row.Current.IsKeyboardFocusable;list_keyboard_focusable=$(if(By-Id $ListId){(By-Id $ListId).Current.IsKeyboardFocusable}else{$null});actual_focus=$null;focus_ancestor_chain=@();foreground_pid=$null;verified=$false;failure=$null}
 $report.row_focus_evidence+=@($evidence);Save-Report
 try{
  $row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  # ListView containers may decline UIA SetFocus; target the actual owned row.
  $row.SetFocus()
  Wait-Ui 'actual owned row focus inside expected results/file list' {
   $desktop=[BlinkRenameUiNative]::DesktopState();$focus=[Windows.Automation.AutomationElement]::FocusedElement
   $insideRow=$false;$insideList=$false;$chain=@();$cursor=$focus
   for($depth=0;$cursor -and $depth -lt 20;$depth++){
    $current=$cursor.Current
    if($current.ProcessId -ne $process.Id){$chain+=@([pscustomobject]@{depth=$depth;pid=$current.ProcessId;outside_owned_process=$true});break}
    $runtimeId=$cursor.GetRuntimeId()-join ','
    $chain+=@([pscustomobject]@{depth=$depth;pid=$current.ProcessId;id=$current.AutomationId;class=$current.ClassName;role=$current.ControlType.ProgrammaticName;runtime_id=$runtimeId;has_keyboard_focus=$current.HasKeyboardFocus;keyboard_focusable=$current.IsKeyboardFocusable})
    if($runtimeId -ceq $rowId){$insideRow=$true};if($current.AutomationId -ceq $ListId){$insideList=$true}
    $cursor=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($cursor)
   }
   $evidence.foreground_pid=$desktop.ForegroundPid;$evidence.actual_focus=$(if($focus -and $focus.Current.ProcessId -eq $process.Id){Peer $focus}else{$null});$evidence.focus_ancestor_chain=$chain
   $valid=$desktop.InputReady -and $desktop.ForegroundPid -eq $process.Id -and $focus -and $focus.Current.ProcessId -eq $process.Id -and $focus.Current.HasKeyboardFocus -and $insideRow -and $insideList
   $evidence.verified=$valid;return $valid
  }
 }catch{$evidence.failure=$_.Exception.Message;try{$evidence.actual_focus=Focus-Evidence 'row SetFocus failure'}catch{};throw}
 finally{Save-Report}
 return $row
}
function Ready{return (Visible (Entries)) -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}
function Navigate-Source{Require-OwnedFocus 'AddressHistoryButton';Key 0x4C 1;Wait-Ui 'AddressEditor revealed' {$null -ne (By-Id 'AddressEditor')};$e=By-Id 'AddressEditor';$e.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($source);$e.SetFocus();Key 13;Wait-Ui 'exact owned directory and listing committed' {(By-Id 'AddressBar').Current.HelpText -ieq $source -and (Ready) -and @(Row-Names) -ccontains $searchName}}
function Dialog([string]$Id){$e=Owned-Id $Id;if(-not $e){return $null};$windowId=(Window).GetRuntimeId()-join ',';$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($e);for($i=0;$p -and $i -lt 14;$i++){if($p.Current.ProcessId -ne $process.Id -or ($p.GetRuntimeId()-join ',') -ceq $windowId){break};if((Visible $p) -and (Find-In $p 'PrimaryButton') -and (Find-In $p 'CloseButton')){return $p};$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)};return $null}
function Selection-Evidence([string]$Id,[string]$Expected,[string]$Why){$e=Owned-Id $Id;$pattern=$null;$texts=@();$supported=$e.TryGetCurrentPattern([Windows.Automation.TextPattern]::Pattern,[ref]$pattern);if($supported){$texts=@($pattern.GetSelection()|ForEach-Object{$_.GetText(-1)})};$same=$supported -and $texts.Count -eq 1 -and $texts[0] -ceq $Expected;$report.selection_evidence+=@([pscustomobject]@{why=$Why;editor=(Peer $e);value=(Value $e);text_pattern_supported=$supported;selected_text=$texts;expected=$Expected;comparison=$(if($supported){$same}else{$null});fallback='If unsupported, this same check must type replacement text and verify exact editor value'});Save-Report;if($supported -and -not $same){throw ('Native selection mismatch '+$Why+'; actual='+($texts-join '|')+'; expected='+$Expected)}}
function Assert-EditorValue([string]$Id,[string]$Expected){
 $script:lastExpectedEditorValue=[pscustomobject]@{editor_id=$Id;expected=(Utf16-Evidence $Expected)}
 $record=[ordered]@{editor_id=$Id;started_utc=[DateTime]::UtcNow.ToString('o');expected=(Utf16-Evidence $Expected);actual=$null;comparison=$null;error=$null}
 $report.editor_value_checks+=@($record);Save-Report
 try{Wait-Ui ('exact '+$Id+' text after actual typing') {(Value (Owned-Id $Id)) -ceq $Expected}}
 finally{try{$actual=Value (Owned-Id $Id);$record.actual=Utf16-Evidence $actual;$record.comparison=$actual -ceq $Expected}catch{$record.error=$_.Exception.Message};Save-Report}
}
function Open-Rename([string]$Name,[string]$Expected,[string]$Entry='F2'){$null=Select-Row $Name;if($Entry -eq 'F2'){Key 0x71}else{Act (By-Id 'CommandRename')};Wait-Ui 'real rename dialog with original text and actual editor focus' {$e=Owned-Id 'RenameName';$e -and (Dialog 'RenameName') -and (Value $e) -ceq $Name -and (Focus-In 'RenameName') -and $e.Current.HasKeyboardFocus};Selection-Evidence 'RenameName' $Expected ($Entry+' '+$Name)}
function Cancel-Rename([string]$Name){Key 27;Wait-Ui 'Escape cancels and returns focus to current parent list' {-not (Owned-Id 'RenameName') -and (Ready) -and (Focus-In 'FolderEntries') -and @(Row-Names) -ccontains $Name};$report.focus_evidence+=@(Focus-Evidence 'Escape returns FolderEntries');Save-Report}
function Identity([string]$Path){if(-not $allowedFiles.ContainsKey($Path) -and $Path -notin $allowedDirectories){throw 'Identity outside GUID whitelist'};return [BlinkRenameUiNative]::Identity($Path)}
function Expected-Bytes([string]$Path){if(-not $allowedFiles.ContainsKey($Path)){throw 'Unowned expected-byte lookup'};return [Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes([string]$allowedFiles[$Path]))}
function Verify-Bytes([string]$Path){if(-not $allowedFiles.ContainsKey($Path) -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) -cne (Expected-Bytes $Path)){throw ('Owned UTF8 bytes changed '+$Path)}}
function Commit-Rename([string]$Old,[string]$New,[string]$InitialIdentity){Key 13;Wait-Ui 'rename committed with old name absent and exact new row' {-not (Owned-Id 'RenameName') -and -not (Test-Path -LiteralPath $Old) -and (Test-Path -LiteralPath $New) -and (Ready) -and @(Row-Names) -ccontains [IO.Path]::GetFileName($New)};if((Identity $New) -cne $InitialIdentity){throw 'Owned rename changed identity'};if($allowedFiles.ContainsKey($New)){Verify-Bytes $New};$report.rename_effects+=@([pscustomobject]@{old=$Old;new=$New;same_identity=$true;bytes_verified=$allowedFiles.ContainsKey($New)});Save-Report}
function Ownership-Failures{$bad=[Collections.Generic.List[string]]::new();$pending=[Collections.Generic.Stack[string]]::new();$r=Get-Item -LiteralPath $fixture -Force;if(-not $r.PSIsContainer -or ($r.Attributes -band [IO.FileAttributes]::ReparsePoint)){return @('Fixture root ownership/type changed')};$pending.Push($fixture);while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){$bad.Add('Reparse '+$item.FullName);continue};if($item.PSIsContainer){if($item.FullName -notin $allowedDirectories){$bad.Add('Unknown directory '+$item.FullName)}else{$pending.Push($item.FullName)}}elseif(-not $allowedFiles.ContainsKey($item.FullName)){$bad.Add('Unknown file '+$item.FullName)}else{try{Verify-Bytes $item.FullName}catch{$bad.Add($_.Exception.Message)}}}};return @($bad.ToArray())}
try{
 New-Item -ItemType Directory -Path $compilerTemp -Force|Out-Null;$env:TEMP=$compilerTemp;$env:TMP=$compilerTemp;$tempChanged=$true
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkRenameUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkRenameUiNative {
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
    public static BlinkRenameUiDesktop DesktopState() {
        var result=new BlinkRenameUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string p,uint a,uint s,IntPtr z,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileInformationByHandleEx(Microsoft.Win32.SafeHandles.SafeFileHandle f,int k,byte[] b,uint n);
    public static string Identity(string path){using(var f=CreateFileW(path,0x80,7,IntPtr.Zero,3,0x02000000,IntPtr.Zero)){if(f.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());var b=new byte[24];if(!GetFileInformationByHandleEx(f,18,b,24))throw new Win32Exception(Marshal.GetLastWin32Error());return Convert.ToBase64String(b);}}
    public static void Text(int pid,string text){Own(OwnedWindow,pid);var w=InputSite(OwnedWindow);Own(w,pid);foreach(char c in text)Message(w,pid,0x102u,(uint)c,new IntPtr(1));}
}
'@
 Require-Desktop;$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Ordinary user UI smoke required'}
 $report.environment=[ordered]@{ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;os_64bit=[Environment]::Is64BitOperatingSystem;is_administrator=$false;actual_theme='NOT CAPTURED';ime='NOT TESTED'};$report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash
 New-Item -ItemType Directory -Path $fixtureParent -Force|Out-Null;if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture already exists'};New-Item -ItemType Directory -Path $fixture|Out-Null;$fixtureCreated=$true
 foreach($path in @($source,$directory)){New-Item -ItemType Directory -Path $path|Out-Null};foreach($path in $initialFiles){[IO.File]::WriteAllText($path,$allowedFiles[$path],[Text.UTF8Encoding]::new($false))}
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false;foreach($name in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')){$psi.EnvironmentVariables.Remove($name)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned launch failed'};$report.process_id=$process.Id;$ownedProcessHandle=$process.Handle;Save-Report
 Wait-Ui 'new owned Blink window' {$null -ne (Window)};[BlinkRenameUiNative]::OwnedWindow=$process.MainWindowHandle;$report.window_dpi=[BlinkRenameUiNative]::GetDpiForWindow($process.MainWindowHandle)

 Check '01 navigate to the exact GUID fixture and verify real directory contents' {Navigate-Source;foreach($path in $initialFiles){Verify-Bytes $path};$null=Capture-State 'owned listing ready'}
 Check '02 F2 selects only a file stem and Escape leaves its source unchanged' {Open-Rename $plain ('file-'+$token);Type-Text ('cancel-'+$token);Assert-EditorValue 'RenameName' ('cancel-'+$token+'.txt');Cancel-Rename $plain;Verify-Bytes (Join-Path $source $plain)}
 Check '03 typed replacement and Enter preserve the final extension bytes and identity' {$old=Join-Path $source $plain;$id=Identity $old;Open-Rename $plain ('file-'+$token);Type-Text ('renamed-'+$token);Assert-EditorValue 'RenameName' $renamed;Commit-Rename $old (Join-Path $source $renamed) $id}
 Check '04 a multi-dot filename excludes only its final suffix' {Open-Rename $multi ('archive-'+$token+'.tar');Type-Text ('probe-'+$token);Assert-EditorValue 'RenameName' ('probe-'+$token+'.gz');Cancel-Rename $multi;Verify-Bytes (Join-Path $source $multi)}
 Check '05 extensionless files and a leading-only dot name select their whole name' {foreach($name in @($bare,$dot)){Open-Rename $name $name;Type-Text ('probe-'+$token);Assert-EditorValue 'RenameName' ('probe-'+$token);Cancel-Rename $name;Verify-Bytes (Join-Path $source $name)}}
 Check '06 Unicode including surrogate pairs survives stem replacement and commit' {$old=Join-Path $source $unicode;$id=Identity $old;Open-Rename $unicode ('资料-😀-'+$token);Type-Text ('新名-😀-'+$token);Assert-EditorValue 'RenameName' $unicodeAfter;Commit-Rename $old (Join-Path $source $unicodeAfter) $id}
 Check '07 a dotted directory selects all text and keeps its child identity and bytes' {$id=Identity $directory;$childId=Identity (Join-Path $directory $child);Open-Rename $directoryName $directoryName;Type-Text $directoryAfterName;Assert-EditorValue 'RenameName' $directoryAfterName;Commit-Rename $directory $directoryAfter $id;Verify-Bytes (Join-Path $directoryAfter $child);if((Identity (Join-Path $directoryAfter $child)) -cne $childId){throw 'Directory rename changed child identity'}}
 Check '08 explicit Ctrl A can replace the full name including extension' {$old=Join-Path $source $renamed;$id=Identity $old;Open-Rename $renamed ('renamed-'+$token);Key 0x41 1;Selection-Evidence 'RenameName' $renamed 'explicit Ctrl A';Type-Text $full;Assert-EditorValue 'RenameName' $full;Commit-Rename $old (Join-Path $source $full) $id}
 Check '09 Delete modifies the selected editor text and never deletes the file' {$rows=@(Row-Names);$id=Identity (Join-Path $source $multi);Open-Rename $multi ('archive-'+$token+'.tar');Key 0x2E;Assert-EditorValue 'RenameName' '.gz';if(-not(Test-Path -LiteralPath (Join-Path $source $multi)) -or -not(Same-Names @(Row-Names) $rows)){throw 'Text Delete changed source filesystem/listing'};Cancel-Rename $multi;Verify-Bytes (Join-Path $source $multi);if((Identity (Join-Path $source $multi)) -cne $id){throw 'Text Delete changed identity'}}
 Check '10 the top Rename command uses the same initial selection as F2' {Open-Rename $full ('all-'+$token) 'Command';Type-Text ('menu-'+$token);Assert-EditorValue 'RenameName' ('menu-'+$token+'.log');Cancel-Rename $full;Verify-Bytes (Join-Path $source $full)}
 Check '11 New Folder retains whole-name selection and creates only its owned directory' {Act (By-Id 'CommandNew');Wait-Ui 'real New folder menu leaf' {$null -ne (Owned-Id 'TopNewFolder')};Act (Owned-Id 'TopNewFolder');Wait-Ui 'NewFolderName actual editor focus' {$e=Owned-Id 'NewFolderName';$e -and (Focus-In 'NewFolderName') -and $e.Current.HasKeyboardFocus};$text=Value (Owned-Id 'NewFolderName');if(-not $text){throw 'New folder default name is empty'};Selection-Evidence 'NewFolderName' $text 'new folder default';Type-Text ([IO.Path]::GetFileName($created)) 'NewFolderName';Assert-EditorValue 'NewFolderName' ([IO.Path]::GetFileName($created));Key 13;Wait-Ui 'new owned folder committed' {-not(Owned-Id 'NewFolderName') -and (Test-Path -LiteralPath $created -PathType Container) -and (Ready) -and @(Row-Names) -ccontains [IO.Path]::GetFileName($created)}}
 Check '12 real indexed search F2 locates the parent then opens shared Rename' {
  Require-OwnedFocus 'SearchQuery';$q=By-Id 'SearchQuery';$q.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($searchName);Key 13
  Wait-Ui 'real exact indexed result, no injected rows' {$rows=@(Rows 'SearchResults'|Where-Object{$_.Current.Name -ceq $searchName});$rows.Count -eq 1} ([Math]::Max(60,$StepTimeoutSeconds))
  $null=Select-Row $searchName 'SearchResults';$report.search_evidence=[pscustomobject]@{query=$searchName;real_exact_result=$true;parent=$source;index_performance='NOT TESTED';pipeline='existing F2 locates parent before ordinary Rename'};Save-Report;Key 0x71
  Wait-Ui 'search F2 parent/list and shared RenameName' {(By-Id 'AddressBar').Current.HelpText -ieq $source -and (Visible(Entries)) -and -not(By-Id 'SearchResults') -and (Owned-Id 'RenameName') -and (Value(Owned-Id 'RenameName')) -ceq $searchName -and (Focus-In 'RenameName')}
  Selection-Evidence 'RenameName' ('search-'+$token) 'real search F2';Type-Text ('search-cancel-'+$token);Assert-EditorValue 'RenameName' ('search-cancel-'+$token+'.txt');Cancel-Rename $searchName;Verify-Bytes (Join-Path $source $searchName)
 }
 Check '13 retain the live Rename editor and unsubmitted draft for normal close' {Open-Rename $full ('all-'+$token);Type-Text ('unsubmitted-'+$token);Assert-EditorValue 'RenameName' ('unsubmitted-'+$token+'.log');$report.dialog_close_evidence=[pscustomobject]@{editor=(Peer(Owned-Id 'RenameName'));value=(Value(Owned-Id 'RenameName'));actual_focus=(Focus-Evidence 'rename draft before close');escape_sent=$false;enter_sent=$false;source_exists=(Test-Path -LiteralPath (Join-Path $source $full));draft_name=('unsubmitted-'+$token+'.log')};Verify-Bytes (Join-Path $source $full);Save-Report}
 $report.status='PASS'
}catch{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{$null=Capture-State 'unexpected failure'}catch{}}
finally{
 if($process){try{
  $process.Refresh();$report.before_close_alive=-not $process.HasExited
  if(-not $process.HasExited){$report.close_requested=$true;$report.close_request_utc=[DateTime]::UtcNow.ToString('o');$report.close_request_result=$process.CloseMainWindow();if(-not $report.close_request_result -or -not $process.WaitForExit(10000)){throw ('Owned process still running; no force termination. PID '+$process.Id)}}
  $process.Refresh();$report.process_exited=$process.HasExited;if($process.HasExited){$report.process_exit_observed_utc=[DateTime]::UtcNow.ToString('o');$report.exit_code=$process.ExitCode;$report.exit_code_hex=('0x{0:X8}' -f [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$process.ExitCode),0));if($process.ExitCode -ne 0){throw ('Owned Blink abnormal exit '+$report.exit_code_hex)}}
  if($report.status -eq 'PASS'){Record '14 owned Blink closes with live name dialog and exits zero without force'}
 }catch{$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '14 owned process normal close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{
  if($process -and -not $process.HasExited){throw 'Owned process still running; fixture retained'};$report.fixture_ownership_failures=@(Ownership-Failures);if($report.fixture_ownership_failures.Count){throw ('Changed/foreign fixture; nothing deleted: '+($report.fixture_ownership_failures-join '; '))}
  $report.fixture_bytes_removed=(Get-ChildItem -LiteralPath $fixture -Force -Recurse -File|Measure-Object Length -Sum).Sum;Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture);if(-not $report.fixture_removed){throw 'Owned fixture remains'};if($report.status -eq 'PASS'){Record '15 exact owned paths identity and UTF8 bytes verified before cleanup'}
 }catch{$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.status='FAIL';$report.failure=(@($report.failure,$_.Exception.ToString())|Where-Object{$_})-join "`n";Record '15 changed or in-use fixture preserved' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp};if(Test-Path -LiteralPath $compilerTemp){try{Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$notes.Add('Owned compiler temp cleanup: '+$_.Exception.Message)}}
 $report.passed=$report.status -eq 'PASS';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report;Write-Output ('UI RENAME SELECTION '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL');Write-Output ('Report: '+$reportPath)
}
if($report.status -ne 'PASS'){exit 1};exit 0
