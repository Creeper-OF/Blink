[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [ValidateRange(1, 10)]
    [int] $CtrlTCount = 10,

    [ValidateRange(1, 5)]
    [int] $RepeatRuns = 5,

    [ValidateRange(5, 60)]
    [int] $StartupTimeoutSeconds = 20,

    [ValidateRange(2, 30)]
    [int] $ActionTimeoutSeconds = 10,

    [string] $RunId = ''
)

[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$blinkExe = Join-Path $projectRoot ('src\Blink\x64\{0}\Blink.exe' -f $Configuration)
if (-not (Test-Path -LiteralPath $blinkExe -PathType Leaf)) {
    throw "Production Blink.exe not found: $blinkExe"
}

if ([string]::IsNullOrWhiteSpace($RunId)) {
    $RunId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
}

$runRoot = Join-Path $projectRoot ('.blink-data\phase1b-tab-lifetime\{0}-{1}' -f $Configuration.ToLowerInvariant(), $RunId)
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$reportPath = Join-Path $runRoot 'report.json'
$utf8 = [System.Text.UTF8Encoding]::new($false)

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class BlinkTabLifetimeNativeInput
{
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern void keybd_event(
        byte virtualKey,
        byte scanCode,
        uint flags,
        UIntPtr extraInfo);

    public static void SendCtrlT()
    {
        keybd_event(0x11, 0, 0, UIntPtr.Zero);
        keybd_event(0x54, 0, 0, UIntPtr.Zero);
        keybd_event(0x54, 0, 2, UIntPtr.Zero);
        keybd_event(0x11, 0, 2, UIntPtr.Zero);
    }
}
'@

$allResults = [System.Collections.Generic.List[object]]::new()
$crashObserved = $false

function Format-ExitCodeHex([object] $Code) {
    if ($null -eq $Code) {
        return $null
    }
    $value = [int64] $Code
    if ($value -lt 0) {
        $value += 4294967296
    }
    return '0x{0:X8}' -f $value
}

function Stop-BlinkProcess([System.Diagnostics.Process] $Process) {
    if ($null -eq $Process) {
        return
    }
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            $Process.CloseMainWindow() | Out-Null
            if (-not $Process.WaitForExit(3000)) {
                $Process.Kill()
                $Process.WaitForExit(3000)
            }
        }
    }
    catch {
        # The process may already have exited between the checks.
    }
}

function Start-BlinkSession([string] $SessionName) {
    $telemetryPath = Join-Path $runRoot ('{0}-startup.log' -f $SessionName)
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $blinkExe
    $psi.WorkingDirectory = Split-Path -Parent $blinkExe
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables['BLINK_STARTUP_TELEMETRY'] = $telemetryPath

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    if (-not $process.Start()) {
        throw "Failed to start Blink.exe for $SessionName"
    }

    try {
        $initial = Wait-BlinkReady -Process $process -MinimumTabs 1 -RequireInitialStartup
        if (-not $initial.ready) {
            throw "Initial Blink startup did not become ready: $($initial.reason)"
        }
        return [pscustomobject]@{
            process = $process
            telemetry_path = $telemetryPath
            started = Get-Date
        }
    }
    catch {
        Stop-BlinkProcess $process
        throw
    }
}

function Get-BlinkWindow([System.Diagnostics.Process] $Process) {
    try {
        $Process.Refresh()
        if ($Process.HasExited -or $Process.MainWindowHandle -eq [IntPtr]::Zero) {
            return $null
        }
        return [System.Windows.Automation.AutomationElement]::FromHandle(
            $Process.MainWindowHandle)
    }
    catch {
        return $null
    }
}

function New-PropertyCondition(
    [System.Windows.Automation.AutomationProperty] $Property,
    [object] $Value) {
    return New-Object System.Windows.Automation.PropertyCondition($Property, $Value)
}

function Find-DescendantByAutomationId(
    [System.Windows.Automation.AutomationElement] $Window,
    [string] $AutomationId) {
    if ($null -eq $Window) {
        return $null
    }
    try {
        $condition = New-PropertyCondition `
            ([System.Windows.Automation.AutomationElement]::AutomationIdProperty) `
            $AutomationId
        return $Window.FindFirst(
            [System.Windows.Automation.TreeScope]::Descendants,
            $condition)
    }
    catch {
        return $null
    }
}

function Find-DescendantsByControlType(
    [System.Windows.Automation.AutomationElement] $Element,
    [System.Windows.Automation.ControlType] $ControlType) {
    if ($null -eq $Element) {
        return @()
    }
    try {
        $condition = New-PropertyCondition `
            ([System.Windows.Automation.AutomationElement]::ControlTypeProperty) `
            $ControlType
        return @($Element.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants,
            $condition))
    }
    catch {
        return @()
    }
}

function Get-TabItems([System.Windows.Automation.AutomationElement] $Window) {
    return Find-DescendantsByControlType `
        $Window `
        ([System.Windows.Automation.ControlType]::TabItem)
}

function Get-BodyRowCount([System.Windows.Automation.AutomationElement] $Window) {
    $lists = Find-DescendantsByControlType `
        $Window `
        ([System.Windows.Automation.ControlType]::List)
    $bestList = $null
    $bestArea = 0.0
    foreach ($list in $lists) {
        try {
            $rect = $list.Current.BoundingRectangle
            $area = [double] $rect.Width * [double] $rect.Height
            if ($area -gt $bestArea) {
                $bestArea = $area
                $bestList = $list
            }
        }
        catch {
            continue
        }
    }
    if ($null -eq $bestList) {
        return 0
    }
    try {
        $condition = New-PropertyCondition `
            ([System.Windows.Automation.AutomationElement]::ControlTypeProperty) `
            ([System.Windows.Automation.ControlType]::ListItem)
        return @($bestList.FindAll(
            [System.Windows.Automation.TreeScope]::Children,
            $condition)).Count
    }
    catch {
        return 0
    }
}

function Get-TreeItemCount([System.Windows.Automation.AutomationElement] $Window) {
    $tree = Find-DescendantByAutomationId $Window 'ListControl'
    return (Find-DescendantsByControlType `
        $tree `
        ([System.Windows.Automation.ControlType]::TreeItem)).Count
}

function Get-BlinkState([System.Diagnostics.Process] $Process) {
    $alive = $false
    $exitCode = $null
    try {
        $Process.Refresh()
        $alive = -not $Process.HasExited
        if (-not $alive) {
            $exitCode = $Process.ExitCode
        }
    }
    catch {
        $alive = $false
    }

    $window = if ($alive) { Get-BlinkWindow $Process } else { $null }
    $tabs = @(Get-TabItems $window)
    $addButton = Find-DescendantByAutomationId $window 'AddButton'
    return [pscustomobject]@{
        alive = $alive
        exit_code = $exitCode
        exit_code_hex = Format-ExitCodeHex $exitCode
        tab_count = $tabs.Count
        body_rows = Get-BodyRowCount $window
        tree_items = Get-TreeItemCount $window
        add_button = ($null -ne $addButton)
    }
}

function Test-TelemetryStage([string] $Path, [string] $Stage) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    try {
        return (Get-Content -LiteralPath $Path -Raw) -match ([regex]::Escape("stage=$Stage"))
    }
    catch {
        return $false
    }
}

function Wait-BlinkReady(
    [System.Diagnostics.Process] $Process,
    [int] $MinimumTabs,
    [int] $TimeoutSeconds = $StartupTimeoutSeconds,
    [switch] $RequireInitialStartup) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $stableSamples = 0
    while ((Get-Date) -lt $deadline) {
        $state = Get-BlinkState $Process
        if (-not $state.alive) {
            return [pscustomobject]@{
                ready = $false
                reason = "process exited $($state.exit_code_hex)"
                state = $state
            }
        }

        $telemetryReady = $true
        if ($RequireInitialStartup) {
            $telemetryReady = Test-TelemetryStage $script:currentTelemetryPath 'initial-this-pc.end'
        }
        if ($telemetryReady -and $state.add_button -and
            $state.tab_count -ge $MinimumTabs -and $state.body_rows -gt 0) {
            $stableSamples++
            if ($stableSamples -ge 2) {
                return [pscustomobject]@{
                    ready = $true
                    reason = 'stable UI and ThisPC content observed'
                    state = $state
                }
            }
        }
        else {
            $stableSamples = 0
        }
        Start-Sleep -Milliseconds 250
    }

    return [pscustomobject]@{
        ready = $false
        reason = 'timeout waiting for stable UI/ThisPC content'
        state = Get-BlinkState $Process
    }
}

function Get-WerEvidence(
    [datetime] $Start,
    [datetime] $End,
    [int] $ProcessId) {
    $events = @()
    try {
        $events = @(Get-WinEvent -FilterHashtable @{
            LogName = 'Application'
            StartTime = $Start.AddSeconds(-1)
            EndTime = $End.AddSeconds(1)
        } -ErrorAction Stop)
    }
    catch {
        # No matching Application events is the normal no-crash result for a
        # short action window. The process state remains authoritative.
        return @()
    }

    $decimalPid = '(?<!\d){0}(?!\d)' -f [regex]::Escape([string] $ProcessId)
    $hexPid = '(?i)(?<![0-9a-f])0x{0}(?![0-9a-f])' -f ('{0:X}' -f $ProcessId)
    $result = @()
    foreach ($event in $events) {
        $message = [string] $event.Message
        if ($message -notmatch '(?i)Blink\.exe') {
            continue
        }
        if ($event.ProviderName -notmatch 'Application Error|Windows Error Reporting') {
            continue
        }
        $exception = $null
        if ($message -match '(?i)(?:exception code|异常代码)\s*[:：]\s*(0x[0-9a-f]+)') {
            $exception = $Matches[1]
        }
        $module = $null
        if ($message -match '(?i)(?:faulting module|出错模块名称)\s*[:：]\s*([^\r\n]+)') {
            $module = $Matches[1].Trim()
        }
        $result += [pscustomobject]@{
            provider = [string] $event.ProviderName
            id = [int] $event.Id
            time = $event.TimeCreated.ToString('o')
            pid_match = ($message -match $decimalPid -or $message -match $hexPid)
            exception = $exception
            module = $module
            message = if ($message.Length -gt 2000) { $message.Substring(0, 2000) } else { $message }
        }
    }
    return @($result)
}

function Record-Action(
    [string] $Scenario,
    [int] $Run,
    [int] $Invocation,
    [string] $Action,
    [System.Diagnostics.Process] $Process,
    [datetime] $Started,
    [string] $ErrorText = '') {
    $ended = Get-Date
    $state = Get-BlinkState $Process
    $wer = Get-WerEvidence $Started $ended $Process.Id
    $result = [pscustomobject]@{
        scenario = $Scenario
        run = $Run
        invocation = $Invocation
        action = $Action
        pid = $Process.Id
        process_alive = $state.alive
        exit_code = $state.exit_code
        exit_code_hex = $state.exit_code_hex
        tab_count = $state.tab_count
        body_rows = $state.body_rows
        tree_items = $state.tree_items
        wer_events = @($wer)
        error = $ErrorText
        started = $Started.ToString('o')
        ended = $ended.ToString('o')
    }
    [void] $allResults.Add($result)
    if (-not $state.alive) {
        $script:crashObserved = $true
    }
    Write-Host ('{0} run={1} invocation={2} pid={3} alive={4} tabs={5} exit={6}' -f `
        $Scenario, $Run, $Invocation, $Process.Id, $state.alive, $state.tab_count, $state.exit_code_hex)
    return $result
}

function Invoke-AddButton(
    [System.Diagnostics.Process] $Process,
    [string] $Scenario,
    [int] $Run,
    [int] $Invocation) {
    $started = Get-Date
    $errorText = ''
    try {
        $before = (Get-BlinkState $Process).tab_count
        $window = Get-BlinkWindow $Process
        $button = Find-DescendantByAutomationId $window 'AddButton'
        if ($null -eq $button) {
            throw 'AddButton automation element not found'
        }
        $button.GetCurrentPattern(
            [System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        Start-Sleep -Milliseconds 350
        $ready = Wait-BlinkReady -Process $Process -MinimumTabs ($before + 1) `
            -TimeoutSeconds $ActionTimeoutSeconds
        if (-not $ready.ready -and (Get-BlinkState $Process).alive) {
            throw $ready.reason
        }
    }
    catch {
        $errorText = $_.Exception.Message
    }
    return Record-Action $Scenario $Run $Invocation 'AddButton.InvokePattern' `
        $Process $started $errorText
}

function Invoke-CtrlT(
    [System.Diagnostics.Process] $Process,
    [string] $Scenario,
    [int] $Run,
    [int] $Invocation) {
    $started = Get-Date
    $errorText = ''
    try {
        $before = (Get-BlinkState $Process).tab_count
        $window = Get-BlinkWindow $Process
        if ($null -eq $window) {
            throw 'Blink window not available'
        }
        $focused = [BlinkTabLifetimeNativeInput]::SetForegroundWindow(
            $Process.MainWindowHandle)
        Start-Sleep -Milliseconds 150
        [BlinkTabLifetimeNativeInput]::SendCtrlT()
        Start-Sleep -Milliseconds 350
        $ready = Wait-BlinkReady -Process $Process -MinimumTabs ($before + 1) `
            -TimeoutSeconds $ActionTimeoutSeconds
        if (-not $ready.ready -and (Get-BlinkState $Process).alive) {
            throw $ready.reason
        }
        if (-not $focused) {
            Write-Host ('warning=SetForegroundWindow returned false pid={0}' -f $Process.Id)
        }
    }
    catch {
        $errorText = $_.Exception.Message
    }
    return Record-Action $Scenario $Run $Invocation 'Ctrl+T' `
        $Process $started $errorText
}

function Select-TabIndex(
    [System.Diagnostics.Process] $Process,
    [int] $Index,
    [string] $Scenario,
    [int] $Run,
    [int] $Invocation) {
    $started = Get-Date
    $errorText = ''
    try {
        $window = Get-BlinkWindow $Process
        $tabs = @(Get-TabItems $window)
        if ($Index -lt 0 -or $Index -ge $tabs.Count) {
            throw "Tab index $Index unavailable; count=$($tabs.Count)"
        }
        try {
            $tabs[$Index].GetCurrentPattern(
                [System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        }
        catch {
            $tabs[$Index].GetCurrentPattern(
                [System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        }
        Start-Sleep -Milliseconds 350
        $ready = Wait-BlinkReady -Process $Process -MinimumTabs $tabs.Count `
            -TimeoutSeconds $ActionTimeoutSeconds
        if (-not $ready.ready -and (Get-BlinkState $Process).alive) {
            throw $ready.reason
        }
    }
    catch {
        $errorText = $_.Exception.Message
    }
    return Record-Action $Scenario $Run $Invocation 'TabViewItem.Select' `
        $Process $started $errorText
}

function Select-TreeRoot(
    [System.Diagnostics.Process] $Process,
    [string] $Scenario,
    [int] $Run) {
    $started = Get-Date
    $errorText = ''
    try {
        $window = Get-BlinkWindow $Process
        $tree = Find-DescendantByAutomationId $window 'ListControl'
        $items = @(Find-DescendantsByControlType `
            $tree `
            ([System.Windows.Automation.ControlType]::TreeItem))
        if ($items.Count -eq 0) {
            throw 'Tree root automation element not found'
        }
        try {
            $items[0].GetCurrentPattern(
                [System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        }
        catch {
            $items[0].GetCurrentPattern(
                [System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        }
        Start-Sleep -Milliseconds 500
    }
    catch {
        $errorText = $_.Exception.Message
    }
    return Record-Action $Scenario $Run 0 'TreeRoot.Select' `
        $Process $started $errorText
}

function Run-AddButtonScenario([int] $Run) {
    $session = $null
    try {
        $script:currentTelemetryPath = Join-Path $runRoot ('add-run-{0}-startup.log' -f $Run)
        $session = Start-BlinkSession ('add-run-{0}' -f $Run)
        $result = Invoke-AddButton $session.process 'AddButton' $Run 1
        return ($result.process_alive -and [string]::IsNullOrWhiteSpace($result.error))
    }
    finally {
        if ($null -ne $session) {
            Stop-BlinkProcess $session.process
        }
    }
}

function Run-TabSwitchScenario {
    $session = $null
    $run = 1
    try {
        $script:currentTelemetryPath = Join-Path $runRoot 'tab-switch-startup.log'
        $session = Start-BlinkSession 'tab-switch'
        $ok = $true
        for ($i = 1; $i -le 3; $i++) {
            $result = Invoke-AddButton $session.process 'TabSwitch' $run $i
            if (-not $result.process_alive -or -not [string]::IsNullOrWhiteSpace($result.error)) {
                $ok = $false
                break
            }
        }
        if ($ok) {
            $tabs = @(Get-TabItems (Get-BlinkWindow $session.process))
            for ($i = 0; $i -lt $tabs.Count; $i++) {
                $result = Select-TabIndex $session.process $i 'TabSwitch' $run ($i + 1)
                if (-not $result.process_alive -or -not [string]::IsNullOrWhiteSpace($result.error)) {
                    $ok = $false
                    break
                }
            }
        }
        if ($ok) {
            $result = Select-TreeRoot $session.process 'TabSwitch' $run
            $ok = $result.process_alive -and [string]::IsNullOrWhiteSpace($result.error)
        }
        return $ok
    }
    finally {
        if ($null -ne $session) {
            Stop-BlinkProcess $session.process
        }
    }
}

function Run-CtrlTScenario([int] $Run) {
    $session = $null
    try {
        $script:currentTelemetryPath = Join-Path $runRoot ('ctrl-t-run-{0}-startup.log' -f $Run)
        $session = Start-BlinkSession ('ctrl-t-run-{0}' -f $Run)
        $ok = $true
        for ($i = 1; $i -le $CtrlTCount; $i++) {
            $result = Invoke-CtrlT $session.process 'Ctrl+T' $Run $i
            if (-not $result.process_alive -or -not [string]::IsNullOrWhiteSpace($result.error)) {
                $ok = $false
                break
            }
        }
        return $ok
    }
    finally {
        if ($null -ne $session) {
            Stop-BlinkProcess $session.process
        }
    }
}

$fatalError = $null
$startedAt = Get-Date
try {
    Write-Host "Blink tab lifetime smoke: $blinkExe"
    Write-Host "Run root: $runRoot"

    if (-not (Run-AddButtonScenario 1)) {
        throw 'AddButton regression failed; bounded run stopped.'
    }
    if ($crashObserved) {
        throw 'Crash observed after AddButton; bounded run stopped.'
    }

    if (-not (Run-TabSwitchScenario)) {
        throw 'TabView/tree switching regression failed; bounded run stopped.'
    }
    if ($crashObserved) {
        throw 'Crash observed during TabView/tree switching; bounded run stopped.'
    }

    for ($run = 1; $run -le $RepeatRuns; $run++) {
        if (-not (Run-AddButtonScenario $run)) {
            throw "AddButton bounded repeat failed on run $run."
        }
        if ($crashObserved) {
            throw "Crash observed during AddButton bounded repeat run $run."
        }
    }

    for ($run = 1; $run -le $RepeatRuns; $run++) {
        if (-not (Run-CtrlTScenario $run)) {
            throw "Ctrl+T bounded repeat failed on run $run."
        }
        if ($crashObserved) {
            throw "Crash observed during Ctrl+T bounded repeat run $run."
        }
    }
}
catch {
    $fatalError = $_.Exception.Message
}
finally {
    $report = [ordered]@{
        configuration = $Configuration
        executable = $blinkExe
        started = $startedAt.ToString('o')
        ended = (Get-Date).ToString('o')
        ctrl_t_count = $CtrlTCount
        repeat_runs = $RepeatRuns
        crash_observed = $crashObserved
        fatal_error = $fatalError
        results = @($allResults.ToArray())
    }
    [System.IO.File]::WriteAllText(
        $reportPath,
        ($report | ConvertTo-Json -Depth 10),
        $utf8)
    Write-Host "Report: $reportPath"
}

if ($null -ne $fatalError) {
    Write-Error $fatalError
    exit 1
}

Write-Host 'Blink tab lifetime smoke: PASS'
exit 0
