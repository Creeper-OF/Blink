param(
    [string[]] $Probe = @('P0', 'P1', 'P2', 'P3', 'P4', 'P5', 'P6'),
    [ValidateRange(12, 60)]
    [int] $SurvivalSeconds = 12
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$blinkExe = Join-Path $projectRoot 'src\Blink\x64\Release\Blink.exe'
$runStamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$probeRoot = Join-Path $projectRoot ('.blink-data\phase1a-startup\ui-probes\{0}' -f $runStamp)
$summaryPath = Join-Path $probeRoot 'probe-results.json'
$requestedProbes = @(
    $Probe |
        ForEach-Object { $_ -split ',' } |
        ForEach-Object { $_.Trim().ToUpperInvariant() } |
        Where-Object { $_ -ne '' }
)
foreach ($requestedProbe in $requestedProbes) {
    if ($requestedProbe -notin @('P0', 'P1', 'P2', 'P3', 'P4', 'P5', 'P6')) {
        throw "Probe must be one of P0, P1, P2, P3, P4, P5, or P6; received '$requestedProbe'."
    }
}

function Write-Utf8Text([string] $path, [string] $text) {
    $parent = Split-Path -Parent $path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText(
        $path,
        $text,
        [System.Text.UTF8Encoding]::new($false))
}

function Add-ProbeLine([System.Collections.Generic.List[string]] $lines, [string] $line) {
    $lines.Add($line)
    [Console]::WriteLine($line)
}

function Query-ProbeEvents(
    [System.Collections.Generic.List[string]] $lines,
    [datetime] $queryStart,
    [datetime] $queryEnd,
    [int] $processId) {
    $decimalPidPattern = '(?<!\d){0}(?!\d)' -f [regex]::Escape([string] $processId)
    $hexPidPattern = '(?i)(?<![0-9a-f])0x{0}(?![0-9a-f])' -f ('{0:X}' -f $processId)
    $pidMatchedEvents = [System.Collections.Generic.List[object]]::new()
    $unattributedEvents = [System.Collections.Generic.List[object]]::new()
    $eventLogs = @('Application', 'Microsoft-Windows-AppModel-Runtime/Admin')

    foreach ($logName in $eventLogs) {
        try {
            $events = Get-WinEvent -FilterHashtable @{
                LogName = $logName
                StartTime = $queryStart
                EndTime = $queryEnd
            } -ErrorAction Stop

            foreach ($event in $events) {
                if (-not ($event.TimeCreated -ge $queryStart -and $event.TimeCreated -le $queryEnd)) {
                    continue
                }

                $message = [string] $event.Message
                $provider = [string] $event.ProviderName
                $providerMatch = $provider -match 'Application Error|\.NET Runtime|Windows Error Reporting|AppModel'
                $blinkMatch = $message -match '(?i)\bBlink\.exe\b'
                if (-not ($providerMatch -and $blinkMatch)) {
                    continue
                }

                $pidMatch = $false
                foreach ($messageLine in ($message -split "`r?`n")) {
                    if ($messageLine -match '(?i)(?:process\s+id|pid)') {
                        if ($messageLine -match $decimalPidPattern -or $messageLine -match $hexPidPattern) {
                            $pidMatch = $true
                            break
                        }
                    }
                }

                $eventEvidence = [pscustomobject]@{
                    log_name = $logName
                    time_created = $event.TimeCreated.ToString('o')
                    provider = $provider
                    event_id = $event.Id
                    message = ($message -replace "`r?`n", ' ')
                }

                if ($pidMatch) {
                    $pidMatchedEvents.Add($eventEvidence)
                    Add-ProbeLine $lines '--- PID-matched Windows Event ---'
                }
                else {
                    $unattributedEvents.Add($eventEvidence)
                    Add-ProbeLine $lines '--- UNATTRIBUTED_BLINK_EVENT ---'
                }

                Add-ProbeLine $lines ('LogName = {0}' -f $logName)
                Add-ProbeLine $lines ('TimeCreated = {0}' -f $event.TimeCreated.ToString('o'))
                Add-ProbeLine $lines ('Provider = {0}' -f $provider)
                Add-ProbeLine $lines ('EventId = {0}' -f $event.Id)
                Add-ProbeLine $lines ('PIDMatched = {0}' -f $pidMatch)
                Add-ProbeLine $lines ('Message = {0}' -f ($message -replace "`r?`n", ' '))
            }
        }
        catch {
            Add-ProbeLine $lines ('EventLogQuery = {0}; unavailable/read failed: {1}' -f $logName, $_.Exception.Message)
        }
    }

    if ($pidMatchedEvents.Count -eq 0) {
        Add-ProbeLine $lines 'No current-PID Windows Event Log evidence was found.'
    }

    if ($unattributedEvents.Count -gt 0) {
        Add-ProbeLine $lines ('UnattributedBlinkEventCount = {0}' -f $unattributedEvents.Count)
    }

    return [pscustomobject]@{
        pid_matched_events = @($pidMatchedEvents.ToArray())
        unattributed_events = @($unattributedEvents.ToArray())
    }
}

function Invoke-UiProbe([string] $probeName) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $probeRootForRun = Join-Path $probeRoot ('probe-{0}' -f $probeName)
    $startupTelemetry = Join-Path $probeRootForRun ('probe-{0}-startup.log' -f $probeName)
    $diagnosticReport = Join-Path $probeRootForRun ('probe-{0}-report.log' -f $probeName)
    $startedAtUtc = [DateTime]::UtcNow
    $startedAt = $startedAtUtc.ToLocalTime()
    $process = $null
    $processId = 0
    $naturalExitCode = $null
    $diagnosticExitCode = $null
    $naturalStatus = 'NOT_STARTED'
    $termination = 'none'
    $naturalExitAtUtc = $null
    $observationEndUtc = $null
    $diagnosticExitAtUtc = $null

    Add-ProbeLine $lines ('Probe = {0}' -f $probeName)
    Add-ProbeLine $lines ('ReleaseExe = {0}' -f $blinkExe)
    Add-ProbeLine $lines ('StartedLocal = {0}' -f $startedAt.ToString('o'))
    Add-ProbeLine $lines ('StartedUtc = {0}' -f $startedAt.ToUniversalTime().ToString('o'))
    Add-ProbeLine $lines ('UiProbeEnvironment = {0}' -f $probeName)
    Add-ProbeLine $lines ('StartupTelemetry = {0}' -f $startupTelemetry)

    try {
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = $blinkExe
        $psi.WorkingDirectory = Split-Path -Parent $blinkExe
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $false
        $psi.EnvironmentVariables['BLINK_PHASE1A_UI_PROBE'] = $probeName
        $psi.EnvironmentVariables['BLINK_STARTUP_TELEMETRY'] = $startupTelemetry

        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $psi
        $null = $process.Start()
        $processId = $process.Id
        Add-ProbeLine $lines ('PID = {0}' -f $processId)

        $survivalDeadlineUtc = $startedAtUtc.AddSeconds($SurvivalSeconds)
        while (-not $process.HasExited -and [DateTime]::UtcNow -lt $survivalDeadlineUtc) {
            Start-Sleep -Milliseconds 50
            $process.Refresh()
        }

        $process.Refresh()
        $hasExited = $process.HasExited
        $processExitAtUtc = $null
        if ($hasExited) {
            # Windows PowerShell can expose Process.ExitTime as the FILETIME
            # epoch (1601-01-01) immediately after HasExited becomes true.
            # The observation timestamp is the reliable boundary for this
            # harness; do not let an uninitialized ExitTime widen or corrupt
            # the event-log query window.
            $processExitAtUtc = [DateTime]::UtcNow
        }

        if ($hasExited -and $processExitAtUtc -le $survivalDeadlineUtc) {
            $process.Refresh()
            $naturalExitCode = $process.ExitCode
            $naturalExitAtUtc = $processExitAtUtc
            $observationEndUtc = $naturalExitAtUtc
            if ($naturalExitCode -eq 0) {
                $naturalStatus = 'NATURAL_EARLY_EXIT'
            }
            else {
                $naturalStatus = 'NATURAL_CRASH'
            }
            Add-ProbeLine $lines ('ExitedBeforeLimit = True')
            Add-ProbeLine $lines ('NaturalExitCode = {0}' -f $naturalExitCode)
        }
        else {
            $naturalStatus = 'SURVIVED'
            $observationEndUtc = $survivalDeadlineUtc
            Add-ProbeLine $lines ('SurvivedSeconds = {0}' -f $SurvivalSeconds)
            Add-ProbeLine $lines 'Result = SURVIVED'

            if ($hasExited) {
                $termination = 'post-deadline-exit-not-natural-classification'
                Add-ProbeLine $lines 'Process exited after the natural observation deadline; this is not classified as NATURAL_CRASH.'
            }
            else {
                $closeRequested = $false
                try {
                    $closeRequested = $process.CloseMainWindow()
                }
                catch {
                    Add-ProbeLine $lines ('CloseMainWindowError = {0}' -f $_.Exception.Message)
                }

                if ($closeRequested -and $process.WaitForExit(2000)) {
                    $termination = 'diagnostic-termination-CloseMainWindow'
                }
                elseif ($process.HasExited) {
                    $termination = 'diagnostic-termination-post-close-exit'
                }
                else {
                    $process.Kill()
                    $null = $process.WaitForExit(2000)
                    $termination = 'diagnostic-termination-StopProcess-equivalent'
                }

                $process.Refresh()
                if ($process.HasExited) {
                    $diagnosticExitCode = $process.ExitCode
                    $diagnosticExitAtUtc = [DateTime]::UtcNow
                }
            }
        }
    }
    catch {
        $naturalStatus = 'HARNESS_ERROR'
        Add-ProbeLine $lines ('HarnessError = {0}' -f $_.Exception.Message)
        if ($process) {
            if ($process.HasExited) {
                $process.Refresh()
                $diagnosticExitCode = $process.ExitCode
                $diagnosticExitAtUtc = [DateTime]::UtcNow
            }
            else {
                try {
                    $process.Kill()
                    $null = $process.WaitForExit(2000)
                    $termination = 'diagnostic-termination-after-harness-error'
                    $diagnosticExitCode = $process.ExitCode
                    $diagnosticExitAtUtc = [DateTime]::UtcNow
                }
                catch {
                    Add-ProbeLine $lines ('DiagnosticTerminationError = {0}' -f $_.Exception.Message)
                }
            }
        }
    }
    finally {
        if (-not $observationEndUtc) {
            $observationEndUtc = [DateTime]::UtcNow
        }
        $survivedSeconds = [math]::Round(($observationEndUtc - $startedAtUtc).TotalSeconds, 3)
        Add-ProbeLine $lines ('ObservationEndLocal = {0}' -f $observationEndUtc.ToLocalTime().ToString('o'))
        Add-ProbeLine $lines ('ObservationEndUtc = {0}' -f $observationEndUtc.ToString('o'))
        Add-ProbeLine $lines ('NaturalStatus = {0}' -f $naturalStatus)
        Add-ProbeLine $lines ('NaturalExitCode = {0}' -f $naturalExitCode)
        Add-ProbeLine $lines ('DiagnosticExitCode = {0}' -f $diagnosticExitCode)
        Add-ProbeLine $lines ('SurvivedSeconds = {0:N3}' -f $survivedSeconds)
        Add-ProbeLine $lines ('DiagnosticTermination = {0}' -f $termination)
        Add-ProbeLine $lines ('StartupTelemetryExists = {0}' -f (Test-Path -LiteralPath $startupTelemetry -PathType Leaf))
        if ($process) {
            $process.Dispose()
        }
        $eventEvidence = Query-ProbeEvents $lines $startedAt $observationEndUtc.ToLocalTime() $processId
        Add-ProbeLine $lines ('DiagnosticReport = {0}' -f $diagnosticReport)
        Write-Utf8Text $diagnosticReport ($lines -join [Environment]::NewLine)
    }

    return [pscustomobject]@{
        probe = $probeName
        natural_status = $naturalStatus
        natural_exit_code = $naturalExitCode
        survived_seconds = $survivedSeconds
        diagnostic_termination = $termination
        diagnostic_exit_code = $diagnosticExitCode
        pid = $processId
        pid_matched_events = @($eventEvidence.pid_matched_events)
        unattributed_events = @($eventEvidence.unattributed_events)
        started_local = $startedAt.ToString('o')
        natural_exit_local = if ($naturalExitAtUtc) { $naturalExitAtUtc.ToLocalTime().ToString('o') } else { $null }
        observation_end_local = $observationEndUtc.ToLocalTime().ToString('o')
        diagnostic_exit_local = if ($diagnosticExitAtUtc) { $diagnosticExitAtUtc.ToLocalTime().ToString('o') } else { $null }
        startup_telemetry = $startupTelemetry
        report = $diagnosticReport
    }
}

if (-not (Test-Path -LiteralPath $blinkExe -PathType Leaf)) {
    Write-Error ('Release Blink.exe not found. Expected path: {0}' -f $blinkExe)
    exit 1
}

New-Item -ItemType Directory -Force -Path $probeRoot | Out-Null
Write-Output ('ProbeRoot = {0}' -f $probeRoot)
Write-Output ('ReleaseExe = {0}' -f $blinkExe)
Write-Output ('SurvivalLimitSeconds = {0}' -f $SurvivalSeconds)

$results = [System.Collections.Generic.List[object]]::new()
foreach ($probeName in $requestedProbes) {
    $result = Invoke-UiProbe $probeName
    $results.Add($result)
}

Write-Utf8Text $summaryPath (($results | ConvertTo-Json -Depth 5))
Write-Output ('Summary = {0}' -f $summaryPath)
foreach ($result in $results) {
    Write-Output ('Probe {0}: natural_status={1}, natural_exit_code={2}, diagnostic_termination={3}, diagnostic_exit_code={4}' -f
        $result.probe, $result.natural_status, $result.natural_exit_code,
        $result.diagnostic_termination, $result.diagnostic_exit_code)
}

if (@($results | Where-Object { $_.natural_status -eq 'HARNESS_ERROR' }).Count -gt 0) {
    exit 2
}
if (@($results | Where-Object { $_.natural_status -eq 'NATURAL_CRASH' }).Count -gt 0) {
    exit 3
}
exit 0
