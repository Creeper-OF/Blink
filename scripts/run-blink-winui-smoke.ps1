param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [ValidateSet('M3', 'M4', 'R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'G0', 'G1', 'GS0')]
    [string[]] $Mode = @('M3', 'M4'),
    [ValidateRange(12, 60)]
    [int] $SurvivalSeconds = 12,
    [string] $RunId = ''
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($RunId)) {
    $RunId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
}
$runRoot = Join-Path $projectRoot ('.blink-data\phase1a-startup\winui-smoke\{0}' -f $RunId)
$summaryPath = Join-Path $runRoot 'results.json'
$utf8 = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Web.Extensions
$jsonSerializer = [System.Web.Script.Serialization.JavaScriptSerializer]::new()
$jsonSerializer.MaxJsonLength = 16 * 1024 * 1024

function Write-Utf8Text([string] $Path, [string] $Text) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, $utf8)
}

function ConvertTo-JsonString([object] $Value) {
    if ($null -eq $Value) {
        return 'null'
    }
    # PowerShell 5.1's legacy JSON path handling is inconsistent for some
    # non-ASCII Windows paths. JSON uses equivalent forward-slash paths so
    # the artifact remains strictly parseable; report.log keeps native paths.
    $text = ([string] $Value).Replace('\', '/')
    return $jsonSerializer.Serialize($text)
}

function ConvertTo-JsonBool([bool] $Value) {
    if ($Value) { return 'true' }
    return 'false'
}

function ConvertTo-JsonInteger([object] $Value) {
    if ($null -eq $Value) { return 'null' }
    return ([Convert]::ToString([int64] $Value, [Globalization.CultureInfo]::InvariantCulture))
}

function ConvertTo-JsonNumber([object] $Value) {
    if ($null -eq $Value) { return 'null' }
    return ([Convert]::ToString([double] $Value, [Globalization.CultureInfo]::InvariantCulture))
}

function ConvertTo-JsonEvidenceText([object] $Evidence) {
    $items = @()
    foreach ($item in @($Evidence)) {
        if ($null -eq $item) { continue }
        $items += ('{{"log_name":{0},"time_created":{1},"provider":{2},"event_id":{3},"pid_field_seen":{4},"message":{5}}}' -f `
            (ConvertTo-JsonString $item.log_name),
            (ConvertTo-JsonString $item.time_created),
            (ConvertTo-JsonString $item.provider),
            (ConvertTo-JsonInteger $item.event_id),
            (ConvertTo-JsonBool ([bool] $item.pid_field_seen)),
            (ConvertTo-JsonString $item.message))
    }
    return ('[{0}]' -f ($items -join ','))
}

function ConvertTo-JsonResultText([object] $Result) {
    return ('{{"mode":{0},"deployment_model":{1},"natural_status":{2},"natural_exit_code":{3},"natural_exit_code_hex":{4},"survived_seconds":{5},"diagnostic_termination":{6},"diagnostic_exit_code":{7},"diagnostic_exit_code_hex":{8},"pid":{9},"exact_pid_event":{10},"pid_matched_events":{11},"unattributed_events":{12},"loaded_xaml_path":{13},"telemetry_path":{14},"telemetry_exists":{15},"report_path":{16},"started_local":{17},"natural_exit_local":{18},"diagnostic_exit_local":{19}}}' -f `
        (ConvertTo-JsonString $Result.mode),
        (ConvertTo-JsonString $Result.deployment_model),
        (ConvertTo-JsonString $Result.natural_status),
        (ConvertTo-JsonInteger $Result.natural_exit_code),
        (ConvertTo-JsonString $Result.natural_exit_code_hex),
        (ConvertTo-JsonNumber $Result.survived_seconds),
        (ConvertTo-JsonString $Result.diagnostic_termination),
        (ConvertTo-JsonInteger $Result.diagnostic_exit_code),
        (ConvertTo-JsonString $Result.diagnostic_exit_code_hex),
        (ConvertTo-JsonInteger $Result.pid),
        (ConvertTo-JsonBool ([bool] $Result.exact_pid_event)),
        (ConvertTo-JsonEvidenceText $Result.pid_matched_events),
        (ConvertTo-JsonEvidenceText $Result.unattributed_events),
        (ConvertTo-JsonString $Result.loaded_xaml_path),
        (ConvertTo-JsonString $Result.telemetry_path),
        (ConvertTo-JsonBool ([bool] $Result.telemetry_exists)),
        (ConvertTo-JsonString $Result.report_path),
        (ConvertTo-JsonString $Result.started_local),
        (ConvertTo-JsonString $Result.natural_exit_local),
        (ConvertTo-JsonString $Result.diagnostic_exit_local))
}

function Format-ExitCode([object] $Code) {
    if ($null -eq $Code) {
        return $null
    }
    $unsigned = [int64] $Code
    if ($unsigned -lt 0) {
        $unsigned += 4294967296
    }
    return ('0x{0:X8}' -f $unsigned)
}

function Get-LoadedXamlPath([System.Diagnostics.Process] $Process) {
    if ($null -eq $Process) {
        return $null
    }

    try {
        $Process.Refresh()
        foreach ($module in $Process.Modules) {
            if ($module.ModuleName -ieq 'Microsoft.UI.Xaml.dll') {
                return $module.FileName
            }
        }
    }
    catch {
        return $null
    }

    return $null
}

function Query-SmokeEvents(
    [datetime] $Start,
    [datetime] $End,
    [int] $ProcessId,
    [System.Collections.Generic.List[string]] $Lines) {
    $decimalPidPattern = '(?<!\d){0}(?!\d)' -f [regex]::Escape([string] $ProcessId)
    $hexPidPattern = '(?i)(?<![0-9a-f])0x{0}(?![0-9a-f])' -f ('{0:X}' -f $ProcessId)
    $pidMatched = [System.Collections.Generic.List[object]]::new()
    $unattributed = [System.Collections.Generic.List[object]]::new()
    $logs = @('Application', 'Microsoft-Windows-AppModel-Runtime/Admin')

    foreach ($logName in $logs) {
        try {
            $events = Get-WinEvent -FilterHashtable @{
                LogName = $logName
                StartTime = $Start
                EndTime = $End
            } -ErrorAction Stop

            foreach ($event in $events) {
                $message = [string] $event.Message
                $provider = [string] $event.ProviderName
                $providerMatch = $provider -match 'Application Error|\.NET Runtime|Windows Error Reporting|AppModel'
                $imageMatch = $message -match '(?i)\bBlink\.WinUiSmoke\.exe\b'
                if (-not ($providerMatch -and $imageMatch)) {
                    continue
                }

                $pidMatch = $false
                $pidFieldSeen = $false
                foreach ($line in ($message -split "`r?`n")) {
                    if ($line -match '(?i)(process\s+id|pid)') {
                        $pidFieldSeen = $true
                        if ($line -match $decimalPidPattern -or $line -match $hexPidPattern) {
                            $pidMatch = $true
                            break
                        }
                    }
                }

                $evidence = [pscustomobject]@{
                    log_name = $logName
                    time_created = if ($event.TimeCreated) { $event.TimeCreated.ToString('o') } else { $null }
                    provider = $provider
                    event_id = $event.Id
                    pid_field_seen = $pidFieldSeen
                    message = ($message -replace "`r?`n", ' ')
                }
                if ($pidMatch) {
                    $pidMatched.Add($evidence)
                    $Lines.Add(('exact_pid_event provider={0} id={1}' -f $provider, $event.Id))
                }
                else {
                    $unattributed.Add($evidence)
                    $Lines.Add(('UNATTRIBUTED_BLINK_EVENT provider={0} id={1}' -f $provider, $event.Id))
                }
            }
        }
        catch {
            $Lines.Add(('event_log_unavailable log={0} error={1}' -f $logName, $_.Exception.Message))
        }
    }

    return [pscustomobject]@{
        exact_pid_event = ($pidMatched.Count -gt 0)
        pid_matched_events = @($pidMatched.ToArray())
        unattributed_events = @($unattributed.ToArray())
    }
}

function Invoke-SmokeMode([string] $ModeName) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $modeRoot = Join-Path $runRoot ("mode-{0}" -f $ModeName.ToLowerInvariant())
    $telemetryPath = Join-Path $modeRoot 'smoke-telemetry.log'
    $reportPath = Join-Path $modeRoot 'report.log'
    $startedAt = Get-Date
    $deadline = $startedAt.AddSeconds($SurvivalSeconds)
    $process = $null
    $processId = 0
    $naturalStatus = 'NOT_STARTED'
    $naturalExitCode = $null
    $diagnosticExitCode = $null
    $naturalExitAt = $null
    $diagnosticExitAt = $null
    $diagnosticTermination = 'none'
    $observationEnd = $null
    $loadedXamlPath = $null

    $lines.Add(('mode={0}' -f $ModeName))
    $lines.Add(('pid_event_query_requires_current_pid=True'))
    $lines.Add(('started_local={0}' -f $startedAt.ToString('o')))
    $lines.Add(('telemetry={0}' -f $telemetryPath))

    try {
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $modeExe = Join-Path $projectRoot ("src\Blink.WinUiSmoke\x64\{0}-{1}\Blink.WinUiSmoke.exe" -f $Configuration, $ModeName.ToLowerInvariant())
        $lines.Add(('exe={0}' -f $modeExe))
        $psi.FileName = $modeExe
        $psi.WorkingDirectory = Split-Path -Parent $modeExe
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $false
        $psi.EnvironmentVariables['BLINK_WINUI_SMOKE_MODE'] = $ModeName
        $psi.EnvironmentVariables['BLINK_SMOKE_TELEMETRY'] = $telemetryPath

        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $psi
        $null = $process.Start()
        $processId = $process.Id
        $lines.Add(('pid={0}' -f $processId))

        while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
            if (-not $loadedXamlPath) {
                $loadedXamlPath = Get-LoadedXamlPath $process
            }
            Start-Sleep -Milliseconds 100
            $process.Refresh()
        }

        if (-not $loadedXamlPath) {
            $loadedXamlPath = Get-LoadedXamlPath $process
        }

        $process.Refresh()
        if ($process.HasExited) {
            $naturalExitAt = Get-Date
            $observationEnd = $naturalExitAt
            $naturalExitCode = $process.ExitCode
            if ($naturalExitCode -eq 0) {
                $naturalStatus = 'NATURAL_EARLY_EXIT'
            }
            else {
                $naturalStatus = 'NATURAL_CRASH'
            }
        }
        else {
            $naturalStatus = 'SURVIVED'
            $observationEnd = $deadline
            try {
                if ($process.CloseMainWindow() -and $process.WaitForExit(5000)) {
                    $diagnosticTermination = 'diagnostic-termination-CloseMainWindow'
                }
                elseif (-not $process.HasExited) {
                    $process.Kill()
                    $null = $process.WaitForExit(5000)
                    $diagnosticTermination = 'diagnostic-termination-Kill'
                }
                else {
                    $diagnosticTermination = 'diagnostic-termination-post-close-exit'
                }
            }
            catch {
                $diagnosticTermination = 'diagnostic-termination-error'
                $lines.Add(('diagnostic_termination_error={0}' -f $_.Exception.Message))
            }

            if ($process.HasExited) {
                $process.Refresh()
                $diagnosticExitCode = $process.ExitCode
                $diagnosticExitAt = Get-Date
            }
        }
    }
    catch {
        $naturalStatus = 'HARNESS_ERROR'
        $observationEnd = if ($observationEnd) { $observationEnd } else { Get-Date }
        $lines.Add(('harness_error={0}' -f $_.Exception.Message))
        if ($process -and -not $process.HasExited) {
            try {
                $process.Kill()
                $null = $process.WaitForExit(5000)
                $diagnosticTermination = 'diagnostic-termination-after-harness-error'
                $diagnosticExitCode = $process.ExitCode
                $diagnosticExitAt = Get-Date
            }
            catch {
                $lines.Add(('harness_cleanup_error={0}' -f $_.Exception.Message))
            }
        }
    }

    if (-not $observationEnd) {
        $observationEnd = Get-Date
    }
    $survivedSeconds = [math]::Round(($observationEnd - $startedAt).TotalSeconds, 3)
    $events = Query-SmokeEvents $startedAt $observationEnd $processId $lines
    $telemetryExists = Test-Path -LiteralPath $telemetryPath -PathType Leaf

    $lines.Add(('natural_status={0}' -f $naturalStatus))
    $lines.Add(('natural_exit_code={0}' -f (Format-ExitCode $naturalExitCode)))
    $lines.Add(('survived_seconds={0}' -f $survivedSeconds))
    $lines.Add(('diagnostic_termination={0}' -f $diagnosticTermination))
    $lines.Add(('diagnostic_exit_code={0}' -f (Format-ExitCode $diagnosticExitCode)))
    $lines.Add(('exact_pid_event={0}' -f $events.exact_pid_event))
    $lines.Add(('loaded_xaml_path={0}' -f $loadedXamlPath))
    $lines.Add(('telemetry_exists={0}' -f $telemetryExists))
    Write-Utf8Text $reportPath ($lines -join [Environment]::NewLine)

    if ($process) {
        $process.Dispose()
    }

    return [pscustomobject]@{
        mode = $ModeName
        natural_status = $naturalStatus
        natural_exit_code = $naturalExitCode
        natural_exit_code_hex = Format-ExitCode $naturalExitCode
        survived_seconds = $survivedSeconds
        diagnostic_termination = $diagnosticTermination
        diagnostic_exit_code = $diagnosticExitCode
        diagnostic_exit_code_hex = Format-ExitCode $diagnosticExitCode
        deployment_model = if (@('M4', 'GS0') -contains $ModeName) { 'self-contained' } else { 'framework-dependent' }
        loaded_xaml_path = $loadedXamlPath
        pid = $processId
        exact_pid_event = $events.exact_pid_event
        pid_matched_events = @($events.pid_matched_events)
        unattributed_events = @($events.unattributed_events)
        telemetry_path = $telemetryPath
        telemetry_exists = $telemetryExists
        report_path = $reportPath
        started_local = $startedAt.ToString('o')
        natural_exit_local = if ($naturalExitAt) { $naturalExitAt.ToString('o') } else { $null }
        diagnostic_exit_local = if ($diagnosticExitAt) { $diagnosticExitAt.ToString('o') } else { $null }
    }
}

New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
Write-Output ("RunRoot = {0}" -f $runRoot)
Write-Output ("NaturalObservationSeconds = {0}" -f $SurvivalSeconds)
Write-Output ("Modes = {0}" -f ($Mode -join ', '))

$results = [System.Collections.Generic.List[object]]::new()
foreach ($modeName in $Mode) {
    $modeExe = Join-Path $projectRoot ("src\Blink.WinUiSmoke\x64\{0}-{1}\Blink.WinUiSmoke.exe" -f $Configuration, $modeName.ToLowerInvariant())
    if (-not (Test-Path -LiteralPath $modeExe -PathType Leaf)) {
        throw "Blink.WinUiSmoke.exe not found for $modeName. Build the independent smoke project first. Expected path: $modeExe"
    }
    Write-Output ("Starting {0}" -f $modeName)
    $result = Invoke-SmokeMode $modeName
    $results.Add($result)
    Write-Output ("{0}: {1}, natural_exit={2}, exact_pid_event={3}, xaml={4}" -f $modeName, $result.natural_status, $result.natural_exit_code_hex, $result.exact_pid_event, $result.loaded_xaml_path)
}

$modeJson = @($results | ForEach-Object { ConvertTo-JsonResultText $_ })
$summaryJson = ('{{"configuration":{0},"executable":{1},"run_id":{2},"natural_observation_seconds":{3},"modes":[{4}]}}' -f `
    (ConvertTo-JsonString $Configuration),
    (ConvertTo-JsonString 'Blink.WinUiSmoke.exe'),
    (ConvertTo-JsonString $RunId),
    (ConvertTo-JsonInteger $SurvivalSeconds),
    ($modeJson -join ','))
Write-Utf8Text $summaryPath $summaryJson
Write-Output ("Summary = {0}" -f $summaryPath)
