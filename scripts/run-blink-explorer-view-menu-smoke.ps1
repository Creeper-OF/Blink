[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Binary = '',
    [ValidateRange(5,60)][int]$StepTimeoutSeconds = 25
)

# View/Show compatibility smoke for the CommandBar presentation.
# Uses this slice's report path; preserves the older verified View/Show evidence.
# No build/service changes, Clipboard, global SendInput or system preferences.
# Exit 0 PASS, 1 FAIL, 2 SKIP/PARTIAL. Durable report after every check.
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$project = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $project '.build\explorer-address-bar'
$integration = Join-Path $stage 'integration'
$fixtureParent = Join-Path $stage 'ui-view-fixtures'
New-Item -ItemType Directory -Path $integration -Force | Out-Null
$reportPath = Join-Path $integration 'ui-view-compat-report.json'
if (-not $Binary) { $Binary = Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe') }
if (-not [IO.Path]::IsPathRooted($Binary)) { $Binary = Join-Path $project $Binary }
$Binary = [IO.Path]::GetFullPath($Binary)
$guid = [Guid]::NewGuid().ToString('N'); $token = $guid.Substring(0,8)
$fixture = Join-Path $fixtureParent ('owned-'+$guid)
$mainFolder = Join-Path $fixture 'main'; $otherFolder = Join-Path $fixture 'other'; $emptyFolder = Join-Path $fixture 'empty'
$marker = 'vm-'+$token+'-a.txt'; $largeName = 'vm-'+$token+'-b.bin'
$hiddenName = 'vm-'+$token+'-hidden.txt'; $hiddenDirectoryName = 'vm-'+$token+'-hidden-dir'
$systemName = 'vm-'+$token+'-system.txt'; $protectedName = 'vm-'+$token+'-protected.txt'
$otherMarker = 'vm-'+$token+'-other.txt'; $otherHiddenName = 'vm-'+$token+'-other-hidden.txt'
$visibleDirectory = Join-Path $mainFolder ('vm-'+$token+'-dir')
$hiddenDirectory = Join-Path $mainFolder $hiddenDirectoryName
$allowedDirectories = @($mainFolder,$otherFolder,$emptyFolder,$visibleDirectory,$hiddenDirectory)
$allowedFiles = @{}
$allowedFiles[(Join-Path $mainFolder $marker)] = 'Blink View '+$guid+' visible marker'
$allowedFiles[(Join-Path $mainFolder $largeName)] = ('Blink View '+$guid+' larger file ')*12
$allowedFiles[(Join-Path $mainFolder $hiddenName)] = 'Blink View '+$guid+' hidden file'
$allowedFiles[(Join-Path $mainFolder $systemName)] = 'Blink View '+$guid+' system file'
$allowedFiles[(Join-Path $mainFolder $protectedName)] = 'Blink View '+$guid+' protected file'
$allowedFiles[(Join-Path $otherFolder $otherMarker)] = 'Blink View '+$guid+' other marker'
$allowedFiles[(Join-Path $otherFolder $otherHiddenName)] = 'Blink View '+$guid+' other hidden file'
$expectedAttributes = @{}
foreach ($filePath in $allowedFiles.Keys) { $expectedAttributes[$filePath] = [IO.FileAttributes]::Normal }
$expectedAttributes[(Join-Path $mainFolder $hiddenName)] = [IO.FileAttributes]::Hidden
$expectedAttributes[(Join-Path $mainFolder $systemName)] = [IO.FileAttributes]::System
$expectedAttributes[(Join-Path $mainFolder $protectedName)] = [IO.FileAttributes]::System -bor [IO.FileAttributes]::Hidden
$expectedAttributes[(Join-Path $otherFolder $otherHiddenName)] = [IO.FileAttributes]::Hidden
$compilerTemp = Join-Path $integration ('compiler-temp\'+$guid)
$originalTemp = $env:TEMP; $originalTmp = $env:TMP; $tempChanged = $false
$checks = [Collections.Generic.List[object]]::new(); $notes = [Collections.Generic.List[string]]::new()
$process = $null; $fixtureCreated = $false; $currentStep = 'preflight'
$report = [ordered]@{
    status='RUNNING';passed=$false;skipped=$false;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null
    configuration=$Configuration;binary=$Binary;binary_sha256=$null;process_id=$null;process_exited=$false;exit_code=$null;forced_termination=$false
    environment=$null;window_dpi=$null;desktop_preflight=$null;input_skip_reason=$null
    fixture=$fixture;fixture_removed=$false;fixture_bytes_removed=0;fixture_retained=$false;fixture_ownership_failures=@()
    compiler_temp=$compilerTemp;compiler_temp_removed=$false;compiler_temp_bytes_removed=0
    input_mode='Owned PID/HWND InputSite keyboard messages and WinUI UIA patterns only; no global input or Clipboard'
    human_acceptance='NOT RUN';not_run=@('Windows 11 24H2 reference comparison','theme/DPI/language matrix','tree expansion','search Hidden integration','Narrator','menu visual screenshot comparison')
    current_step=$currentStep;failure=$null;checks=@();notes=@();menu_snapshots=@();layout_snapshots=@();keyboard_evidence=@();tab_evidence=@();stale_peer=$null;last_ui_snapshot=$null
}
function Save-Report {
    $report.current_step=$script:currentStep; $report.checks=@($checks.ToArray()); $report.notes=@($notes.ToArray())
    [IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($true))
}
function Record([string]$RecordLabel,[string]$Result='PASS',[string]$Detail='') {
    $checks.Add([pscustomobject]@{name=$RecordLabel;status=$Result;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')}); Save-Report
}
function Skip([string]$Reason) { throw ('VIEW_MENU_UI_SKIP: '+$Reason) }
function Check([string]$CheckLabel,[scriptblock]$Body) {
    $script:currentStep=$CheckLabel; Save-Report
    try { $null=& $Body; Record $CheckLabel }
    catch { if ($_.Exception.Message.StartsWith('VIEW_MENU_UI_SKIP: ')) { Record $CheckLabel 'SKIP' $_.Exception.Message.Substring(19) }
        else { Record $CheckLabel 'FAIL' ($_.Exception.ToString()+"`n"+$_.ScriptStackTrace) }; throw }
}
function Window {
    $process.Refresh(); if ($process.HasExited) { throw ('Owned Blink exited: '+$process.ExitCode) }
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { return $null }
    $ownedWindow=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    if ($ownedWindow.Current.ProcessId -ne $process.Id) { throw 'Owned Blink UIA PID mismatch' }
    return $ownedWindow
}
function Of-Type($Ancestor,$ControlType) {
    if (-not $Ancestor) { return @() }
    return @($Ancestor.FindAll([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,$ControlType)))
}
function Visible($Element) { return $null -ne $Element -and -not $Element.Current.IsOffscreen }
function By-Id([string]$AutomationId) {
    $ownedWindow=Window; if (-not $ownedWindow) { return $null }
    return $ownedWindow.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$AutomationId))
}
function Owned-Match($ownedRoot,$condition,[string]$AutomationId) {
    if (-not $ownedRoot -or $ownedRoot.Current.ProcessId -ne $process.Id) { return $null }
    if ($ownedRoot.Current.AutomationId -eq $AutomationId -and (Visible $ownedRoot)) { return $ownedRoot }
    foreach ($ownedElement in @($ownedRoot.FindAll([Windows.Automation.TreeScope]::Descendants,$condition))) {
        if ($ownedElement.Current.ProcessId -eq $process.Id -and (Visible $ownedElement)) { return $ownedElement }
    }
    return $null
}
function Owned-Element([string]$AutomationId) {
    $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$AutomationId)
    # WinUI normally exposes its popup through the owned window. Search that
    # first; enumerate same-PID popup roots only when it has no visible match.
    $ownedMatch=Owned-Match (Window) $condition $AutomationId
    if ($ownedMatch) { return $ownedMatch }
    # Native HWND/PID filtering avoids activating unrelated desktop UIA
    # providers merely to discover this process's sibling popup roots.
    foreach ($ownedHwnd in [BlinkExplorerViewUiNative]::OwnedTopWindows($process.Id)) {
        if ($ownedHwnd -eq $process.MainWindowHandle -or
            -not [BlinkExplorerViewUiNative]::IsOwnedVisible($ownedHwnd,$process.Id)) { continue }
        try {
            $ownedRoot=[Windows.Automation.AutomationElement]::FromHandle($ownedHwnd)
            $ownedMatch=Owned-Match $ownedRoot $condition $AutomationId
            if ($ownedMatch) { return $ownedMatch }
        } catch {
            # A popup can disappear between native enumeration and UIA lookup.
            # Preserve errors for live HWNDs rather than hiding provider faults.
            if ([BlinkExplorerViewUiNative]::WindowExists($ownedHwnd)) { throw }
        }
    }
    return $null
}
# AddressBar.HelpText is a passive draft/committed address read.
function Address {
    $addressHost=By-Id 'AddressBar'; if ($addressHost) { return $addressHost }
    return Address-Editor
}
function Address-Editor {
    $addressEditor=By-Id 'AddressEditor'; if (Visible $addressEditor) { return $addressEditor }
    $editors=@(Of-Type (Window) ([Windows.Automation.ControlType]::Edit) | Where-Object {
        $_.Current.IsEnabled -and -not $_.Current.IsOffscreen -and $_.Current.AutomationId -notin @('SearchQuery','SearchSelectedVolumes','SearchSelectedFolder','RenameName','NewFolderName') })
    if ($editors.Count -ne 1) { return $null }
    return $editors[0]
}
function Value($Element) { if ($Element.Current.AutomationId -ceq 'AddressBar') { return $Element.Current.HelpText }; return $Element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value }
function Folder-Rows([string]$RowLabel='') {
    return @(Of-Type (By-Id 'FolderEntries') ([Windows.Automation.ControlType]::ListItem) | Where-Object {
        -not $_.Current.IsOffscreen -and (-not $RowLabel -or $_.Current.Name -ceq $RowLabel) })
}
function Row-Names { return @(Folder-Rows | ForEach-Object { $_.Current.Name }) }
function Selected-Names {
    return @((By-Id 'FolderEntries').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection() | ForEach-Object { $_.Current.Name })
}
function Same-Names([string[]]$First,[string[]]$Second) { return ($First.Count -eq $Second.Count -and ($First -join "`n") -ceq ($Second -join "`n")) }
function Focused-Id {
    $focused=[Windows.Automation.AutomationElement]::FocusedElement
    for ($depth=0; $focused -and $depth -lt 8; $depth++) {
        if ($focused.Current.ProcessId -ne $process.Id) { return '' }
        if ($focused.Current.AutomationId) { return $focused.Current.AutomationId }
        $focused=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($focused)
    }
    return ''
}
function Resource-Values([string]$ResourceKey) {
    $values=[Collections.Generic.List[string]]::new()
    foreach ($culture in @('en-US','zh-CN')) {
        [xml]$resource=[IO.File]::ReadAllText((Join-Path $project ('src\Blink\Strings\'+$culture+'\Resources.resw')))
        foreach ($item in $resource.root.data) { if ($item.name -ceq $ResourceKey) { $values.Add([string]$item.value) } }
    }
    return @($values.ToArray() | Select-Object -Unique)
}
$viewLabels=@{}; foreach ($modeLabel in @('Details','List','Grid')) { $viewLabels[$modeLabel]=@(Resource-Values ('View.'+$modeLabel)) }
$columnLabels=@{}; foreach ($columnKey in @('Name','Type','Size','Modified')) { $columnLabels[$columnKey]=@(Resource-Values ('Column.'+$columnKey)) }
$thisPcLabels=@(Resource-Values 'Location.ThisPC'); $toolbarViewLabels=@(Resource-Values 'Command.View')
function Layout-Snapshot {
    $entries=By-Id 'FolderEntries'; $headers=[ordered]@{}
    $buttons=@(Of-Type (Window) ([Windows.Automation.ControlType]::Button) | Where-Object { -not $_.Current.IsOffscreen })
    foreach ($columnKey in @('Name','Type','Size','Modified')) { $headers[$columnKey]=@($buttons | Where-Object { $columnLabels[$columnKey] -ccontains $_.Current.Name }).Count }
    return [pscustomobject]@{class_name=$entries.Current.ClassName;headers=$headers;help_text=(By-Id 'FolderViewMode').Current.HelpText;address=(Value (Address));rows=@(Row-Names);selected=@(Selected-Names)}
}
function Is-Layout([string]$Mode) {
    $layout=Layout-Snapshot; $headers=@($layout.headers.Values | Where-Object { $_ -gt 0 }).Count
    if ($viewLabels[$Mode] -cnotcontains $layout.help_text) { return $false }
    if ($Mode -eq 'Grid') { return $layout.class_name -eq 'GridView' -and $headers -eq 0 }
    if ($Mode -eq 'Details') { return $layout.class_name -eq 'ListView' -and $headers -eq 4 }
    return $layout.class_name -eq 'ListView' -and $headers -eq 0
}
function Capture-UiState([string]$Reason,[string]$WaitError='') {
    $snapshot=[ordered]@{reason=$Reason;last_wait_error=$WaitError;utc=[DateTime]::UtcNow.ToString('o');focused_id=$null;layout=$null;menus=@();errors=@()}
    try { $snapshot.focused_id=Focused-Id; $snapshot.layout=Layout-Snapshot } catch { $snapshot.errors+=@($_.Exception.Message) }
    foreach ($surfaceId in @('TopViewMenu','TopViewShowHidden','FolderContextMenu')) {
        try { $surface=Owned-Element $surfaceId; if ($surface) { $snapshot.menus+=@([pscustomobject]@{id=$surfaceId;class_name=$surface.Current.ClassName;enabled=$surface.Current.IsEnabled}) } } catch { $snapshot.errors+=@($_.Exception.Message) }
    }
    $report.last_ui_snapshot=$snapshot; Save-Report
}
function Wait-Ui([string]$WaitLabel,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds) {
    $timer=[Diagnostics.Stopwatch]::StartNew(); $lastWaitError=''
    do {
        try { if (& $Predicate) { return } } catch { if ($_.Exception.Message.StartsWith('VIEW_MENU_UI_SKIP: ')) { throw }; $lastWaitError=$_.Exception.Message }
        if ($process -and $process.WaitForExit(150)) { throw ('Owned Blink exited while waiting for '+$WaitLabel) }
    } while ($timer.Elapsed.TotalSeconds -lt $Seconds)
    try { Capture-UiState ('timeout: '+$WaitLabel) $lastWaitError } catch { $notes.Add('Timeout snapshot: '+$_.Exception.Message); Save-Report }
    throw ('Timed out: '+$WaitLabel+$(if ($lastWaitError) { '; '+$lastWaitError }))
}
function Require-InputDesktop {
    $desktop=[BlinkExplorerViewUiNative]::DesktopState(); $report.desktop_preflight=$desktop
    if (-not $desktop.InputReady) { $report.input_skip_reason=$desktop.Reason; Save-Report; Skip ('An unlocked desktop is required: '+$desktop.Reason) }
}
function Key([uint32]$KeyCode,[int]$Modifiers=0) {
    Require-InputDesktop; [BlinkExplorerViewUiNative]::Key($process.Id,$KeyCode,$Modifiers)
}
function Act($Element) {
    if (-not $Element -or $Element.Current.ProcessId -ne $process.Id -or -not (Visible $Element) -or -not $Element.Current.IsEnabled) { throw 'Visible enabled owned UIA element required' }
    $pattern=$null
    if ($Element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)) { $pattern.Invoke() }
    elseif ($Element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)) { $pattern.Toggle() }
    elseif ($Element.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)) { $pattern.Expand() }
    else { Skip ('No usable Invoke/Toggle/Expand pattern on '+$Element.Current.AutomationId) }
}
function Toggle-State($Element) {
    if (-not $Element -or $Element.Current.ProcessId -ne $process.Id) { throw 'Owned toggle required' }
    $pattern=$null
    if ($Element.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)) { return $pattern.Current.ToggleState -eq [Windows.Automation.ToggleState]::On }
    if ($Element.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)) { return $pattern.Current.IsSelected }
    Skip ('Toggle state unavailable on '+$Element.Current.AutomationId)
}
function Close-Menus {
    for ($attempt=0; $attempt -lt 3; $attempt++) {
        if (-not (Owned-Element 'TopViewMenu') -and -not (Owned-Element 'FolderContextMenu') -and -not (Owned-Element 'TopViewShowHidden') -and -not (Owned-Element 'TopSortMenu') -and -not (Owned-Element 'TopNewMenu') -and -not (Owned-Element 'TopMoreMenu')) { return }
        Key 27
        $null=$process.WaitForExit(150)
    }
    Wait-Ui 'owned menu presenters close' { -not (Owned-Element 'TopViewMenu') -and -not (Owned-Element 'FolderContextMenu') -and -not (Owned-Element 'TopViewShowHidden') -and -not (Owned-Element 'TopSortMenu') -and -not (Owned-Element 'TopNewMenu') -and -not (Owned-Element 'TopMoreMenu') }
}
function Open-Top {
    Wait-Ui 'owned top View presenter reflects a ready page' {
        if (-not (Owned-Element 'TopViewMenu')) { Act (By-Id 'FolderViewMode') }
        $firstChoice=Owned-Element 'TopViewGrid'
        if ($firstChoice -and $firstChoice.Current.IsEnabled) { return $true }
        # A menu snapshots capabilities at Opening. Reopen after the async page
        # settles rather than invoking an item captured while it was disabled.
        Close-Menus; return $false
    }
}
function Expand-Show {
    Open-Top
    $parent=Owned-Element 'TopViewShow'; $pattern=$null
    if (-not $parent) { throw 'TopViewShow missing' }
    if ($parent.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)) { $pattern.Expand() }
    elseif ($parent.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)) { $pattern.Invoke() }
    else { $parent.SetFocus(); Key 0x27 }
    Wait-Ui 'owned Show Hidden child realized' { $null -ne (Owned-Element 'TopViewShowHidden') }
}
function Assert-TopMode([string]$ExpectedMode) {
    Open-Top; $selected=[Collections.Generic.List[string]]::new(); $items=@()
    foreach ($choice in @('Grid','List','Details')) {
        $item=Owned-Element ('TopView'+$choice); if (-not $item) { throw ('View menu choice missing: '+$choice) }
        $on=Toggle-State $item; if ($on) { $selected.Add($choice) }
        $items+=@([pscustomobject]@{id=$item.Current.AutomationId;name=$item.Current.Name;enabled=$item.Current.IsEnabled;checked=$on;class_name=$item.Current.ClassName;pid=$item.Current.ProcessId})
    }
    $report.menu_snapshots+=@([pscustomobject]@{expected=$ExpectedMode;items=$items;utc=[DateTime]::UtcNow.ToString('o')}); Save-Report
    if ($selected.Count -ne 1 -or $selected[0] -cne $ExpectedMode) { throw ('Wrong exclusive View selection: '+($selected.ToArray() -join ',')) }
    Close-Menus
}
function Set-TopMode([ValidateSet('Details','List','Grid')][string]$Mode) {
    Open-Top; Act (Owned-Element ('TopView'+$Mode))
    Wait-Ui ('real '+$Mode+' layout and closed menu') { -not (Owned-Element 'TopViewMenu') -and (Is-Layout $Mode) }
    $report.layout_snapshots+=@(Layout-Snapshot); Save-Report; Assert-TopMode $Mode
}
function Read-Hidden {
    Expand-Show; $on=Toggle-State (Owned-Element 'TopViewShowHidden'); Close-Menus; return $on
}
function Set-Hidden([bool]$Enabled) {
    Expand-Show; $item=Owned-Element 'TopViewShowHidden'
    if ((Toggle-State $item) -ne $Enabled) { Act $item; Wait-Ui 'Hidden choice closes its menu' { -not (Owned-Element 'TopViewMenu') } }
    else { Close-Menus }
    if ((Read-Hidden) -ne $Enabled) { throw 'Hidden menu check did not match the requested state' }
}
function Navigate-Owned([string]$TargetFolder,[string]$MarkerName='') {
    if ($TargetFolder -notin @($mainFolder,$otherFolder,$emptyFolder)) { throw 'Refusing navigation outside this run fixture' }
    Close-Menus; Key 0x4C 1; Wait-Ui 'Ctrl L reveals the owned address editor' { $null -ne (Address-Editor) }; $editor=Address-Editor; $editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($TargetFolder); $editor.SetFocus(); Key 13
    Wait-Ui 'committed owned folder and its marker/empty page' {
        (Value (Address)) -ieq $TargetFolder -and (Visible (By-Id 'FolderEntries')) -and
        (By-Id 'CommandSort').Current.IsEnabled -and
        $(if ($MarkerName) { @(Folder-Rows $MarkerName).Count -eq 1 } else { @(Folder-Rows).Count -eq 0 }) }
}
function Select-Owned([string]$RowLabel) {
    if ($RowLabel -notin @($marker,$otherMarker,$hiddenName)) { throw 'Refusing non-owned selection' }
    Wait-Ui 'exact owned row available' { @(Folder-Rows $RowLabel).Count -eq 1 }
    $row=@(Folder-Rows $RowLabel)[0]; $row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select(); $row.SetFocus()
    Wait-Ui 'exact owned single selection' { $selected=@(Selected-Names); $selected.Count -eq 1 -and $selected[0] -ceq $RowLabel }
}
function Open-Context {
    Close-Menus; Select-Owned $marker; Key 0x5D
    Wait-Ui 'owned ordinary context menu' { $null -ne (Owned-Element 'FolderContextMenu') }
}
function Set-ContextChoice([string]$ParentId,[string]$ChoiceId) {
    Open-Context; $parent=Owned-Element $ParentId; $pattern=$null
    if ($parent.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$pattern)) { $pattern.Expand() }
    elseif ($parent.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)) { $pattern.Invoke() }
    else { $parent.SetFocus(); Key 0x27 }
    Wait-Ui ('owned context choice '+$ChoiceId) { $null -ne (Owned-Element $ChoiceId) }
    Act (Owned-Element $ChoiceId); Wait-Ui 'ordinary context command closes menu' { -not (Owned-Element 'FolderContextMenu') }
}
function Set-TopSort([ValidateSet('Name','Type','Size','Modified','Ascending','Descending')][string]$Choice) {
    Close-Menus; Act (By-Id 'CommandSort')
    Wait-Ui 'owned top Sort menu' { $null -ne (Owned-Element 'TopSortMenu') }
    Act (Owned-Element ('TopSort'+$Choice))
    Wait-Ui 'top Sort choice commits and closes' { -not (Owned-Element 'TopSortMenu') -and (By-Id 'CommandSort').Current.IsEnabled }
    $expected=@(Resource-Values ($(if ($Choice -in @('Ascending','Descending')) { 'Sort.' } else { 'Column.' })+$Choice))
    Wait-Ui 'Sort accessible state matches its authoritative descriptor' {
        $sort=By-Id 'CommandSort'
        $expected -ccontains $(if ($Choice -in @('Ascending','Descending')) { $sort.Current.ItemStatus } else { $sort.Current.HelpText })
    }
}
function Sort-Snapshot {
    $sort=By-Id 'CommandSort'
    return [pscustomobject]@{field=$sort.Current.HelpText;direction=$sort.Current.ItemStatus}
}
function Tabs { return @(Of-Type (By-Id 'WindowTabs') ([Windows.Automation.ControlType]::TabItem)) }
function New-OwnedTab([int]$ExpectedCount) {
    Close-Menus; (By-Id 'FolderViewMode').SetFocus(); Key 0x54 1
    Wait-Ui 'new owned TabView item and ThisPC page' { @(Tabs).Count -eq $ExpectedCount -and $thisPcLabels -ccontains (Value (Address)) }
}
function Select-Tab([int]$TabIndex,[string]$ExpectedPath,[string]$ExpectedMode) {
    Close-Menus; $tabs=@(Tabs); if ($TabIndex -ge $tabs.Count) { throw 'Owned tab index unavailable' }
    $tabs[$TabIndex].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Wait-Ui 'selected owned tab commits its saved path/layout' { (Value (Address)) -ieq $ExpectedPath -and (Is-Layout $ExpectedMode) }
    $report.tab_evidence+=@([pscustomobject]@{index=$TabIndex;path=(Value (Address));mode=$ExpectedMode;utc=[DateTime]::UtcNow.ToString('o')}); Save-Report
}
function Assert-SystemExcluded {
    if (@(Folder-Rows $systemName).Count -or @(Folder-Rows $protectedName).Count) { throw 'SYSTEM/protected-system owned file became visible' }
    if (([IO.File]::GetAttributes((Join-Path $mainFolder $systemName)) -band [IO.FileAttributes]::System) -eq 0 -or
        ([IO.File]::GetAttributes((Join-Path $mainFolder $protectedName)) -band ([IO.FileAttributes]::System -bor [IO.FileAttributes]::Hidden)) -ne ([IO.FileAttributes]::System -bor [IO.FileAttributes]::Hidden)) { throw 'Owned SYSTEM fixture attributes changed' }
}
function Fixture-OwnershipFailures {
    $failures=[Collections.Generic.List[string]]::new(); $pending=[Collections.Generic.Stack[string]]::new()
    $rootEntry=Get-Item -LiteralPath $fixture -Force
    if (-not $rootEntry.PSIsContainer -or ($rootEntry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return @('Owned fixture root changed type or became a reparse point') }
    $pending.Push($fixture)
    while ($pending.Count) {
        foreach ($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)) {
            $relative=$entry.FullName.Substring($fixture.Length).TrimStart('\')
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { $failures.Add('Unowned reparse entry: '+$relative); continue }
            if ($entry.PSIsContainer) {
                if ($allowedDirectories -icontains $entry.FullName) {
                    $expectedFlags=$(if ($entry.FullName -ieq $hiddenDirectory) { [IO.FileAttributes]::Hidden } else { 0 })
                    $directoryMask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
                    if (($entry.Attributes -band $directoryMask) -ne $expectedFlags) { $failures.Add('Unexpected directory attributes: '+$relative) }
                    $pending.Push($entry.FullName)
                } else { $failures.Add('Unknown directory: '+$relative) }; continue
            }
            if (-not $allowedFiles.ContainsKey($entry.FullName)) { $failures.Add('Unknown file: '+$relative); continue }
            try {
                $actual=[Convert]::ToBase64String([IO.File]::ReadAllBytes($entry.FullName))
                $expected=[Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes($allowedFiles[$entry.FullName]))
                if ($actual -cne $expected) { $failures.Add('Owned filename contains unknown bytes: '+$relative) }
                $mask=[IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::ReadOnly
                if (($entry.Attributes -band $mask) -ne ($expectedAttributes[$entry.FullName] -band $mask)) { $failures.Add('Unexpected file attributes: '+$relative) }
            } catch { $failures.Add('Cannot verify owned bytes: '+$relative) }
        }
    }
    foreach ($ownedFile in $allowedFiles.Keys) { if (-not (Test-Path -LiteralPath $ownedFile -PathType Leaf)) { $failures.Add('Owned file unexpectedly missing: '+[IO.Path]::GetFileName($ownedFile)) } }
    foreach ($ownedDirectory in $allowedDirectories) { if (-not (Test-Path -LiteralPath $ownedDirectory -PathType Container)) { $failures.Add('Owned directory unexpectedly missing: '+$ownedDirectory) } }
    return @($failures.ToArray())
}

Save-Report
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or -not [Environment]::UserInteractive) { Skip 'An interactive Windows desktop is required' }
    if (-not (Test-Path -LiteralPath $Binary -PathType Leaf)) { Skip 'The candidate Blink runtime has not been published' }
    $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Skip 'Run this owned interaction smoke as an ordinary user' }
    $version=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $report.environment=[ordered]@{display_version=$version.DisplayVersion;build=$version.CurrentBuildNumber;ubr=$version.UBR;ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;os_64bit=[Environment]::Is64BitOperatingSystem;process_64bit=[Environment]::Is64BitProcess;is_administrator=$false;explorer_reference='NOT RUN by this smoke';theme='not captured';configuration_requested=$Configuration}
    New-Item -ItemType Directory -Path $compilerTemp -Force | Out-Null
    $env:TEMP=$compilerTemp; $env:TMP=$compilerTemp; $tempChanged=$true
    if (-not ('BlinkExplorerViewUiNative' -as [type])) {
        Add-Type @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkExplorerViewUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid; public long Foreground; }
public static class BlinkExplorerViewUiNative {
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
    public static BlinkExplorerViewUiDesktop DesktopState() {
        var result=new BlinkExplorerViewUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
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
}
'@
    }
    Require-InputDesktop
    try { Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes } catch { Skip ('WinUI UIAutomation unavailable: '+$_.Exception.Message) }
    $report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash
    New-Item -ItemType Directory -Path $fixtureParent -Force | Out-Null
    if (Test-Path -LiteralPath $fixture) { throw 'Fresh GUID fixture already exists' }
    New-Item -ItemType Directory -Path $fixture | Out-Null; $fixtureCreated=$true
    foreach ($ownedDirectory in $allowedDirectories) { New-Item -ItemType Directory -Path $ownedDirectory -Force | Out-Null }
    foreach ($ownedFile in $allowedFiles.Keys) {
        [IO.File]::WriteAllText($ownedFile,$allowedFiles[$ownedFile],[Text.UTF8Encoding]::new($false))
        [IO.File]::SetAttributes($ownedFile,$expectedAttributes[$ownedFile])
    }
    [IO.File]::SetAttributes($hiddenDirectory,[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::Hidden)
    $psi=[Diagnostics.ProcessStartInfo]::new(); $psi.FileName=$Binary; $psi.WorkingDirectory=$project; $psi.UseShellExecute=$false
    foreach ($diagnosticVariable in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')) { $psi.EnvironmentVariables.Remove($diagnosticVariable) }
    $process=[Diagnostics.Process]::new(); $process.StartInfo=$psi; if (-not $process.Start()) { throw 'Owned Blink launch failed' }
    $report.process_id=$process.Id; Save-Report
    try { Wait-Ui 'owned automation window' { $null -ne (Window) } } catch { Skip ('Owned automation window unavailable: '+$_.Exception.Message) }
    [BlinkExplorerViewUiNative]::OwnedWindow=$process.MainWindowHandle
    $report.window_dpi=[BlinkExplorerViewUiNative]::GetDpiForWindow($process.MainWindowHandle); Save-Report

    Check '01 owned production window commits only the GUID fixture' {
        Wait-Ui 'ordinary file-view control ready' { Visible (By-Id 'FolderEntries') }
        Navigate-Owned $mainFolder $marker
        if (@(Tabs).Count -ne 1) { throw 'New owned window must have one initial tab' }
        Set-Hidden $false; Assert-SystemExcluded
    }
    Check '02 top View is the owned CommandBar AppBarButton with real exclusive commands and Show' {
        $button=By-Id 'FolderViewMode'
        # Verify the AppBarButton peer as well as its Button role and both
        # action patterns; a generic decorated button is insufficient.
        $invokePattern=$null; $expandPattern=$null
        if ($button.Current.ClassName -notin @('AppBarButton','Microsoft.UI.Xaml.Controls.AppBarButton') -or
            $button.Current.ControlType -ne [Windows.Automation.ControlType]::Button -or
            $toolbarViewLabels -cnotcontains $button.Current.Name -or
            -not $button.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$invokePattern) -or
            -not $button.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern,[ref]$expandPattern)) {
            throw ('View control has an unexpected role, action patterns or name: '+$button.Current.ClassName+' / '+$button.Current.Name)
        }
        Open-Top
        foreach ($commandId in @('TopViewGrid','TopViewList','TopViewDetails','TopViewShow')) {
            $item=Owned-Element $commandId; if (-not $item -or -not $item.Current.IsEnabled) { throw ('Missing enabled owned menu command: '+$commandId) }
        }
        Close-Menus; Assert-TopMode 'Details'; Expand-Show
        if (-not (Owned-Element 'TopViewShowHidden').Current.IsEnabled) { throw 'Hidden child is disabled for a local folder' }
        Close-Menus
    }
    Check '03 Details List and Grid change actual layout while preserving path selection order and sort' {
        Set-TopSort 'Size'; Set-TopSort 'Descending'
        Select-Owned $marker; $beforeRows=@(Row-Names); $beforeSelection=@(Selected-Names); $beforeSort=Sort-Snapshot
        foreach ($mode in @('List','Grid','Details')) {
            Set-TopMode $mode
            Wait-Ui 'selection and row order retained after view projection' { (Same-Names @(Selected-Names) $beforeSelection) -and (Same-Names @(Row-Names) $beforeRows) }
            $afterSort=Sort-Snapshot
            if ((Value (Address)) -ine $mainFolder -or $afterSort.field -cne $beforeSort.field -or $afterSort.direction -cne $beforeSort.direction) { throw 'View change altered committed path/sort' }
        }
    }
    Check '04 ordinary ContextView command and top View selection stay synchronized' {
        Set-ContextChoice 'ContextView' 'ContextViewList'; Wait-Ui 'context command produces actual List layout' { Is-Layout 'List' }; Assert-TopMode 'List'
        Set-TopMode 'Grid'
    }
    Check '05 Show Hidden reveals actual hidden file and directory but excludes both SYSTEM forms' {
        Set-Hidden $true
        Wait-Ui 'real hidden file and directory appear' { @(Folder-Rows $hiddenName).Count -eq 1 -and @(Folder-Rows $hiddenDirectoryName).Count -eq 1 }
        Assert-SystemExcluded
        if (([IO.File]::GetAttributes((Join-Path $mainFolder $hiddenName)) -band [IO.FileAttributes]::Hidden) -eq 0) { throw 'Hidden test did not use a real hidden file' }
    }
    Check '06 hiding items removes hidden selection without resurrecting it' {
        Select-Owned $hiddenName; Set-Hidden $false
        Wait-Ui 'hidden rows and hidden selection pruned' { @(Folder-Rows $hiddenName).Count -eq 0 -and @(Folder-Rows $hiddenDirectoryName).Count -eq 0 -and @(Selected-Names).Count -eq 0 }
        Set-Hidden $true; Wait-Ui 'hidden row reappears without selection' { @(Folder-Rows $hiddenName).Count -eq 1 -and @(Selected-Names).Count -eq 0 }
        Set-Hidden $false; Assert-SystemExcluded
    }
    Check '07 two tabs independently retain real Grid and List layouts and paths' {
        Set-TopMode 'Grid'; New-OwnedTab 2; Navigate-Owned $otherFolder $otherMarker; Set-TopMode 'List'
        Select-Tab 0 $mainFolder 'Grid'; Assert-TopMode 'Grid'
        Select-Tab 1 $otherFolder 'List'; Assert-TopMode 'List'
    }
    Check '08 Hidden is synchronized across existing tabs and inherited by a new tab' {
        Set-Hidden $true; Wait-Ui 'second tab shows its actual hidden file' { @(Folder-Rows $otherHiddenName).Count -eq 1 }
        Select-Tab 0 $mainFolder 'Grid'; Wait-Ui 'first tab adopts window Hidden policy' { @(Folder-Rows $hiddenName).Count -eq 1 -and @(Folder-Rows $hiddenDirectoryName).Count -eq 1 }
        if (-not (Read-Hidden)) { throw 'First tab menu did not inherit window Hidden policy' }; Assert-SystemExcluded
        New-OwnedTab 3; Navigate-Owned $mainFolder $marker
        Wait-Ui 'new tab inherits Hidden policy' { @(Folder-Rows $hiddenName).Count -eq 1 }
        if (-not (Read-Hidden)) { throw 'New tab menu is not checked for Hidden' }; Assert-SystemExcluded
        (By-Id 'FolderViewMode').SetFocus(); Key 0x57 1; Wait-Ui 'only the new owned tab closes' { @(Tabs).Count -eq 2 }
        Select-Tab 0 $mainFolder 'Grid'
    }
    Check '09 stale top View peer cannot change the next tab or its original tab' {
        Open-Top; $oldItem=Owned-Element 'TopViewDetails'; $oldPattern=$null
        if (-not $oldItem.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$oldPattern)) {
            if (-not $oldItem.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$oldPattern)) { Skip 'Old view peer has no invocable pattern' }
        }
        Close-Menus; Select-Tab 1 $otherFolder 'List'; $outcome='returned'
        try { if ($oldPattern -is [Windows.Automation.InvokePattern]) { $oldPattern.Invoke() } else { $oldPattern.Toggle() } } catch { $outcome='rejected: '+$_.Exception.GetType().Name }
        if ($process.WaitForExit(750)) { throw 'Owned Blink exited during stale peer observation' }
        if (-not (Is-Layout 'List') -or (Value (Address)) -ine $otherFolder) { throw 'Old View callback changed the active second tab' }
        Assert-TopMode 'List'; Select-Tab 0 $mainFolder 'Grid'; Assert-TopMode 'Grid'
        $report.stale_peer=[ordered]@{outcome=$outcome;new_tab_unchanged=$true;original_tab_unchanged=$true}; Save-Report
    }
    Check '10 Space arrows Enter and Escape navigate Show using owned keyboard input and return focus' {
        $before=Read-Hidden; (By-Id 'FolderViewMode').SetFocus(); Key 0x20
        Wait-Ui 'Space opens the top View menu' { $null -ne (Owned-Element 'TopViewMenu') }
        Key 0x24; for ($arrow=0; $arrow -lt 3; $arrow++) { Key 0x28 }
        Wait-Ui 'keyboard focus reaches Show after three enabled choices' { (Focused-Id) -ceq 'TopViewShow' }
        Key 0x27; Wait-Ui 'Right opens Show submenu and focuses Hidden' { $null -ne (Owned-Element 'TopViewShowHidden') -and (Focused-Id) -ceq 'TopViewShowHidden' }
        Key 13; Wait-Ui 'keyboard Enter toggles Hidden and dismisses menu' { -not (Owned-Element 'TopViewMenu') }
        if ((Read-Hidden) -eq $before) { throw 'Keyboard Enter did not change Hidden policy' }
        Wait-Ui 'keyboard Hidden change affects real rows' { @(Folder-Rows $hiddenName).Count -eq $(if ($before) { 0 } else { 1 }) }
        (By-Id 'FolderViewMode').SetFocus(); Key 0x20; Wait-Ui 'Space reopens top menu' { $null -ne (Owned-Element 'TopViewMenu') }
        Key 0x24; for ($arrow=0; $arrow -lt 3; $arrow++) { Key 0x28 }; Key 0x27
        Wait-Ui 'keyboard Show submenu is visible again' { $null -ne (Owned-Element 'TopViewShowHidden') }
        Key 27; Wait-Ui 'first Escape closes only Show submenu' { -not (Owned-Element 'TopViewShowHidden') -and $null -ne (Owned-Element 'TopViewMenu') }
        Key 27; Wait-Ui 'second Escape closes top menu and returns button focus' { -not (Owned-Element 'TopViewMenu') -and (Focused-Id) -ceq 'FolderViewMode' }
        $report.keyboard_evidence+=@([pscustomobject]@{sequence='Space Home Down Down Down Right Enter; Space Home Down Down Down Right Escape Escape';hidden_before=$before;hidden_after=(-not $before);focus_return_id=(Focused-Id);pid=$process.Id}); Save-Report
        Assert-SystemExcluded
    }
    Check '11 empty local directory supports actual layouts and Show without stale rows' {
        Navigate-Owned $emptyFolder
        foreach ($mode in @('Details','List','Grid')) { Set-TopMode $mode; if (@(Folder-Rows).Count -ne 0 -or (Value (Address)) -ine $emptyFolder) { throw 'Empty folder reused old rows or path' } }
        $emptyHidden=Read-Hidden; Set-Hidden (-not $emptyHidden); Set-Hidden $emptyHidden
        if (@(Folder-Rows).Count -ne 0) { throw 'Hidden toggle produced entries in an empty owned directory' }
    }
    Check '12 ThisPC offers real view choices and retains virtual location semantics' {
        New-OwnedTab 3
        foreach ($mode in @('List','Grid','Details')) {
            Set-TopMode $mode
            if ($thisPcLabels -cnotcontains (Value (Address))) { throw 'View choice turned ThisPC into a filesystem path' }
        }
        $pcHidden=Read-Hidden; Set-Hidden (-not $pcHidden); Set-Hidden $pcHidden
        (By-Id 'FolderViewMode').SetFocus(); Key 0x57 1; Wait-Ui 'temporary ThisPC tab closes normally' { @(Tabs).Count -eq 2 }
        Select-Tab 0 $emptyFolder 'Grid'; Navigate-Owned $mainFolder $marker
    }
    Check '13 text-focused Ctrl A and Delete edit only address text and preserve selected file and View' {
        Select-Owned $marker; $beforeRows=@(Row-Names); $beforeMode=(By-Id 'FolderViewMode').Current.HelpText
        Key 0x4C 1; Wait-Ui 'Ctrl L focuses the owned address editor' { $focused=[Windows.Automation.AutomationElement]::FocusedElement; $focused.Current.ProcessId -eq $process.Id -and $focused.Current.ControlType -eq [Windows.Automation.ControlType]::Edit -and (Value $focused) -ieq $mainFolder }
        Key 0x41 1; Key 0x2E; Wait-Ui 'Delete clears only the address text selection' { (Value (Address)) -ceq '' }
        if (-not (Test-Path -LiteralPath (Join-Path $mainFolder $marker) -PathType Leaf) -or -not (Same-Names @(Row-Names) $beforeRows) -or (By-Id 'FolderViewMode').Current.HelpText -cne $beforeMode) { throw 'Text editing affected the selected file, listing or View' }
        (Address-Editor).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($mainFolder)
        (By-Id 'FolderViewMode').SetFocus()
    }
    $notes.Add('Search Hidden, navigation tree expansion, human visual/IME/accessibility and theme/DPI matrix were not exercised. No Clipboard or file mutation command was invoked.')
    $report.status='PASS'
} catch {
    if ($_.Exception.Message.StartsWith('VIEW_MENU_UI_SKIP: ')) {
        $report.status='SKIP'; $report.failure=$_.Exception.Message.Substring(19)
        if (-not $checks.Count -or $checks[$checks.Count-1].status -ne 'SKIP') { Record $currentStep 'SKIP' $report.failure }
    } else { $report.status='FAIL'; $report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace; try { Capture-UiState 'unexpected smoke failure' $_.Exception.Message } catch {} }
} finally {
    if ($process) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) { if (-not $process.CloseMainWindow() -or -not $process.WaitForExit(10000)) { throw ('Owned Blink did not close normally; PID '+$process.Id+'. No force termination attempted.') } }
            $process.Refresh(); $report.process_exited=$process.HasExited; if ($process.HasExited) { $report.exit_code=$process.ExitCode }
            if ($report.status -eq 'PASS') { if ($report.exit_code -ne 0) { throw 'Owned Blink returned a nonzero exit code' }; Record '14 owned Blink closes normally' }
        } catch { $notes.Add('Window cleanup: '+$_.Exception.Message); $report.status='FAIL'; $report.failure=(@($report.failure,$_.Exception.ToString()) | Where-Object { $_ }) -join "`n"; Record '14 owned Blink normal close failed' 'FAIL' $_.Exception.Message }
    }
    if ($fixtureCreated) {
        try {
            if ($process -and -not $process.HasExited) { throw 'Owned Blink is still active; fixture must remain' }
            $report.fixture_ownership_failures=@(Fixture-OwnershipFailures)
            if ($report.fixture_ownership_failures.Count) { throw ('Unknown or changed fixture entries; nothing deleted: '+($report.fixture_ownership_failures -join '; ')) }
            $report.fixture_bytes_removed=(Get-ChildItem -LiteralPath $fixture -Recurse -Force -File | Measure-Object Length -Sum).Sum
            Remove-Item -LiteralPath $fixture -Recurse -Force; $report.fixture_removed=-not (Test-Path -LiteralPath $fixture)
            if (-not $report.fixture_removed) { throw 'Owned fixture remains' }
            if ($report.status -eq 'PASS') { Record '15 exact owned marker bytes and paths verified before fixture cleanup' }
        } catch { $report.fixture_retained=Test-Path -LiteralPath $fixture; $cleanupReason='Fixture retained: '+$_.Exception.Message; $notes.Add($cleanupReason); $report.status='FAIL'; $report.failure=(@($report.failure,$cleanupReason) | Where-Object { $_ }) -join "`n"; Record '15 unknown or in-use fixture preserved' 'FAIL' $cleanupReason }
    }
    if ($tempChanged) { $env:TEMP=$originalTemp; $env:TMP=$originalTmp }
    if (Test-Path -LiteralPath $compilerTemp) {
        try {
            $report.compiler_temp_bytes_removed=(Get-ChildItem -LiteralPath $compilerTemp -Recurse -Force -File | Measure-Object Length -Sum).Sum
            Remove-Item -LiteralPath $compilerTemp -Recurse -Force; $report.compiler_temp_removed=-not (Test-Path -LiteralPath $compilerTemp)
        } catch { $notes.Add('Owned Add-Type compiler temp cleanup: '+$_.Exception.Message); $report.status='FAIL'; $report.failure=(@($report.failure,$_.Exception.Message) | Where-Object { $_ }) -join "`n" }
    }
    $report.passed=$report.status -eq 'PASS'; $report.skipped=$report.status -in @('SKIP','PARTIAL'); $report.ended_utc=[DateTime]::UtcNow.ToString('o'); Save-Report
    Write-Output ('UI VIEW MENU '+$report.status+': '+@($checks | Where-Object { $_.status -eq 'PASS' }).Count+' PASS / '+@($checks | Where-Object { $_.status -eq 'FAIL' }).Count+' FAIL / '+@($checks | Where-Object { $_.status -eq 'SKIP' }).Count+' SKIP')
    Write-Output ('Report: '+$reportPath)
}
if ($report.status -eq 'FAIL') { exit 1 }; if ($report.status -ne 'PASS') { exit 2 }; exit 0
