[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$VolumeRoot = 'D:\',
    [switch]$TestLocalSystem,
    [switch]$ReplaceExistingService,
    [switch]$KeepService,
    [switch]$KeepAcceptanceData,
    [switch]$TargetedSentinelAcceptance,
    [switch]$HardenedLocalSystemCandidate,
    [string]$ExistingStorePath = '',
    [string]$TargetedServiceAccount = '',
    [switch]$PrepareWindowsRestartAcceptance,
    [switch]$CompleteWindowsRestartAcceptance,
    [string]$RunId = ''
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-AdministratorHighIntegrity {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $groups = whoami /groups | Out-String
    $isHigh = $groups -match 'High Mandatory Level|System Mandatory Level'
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or -not $isHigh) {
        Write-Error 'Blink Phase 0.5 elevated acceptance requires an Administrator High Integrity PowerShell. This script will not self-elevate and will not run tests.'
        exit 740
    }
    return [ordered]@{
        user = $identity.Name
        is_administrator = $true
        integrity = if ($groups -match 'System Mandatory Level') { 'System' } else { 'High' }
        process_id = $PID
    }
}

# This is intentionally the first operation. No build, service, file, or test
# action occurs before the administrator/high-integrity gate succeeds.
$privilege = Assert-AdministratorHighIntegrity

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'phase05-harness-common.ps1')

$restartModeCount = @(
    if ($PrepareWindowsRestartAcceptance) { 'prepare' }
    if ($CompleteWindowsRestartAcceptance) { 'complete' }
).Count
if ($restartModeCount -gt 1) {
    throw 'Choose only one of -PrepareWindowsRestartAcceptance or -CompleteWindowsRestartAcceptance.'
}
if (($PrepareWindowsRestartAcceptance -or $CompleteWindowsRestartAcceptance) -and $TargetedSentinelAcceptance) {
    throw 'Windows Restart Acceptance modes cannot be combined with -TargetedSentinelAcceptance.'
}
if ($CompleteWindowsRestartAcceptance) {
    if ([string]::IsNullOrWhiteSpace($RunId) -or $RunId -notmatch '^\d{8}-\d{6}$') {
        throw '-CompleteWindowsRestartAcceptance requires -RunId in yyyyMMdd-HHmmss form.'
    }
    $acceptanceMode = 'windows_restart_complete'
}
elseif ($PrepareWindowsRestartAcceptance) {
    if (-not [string]::IsNullOrWhiteSpace($RunId)) {
        throw '-RunId is only valid with -CompleteWindowsRestartAcceptance.'
    }
    $RunId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $acceptanceMode = 'windows_restart_prepare'
}
elseif ($TargetedSentinelAcceptance) {
    if (-not [string]::IsNullOrWhiteSpace($RunId)) {
        throw '-RunId is only valid with -CompleteWindowsRestartAcceptance.'
    }
    $RunId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $acceptanceMode = 'targeted_sentinel'
}
else {
    if (-not [string]::IsNullOrWhiteSpace($RunId)) {
        throw '-RunId is only valid with -CompleteWindowsRestartAcceptance.'
    }
    $RunId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $acceptanceMode = 'full_phase05'
}
if ($HardenedLocalSystemCandidate -and -not $TargetedSentinelAcceptance) {
    throw '-HardenedLocalSystemCandidate is reserved for the targeted security acceptance mode.'
}
$runId = $RunId
$executionId = if ($CompleteWindowsRestartAcceptance) {
    '{0}-complete-{1}' -f $runId, (Get-Date).ToUniversalTime().ToString('HHmmss')
}
else {
    $runId
}

# Live Harness Artifacts (LOGS, RESULTS, TEMP TELEMETRY) MUST BE OFF-TARGET
# (Placed in %TEMP%\BlinkAcceptance\<RunId> to avoid polluting Target Volume USN)
$harnessLiveRoot = Join-Path $env:TEMP ('BlinkAcceptance\' + $executionId)
$logPath = Join-Path $harnessLiveRoot 'phase05-acceptance.log'
$failurePath = Join-Path $harnessLiveRoot 'phase05-failures.log'
$resultsPath = Join-Path $harnessLiveRoot 'phase05-results.json'
$benchmarkPath = Join-Path $harnessLiveRoot 'phase05-benchmark.csv'
$ipcClientTracePath = Join-Path $harnessLiveRoot 'ipc-client-trace.jsonl'
$manualRestartPath = Join-Path $harnessLiveRoot 'windows-restart-manual.md'
$telemetrySnapshotRoot = Join-Path $harnessLiveRoot 'telemetry-snapshots'
$targetedSearchAttemptsPath = Join-Path $harnessLiveRoot 'targeted-search-attempts.jsonl'

# Final Artifacts Destination (Copied back to project dir after test completion)
$acceptanceRoot = Join-Path $repositoryRoot '.blink-data\acceptance'
$runRoot = Join-Path $acceptanceRoot $runId
$artifactRunRoot = if ($CompleteWindowsRestartAcceptance) {
    # A manually retried Complete must preserve the previous failed attempt.
    Join-Path $runRoot ('complete-' + $executionId)
}
else {
    $runRoot
}
$rebootStatePath = Join-Path $runRoot 'reboot-state.json'
$fixtureOwnershipPath = Join-Path $runRoot 'fixture-ownership.json'

# Service DataRoot MUST BE ON TARGET VOLUME to test D-0021 / InternalExcludedRoots self-indexing!
$serviceDataRoot = if ($TargetedSentinelAcceptance) {
    Join-Path $repositoryRoot ('.blink-data\acceptance\' + $runId + '\service-data-targeted')
}
elseif ($PrepareWindowsRestartAcceptance -or $CompleteWindowsRestartAcceptance) {
    Join-Path $repositoryRoot ('.blink-data\acceptance\' + $runId + '\service-data-restart')
}
else {
    Join-Path $repositoryRoot ('.blink-data\acceptance\' + $runId + '\service-data')
}
$telemetryRoot = Join-Path $serviceDataRoot 'telemetry'

# Dynamic Fixture TestRoot on the project volume, outside .blink-data.
# The repository is expected to be on the target volume for real USN/Search
# observation.  This path is deliberately not an InternalExcludedRoot.
$fixtureRootBase = Join-Path $repositoryRoot '.acceptance-fixtures'
$testRoot = Join-Path $fixtureRootBase $runId

$serviceName = 'Blink.IndexService'
$script:requestId = [uint64]0
$script:serviceInstalledByRun = $false
$script:serviceCreationAttemptedByRun = $false
$script:activeServiceAccount = $null
$script:activeAccountRecord = $null
$script:operationSnapshots = @{}
$script:benchmarks = @()
$script:checks = @()
$script:stageTimeline = [System.Collections.Generic.List[PSCustomObject]]::new()
$script:targetedSearchAttempts = [System.Collections.Generic.List[PSCustomObject]]::new()
$script:targetedWriterJob = $null
$script:targetedAcceptance = $null
$script:restartAcceptance = $null
$script:restartCompletePass = $false
$script:restartServiceStartedByComplete = $false
$script:runtimeServiceTokenEvidence = $null
$script:exitCode = 0

if ($null -eq ('Phase05PipeDiagnostics.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Phase05PipeDiagnostics
{
    public static class Native
    {
        [DllImport("kernel32.dll", EntryPoint = "WaitNamedPipeW", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool WaitNamedPipe(string name, uint timeoutMilliseconds);

        public static bool Probe(string name, out int error)
        {
            var available = WaitNamedPipe(name, 0);
            error = available ? 0 : Marshal.GetLastWin32Error();
            return available;
        }
    }
}
'@ -Language CSharp
}

function Get-NormalizedVolumeRoot {
    param([string]$Path)
    try {
        $resolvedItem = Resolve-Path $Path -ErrorAction SilentlyContinue
        $resolved = if ($null -ne $resolvedItem) { $resolvedItem.Path } else { $Path }
        if ([string]::IsNullOrEmpty($resolved)) { $resolved = $Path }
        $root = [System.IO.Path]::GetPathRoot($resolved)
        if ([string]::IsNullOrEmpty($root)) {
            throw "Empty volume root for path '$Path'"
        }
        return $root.TrimEnd('\').ToUpperInvariant()
    }
    catch {
        throw "Failed to resolve volume root for path '$Path': $($_.Exception.Message)"
    }
}

function Test-SameVolume {
    param([string]$PathA, [string]$PathB)
    $volA = Get-NormalizedVolumeRoot $PathA
    $volB = Get-NormalizedVolumeRoot $PathB
    return $volA -eq $volB
}

$script:targetVolumeRoot = Get-NormalizedVolumeRoot $VolumeRoot
$script:harnessVolumeRoot = Get-NormalizedVolumeRoot $harnessLiveRoot
$script:serviceDataVolumeRoot = Get-NormalizedVolumeRoot $serviceDataRoot
$script:fixtureRootVolumeRoot = Get-NormalizedVolumeRoot $fixtureRootBase

Write-Host ("HarnessLiveRoot  = {0}" -f $harnessLiveRoot)
Write-Host ("HarnessVolume    = {0}" -f $script:harnessVolumeRoot)
Write-Host ("TargetVolume     = {0}" -f $script:targetVolumeRoot)
Write-Host ("ServiceDataRoot  = {0}" -f $serviceDataRoot)
Write-Host ("ServiceDataVolume= {0}" -f $script:serviceDataVolumeRoot)
Write-Host ("FixtureRoot      = {0}" -f $fixtureRootBase)
Write-Host ("FixtureVolume    = {0}" -f $script:fixtureRootVolumeRoot)

if ($script:targetVolumeRoot -eq $script:harnessVolumeRoot) {
    Write-Host 'HarnessIsolation = FAIL'
    Write-Error ("FATAL: Live harness root ({0}, volume={1}) is on the same volume as Target Volume ({2}). " +
        'Harness file writes would pollute Target Volume USN and invalidate acceptance results. ' +
        'Set $env:TEMP to a path on a different volume, or specify a non-target volume root.') -f $harnessLiveRoot, $script:harnessVolumeRoot, $script:targetVolumeRoot
    exit 741
}
Write-Host 'HarnessIsolation = PASS'
if ($script:serviceDataVolumeRoot -ne $script:targetVolumeRoot) {
    Write-Host 'ServiceDataIsolation = FAIL'
    Write-Error ("FATAL: Acceptance service DataRoot ({0}, volume={1}) is not on Target Volume ({2}). " +
        'InternalExcludedRoots self-index validation would be invalid. Set -VolumeRoot to the service DataRoot volume or change the acceptance DataRoot mapping.') -f
        $serviceDataRoot, $script:serviceDataVolumeRoot, $script:targetVolumeRoot
    exit 742
}
Write-Host 'ServiceDataIsolation = PASS'
if ($script:fixtureRootVolumeRoot -ne $script:targetVolumeRoot) {
    Write-Host 'FixtureIsolation = FAIL'
    Write-Error ("FATAL: Acceptance fixture root ({0}, volume={1}) is not on Target Volume ({2}). " +
        'The fixture must be project-local and same-volume for real USN/Search observation; no volume-root fallback is allowed.') -f
        $fixtureRootBase, $script:fixtureRootVolumeRoot, $script:targetVolumeRoot
    exit 743
}
Write-Host 'FixtureIsolation = PASS'

# No harness or project files are created until both volume gates above pass.
New-Item -ItemType Directory -Force -Path $harnessLiveRoot, $telemetrySnapshotRoot, $acceptanceRoot, $runRoot | Out-Null
Set-Content -LiteralPath $logPath -Value '' -Encoding utf8
Set-Content -LiteralPath $failurePath -Value '' -Encoding utf8
Set-Content -LiteralPath $ipcClientTracePath -Value '' -Encoding utf8

$results = [ordered]@{
    schema_version = 1
    run_id = $runId
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    ended_utc = $null
    repository_root = $repositoryRoot
    volume_root = $VolumeRoot
    target_volume_root = $script:targetVolumeRoot
    harness_volume_root = $script:harnessVolumeRoot
    service_data_volume_root = $script:serviceDataVolumeRoot
    privilege = $privilege
    configuration = $Configuration
    acceptance_mode = $acceptanceMode
    execution_id = $executionId
    service_name = $serviceName
    service_data_root = $serviceDataRoot
    test_root = $testRoot
    fixture_root = $testRoot
    fixture_ownership_manifest = $fixtureOwnershipPath
    fixture_ownership = $null
    harness_live_root = $harnessLiveRoot
    service_accounts = @()
    service_token = $null
    volume_probe = $null
    initial_index = $null
    search_benchmark = @()
    search_correctness = $null
    targeted_sentinel_acceptance = $null
    mutations = $null
    stress = $null
    restart_recovery = $null
    service_failure = $null
    persistence = $null
    journal_invalid = $null
    windows_restart_acceptance = if ($PrepareWindowsRestartAcceptance) { 'PREPARE_PENDING_MANUAL_RESTART' } elseif ($CompleteWindowsRestartAcceptance) { 'NOT_EXECUTED' } else { 'NOT EXECUTED' }
    windows_restart_state_path = $rebootStatePath
    windows_restart_instructions = if ($acceptanceMode -eq 'full_phase05') { $manualRestartPath } else { $null }
    artifacts_root = $acceptanceRoot
    artifact_export = 'NOT_EXECUTED'
    artifact_export_error = $null
    stage_timeline = @()
    checks = @()
    failures = @()
    final_result = 'NOT READY'
    phase1_recommendation = 'DO NOT PROCEED'
}

$script:fixtureOwnership = [ordered]@{
    schema_version = 1
    owner = 'Blink Phase 0.5 Acceptance Harness'
    run_id = $runId
    acceptance_mode = $acceptanceMode
    target_volume_root = $script:targetVolumeRoot
    fixture_root_base = $fixtureRootBase
    fixture_root = $testRoot
    created_by_run = $false
    lifecycle_state = 'planned'
    created_utc = $null
    retained_fixture_path = $null
    cleanup_reason = $null
    updated_utc = (Get-Date).ToUniversalTime().ToString('o')
}

if ($CompleteWindowsRestartAcceptance -and (Test-Path -LiteralPath $fixtureOwnershipPath -PathType Leaf)) {
    $persistedOwnership = Get-Content -LiteralPath $fixtureOwnershipPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([string]$persistedOwnership.run_id -ne $runId) {
        throw "Fixture ownership manifest RunId mismatch: $($persistedOwnership.run_id)"
    }
    if ([IO.Path]::GetFullPath([string]$persistedOwnership.fixture_root).TrimEnd('\') -ne
        [IO.Path]::GetFullPath($testRoot).TrimEnd('\')) {
        throw 'Fixture ownership manifest points outside the current project-local fixture root; refusing reuse or cleanup.'
    }
    foreach ($property in @('created_by_run', 'lifecycle_state', 'created_utc', 'retained_fixture_path', 'cleanup_reason')) {
        if ($null -ne $persistedOwnership.PSObject.Properties[$property]) {
            $script:fixtureOwnership[$property] = $persistedOwnership.$property
        }
    }
}

function Save-FixtureOwnership {
    $script:fixtureOwnership.updated_utc = (Get-Date).ToUniversalTime().ToString('o')
    $script:fixtureOwnership | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $fixtureOwnershipPath -Encoding utf8
}

function Set-FixtureOwnershipState {
    param(
        [Parameter(Mandatory)][string]$State,
        [string]$RetainedPath = $null,
        [string]$Reason = $null
    )
    $script:fixtureOwnership.lifecycle_state = $State
    $script:fixtureOwnership.retained_fixture_path = $RetainedPath
    $script:fixtureOwnership.cleanup_reason = $Reason
    Save-FixtureOwnership
    $results.fixture_ownership = $script:fixtureOwnership
}

function Test-CurrentRunOwnedFixture {
    if (-not [bool]$script:fixtureOwnership.created_by_run) {
        return $false
    }
    if (-not (Test-Path -LiteralPath $testRoot -PathType Container)) {
        return $false
    }
    $candidate = [IO.Path]::GetFullPath($testRoot).TrimEnd('\')
    $expected = [IO.Path]::GetFullPath([string]$script:fixtureOwnership.fixture_root).TrimEnd('\')
    $base = [IO.Path]::GetFullPath($fixtureRootBase).TrimEnd('\')
    return $candidate.Equals($expected, [StringComparison]::OrdinalIgnoreCase) -and
        $candidate.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($candidate).Equals($runId, [StringComparison]::OrdinalIgnoreCase)
}

Save-FixtureOwnership

# Same-volume logging is now impossible (we hard-fail above), but keep the flag for defense-in-depth
$script:sameVolumeLogging = $false

$script:memoryLogBuffer = [System.Collections.Generic.List[string]]::new()
$script:memoryFailureBuffer = [System.Collections.Generic.List[string]]::new()

function Flush-LogBuffer {
    if ($script:memoryLogBuffer.Count -gt 0) {
        Add-Content -LiteralPath $logPath -Value $script:memoryLogBuffer -Encoding utf8
        $script:memoryLogBuffer.Clear()
    }
    if ($script:memoryFailureBuffer.Count -gt 0) {
        Add-Content -LiteralPath $failurePath -Value $script:memoryFailureBuffer -Encoding utf8
        $script:memoryFailureBuffer.Clear()
    }
}

function Write-Log {
    param([string]$Message, [switch]$Failure)
    $line = '{0} {1}' -f (Get-Date).ToString('s'), $Message
    if ($script:sameVolumeLogging) {
        $script:memoryLogBuffer.Add($line)
        if ($Failure) {
            $script:memoryFailureBuffer.Add($line)
        }
    }
    else {
        Add-Content -LiteralPath $logPath -Value $line -Encoding utf8
        if ($Failure) {
            Add-Content -LiteralPath $failurePath -Value $line -Encoding utf8
        }
    }
    Write-Host $line
}

function Write-IpcClientTrace {
    param(
        [Parameter(Mandatory)][string]$Stage,
        [int]$RequestType = 0,
        [bool]$Success = $false,
        [bool]$HandleValid = $false,
        [bool]$NativeErrorKnown = $false,
        [int]$NativeError = -1,
        [int]$TimeoutMs = 0,
        [double]$ElapsedMs = -1,
        [string]$Detail = '',
        [Exception]$Exception = $null
    )
    try {
        $record = [ordered]@{
            timestamp_utc = (Get-Date).ToUniversalTime().ToString('o')
            stage = $Stage
            pipe_name = '\\.\pipe\Blink.IndexService.v1'
            request_type = $RequestType
            success = $Success
            handle_valid = $HandleValid
            native_error_known = $NativeErrorKnown
            native_error = $NativeError
            timeout_ms = $TimeoutMs
            elapsed_ms = $ElapsedMs
            detail = $Detail
            exception_type = if ($null -eq $Exception) { $null } else { $Exception.GetType().FullName }
            exception_hresult = if ($null -eq $Exception) { $null } else { [int64]$Exception.HResult }
            exception_message = if ($null -eq $Exception) { $null } else { $Exception.Message }
        }
        $json = $record | ConvertTo-Json -Compress -Depth 8
        Add-Content -LiteralPath $ipcClientTracePath -Value $json -Encoding utf8
    }
    catch {
        # Diagnostic output must never change the IPC behavior being measured.
    }
}

$script:phase05LogAction = {
    param([string]$Message, [bool]$Failure)
    Write-Log -Message $Message -Failure:$Failure
}

function Trace-Stage {
    param([string]$StageName, [scriptblock]$Action)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $startTime = (Get-Date).ToUniversalTime().ToString('o')
    Write-Log ("STAGE START: {0}" -f $StageName)
    $resultStatus = 'PASS'
    try {
        return (&$Action)
    }
    catch {
        $resultStatus = 'FAIL'
        throw
    }
    finally {
        $sw.Stop()
        $endTime = (Get-Date).ToUniversalTime().ToString('o')
        $record = [ordered]@{
            stage_name = $StageName
            start_time = $startTime
            end_time = $endTime
            elapsed_seconds = $sw.Elapsed.TotalSeconds
            result = $resultStatus
        }
        $script:stageTimeline.Add([pscustomobject]$record)
        $results.stage_timeline = @($script:stageTimeline)
        Write-Log ("STAGE END: {0} ({1}, elapsed={2:F2}s)" -f $StageName, $resultStatus, $sw.Elapsed.TotalSeconds)
    }
}

function Add-Check {
    param(
        [string]$Area,
        [string]$Name,
        [ValidateSet('PASS', 'FAIL', 'NOT_EXECUTED', 'SKIP')]
        [string]$Status,
        [string]$Details = '',
        [Nullable[int]]$Win32Error = $null
    )
    $check = [ordered]@{
        area = $Area
        name = $Name
        status = $Status
        details = $Details
        win32_error = $Win32Error
    }
    $script:checks += [pscustomobject]$check
    $results.checks = @($script:checks)
    if ($Status -eq 'FAIL') {
        $failure = [ordered]@{ area = $Area; name = $Name; details = $Details; win32_error = $Win32Error }
        $results.failures = @($results.failures) + [pscustomobject]$failure
        Write-Log -Message ("FAIL {0}/{1}: {2} (Win32={3})" -f $Area, $Name, $Details, $Win32Error) -Failure
    }
    else {
        Write-Log -Message ("{0} {1}/{2}: {3}" -f $Status, $Area, $Name, $Details)
    }
}

function Save-Results {
    $results.ended_utc = (Get-Date).ToUniversalTime().ToString('o')
    $results | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $resultsPath -Encoding utf8
    if ($script:benchmarks.Count -gt 0) {
        $script:benchmarks | Export-Csv -LiteralPath $benchmarkPath -NoTypeInformation -Encoding utf8
    }
    else {
        Set-Content -LiteralPath $benchmarkPath -Value 'run_id,query_type,status,average_ms,p95_ms,peak_working_set_bytes,result_count,details' -Encoding utf8
    }
}

function Add-ResultProperty {
    param([string]$Name, $Value)
    $results[$Name] = $Value
}

function Read-WireString {
    param([IO.BinaryReader]$Reader)
    $length = $Reader.ReadUInt32()
    if ($length -gt 262144) {
        throw "IPC string exceeds acceptance limit: $length"
    }
    if ($length -eq 0) {
        return ''
    }
    $bytes = $Reader.ReadBytes([int]($length * 2))
    if ($bytes.Length -ne $length * 2) {
        throw 'IPC string was truncated'
    }
    return [Text.Encoding]::Unicode.GetString($bytes)
}

function Read-WireIdentity {
    param([IO.BinaryReader]$Reader)
    $volumeSerial = $Reader.ReadUInt64()
    $valid = $Reader.ReadByte() -ne 0
    $fileId = $Reader.ReadBytes(16)
    if ($fileId.Length -ne 16) {
        throw 'IPC identity was truncated'
    }
    $hex = ([BitConverter]::ToString($fileId)).Replace('-', '')
    return [ordered]@{
        valid = $valid
        volume_serial = $volumeSerial
        file_id = $hex
        key = ('{0:X16}:{1}' -f $volumeSerial, $hex)
    }
}

function Read-WireResponse {
    param([byte[]]$Bytes)
    $stream = [IO.MemoryStream]::new($Bytes)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        $magic = $reader.ReadUInt32()
        $kind = $reader.ReadUInt32()
        $protocol = $reader.ReadUInt32()
        $requestId = $reader.ReadUInt64()
        $status = $reader.ReadUInt32()
        $error = $reader.ReadUInt32()
        $serviceState = $reader.ReadUInt32()
        $indexState = $reader.ReadUInt32()
        $operationId = $reader.ReadUInt64()
        $records = $reader.ReadUInt64()
        $files = $reader.ReadUInt64()
        $directories = $reader.ReadUInt64()
        $indexBytes = $reader.ReadUInt64()
        $lastUsn = $reader.ReadInt64()
        $journalId = $reader.ReadUInt64()
        $message = Read-WireString $reader
        $resultCount = $reader.ReadUInt32()
        if ($resultCount -gt 10000) {
            throw "IPC result count exceeds acceptance limit: $resultCount"
        }
        $hits = @()
        for ($i = 0; $i -lt $resultCount; $i++) {
            $identity = Read-WireIdentity $reader
            $parent = Read-WireIdentity $reader
            $attributes = $reader.ReadUInt32()
            $hitUsn = $reader.ReadInt64()
            $isDirectory = $reader.ReadByte() -ne 0
            $name = Read-WireString $reader
            $fullPath = Read-WireString $reader
            $hits += [pscustomobject][ordered]@{
                identity = [pscustomobject]$identity
                parent_identity = [pscustomobject]$parent
                attributes = $attributes
                last_usn = $hitUsn
                is_directory = $isDirectory
                name = $name
                full_path = $fullPath
            }
        }
        if ($magic -ne 0x31504C42 -or $kind -ne 2 -or $protocol -ne 1) {
            throw 'IPC response magic, kind, or protocol mismatch'
        }
        return [pscustomobject][ordered]@{
            protocol_version = $protocol
            request_id = $requestId
            status_code = $status
            status = (@('Ok', 'InvalidRequest', 'NotFound', 'Busy', 'Failed', 'Unsupported')[$status])
            error = $error
            service_state_code = $serviceState
            service_state = (@('Starting', 'Running', 'Stopping', 'Stopped', 'Faulted')[$serviceState])
            index_state_code = $indexState
            index_state = (@('Unknown', 'Healthy', 'CatchingUp', 'RebuildRequired', 'Rebuilding', 'Unavailable', 'Corrupted')[$indexState])
            operation_id = $operationId
            records_seen = $records
            files = $files
            directories = $directories
            index_bytes = $indexBytes
            last_usn = $lastUsn
            journal_id = $journalId
            message = $message
            results = @($hits)
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Write-WireString {
    param([IO.BinaryWriter]$Writer, [string]$Value)
    if ($null -eq $Value) {
        $Value = ''
    }
    $bytes = [Text.Encoding]::Unicode.GetBytes($Value)
    $Writer.Write([uint32]$Value.Length)
    if ($bytes.Length -gt 0) {
        $Writer.Write($bytes)
    }
}

function Send-PipeRequest {
    param(
        [int]$Type,
        [string]$RequestVolumeRoot = '',
        [uint64]$OperationId = 0,
        [string]$Text = '',
        [string]$Extension = '',
        [string]$PathPrefix = '',
        [switch]$Prefix,
        [uint32]$Limit = 200,
        [int]$TimeoutMs = 5000
    )
    # Keep the acceptance client aligned with the hardened pipe DACL.  The
    # default NamedPipeClientStream constructor requests GENERIC_READ /
    # GENERIC_WRITE, which also maps to FILE_APPEND_DATA and extended
    # attributes.  Those rights are intentionally not granted to the client
    # ACE (and FILE_CREATE_PIPE_INSTANCE is server-only).  Request the exact
    # client rights used by Blink.Index.Contracts instead.
    $pipeClientRights = [IO.Pipes.PipeAccessRights]::ReadData -bor
        [IO.Pipes.PipeAccessRights]::WriteData -bor
        [IO.Pipes.PipeAccessRights]::ReadAttributes -bor
        [IO.Pipes.PipeAccessRights]::WriteAttributes -bor
        [IO.Pipes.PipeAccessRights]::ReadPermissions -bor
        [IO.Pipes.PipeAccessRights]::Synchronize
    $pipe = [IO.Pipes.NamedPipeClientStream]::new(
        '.',
        'Blink.IndexService.v1',
        $pipeClientRights,
        [IO.Pipes.PipeOptions]::None,
        [Security.Principal.TokenImpersonationLevel]::Impersonation,
        [IO.HandleInheritability]::None)
    $ipcStopwatch = [Diagnostics.Stopwatch]::StartNew()
    $writer = $null
    $requestStream = $null
    $responseStream = $null
    try {
        $probeError = 0
        try {
            $pipeAvailable = [Phase05PipeDiagnostics.Native]::Probe('\\.\pipe\Blink.IndexService.v1', [ref]$probeError)
            Write-IpcClientTrace -Stage 'wait_named_pipe_probe' -RequestType $Type -Success:$pipeAvailable `
                -NativeErrorKnown:(-not $pipeAvailable) -NativeError $probeError -TimeoutMs $TimeoutMs `
                -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds -Detail 'zero_timeout_probe_before_managed_connect'
        }
        catch {
            Write-IpcClientTrace -Stage 'wait_named_pipe_probe_exception' -RequestType $Type `
                -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds -Detail 'probe_exception' -Exception $_.Exception
        }

        Write-IpcClientTrace -Stage 'client_connect_start' -RequestType $Type -TimeoutMs $TimeoutMs `
            -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds -Detail 'NamedPipeClientStream.Connect'
        try {
            $pipe.Connect($TimeoutMs)
            $handleValid = $false
            try {
                $handleValid = $null -ne $pipe.SafePipeHandle -and
                    -not $pipe.SafePipeHandle.IsInvalid -and
                    -not $pipe.SafePipeHandle.IsClosed
            }
            catch {}
            Write-IpcClientTrace -Stage 'client_connect_return' -RequestType $Type -Success $true `
                -HandleValid $handleValid -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds `
                -Detail 'managed_connect_returned'
        }
        catch {
            $nativeError = Get-NativeErrorFromException -Exception $_.Exception
            Write-IpcClientTrace -Stage 'client_connect_return' -RequestType $Type -Success $false `
                -NativeErrorKnown:($null -ne $nativeError) -NativeError $(if ($null -eq $nativeError) { -1 } else { $nativeError }) `
                -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds `
                -Detail 'managed_connect_threw' -Exception $_.Exception
            throw
        }

        Write-IpcClientTrace -Stage 'set_message_read_mode_start' -RequestType $Type `
            -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds
        try {
            $pipe.ReadMode = [IO.Pipes.PipeTransmissionMode]::Message
            Write-IpcClientTrace -Stage 'set_message_read_mode_return' -RequestType $Type -Success $true `
                -HandleValid $handleValid -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds
        }
        catch {
            $nativeError = Get-NativeErrorFromException -Exception $_.Exception
            Write-IpcClientTrace -Stage 'set_message_read_mode_return' -RequestType $Type -Success $false `
                -NativeErrorKnown:($null -ne $nativeError) -NativeError $(if ($null -eq $nativeError) { -1 } else { $nativeError }) `
                -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds -Exception $_.Exception
            throw
        }
        $script:requestId++
        $requestId = $script:requestId
        $requestStream = [IO.MemoryStream]::new()
        $writer = [IO.BinaryWriter]::new($requestStream)
        $writer.Write([uint32]0x31504C42)
        $writer.Write([uint32]1)
        $writer.Write([uint32]1)
        $writer.Write([uint64]$requestId)
        $writer.Write([uint32]$Type)
        $writer.Write([uint64]$OperationId)
        $writer.Write([byte]([int]$Prefix.IsPresent))
        $writer.Write([uint32]$Limit)
        Write-WireString $writer $RequestVolumeRoot
        Write-WireString $writer $Text
        Write-WireString $writer $Extension
        Write-WireString $writer $PathPrefix
        $writer.Flush()
        $payload = $requestStream.ToArray()
        Write-IpcClientTrace -Stage 'request_write_start' -RequestType $Type -TimeoutMs $TimeoutMs `
            -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds -Detail ("request_id={0},bytes={1}" -f $requestId,$payload.Length)
        try {
            $writeTask = $pipe.WriteAsync($payload, 0, $payload.Length)
            if (-not $writeTask.Wait($TimeoutMs)) {
                throw "IPC request write timed out after $TimeoutMs ms"
            }
            $pipe.Flush()
            Write-IpcClientTrace -Stage 'request_write_return' -RequestType $Type -Success $true `
                -HandleValid $handleValid -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds `
                -Detail ("request_id={0},bytes={1}" -f $requestId,$payload.Length)
        }
        catch {
            $nativeError = Get-NativeErrorFromException -Exception $_.Exception
            Write-IpcClientTrace -Stage 'request_write_return' -RequestType $Type -Success $false `
                -NativeErrorKnown:($null -ne $nativeError) -NativeError $(if ($null -eq $nativeError) { -1 } else { $nativeError }) `
                -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds `
                -Detail ("request_id={0}" -f $requestId) -Exception $_.Exception
            throw
        }

        $responseStream = [IO.MemoryStream]::new()
        $buffer = [byte[]]::new(65536)
        Write-IpcClientTrace -Stage 'response_read_start' -RequestType $Type -TimeoutMs $TimeoutMs `
            -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds -Detail ("request_id={0}" -f $requestId)
        try {
            do {
                $readTask = $pipe.ReadAsync($buffer, 0, $buffer.Length)
                if (-not $readTask.Wait($TimeoutMs)) {
                    throw "IPC response read timed out after $TimeoutMs ms"
                }
                $count = $readTask.Result
                if ($count -le 0) {
                    throw 'IPC response ended before a complete message was received'
                }
                $responseStream.Write($buffer, 0, $count)
            } while (-not $pipe.IsMessageComplete)
            Write-IpcClientTrace -Stage 'response_read_return' -RequestType $Type -Success $true `
                -HandleValid $handleValid -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds `
                -Detail ("request_id={0},bytes={1}" -f $requestId,$responseStream.Length)
        }
        catch {
            $nativeError = Get-NativeErrorFromException -Exception $_.Exception
            Write-IpcClientTrace -Stage 'response_read_return' -RequestType $Type -Success $false `
                -NativeErrorKnown:($null -ne $nativeError) -NativeError $(if ($null -eq $nativeError) { -1 } else { $nativeError }) `
                -TimeoutMs $TimeoutMs -ElapsedMs $ipcStopwatch.Elapsed.TotalMilliseconds `
                -Detail ("request_id={0}" -f $requestId) -Exception $_.Exception
            throw
        }
        return Read-WireResponse $responseStream.ToArray()
    }
    finally {
        if ($null -ne $writer) { $writer.Dispose() }
        if ($null -ne $requestStream) { $requestStream.Dispose() }
        if ($null -ne $responseStream) { $responseStream.Dispose() }
        $pipe.Dispose()
    }
}

function Get-NativeErrorFromException {
    param([Exception]$Exception)
    $current = $Exception
    while ($null -ne $current) {
        $property = $current.PSObject.Properties['NativeErrorCode']
        if ($null -ne $property -and $null -ne $property.Value) {
            return [int]$property.Value
        }
        $current = $current.InnerException
    }
    return $null
}

function Get-IpcRejectionDiagnostic {
    $path = Join-Path $telemetryRoot 'ipc-rejections.log'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return ''
    }
    return (@(Get-Content -LiteralPath $path -Encoding UTF8 | Select-Object -Last 4) -join '; ')
}

function Wait-ServiceStatus {
    param([string]$Expected = 'Running', [int]$TimeoutSeconds = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
        if ($null -ne $service -and $service.Status.ToString() -eq $Expected) {
            return $service
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    $current = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    $currentState = if ($null -eq $current) { 'Absent' } else { $current.Status }
    throw "Service did not reach $Expected; current status=$currentState"
}

function Stop-InstalledService {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -ne $service -and $service.Status -ne 'Stopped') {
        Write-Log 'Stopping Blink.IndexService'
        Stop-Service -Name $serviceName -Force
        Wait-ServiceStatus -Expected 'Stopped' | Out-Null
    }
}

function Remove-InstalledService {
    Stop-InstalledService
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -ne $service) {
        Write-Log 'Removing Blink.IndexService created for this acceptance run'
        $deleteResult = Invoke-Phase05NativeOemCommand -FilePath 'sc.exe' -Arguments ('delete "{0}"' -f $serviceName)
        foreach ($line in @($deleteResult.stdout -split '[\r\n]+')) {
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                Write-Log ([string]$line)
            }
        }
        foreach ($line in @($deleteResult.stderr -split '[\r\n]+')) {
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                Write-Log ([string]$line) -Failure
            }
        }
        if ($deleteResult.exit_code -ne 0) {
            throw "sc.exe delete failed with exit code $($deleteResult.exit_code)"
        }
        Start-Sleep -Milliseconds 500
    }
    $script:serviceInstalledByRun = $false
    $script:serviceCreationAttemptedByRun = $false
}

function Install-TestService {
    param(
        [ValidateSet('LocalService', 'LocalSystem')][string]$Account,
        [string]$DataRoot,
        [switch]$HardenedCandidate
    )
    if ($HardenedCandidate -and ($Account -ne 'LocalSystem' -or -not $TargetedSentinelAcceptance)) {
        throw 'Hardened service installation is limited to the targeted LocalSystem security acceptance.'
    }
    $existing = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -ne $existing) {
        if (($TargetedSentinelAcceptance -or $PrepareWindowsRestartAcceptance) -and -not $ReplaceExistingService) {
            throw "Blink.IndexService already exists. This acceptance mode will not replace an existing service unless -ReplaceExistingService is supplied."
        }
        Remove-InstalledService
    }
    # Mark ownership before invoking the installer.  CreateService can succeed
    # and a later installer step can still fail before serviceInstalledByRun is
    # set; the finally block must then clean up this run's service.
    $script:serviceCreationAttemptedByRun = $true
    $installer = Join-Path $repositoryRoot 'install-index-service.ps1'
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $installer,
        '-Configuration', $Configuration,
        '-Account', $Account,
        '-DataRoot', $DataRoot,
        '-TelemetryRoot', $telemetryRoot,
        '-Start'
    )
    if ($HardenedCandidate) {
        $arguments += '-HardenedLocalSystemCandidate'
    }
    Invoke-Phase05LoggedCommand -FilePath 'powershell.exe' -Arguments $arguments -LogAction $script:phase05LogAction
    $script:serviceInstalledByRun = $true
    $script:activeServiceAccount = $Account
    Wait-ServiceStatus -Expected 'Running' | Out-Null
    if ($HardenedCandidate) {
        # Capture the actual process token immediately after SCM reports the
        # service running.  This intentionally precedes the first Status IPC
        # request so a pipe connection failure cannot erase runtime-token
        # evidence from the acceptance artifacts.
        try {
            $script:runtimeServiceTokenEvidence = Get-RunningServiceTokenEvidence
            Write-Log 'Runtime service token captured after SCM start and before Status IPC'
        }
        catch {
            $script:runtimeServiceTokenEvidence = $null
            Write-Log ("Runtime service token capture before Status IPC failed: {0}" -f $_.Exception.Message) -Failure
        }
    }
    Add-Check -Area 'Service' -Name ("Install/$Account") -Status 'PASS' -Details 'SCM service installed and running'
}

function Get-ServiceProcessSample {
    $process = Get-Process -Name 'Blink.IndexService' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $process) {
        return $null
    }
    return [pscustomobject][ordered]@{
        pid = $process.Id
        working_set_bytes = [uint64]$process.WorkingSet64
        cpu_seconds = $process.TotalProcessorTime.TotalSeconds
    }
}

function Test-HardenedSecurityEvidence {
    $installerEvidencePath = Join-Path $serviceDataRoot 'security\installer-service-config.json'
    $pipeEvidencePath = Join-Path $serviceDataRoot 'security\pipe-security.json'
    $installerEvidence = $null
    $pipeEvidence = $null
    try {
        if (Test-Path -LiteralPath $installerEvidencePath -PathType Leaf) {
            $installerEvidence = Get-Content -LiteralPath $installerEvidencePath -Raw -Encoding utf8 | ConvertFrom-Json
        }
        $deadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $pipeEvidencePath -PathType Leaf)) {
            Start-Sleep -Milliseconds 100
        }
        if (Test-Path -LiteralPath $pipeEvidencePath -PathType Leaf) {
            $pipeEvidence = Get-Content -LiteralPath $pipeEvidencePath -Raw -Encoding utf8 | ConvertFrom-Json
        }
    }
    catch {
        Write-Log ("Security evidence parse failed: {0}" -f $_.Exception.Message) -Failure
    }

    $serviceTokenEvidence = $script:runtimeServiceTokenEvidence
    if ($null -eq $serviceTokenEvidence) {
        try {
            # Retain the existing fallback for a transient process-token race;
            # the normal path has already captured and persisted evidence
            # before Wait-Healthy/Status IPC begins.
            $serviceTokenEvidence = Get-RunningServiceTokenEvidence
        }
        catch {
            Write-Log ("Runtime service token evidence capture failed: {0}" -f $_.Exception.Message) -Failure
        }
    }

    $serviceConfigPass = $null -ne $installerEvidence -and
        [bool]$installerEvidence.hardened_local_system_candidate -and
        [string]$installerEvidence.logon_account -eq 'LocalSystem' -and
        [string]$installerEvidence.service_sid_name -eq 'NT SERVICE\Blink.IndexService' -and
        [string]$installerEvidence.service_sid_type_requested -eq 'UNRESTRICTED' -and
        @($installerEvidence.required_privileges_requested) -contains 'SeImpersonatePrivilege' -and
        $null -ne $installerEvidence.query_service_config2 -and
        [int]$installerEvidence.query_service_config2.service_sid_type -eq 1 -and
        @($installerEvidence.query_service_config2.required_privileges) -contains 'SeImpersonatePrivilege' -and
        -not [string]::IsNullOrWhiteSpace([string]$installerEvidence.authorized_client_sid)
    Add-Check -Area 'SecurityHardening' -Name 'ServiceConfig2_HardenedCandidate' `
        -Status $(if ($serviceConfigPass) { 'PASS' } else { 'FAIL' }) `
        -Details ("logon_account={0}, service_sid_type={1}, required_privileges={2}, authorized_client_sid={3}" -f
            $installerEvidence.logon_account, $installerEvidence.service_sid_type_requested,
            (@($installerEvidence.required_privileges_requested) -join '|'), $installerEvidence.authorized_client_sid)

    $runtimeToken = if ($null -eq $serviceTokenEvidence) { $null } else { $serviceTokenEvidence.runtime_token }
    $tokenPredicate = if ($null -eq $serviceTokenEvidence) { $null } else { $serviceTokenEvidence.acceptance_predicate }
    $tokenPass = $null -ne $runtimeToken -and
        $null -ne $tokenPredicate -and
        [bool]$tokenPredicate.capture_pass -and
        [bool]$tokenPredicate.process_id_matches_scm -and
        [bool]$tokenPredicate.process_image_matches_expected -and
        [bool]$tokenPredicate.token_user_is_localsystem -and
        [bool]$tokenPredicate.service_sid_present -and
        [bool]$tokenPredicate.se_impersonate_present -and
        [bool]$tokenPredicate.se_impersonate_enabled -and
        [bool]$tokenPredicate.all_token_privileges_recorded
    Add-Check -Area 'SecurityHardening' -Name 'RuntimeProcessToken' `
        -Status $(if ($tokenPass) { 'PASS' } else { 'FAIL' }) `
        -Details ("capture={0}, pid={1}, pid_matches_scm={2}, image_matches={3}, token_user={4}, service_sid_present={5}, service_sid_attributes={6}, SeImpersonate_present={7}, enabled={8}, attributes={9}, privilege_count={10}" -f
            $(if ($null -eq $runtimeToken) { 'MISSING' } else { $runtimeToken.capture_status }),
            $(if ($null -eq $runtimeToken) { 0 } else { $runtimeToken.process_id }),
            $(if ($null -eq $tokenPredicate) { $false } else { $tokenPredicate.process_id_matches_scm }),
            $(if ($null -eq $tokenPredicate) { $false } else { $tokenPredicate.process_image_matches_expected }),
            $(if ($null -eq $runtimeToken) { '' } else { $runtimeToken.token_user_sid }),
            $(if ($null -eq $runtimeToken) { $false } else { $runtimeToken.service_sid_present }),
            $(if ($null -eq $runtimeToken -or $null -eq $runtimeToken.service_sid_group) { '' } else { $runtimeToken.service_sid_group.attributes_hex }),
            $(if ($null -eq $runtimeToken) { $false } else { $runtimeToken.se_impersonate_present }),
            $(if ($null -eq $runtimeToken) { $false } else { $runtimeToken.se_impersonate_enabled }),
            $(if ($null -eq $runtimeToken) { '' } else { $runtimeToken.se_impersonate_attributes_hex }),
            $(if ($null -eq $runtimeToken) { 0 } else { @($runtimeToken.token_privileges).Count }))

    $actualSddl = if ($null -eq $pipeEvidence) { '' } else { [string]$pipeEvidence.actual_pipe_sddl }
    $forbidden = @(';;;AU', ';;;BA', 'S-1-5-11', 'S-1-1-0', 'Authenticated Users', 'Everyone')
    $containsForbidden = @($forbidden | Where-Object { $actualSddl -like ('*' + $_ + '*') }).Count -gt 0
    $serviceSid = if ($null -eq $pipeEvidence) { '' } else { [string]$pipeEvidence.service_sid }
    $authorizedSid = if ($null -eq $pipeEvidence) { '' } else { [string]$pipeEvidence.authorized_client_sid }
    $expectedServerRightsHex = '0x0012019F'
    $actualHasSystemServerRights = $actualSddl -match '(?i)0x0*12019f;;;SY'
    $actualHasServiceServerRights = -not [string]::IsNullOrWhiteSpace($serviceSid) -and
        $actualSddl -match ('(?i)0x0*12019f;;;' + [regex]::Escape($serviceSid))
    $actualHasAuthorizedClientRights = -not [string]::IsNullOrWhiteSpace($authorizedSid) -and
        $actualSddl -match ('(?i)0x0*120183;;;' + [regex]::Escape($authorizedSid))
    $pipePass = $null -ne $pipeEvidence -and
        [string]$pipeEvidence.descriptor_query -eq 'PASS' -and
        [bool]$pipeEvidence.pipe_reject_remote_clients -and
        [string]$pipeEvidence.pipe_client_rights_hex -eq '0x00120183' -and
        [string]$pipeEvidence.pipe_server_rights_hex -eq $expectedServerRightsHex -and
        $actualHasSystemServerRights -and
        $actualHasServiceServerRights -and
        $actualHasAuthorizedClientRights -and
        -not [string]::IsNullOrWhiteSpace($actualSddl) -and
        -not $containsForbidden -and
        $actualSddl -like ('*' + $serviceSid + '*') -and
        $actualSddl -like ('*' + $authorizedSid + '*')
    Add-Check -Area 'SecurityHardening' -Name 'NamedPipe_Dacl_And_RemoteBoundary' `
        -Status $(if ($pipePass) { 'PASS' } else { 'FAIL' }) `
        -Details ("descriptor_query={0}, remote_reject={1}, client_rights={2}, server_rights={3}, system_server_ace={4}, service_server_ace={5}, client_ace={6}, forbidden_ace={7}" -f
            $pipeEvidence.descriptor_query, $pipeEvidence.pipe_reject_remote_clients,
            $pipeEvidence.pipe_client_rights_hex, $pipeEvidence.pipe_server_rights_hex,
            $actualHasSystemServerRights, $actualHasServiceServerRights,
            $actualHasAuthorizedClientRights, $containsForbidden)
    return $serviceConfigPass -and $pipePass -and $tokenPass
}

# Live Harness Telemetry is written to %TEMP% harnessLiveRoot, NOT Target Volume!
function Get-RemainingMilliseconds {
    param([datetime]$Deadline, [int]$CapMilliseconds = 5000)
    $remaining = [math]::Floor(($Deadline - (Get-Date)).TotalMilliseconds)
    if ($remaining -le 0) {
        return 0
    }
    return [int][math]::Min($CapMilliseconds, $remaining)
}

function Get-Telemetry {
    param(
        [uint64]$OperationId,
        [string]$SnapshotName,
        [datetime]$AbsoluteDeadline = [datetime]::MinValue
    )
    $path = Join-Path $telemetryRoot ("operation-{0}.json" -f $OperationId)
    $deadline = (Get-Date).AddSeconds(30)
    if ($AbsoluteDeadline -ne [datetime]::MinValue -and $AbsoluteDeadline -lt $deadline) {
        $deadline = $AbsoluteDeadline
    }
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $telemetry = Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json
            $snapshotPath = Join-Path $telemetrySnapshotRoot ($SnapshotName + '.json')
            $telemetry | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $snapshotPath -Encoding utf8
            $script:operationSnapshots[$SnapshotName] = $telemetry
            return $telemetry
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 250
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    return $null
}

function Get-LatestServiceTelemetry {
    $files = @(Get-ChildItem -LiteralPath $telemetryRoot -Filter 'operation-*.json' -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
    foreach ($file in $files) {
        try {
            $telemetry = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8 | ConvertFrom-Json
            return [ordered]@{
                path = $file.FullName
                operation = $telemetry
            }
        }
        catch {
            Write-Log ("Unable to parse service telemetry {0}: {1}" -f $file.FullName, $_.Exception.Message) -Failure
        }
    }
    return $null
}

function Ensure-Phase05BootEvidenceType {
    if ($null -eq ('Phase05BootEvidence' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class Phase05BootEvidence
{
    [DllImport("kernel32.dll", ExactSpelling = true)]
    public static extern ulong GetTickCount64();
}
'@
    }
}

function Ensure-Phase05TokenEvidenceType {
    if ($null -eq ('Phase05TokenEvidence.Native' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Phase05TokenEvidence
{
    public sealed class TokenGroup
    {
        public string sid { get; set; }
        public uint attributes { get; set; }
        public string attributes_hex { get; set; }
        public bool enabled { get; set; }
        public bool enabled_by_default { get; set; }
        public bool mandatory { get; set; }
        public bool deny_only { get; set; }
        public bool owner { get; set; }
        public bool integrity { get; set; }
        public bool logon_id { get; set; }
    }

    public sealed class TokenPrivilege
    {
        public string name { get; set; }
        public string luid { get; set; }
        public uint attributes { get; set; }
        public string attributes_hex { get; set; }
        public string[] attribute_names { get; set; }
        public bool enabled { get; set; }
        public bool enabled_by_default { get; set; }
        public bool removed { get; set; }
        public bool used_for_access { get; set; }
    }

    public sealed class Snapshot
    {
        public string capture_status { get; set; }
        public int error_code { get; set; }
        public string error_message { get; set; }
        public uint process_id { get; set; }
        public string process_image { get; set; }
        public string service_sid_name { get; set; }
        public string service_sid_expected { get; set; }
        public string service_sid_resolution_error { get; set; }
        public string token_user_sid { get; set; }
        public TokenGroup[] token_groups { get; set; }
        public TokenPrivilege[] token_privileges { get; set; }
        public bool service_sid_present { get; set; }
        public TokenGroup service_sid_group { get; set; }
        public bool se_impersonate_present { get; set; }
        public bool se_impersonate_enabled { get; set; }
        public string se_impersonate_attributes_hex { get; set; }
    }

    internal enum TokenInformationClass
    {
        TokenUser = 1,
        TokenGroups = 2,
        TokenPrivileges = 3
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Luid
    {
        public uint low_part;
        public uint high_part;
    }

    public static class Native
    {
        private const uint ProcessQueryLimitedInformation = 0x1000;
        private const uint TokenQuery = 0x0008;
        private const int ErrorInsufficientBuffer = 122;
        private const uint GroupMandatory = 0x00000001;
        private const uint GroupEnabledByDefault = 0x00000002;
        private const uint GroupEnabled = 0x00000004;
        private const uint GroupOwner = 0x00000008;
        private const uint GroupUseForDenyOnly = 0x00000010;
        private const uint GroupIntegrity = 0x00000020;
        private const uint GroupLogonId = 0xC0000000;
        private const uint PrivilegeEnabledByDefault = 0x00000001;
        private const uint PrivilegeEnabled = 0x00000002;
        private const uint PrivilegeRemoved = 0x00000004;
        private const uint PrivilegeUsedForAccess = 0x80000000;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint desiredAccess, bool inheritHandle, uint processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "QueryFullProcessImageNameW", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryFullProcessImageName(
            IntPtr process,
            uint flags,
            StringBuilder exeName,
            ref uint size);

        [DllImport("advapi32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool OpenProcessToken(
            IntPtr processHandle,
            uint desiredAccess,
            out IntPtr tokenHandle);

        [DllImport("advapi32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetTokenInformation(
            IntPtr tokenHandle,
            TokenInformationClass tokenInformationClass,
            IntPtr tokenInformation,
            uint tokenInformationLength,
            out uint returnLength);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, EntryPoint = "LookupPrivilegeNameW", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool LookupPrivilegeName(
            string systemName,
            ref Luid luid,
            StringBuilder name,
            ref uint nameLength);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, EntryPoint = "LookupAccountNameW", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool LookupAccountName(
            string systemName,
            string accountName,
            byte[] sid,
            ref uint sidSize,
            StringBuilder referencedDomainName,
            ref uint domainSize,
            out int use);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, EntryPoint = "ConvertSidToStringSidW", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ConvertSidToStringSid(
            IntPtr sid,
            out IntPtr stringSid);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LocalFree(IntPtr memory);

        private static void ThrowLastError(string api)
        {
            int error = Marshal.GetLastWin32Error();
            throw new Win32Exception(error, api);
        }

        private static string SidToString(IntPtr sid)
        {
            if (sid == IntPtr.Zero)
            {
                return string.Empty;
            }
            IntPtr text = IntPtr.Zero;
            if (!ConvertSidToStringSid(sid, out text))
            {
                ThrowLastError("ConvertSidToStringSidW");
            }
            try
            {
                return Marshal.PtrToStringUni(text) ?? string.Empty;
            }
            finally
            {
                if (text != IntPtr.Zero)
                {
                    LocalFree(text);
                }
            }
        }

        private static string ResolveAccountSid(string accountName)
        {
            uint sidSize = 0;
            uint domainSize = 0;
            int use;
            LookupAccountName(null, accountName, null, ref sidSize, null, ref domainSize, out use);
            int firstError = Marshal.GetLastWin32Error();
            if (firstError != ErrorInsufficientBuffer || sidSize == 0)
            {
                throw new Win32Exception(firstError, "LookupAccountNameW");
            }

            byte[] sid = new byte[sidSize];
            uint domainCapacity = Math.Max(domainSize, 1u);
            StringBuilder domain = new StringBuilder((int)domainCapacity);
            if (!LookupAccountName(null, accountName, sid, ref sidSize, domain, ref domainCapacity, out use))
            {
                ThrowLastError("LookupAccountNameW");
            }

            GCHandle pinned = GCHandle.Alloc(sid, GCHandleType.Pinned);
            try
            {
                return SidToString(pinned.AddrOfPinnedObject());
            }
            finally
            {
                pinned.Free();
            }
        }

        private static IntPtr QueryTokenBuffer(
            IntPtr token,
            TokenInformationClass informationClass,
            out uint bytes)
        {
            bytes = 0;
            GetTokenInformation(token, informationClass, IntPtr.Zero, 0, out bytes);
            int firstError = Marshal.GetLastWin32Error();
            if (firstError != ErrorInsufficientBuffer || bytes == 0)
            {
                throw new Win32Exception(firstError, "GetTokenInformation(size)");
            }

            IntPtr buffer = Marshal.AllocHGlobal((int)bytes);
            if (!GetTokenInformation(token, informationClass, buffer, bytes, out bytes))
            {
                Marshal.FreeHGlobal(buffer);
                ThrowLastError("GetTokenInformation");
            }
            return buffer;
        }

        private static string PrivilegeName(Luid luid)
        {
            uint characterCount = 0;
            LookupPrivilegeName(null, ref luid, null, ref characterCount);
            int firstError = Marshal.GetLastWin32Error();
            if (characterCount == 0 || (firstError != ErrorInsufficientBuffer && firstError != 0))
            {
                return "LUID_0x" + (((ulong)luid.high_part << 32) | luid.low_part).ToString("X16");
            }

            StringBuilder name = new StringBuilder((int)characterCount + 1);
            if (!LookupPrivilegeName(null, ref luid, name, ref characterCount))
            {
                return "LUID_0x" + (((ulong)luid.high_part << 32) | luid.low_part).ToString("X16");
            }
            return name.ToString();
        }

        private static string[] PrivilegeAttributeNames(uint attributes)
        {
            List<string> names = new List<string>();
            if ((attributes & PrivilegeEnabledByDefault) != 0) names.Add("SE_PRIVILEGE_ENABLED_BY_DEFAULT");
            if ((attributes & PrivilegeEnabled) != 0) names.Add("SE_PRIVILEGE_ENABLED");
            if ((attributes & PrivilegeRemoved) != 0) names.Add("SE_PRIVILEGE_REMOVED");
            if ((attributes & PrivilegeUsedForAccess) != 0) names.Add("SE_PRIVILEGE_USED_FOR_ACCESS");
            return names.ToArray();
        }

        private static TokenGroup ReadGroup(IntPtr buffer, int offset)
        {
            IntPtr sid = Marshal.ReadIntPtr(buffer, offset);
            uint attributes = unchecked((uint)Marshal.ReadInt32(buffer, offset + IntPtr.Size));
            return new TokenGroup
            {
                sid = SidToString(sid),
                attributes = attributes,
                attributes_hex = "0x" + attributes.ToString("X8"),
                enabled = (attributes & GroupEnabled) != 0,
                enabled_by_default = (attributes & GroupEnabledByDefault) != 0,
                mandatory = (attributes & GroupMandatory) != 0,
                deny_only = (attributes & GroupUseForDenyOnly) != 0,
                owner = (attributes & GroupOwner) != 0,
                integrity = (attributes & GroupIntegrity) != 0,
                logon_id = (attributes & GroupLogonId) == GroupLogonId
            };
        }

        private static TokenGroup[] ReadGroups(IntPtr buffer)
        {
            int count = Marshal.ReadInt32(buffer, 0);
            int entrySize = IntPtr.Size == 8 ? 16 : 8;
            int firstEntryOffset = IntPtr.Size == 8 ? 8 : 4;
            List<TokenGroup> groups = new List<TokenGroup>();
            for (int index = 0; index < count; index++)
            {
                groups.Add(ReadGroup(buffer, firstEntryOffset + (index * entrySize)));
            }
            return groups.ToArray();
        }

        private static TokenPrivilege[] ReadPrivileges(IntPtr buffer)
        {
            int count = Marshal.ReadInt32(buffer, 0);
            const int entrySize = 12;
            List<TokenPrivilege> privileges = new List<TokenPrivilege>();
            for (int index = 0; index < count; index++)
            {
                int offset = 4 + (index * entrySize);
                Luid luid = new Luid
                {
                    low_part = unchecked((uint)Marshal.ReadInt32(buffer, offset)),
                    high_part = unchecked((uint)Marshal.ReadInt32(buffer, offset + 4))
                };
                uint attributes = unchecked((uint)Marshal.ReadInt32(buffer, offset + 8));
                privileges.Add(new TokenPrivilege
                {
                    name = PrivilegeName(luid),
                    luid = "0x" + (((ulong)luid.high_part << 32) | luid.low_part).ToString("X16"),
                    attributes = attributes,
                    attributes_hex = "0x" + attributes.ToString("X8"),
                    attribute_names = PrivilegeAttributeNames(attributes),
                    enabled = (attributes & PrivilegeEnabled) != 0,
                    enabled_by_default = (attributes & PrivilegeEnabledByDefault) != 0,
                    removed = (attributes & PrivilegeRemoved) != 0,
                    used_for_access = (attributes & PrivilegeUsedForAccess) != 0
                });
            }
            return privileges.ToArray();
        }

        private static string ProcessImage(IntPtr process)
        {
            StringBuilder image = new StringBuilder(32768);
            uint size = (uint)image.Capacity;
            if (!QueryFullProcessImageName(process, 0, image, ref size))
            {
                ThrowLastError("QueryFullProcessImageNameW");
            }
            return image.ToString();
        }

        public static Snapshot Capture(uint processId, string serviceSidName)
        {
            Snapshot snapshot = new Snapshot
            {
                capture_status = "FAIL",
                error_code = 0,
                error_message = string.Empty,
                process_id = processId,
                process_image = string.Empty,
                service_sid_name = serviceSidName,
                service_sid_expected = string.Empty,
                service_sid_resolution_error = string.Empty,
                token_user_sid = string.Empty,
                token_groups = new TokenGroup[0],
                token_privileges = new TokenPrivilege[0],
                service_sid_present = false,
                service_sid_group = null,
                se_impersonate_present = false,
                se_impersonate_enabled = false,
                se_impersonate_attributes_hex = string.Empty
            };

            IntPtr process = IntPtr.Zero;
            IntPtr token = IntPtr.Zero;
            try
            {
                try
                {
                    snapshot.service_sid_expected = ResolveAccountSid(serviceSidName);
                }
                catch (Exception error)
                {
                    snapshot.service_sid_resolution_error = error.Message;
                }

                process = OpenProcess(ProcessQueryLimitedInformation, false, processId);
                if (process == IntPtr.Zero)
                {
                    ThrowLastError("OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)");
                }
                snapshot.process_image = ProcessImage(process);
                if (!OpenProcessToken(process, TokenQuery, out token))
                {
                    ThrowLastError("OpenProcessToken(TOKEN_QUERY)");
                }

                uint bytes;
                IntPtr userBuffer = QueryTokenBuffer(token, TokenInformationClass.TokenUser, out bytes);
                try
                {
                    snapshot.token_user_sid = SidToString(Marshal.ReadIntPtr(userBuffer, 0));
                }
                finally
                {
                    Marshal.FreeHGlobal(userBuffer);
                }

                IntPtr groupsBuffer = QueryTokenBuffer(token, TokenInformationClass.TokenGroups, out bytes);
                try
                {
                    snapshot.token_groups = ReadGroups(groupsBuffer);
                }
                finally
                {
                    Marshal.FreeHGlobal(groupsBuffer);
                }

                foreach (TokenGroup group in snapshot.token_groups)
                {
                    if (!string.IsNullOrEmpty(snapshot.service_sid_expected) &&
                        string.Equals(group.sid, snapshot.service_sid_expected, StringComparison.OrdinalIgnoreCase))
                    {
                        snapshot.service_sid_present = true;
                        snapshot.service_sid_group = group;
                        break;
                    }
                }

                IntPtr privilegesBuffer = QueryTokenBuffer(token, TokenInformationClass.TokenPrivileges, out bytes);
                try
                {
                    snapshot.token_privileges = ReadPrivileges(privilegesBuffer);
                }
                finally
                {
                    Marshal.FreeHGlobal(privilegesBuffer);
                }

                foreach (TokenPrivilege privilege in snapshot.token_privileges)
                {
                    if (string.Equals(privilege.name, "SeImpersonatePrivilege", StringComparison.OrdinalIgnoreCase))
                    {
                        snapshot.se_impersonate_present = true;
                        snapshot.se_impersonate_enabled = privilege.enabled && !privilege.removed;
                        snapshot.se_impersonate_attributes_hex = privilege.attributes_hex;
                        break;
                    }
                }

                snapshot.capture_status = "PASS";
                return snapshot;
            }
            catch (Win32Exception error)
            {
                snapshot.error_code = error.NativeErrorCode;
                snapshot.error_message = error.Message;
                return snapshot;
            }
            catch (Exception error)
            {
                snapshot.error_code = -1;
                snapshot.error_message = error.Message;
                return snapshot;
            }
            finally
            {
                if (token != IntPtr.Zero) CloseHandle(token);
                if (process != IntPtr.Zero) CloseHandle(process);
            }
        }
    }
}
'@
    }
}

function Get-BootEvidence {
    $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    $boot = if ($os.LastBootUpTime -is [datetime]) {
        [datetime]$os.LastBootUpTime
    }
    else {
        [Management.ManagementDateTimeConverter]::ToDateTime([string]$os.LastBootUpTime)
    }
    $tickSource = 'GetTickCount64'
    try {
        Ensure-Phase05BootEvidenceType
        $tickCount = [uint64][Phase05BootEvidence]::GetTickCount64()
    }
    catch {
        # Windows PowerShell/.NET Framework builds may not expose
        # Environment.TickCount64 as a managed property.  Keep boot evidence
        # usable on those hosts without weakening the primary LastBootUpTime
        # proof; the 32-bit fallback is recorded explicitly.
        $tickSource = 'Environment.TickCount_32_Fallback'
        $tickSigned = [int64][Environment]::TickCount
        if ($tickSigned -lt 0) {
            $tickSigned += 4294967296
        }
        $tickCount = [uint64]$tickSigned
    }
    return [ordered]@{
        computer_name = $env:COMPUTERNAME
        last_boot_time_utc = $boot.ToUniversalTime().ToString('o')
        observed_utc = (Get-Date).ToUniversalTime().ToString('o')
        process_id = $PID
        process_start_time_utc = [Diagnostics.Process]::GetCurrentProcess().StartTime.ToUniversalTime().ToString('o')
        tick_count_64 = $tickCount
        tick_count_source = $tickSource
    }
}

function Get-ServiceRegistrationEvidence {
    $service = Get-CimInstance -ClassName Win32_Service -Filter ("Name='{0}'" -f $serviceName) -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        return [ordered]@{
            exists = $false
            name = $serviceName
            state = 'Absent'
        }
    }
    return [ordered]@{
        exists = $true
        name = [string]$service.Name
        state = [string]$service.State
        start_mode = [string]$service.StartMode
        start_name = [string]$service.StartName
        process_id = [uint32]$service.ProcessId
        path_name = [string]$service.PathName
    }
}

function Get-RunningServiceTokenEvidence {
    Ensure-Phase05TokenEvidenceType
    $registration = Get-ServiceRegistrationEvidence
    $serviceExecutable = [IO.Path]::GetFullPath((Join-Path $repositoryRoot ("x64\{0}\Blink.IndexService.exe" -f $Configuration)))
    $serviceSidName = 'NT SERVICE\Blink.IndexService'
    $runtimeToken = [Phase05TokenEvidence.Native]::Capture(
        [uint32]$registration.process_id,
        $serviceSidName)
    $processImage = [string]$runtimeToken.process_image
    $processImageMatches = -not [string]::IsNullOrWhiteSpace($processImage) -and
        $processImage.Equals($serviceExecutable, [StringComparison]::OrdinalIgnoreCase)
    $processIdMatches = [uint32]$registration.process_id -ne 0 -and
        [uint32]$registration.process_id -eq [uint32]$runtimeToken.process_id
    $installerEvidencePath = Join-Path $serviceDataRoot 'security\installer-service-config.json'
    $scmConfiguration = $null
    if (Test-Path -LiteralPath $installerEvidencePath -PathType Leaf) {
        $scmConfiguration = Get-Content -LiteralPath $installerEvidencePath -Raw -Encoding utf8 | ConvertFrom-Json
    }
    $tokenPredicate = [ordered]@{
        capture_pass = [string]$runtimeToken.capture_status -eq 'PASS'
        process_id_matches_scm = $processIdMatches
        process_image_matches_expected = $processImageMatches
        token_user_is_localsystem = [string]$runtimeToken.token_user_sid -eq 'S-1-5-18'
        service_sid_present = [bool]$runtimeToken.service_sid_present
        se_impersonate_present = [bool]$runtimeToken.se_impersonate_present
        se_impersonate_enabled = [bool]$runtimeToken.se_impersonate_enabled
        all_token_privileges_recorded = $true
        token_privilege_count = @($runtimeToken.token_privileges).Count
    }
    $tokenPredicate['pass'] = $tokenPredicate.capture_pass -and
        $tokenPredicate.process_id_matches_scm -and
        $tokenPredicate.process_image_matches_expected -and
        $tokenPredicate.token_user_is_localsystem -and
        $tokenPredicate.service_sid_present -and
        $tokenPredicate.se_impersonate_present -and
        $tokenPredicate.se_impersonate_enabled
    $evidence = [ordered]@{
        schema = 'blink.service-token.v1'
        captured_utc = (Get-Date).ToUniversalTime().ToString('o')
        service_name = $serviceName
        service_sid_name = $serviceSidName
        expected_process_image = $serviceExecutable
        scm_registration = $registration
        scm_configuration_artifact_path = $installerEvidencePath
        scm_configuration = $scmConfiguration
        runtime_token = $runtimeToken
        acceptance_predicate = $tokenPredicate
        token_privileges_source = 'OpenProcessToken/GetTokenInformation(TokenPrivileges)/LookupPrivilegeName'
        token_groups_source = 'OpenProcessToken/GetTokenInformation(TokenGroups)'
    }
    $securityDirectory = Join-Path $serviceDataRoot 'security'
    New-Item -ItemType Directory -Force -Path $securityDirectory | Out-Null
    $json = $evidence | ConvertTo-Json -Depth 40
    $securityPath = Join-Path $securityDirectory 'service-token.json'
    [IO.File]::WriteAllText($securityPath, $json, [Text.UTF8Encoding]::new($false))
    $telemetryPath = Join-Path $telemetryRoot 'service-token.json'
    [IO.File]::WriteAllText($telemetryPath, $json, [Text.UTF8Encoding]::new($false))
    Add-ResultProperty -Name 'service_token' -Value $evidence
    Save-Results
    Write-Log ("Runtime service token: pid={0}, image_match={1}, token_user={2}, service_sid_present={3}, SeImpersonate present={4}, enabled={5}, attributes={6}, privilege_count={7}" -f
        $runtimeToken.process_id, $processImageMatches, $runtimeToken.token_user_sid,
        $runtimeToken.service_sid_present, $runtimeToken.se_impersonate_present,
        $runtimeToken.se_impersonate_enabled, $runtimeToken.se_impersonate_attributes_hex,
        @($runtimeToken.token_privileges).Count)
    return $evidence
}

function Copy-ExistingStoreSeed {
    param([string]$SourcePath)
    if ([string]::IsNullOrWhiteSpace($SourcePath)) {
        throw 'Windows Restart Prepare requires -ExistingStorePath pointing to a valid BLK5 file.'
    }
    $sourceItem = Get-Item -LiteralPath $SourcePath -Force -ErrorAction Stop
    if ($sourceItem.PSIsContainer -or $sourceItem.Extension -ne '.blink5') {
        throw "ExistingStorePath must be a BLK5 file: $SourcePath"
    }
    $sourceFullPath = $sourceItem.FullName
    $destinationPath = Join-Path $serviceDataRoot 'index\d.blink5'
    $destinationFullPath = [IO.Path]::GetFullPath($destinationPath)
    if ($sourceFullPath.Equals($destinationFullPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Windows Restart Prepare refuses to use the destination BLK5 as its own source.'
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceFullPath -Algorithm SHA256).Hash
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
    Copy-Item -LiteralPath $sourceFullPath -Destination $destinationPath -Force
    $destinationItem = Get-Item -LiteralPath $destinationPath -Force
    if ($destinationItem.Length -ne $sourceItem.Length) {
        throw "Copied BLK5 size mismatch: source=$($sourceItem.Length), destination=$($destinationItem.Length)"
    }
    $destinationHash = (Get-FileHash -LiteralPath $destinationFullPath -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Copied BLK5 hash mismatch: source=$sourceHash, destination=$destinationHash"
    }
    return [ordered]@{
        source_path = $sourceFullPath
        source_size_bytes = [uint64]$sourceItem.Length
        source_sha256 = $sourceHash
        copied_path = $destinationFullPath
        copied_size_bytes = [uint64]$destinationItem.Length
        copied_sha256 = $destinationHash
        source_is_read_only_seed = $true
        initial_mft_build_executed = $false
    }
}

function Save-RebootState {
    param([object]$State)
    $State.updated_utc = (Get-Date).ToUniversalTime().ToString('o')
    $State | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $rebootStatePath -Encoding utf8
}

function Load-RebootState {
    if (-not (Test-Path -LiteralPath $rebootStatePath -PathType Leaf)) {
        throw "Windows Restart state file was not found: $rebootStatePath"
    }
    $state = Get-Content -LiteralPath $rebootStatePath -Raw -Encoding utf8 | ConvertFrom-Json
    if ([int]$state.schema_version -ne 1 -or [string]$state.mode -ne 'windows_restart_acceptance') {
        throw "Unsupported Windows Restart state file: $rebootStatePath"
    }
    if ([string]$state.run_id -ne $runId) {
        throw "Windows Restart state RunId mismatch: expected=$runId, actual=$($state.run_id)"
    }
    return $state
}

function Ensure-Phase05JournalQueryType {
    if ($null -eq ('Phase05JournalQuery' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public sealed class Phase05JournalQueryResult
{
    public bool success;
    public int error;
    public ulong journal_id;
    public long first_usn;
    public long next_usn;
    public long lowest_valid_usn;
    public long max_usn;
    public ulong maximum_size;
    public ulong allocation_delta;
}

public static class Phase05JournalQuery
{
    private const uint GENERIC_READ = 0x80000000;
    private const uint FILE_SHARE_READ = 0x00000001;
    private const uint FILE_SHARE_WRITE = 0x00000002;
    private const uint FILE_SHARE_DELETE = 0x00000004;
    private const uint OPEN_EXISTING = 3;
    private const uint FSCTL_QUERY_USN_JOURNAL = 0x000900F4;

    [StructLayout(LayoutKind.Sequential)]
    private struct JournalDataV0
    {
        public ulong UsnJournalID;
        public long FirstUsn;
        public long NextUsn;
        public long LowestValidUsn;
        public long MaxUsn;
        public ulong MaximumSize;
        public ulong AllocationDelta;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        IntPtr securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(
        SafeFileHandle device,
        uint controlCode,
        byte[] inBuffer,
        uint inBufferSize,
        byte[] outBuffer,
        uint outBufferSize,
        out uint bytesReturned,
        IntPtr overlapped);

    public static Phase05JournalQueryResult Query(string volumeRoot)
    {
        var result = new Phase05JournalQueryResult();
        var drive = volumeRoot.Substring(0, 1).ToUpperInvariant();
        var devicePath = "\\\\.\\" + drive + ":";
        using (var handle = CreateFile(devicePath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero))
        {
            if (handle == null || handle.IsInvalid)
            {
                result.error = Marshal.GetLastWin32Error();
                return result;
            }

            var output = new byte[Marshal.SizeOf(typeof(JournalDataV0))];
            uint returned;
            if (!DeviceIoControl(handle, FSCTL_QUERY_USN_JOURNAL, null, 0,
                output, (uint)output.Length, out returned, IntPtr.Zero))
            {
                result.error = Marshal.GetLastWin32Error();
                return result;
            }

            var memory = Marshal.AllocHGlobal(output.Length);
            try
            {
                Marshal.Copy(output, 0, memory, output.Length);
                var data = (JournalDataV0)Marshal.PtrToStructure(memory, typeof(JournalDataV0));
                result.success = true;
                result.journal_id = data.UsnJournalID;
                result.first_usn = data.FirstUsn;
                result.next_usn = data.NextUsn;
                result.lowest_valid_usn = data.LowestValidUsn;
                result.max_usn = data.MaxUsn;
                result.maximum_size = data.MaximumSize;
                result.allocation_delta = data.AllocationDelta;
                return result;
            }
            finally
            {
                Marshal.FreeHGlobal(memory);
            }
        }
    }
}
'@
    }
}

function Get-VolumeJournalEvidence {
    Ensure-Phase05JournalQueryType
    $result = [Phase05JournalQuery]::Query($VolumeRoot)
    if (-not $result.success) {
        throw "FSCTL_QUERY_USN_JOURNAL failed for $VolumeRoot with Win32 error $($result.error)"
    }
    return [ordered]@{
        volume_root = $VolumeRoot
        journal_id = [uint64]$result.journal_id
        first_usn = [int64]$result.first_usn
        next_usn = [int64]$result.next_usn
        lowest_valid_usn = [int64]$result.lowest_valid_usn
        max_usn = [int64]$result.max_usn
        maximum_size = [uint64]$result.maximum_size
        allocation_delta = [uint64]$result.allocation_delta
    }
}

function Get-ServiceTelemetryRecords {
    $records = [System.Collections.Generic.List[object]]::new()
    $files = @(Get-ChildItem -LiteralPath $telemetryRoot -Filter 'operation-*.json' -File -ErrorAction SilentlyContinue)
    foreach ($file in $files) {
        try {
            $telemetry = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8 | ConvertFrom-Json
            $records.Add([pscustomobject][ordered]@{
                path = $file.FullName
                operation = $telemetry
            })
        }
        catch {
            Write-Log ("Unable to parse restart telemetry {0}: {1}" -f $file.FullName, $_.Exception.Message) -Failure
        }
    }
    return @($records | Sort-Object { [uint64]$_.operation.operation_id })
}

function Wait-RestartRecoveryTelemetry {
    param(
        [uint64]$MinimumOperationId,
        [int64]$MinimumCompletedUsn,
        [int]$TimeoutSeconds = 180
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $records = @(Get-ServiceTelemetryRecords | Where-Object {
            [uint64]$_.operation.operation_id -gt $MinimumOperationId -and
            $_.operation.initial -eq $false -and
            [int]$_.operation.error -eq 0 -and
            [int64]$_.operation.catchup_completed_usn -ge $MinimumCompletedUsn
        })
        if ($records.Count -gt 0) {
            return $records[-1]
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "No completed non-initial USN CatchUp telemetry was observed after operation $MinimumOperationId."
}

function Wait-OperationStartRecovery {
    param(
        [string]$Label,
        [int]$RecoveryTimeoutSeconds = 120,
        [datetime]$AbsoluteDeadline = [datetime]::MinValue
    )
    $deadline = (Get-Date).AddSeconds($RecoveryTimeoutSeconds)
    if ($AbsoluteDeadline -ne [datetime]::MinValue -and $AbsoluteDeadline -lt $deadline) {
        $deadline = $AbsoluteDeadline
    }
    $lastError = $null
    while ((Get-Date) -lt $deadline) {
        try {
            $timeoutMs = Get-RemainingMilliseconds -Deadline $deadline
            if ($timeoutMs -le 0) { break }
            $progress = Send-PipeRequest -Type 4 -OperationId 0 -TimeoutMs $timeoutMs
            if ($progress.status -eq 'Ok' -and $progress.operation_id -ne 0) {
                Write-Log ("{0}: recovered operation_id={1} after start-response transport failure" -f $Label, $progress.operation_id)
                return $progress
            }
            $lastError = "status=$($progress.status), error=$($progress.error), operation_id=$($progress.operation_id)"
        }
        catch {
            $lastError = $_.Exception.Message
            Write-Log ("{0}: start recovery poll unavailable: {1}" -f $Label, $lastError)
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    throw "Unable to recover the initial operation after $RecoveryTimeoutSeconds seconds; last response=$lastError"
}

# STAGE-SPECIFIC BOUNDED TIMEOUTS & INACTIVITY WATCHDOG
function Wait-IndexOperation {
    param(
        [uint64]$OperationId,
        [string]$Label,
        [int]$TimeoutSeconds = 180,
        [datetime]$AbsoluteDeadline = [datetime]::MinValue
    )
    $started = [Diagnostics.Stopwatch]::StartNew()
    $peakWorkingSet = [uint64]0
    $firstCpu = $null
    $lastCpu = $null
    $lastProgress = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    if ($AbsoluteDeadline -ne [datetime]::MinValue -and $AbsoluteDeadline -lt $deadline) {
        $deadline = $AbsoluteDeadline
    }
    $transportFailureStarted = $null
    $transportFailureCount = 0
    # FastIndex::BuildInitial/Load serializes its snapshot reads with the
    # index mutex.  On a real million-record volume a progress request can
    # therefore wait longer than one 5-second IPC read while the operation is
    # still making bounded progress.  Keep the absolute stage deadline as the
    # hard limit, but allow the known long stages a larger transport grace.
    $transportGraceSeconds = if ($Label -eq 'initial' -or $Label -match 'store-load|restart|reconnect|after-stop-start') { 120 } else { 30 }

    # Inactivity Watchdog Tracking
    $lastChangeTime = Get-Date
    $lastHeartbeatTime = Get-Date
    $prevOpId = [uint64]0
    $prevRecords = [uint64]0
    $prevUsn = [int64]0
    $prevFiles = [uint64]0
    $prevDirs = [uint64]0
    $prevCpu = $null
    $prevWorkingSet = $null
    $loadingWatchdogLogged = $false

    while ($true) {
        $elapsed = $started.Elapsed.TotalSeconds
        if ((Get-Date) -gt $deadline) {
            throw "Operation $OperationId ($Label) timed out after $TimeoutSeconds seconds (stage limit)"
        }
        $sample = Get-ServiceProcessSample
        $processActivity = $false
        if ($null -ne $sample) {
            if ($sample.working_set_bytes -gt $peakWorkingSet) { $peakWorkingSet = $sample.working_set_bytes }
            if ($null -eq $firstCpu) { $firstCpu = $sample.cpu_seconds }
            $lastCpu = $sample.cpu_seconds
            if ($null -ne $prevCpu -and ($sample.cpu_seconds -ne $prevCpu -or $sample.working_set_bytes -ne $prevWorkingSet)) {
                $processActivity = $true
            }
            $prevCpu = $sample.cpu_seconds
            $prevWorkingSet = $sample.working_set_bytes
        }
        try {
            $pollTimeoutMs = Get-RemainingMilliseconds -Deadline $deadline
            if ($pollTimeoutMs -le 0) {
                throw "Operation $OperationId ($Label) reached its absolute stage deadline"
            }
            $progress = Send-PipeRequest -Type 4 -OperationId $OperationId -TimeoutMs $pollTimeoutMs
            if ($null -ne $transportFailureStarted) {
                Write-Log ("{0}: IPC progress recovered after {1} transport failure(s)" -f $Label, $transportFailureCount)
            }
            $transportFailureStarted = $null
            $transportFailureCount = 0
        }
        catch {
            $transportFailureCount++
            if ($null -eq $transportFailureStarted) {
                $transportFailureStarted = Get-Date
                Write-Log ("{0}: transient IPC progress failure; retrying without restarting operation {1}: {2}" -f $Label, $OperationId, $_.Exception.Message) -Failure
            }
            elseif ($transportFailureCount % 3 -eq 0) {
                Write-Log ("{0}: IPC progress still unavailable after {1} attempt(s): {2}" -f $Label, $transportFailureCount, $_.Exception.Message) -Failure
            }
            if (((Get-Date) - $transportFailureStarted).TotalSeconds -ge $transportGraceSeconds) {
                throw "IPC progress unavailable for $transportGraceSeconds seconds while operation $OperationId was running; last error=$($_.Exception.Message)"
            }
            $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
            if ($sleepMs -le 0) {
                throw "Operation $OperationId ($Label) timed out while IPC progress was unavailable"
            }
            Start-Sleep -Milliseconds $sleepMs
            continue
        }
        $lastProgress = $progress

        # Loading a persisted store does not advance CatchUp fields.  It is
        # still hard-bounded by the absolute stage deadline, but those fields
        # must not be used as a livelock signal while the service deserializes.
        $advanced = ($progress.operation_id -ne $prevOpId) -or
                    ($progress.records_seen -ne $prevRecords) -or
                    ($progress.last_usn -ne $prevUsn) -or
                    ($progress.files -ne $prevFiles) -or
                    ($progress.directories -ne $prevDirs)
        $loadingCandidate = $progress.operation_id -ne 0 -and
            $progress.records_seen -eq 0 -and
            $progress.files -eq 0 -and
            $progress.directories -eq 0 -and
            $progress.last_usn -eq 0 -and
            $progress.index_state -in @('CatchingUp', 'Rebuilding')
        if ($loadingCandidate) {
            if (-not $loadingWatchdogLogged) {
                Write-Log ("{0}: load-like operation detected; CatchUp progress fields are excluded from inactivity watchdog; hard deadline remains active" -f $Label)
                $loadingWatchdogLogged = $true
            }
            if ($processActivity) { $lastChangeTime = Get-Date }
        }
        elseif ($advanced -or $processActivity) {
            $lastChangeTime = Get-Date
            $prevOpId = $progress.operation_id
            $prevRecords = $progress.records_seen
            $prevUsn = $progress.last_usn
            $prevFiles = $progress.files
            $prevDirs = $progress.directories
        }
        elseif (((Get-Date) - $lastChangeTime).TotalSeconds -ge 60) {
            $stuckSec = [int]((Get-Date) - $lastChangeTime).TotalSeconds
            throw "INACTIVITY WATCHDOG: Operation $OperationId ($Label) progress has not advanced for $stuckSec seconds (records=$($progress.records_seen), last_usn=$($progress.last_usn), state=$($progress.index_state))"
        }

        # 10s Periodic Heartbeat for long operations (> 30s)
        if ($elapsed -ge 30 -and ((Get-Date) - $lastHeartbeatTime).TotalSeconds -ge 10) {
            Write-Log ("Heartbeat [{0}]: op_id={1}, state={2}, records={3}, files={4}, dirs={5}, last_usn={6}, elapsed={7:F0}s" -f $Label, $progress.operation_id, $progress.index_state, $progress.records_seen, $progress.files, $progress.directories, $progress.last_usn, $elapsed)
            $lastHeartbeatTime = Get-Date
        }

        if ($progress.error -ne 997) {
            break
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
        if ($sleepMs -le 0) {
            throw "Operation $OperationId ($Label) timed out at its absolute stage deadline"
        }
        Start-Sleep -Milliseconds $sleepMs
    }
    $started.Stop()
    $cpuSeconds = if ($null -ne $firstCpu -and $null -ne $lastCpu) { $lastCpu - $firstCpu } else { $null }
    $telemetry = Get-Telemetry -OperationId $OperationId -SnapshotName $Label -AbsoluteDeadline $deadline
    return [ordered]@{
        label = $Label
        operation_id = $OperationId
        operation_kind = if ($Label -eq 'initial') { 'initial' } else { 'usn_catchup' }
        response = $lastProgress
        transport_grace_seconds = $transportGraceSeconds
        wall_seconds = $started.Elapsed.TotalSeconds
        cpu_seconds = $cpuSeconds
        average_cpu_percent = if ($null -ne $cpuSeconds -and $started.Elapsed.TotalSeconds -gt 0) { ($cpuSeconds / $started.Elapsed.TotalSeconds) * 100 } else { $null }
        peak_working_set_bytes = $peakWorkingSet
        telemetry = $telemetry
    }
}

function Start-InitialIndex {
    $transportRecovered = $false
    $startResponse = $null
    # Initial MFT Index Stage Timeout: 600 seconds (10 minutes), shared by
    # start-response recovery, operation polling, and telemetry collection.
    $stageDeadline = (Get-Date).AddSeconds(600)
    try {
        $startTimeoutMs = Get-RemainingMilliseconds -Deadline $stageDeadline
        if ($startTimeoutMs -le 0) { throw 'Initial index stage deadline elapsed before start request' }
        $startResponse = Send-PipeRequest -Type 3 -RequestVolumeRoot $VolumeRoot -TimeoutMs $startTimeoutMs
    }
    catch {
        $startError = $_.Exception.Message
        Write-Log ("StartInitialIndex response transport failure; do not resubmit request: {0}" -f $startError) -Failure
        $startResponse = Wait-OperationStartRecovery -Label 'initial-start' -RecoveryTimeoutSeconds 120 -AbsoluteDeadline $stageDeadline
        $transportRecovered = $true
    }
    if ($startResponse.status -ne 'Ok') {
        throw "StartInitialIndex rejected: status=$($startResponse.status), error=$($startResponse.error), message=$($startResponse.message)"
    }
    return [ordered]@{
        start_response = $startResponse
        operation = Wait-IndexOperation -OperationId $startResponse.operation_id -Label 'initial' -TimeoutSeconds 600 -AbsoluteDeadline $stageDeadline
        transport_recovered = $transportRecovered
    }
}

function Wait-Healthy {
    param(
        [string]$Label = 'catchup',
        [int]$TimeoutSeconds = 120,
        [int]$RequestTimeoutMs = 5000
    )
    $started = [Diagnostics.Stopwatch]::StartNew()
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $last = $null
    $operation = $null
    $transportFailureStarted = $null
    $transportFailureCount = 0
    # Loading a large BLK5 or reconnecting after SCM restart can hold the
    # FastIndex mutex while a status request is in flight.  This remains
    # bounded by the stage deadline below; it is not an unbounded retry.
    $transportGraceSeconds = if ($Label -match 'initial-store-load|restart|reconnect|after-stop-start') { 120 } else { 30 }

    # Inactivity Watchdog Tracking
    $lastChangeTime = Get-Date
    $lastHeartbeatTime = Get-Date
    $prevOpId = [uint64]0
    $prevRecords = [uint64]0
    $prevUsn = [int64]0
    $prevCpu = $null
    $prevWorkingSet = $null
    $loadingWatchdogLogged = $false

    while ((Get-Date) -lt $deadline) {
        $elapsed = $started.Elapsed.TotalSeconds
        $sample = Get-ServiceProcessSample
        $processActivity = $false
        if ($null -ne $sample) {
            if ($null -ne $prevCpu -and ($sample.cpu_seconds -ne $prevCpu -or $sample.working_set_bytes -ne $prevWorkingSet)) {
                $processActivity = $true
            }
            $prevCpu = $sample.cpu_seconds
            $prevWorkingSet = $sample.working_set_bytes
        }
        try {
            $statusTimeoutMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds $RequestTimeoutMs
            if ($statusTimeoutMs -le 0) { break }
            $last = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs $statusTimeoutMs
            if ($null -ne $transportFailureStarted) {
                Write-Log ("{0}: status IPC recovered after {1} transport failure(s)" -f $Label, $transportFailureCount)
            }
            $transportFailureStarted = $null
            $transportFailureCount = 0
        }
        catch {
            $transportFailureCount++
            if ($null -eq $transportFailureStarted) {
                $transportFailureStarted = Get-Date
                Write-Log ("{0}: transient status IPC failure; retrying: {1}" -f $Label, $_.Exception.Message) -Failure
            }
            if (((Get-Date) - $transportFailureStarted).TotalSeconds -ge $transportGraceSeconds) {
                throw "Status IPC unavailable for $transportGraceSeconds seconds while waiting for Healthy; last error=$($_.Exception.Message)"
            }
            $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
            if ($sleepMs -le 0) { break }
            Start-Sleep -Milliseconds $sleepMs
            continue
        }

        # A persisted-store load can report CatchingUp/Rebuilding with zero
        # records while the service is doing CPU/IO work.  Use process activity
        # for observability when available, but keep the absolute deadline.
        $advanced = ($last.operation_id -ne $prevOpId) -or
                    ($last.records_seen -ne $prevRecords) -or
                    ($last.last_usn -ne $prevUsn)
        $loadingCandidate = $last.operation_id -ne 0 -and
            $last.records_seen -eq 0 -and
            $last.files -eq 0 -and
            $last.directories -eq 0 -and
            $last.last_usn -eq 0 -and
            $last.index_state -in @('CatchingUp', 'Rebuilding')
        if ($loadingCandidate) {
            if (-not $loadingWatchdogLogged) {
                Write-Log ("{0}: load-like operation detected; CatchUp progress fields are excluded from inactivity watchdog; hard deadline remains active" -f $Label)
                $loadingWatchdogLogged = $true
            }
            if ($processActivity) { $lastChangeTime = Get-Date }
        }
        elseif ($advanced -or $processActivity) {
            $lastChangeTime = Get-Date
            $prevOpId = $last.operation_id
            $prevRecords = $last.records_seen
            $prevUsn = $last.last_usn
        }
        elseif (((Get-Date) - $lastChangeTime).TotalSeconds -ge 60) {
            $stuckSec = [int]((Get-Date) - $lastChangeTime).TotalSeconds
            throw "INACTIVITY WATCHDOG: Wait-Healthy ({0}) has not advanced for $stuckSec seconds (state=$($last.index_state), op_id=$($last.operation_id), last_usn=$($last.last_usn))"
        }

        # 10s Periodic Heartbeat (> 30s)
        if ($elapsed -ge 30 -and ((Get-Date) - $lastHeartbeatTime).TotalSeconds -ge 10) {
            Write-Log ("Heartbeat [{0}]: state={1}, op_id={2}, records={3}, last_usn={4}, elapsed={5:F0}s" -f $Label, $last.index_state, $last.operation_id, $last.records_seen, $last.last_usn, $elapsed)
            $lastHeartbeatTime = Get-Date
        }

        if ($last.index_state -eq 'Healthy') {
            return [ordered]@{ response = $last; operation = $operation }
        }
        if ($last.index_state -eq 'RebuildRequired' -or $last.index_state -eq 'Corrupted') {
            throw "Index entered $($last.index_state), error=$($last.error)"
        }
        if ($last.operation_id -ne 0 -and $last.error -eq 997) {
            $remainingSeconds = ($deadline - (Get-Date)).TotalSeconds
            if ($remainingSeconds -le 0) {
                throw "Wait-Healthy ($Label) reached its absolute stage deadline before nested operation completion"
            }
            $operation = Wait-IndexOperation -OperationId $last.operation_id -Label $Label -TimeoutSeconds ([int][math]::Ceiling($remainingSeconds)) -AbsoluteDeadline $deadline
            if ($operation.response.error -ne 0) {
                throw "Catch-up failed: error=$($operation.response.error)"
            }
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    throw "Index did not become Healthy before timeout ($TimeoutSeconds seconds)"
}

function Invoke-Search {
    param(
        [string]$QueryText,
        [switch]$QueryPrefix,
        [string]$QueryExtension = '',
        [string]$QueryPathPrefix = '',
        [int]$QueryLimit = 200
    )
    # Catch-up is deliberately visible to clients as Busy/ERROR_IO_PENDING.
    # Retry only that transient state; keep the per-request IPC timeout and
    # reject every other status immediately. The measured elapsed time is for
    # the successful Search attempt, not time spent waiting for catch-up.
    $busyDeadline = (Get-Date).AddSeconds(120)
    $busyRetries = 0
    while ($true) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $response = Send-PipeRequest -Type 5 -RequestVolumeRoot $VolumeRoot -Text $QueryText -Extension $QueryExtension -PathPrefix $QueryPathPrefix -Limit ([uint32]$QueryLimit) -Prefix:$QueryPrefix -TimeoutMs 5000
        $sw.Stop()
        if ($response.status -eq 'Ok') {
            return [ordered]@{
                response = $response
                elapsed_ms = $sw.Elapsed.TotalMilliseconds
                busy_retries = $busyRetries
            }
        }
        if ($response.status -ne 'Busy' -or $response.error -ne 997) {
            throw "Search failed: status=$($response.status), state=$($response.index_state), error=$($response.error)"
        }
        if ((Get-Date) -ge $busyDeadline) {
            throw "Search remained Busy for 120 seconds: state=$($response.index_state), error=$($response.error)"
        }
        $busyRetries++
        Start-Sleep -Milliseconds 100
    }
}

function Invoke-SearchHits {
    param([string]$QueryText, [switch]$QueryPrefix, [string]$QueryExtension = '', [string]$QueryPathPrefix = '')
    return @((Invoke-Search -QueryText $QueryText -QueryPrefix:$QueryPrefix -QueryExtension $QueryExtension -QueryPathPrefix $QueryPathPrefix).response.results)
}

function Wait-SearchHit {
    param(
        [string]$Name,
        [string]$PathLike = '',
        [int]$TimeoutSeconds = 120
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastHits = @()
    while ((Get-Date) -lt $deadline) {
        $hits = @(Invoke-SearchHits $Name -QueryPathPrefix $PathLike)
        $lastHits = $hits
        $hit = Find-Hit -Hits $hits -Name $Name -PathLike $PathLike
        if ($null -ne $hit) {
            return $hit
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    $diagnosticHits = @($lastHits | Select-Object -First 5 | ForEach-Object {
        [ordered]@{
            name = $_.name
            full_path = $_.full_path
            identity = if ($null -ne $_.identity) { $_.identity.key } else { $null }
        }
    })
    Write-Log ("Search hit wait expired: name={0}, path_scope={1}, last_result_count={2}, candidates={3}" -f
        $Name, $PathLike, @($lastHits).Count, ($diagnosticHits | ConvertTo-Json -Compress)) -Failure
    return $null
}

function Wait-SearchEmpty {
    param(
        [string]$Name,
        [string]$PathLike = '',
        [int]$TimeoutSeconds = 120
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastHits = @()
    while ((Get-Date) -lt $deadline) {
        $hits = @(Invoke-SearchHits $Name -QueryPathPrefix $PathLike)
        $lastHits = $hits
        if (@($hits).Count -eq 0) {
            return $true
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    $diagnosticHits = @($lastHits | Select-Object -First 5 | ForEach-Object {
        [ordered]@{
            name = $_.name
            full_path = $_.full_path
            identity = if ($null -ne $_.identity) { $_.identity.key } else { $null }
        }
    })
    Write-Log ("Search empty wait expired: name={0}, path_scope={1}, last_result_count={2}, candidates={3}" -f
        $Name, $PathLike, @($lastHits).Count, ($diagnosticHits | ConvertTo-Json -Compress)) -Failure
    return $false
}

function New-Fixture {
    if (Test-Path -LiteralPath $testRoot) {
        throw "Refusing to overwrite existing test directory: $testRoot"
    }
    Set-FixtureOwnershipState -State 'creating'
    New-Item -ItemType Directory -Path $fixtureRootBase -Force | Out-Null
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $script:fixtureOwnership.created_by_run = $true
    $script:fixtureOwnership.created_utc = (Get-Date).ToUniversalTime().ToString('o')
    Set-FixtureOwnershipState -State 'created'
    $baseline = Join-Path $testRoot 'Baseline'
    $renameDirectory = Join-Path $baseline 'RenameDirectory'
    $moveDirectory = Join-Path $baseline 'MoveDirectory'
    $deleteDirectory = Join-Path $baseline 'DeleteDirectory'
    $lazyDirectory = Join-Path $baseline 'LazyDirectory'
    $outsideDirectory = Join-Path $baseline 'OutsideScopedDirectory'
    New-Item -ItemType Directory -Force -Path $baseline, $renameDirectory, $moveDirectory, $deleteDirectory, $lazyDirectory, $outsideDirectory | Out-Null
    $files = [ordered]@{
        stable = Join-Path $baseline 'StableFile-Phase05.txt'
        move = Join-Path $baseline 'MoveMe-Phase05.txt'
        delete = Join-Path $baseline 'DeleteMe-Phase05.txt'
        unicode = Join-Path $baseline 'Unicode-文件-Phase05.txt'
        renameChild = Join-Path $renameDirectory 'RenameChild-Phase05.txt'
        moveChild = Join-Path $moveDirectory 'MoveChild-Phase05.txt'
        deleteChild = Join-Path $deleteDirectory 'DeleteChild-Phase05.txt'
        lazyChild = Join-Path $lazyDirectory 'LazyChild-Phase05.txt'
        outsideScopedChild = Join-Path $outsideDirectory 'RenameChild-Phase05.txt'
        hardSource = Join-Path $baseline 'HardLinkSource-Phase05.bin'
        hardAlias = Join-Path $baseline 'HardLinkAlias-Phase05.bin'
        symlinkTarget = Join-Path $baseline 'SymlinkTarget-Phase05.txt'
        symlink = Join-Path $baseline 'Symlink-Phase05.txt'
    }
    foreach ($path in $files.Values) {
        if ($path -notlike '*HardLinkAlias*' -and $path -notlike '*Symlink-Phase05*') {
            [IO.File]::WriteAllText($path, 'Blink Phase 0.5 acceptance seed', [Text.UTF8Encoding]::new($false))
        }
    }
    [IO.File]::WriteAllText($files.hardSource, 'Blink hard-link seed', [Text.UTF8Encoding]::new($false))
    # Create the hard link during the post-initial-index mutation stage. A
    # hard link created before FSCTL_ENUM_USN_DATA is not a valid USN catch-up
    # test fixture: the initial MFT record supplies only one FILE_NAME view,
    # while the pre-build journal event is intentionally outside the saved
    # checkpoint. The mutation stage below verifies that USN_REASON_HARD_LINK
    # adds the second name to the authoritative link set.
    $hardLink = $false
    $symlink = $false
    try {
        New-Item -ItemType SymbolicLink -Path $files.symlink -Target $files.symlinkTarget -ErrorAction Stop | Out-Null
        $symlink = $true
    }
    catch {
        Write-Log ("Symlink/reparse creation unavailable: {0}" -f $_.Exception.Message)
    }
    return [ordered]@{
        root = $testRoot
        baseline = $baseline
        renameDirectory = $renameDirectory
        moveDirectory = $moveDirectory
        deleteDirectory = $deleteDirectory
        lazyDirectory = $lazyDirectory
        outsideDirectory = $outsideDirectory
        files = $files
        hard_link_created = $hardLink
        symlink_created = $symlink
    }
}

function Find-Hit {
    param([object[]]$Hits, [string]$Name, [string]$PathLike = '')
    return @($Hits | Where-Object {
        if ($null -eq $_) {
            return $false
        }
        $nameProperty = $_.PSObject.Properties['name']
        if ($null -eq $nameProperty -or -not ([string]$nameProperty.Value).Equals($Name, [StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
        if ([string]::IsNullOrWhiteSpace($PathLike)) {
            return $true
        }
        $pathProperty = $_.PSObject.Properties['full_path']
        if ($null -eq $pathProperty) {
            return $false
        }
        # Existing mutation checks may pass a wildcard pattern.  The stress
        # sentinel passes a concrete scope; use the same normalized subtree
        # predicate as Search correctness for that case so harmless path
        # spelling differences cannot hide a valid result.
        if ($PathLike -match '[*?]') {
            return [string]$pathProperty.Value -like $PathLike
        }
        try {
            return Test-SearchPathWithin -Path ([string]$pathProperty.Value) -Scope $PathLike
        }
        catch {
            return $false
        }
    } | Select-Object -First 1)
}

function Invoke-SearchBenchmark {
    param([string]$PathPrefix)
    Trace-Stage 'search-benchmark' {
        $cases = @(
            [ordered]@{ name = 'exact_filename'; text = 'StableFile-Phase05.txt'; prefix = $false; extension = ''; path = $PathPrefix },
            [ordered]@{ name = 'prefix'; text = 'StableFile'; prefix = $true; extension = ''; path = $PathPrefix },
            [ordered]@{ name = 'substring'; text = 'Phase05'; prefix = $false; extension = ''; path = $PathPrefix },
            [ordered]@{ name = 'extension'; text = ''; prefix = $false; extension = '.txt'; path = $PathPrefix },
            [ordered]@{ name = 'directory_scoped'; text = 'RenameChild-Phase05.txt'; prefix = $false; extension = ''; path = (Join-Path $PathPrefix 'Baseline\RenameDirectory') }
        )
        foreach ($case in $cases) {
            $times = @()
            $last = $null
            $peak = [uint64]0
            for ($i = 0; $i -lt 20; $i++) {
                $sample = Get-ServiceProcessSample
                if ($null -ne $sample -and $sample.working_set_bytes -gt $peak) { $peak = $sample.working_set_bytes }
                $run = Invoke-Search -QueryText $case.text -QueryPrefix:$case.prefix -QueryExtension $case.extension -QueryPathPrefix $case.path
                $times += [double]$run.elapsed_ms
                $last = $run.response
            }
            $sorted = @($times | Sort-Object)
            $p95Index = [math]::Max(0, [int]([math]::Ceiling($sorted.Count * 0.95) - 1))
            $record = [ordered]@{
                run_id = $runId
                query_type = $case.name
                status = 'PASS'
                average_ms = ($times | Measure-Object -Average).Average
                p95_ms = $sorted[$p95Index]
                peak_working_set_bytes = $peak
                result_count = @($last.results).Count
                busy_retries = $run.busy_retries
                details = 'IPC FastIndex search; no filesystem traversal requested by the client'
            }
            $script:benchmarks += [pscustomobject]$record
            $results.search_benchmark = @($script:benchmarks)
            Write-Log ("Search {0}: avg={1:F2}ms p95={2:F2}ms results={3}" -f $case.name, $record.average_ms, $record.p95_ms, $record.result_count)
        }
    }
}

function Invoke-Mutations {
    param([hashtable]$Fixture)
    Trace-Stage 'file-mutations' {
        $f = $Fixture.files
        $baseline = [ordered]@{
            stable = (Find-Hit (Invoke-SearchHits 'StableFile-Phase05.txt' -QueryPathPrefix $Fixture.root) 'StableFile-Phase05.txt' ('*' + $f.stable))
            move = (Find-Hit (Invoke-SearchHits 'MoveMe-Phase05.txt' -QueryPathPrefix $Fixture.root) 'MoveMe-Phase05.txt' ('*' + $f.move))
            renameDirectory = (Find-Hit (Invoke-SearchHits 'RenameDirectory' -QueryPathPrefix $Fixture.root) 'RenameDirectory')
            moveDirectory = (Find-Hit (Invoke-SearchHits 'MoveDirectory' -QueryPathPrefix $Fixture.root) 'MoveDirectory')
            moveChild = (Find-Hit (Invoke-SearchHits 'MoveChild-Phase05.txt' -QueryPathPrefix $Fixture.root) 'MoveChild-Phase05.txt')
            lazyChild = (Find-Hit (Invoke-SearchHits 'LazyChild-Phase05.txt' -QueryPathPrefix $Fixture.root) 'LazyChild-Phase05.txt')
            delete = (Find-Hit (Invoke-SearchHits 'DeleteMe-Phase05.txt' -QueryPathPrefix $Fixture.root) 'DeleteMe-Phase05.txt')
        }
        $hardHits = Invoke-SearchHits 'HardLink' -QueryPathPrefix $Fixture.root
        $baseline.hard_link_hits = @($hardHits)

        $renamedStable = Join-Path $Fixture.baseline 'RenamedFile-Phase05.txt'
        Rename-Item -LiteralPath $f.stable -NewName (Split-Path -Leaf $renamedStable)
        $movedFile = Join-Path $Fixture.root 'MovedFile-Phase05.txt'
        Move-Item -LiteralPath $f.move -Destination $movedFile
        $renamedDirectory = Join-Path $Fixture.baseline 'RenamedDirectory'
        Rename-Item -LiteralPath $Fixture.renameDirectory -NewName (Split-Path -Leaf $renamedDirectory)
        $movedDirectory = Join-Path $Fixture.root 'MovedDirectory'
        Move-Item -LiteralPath $Fixture.moveDirectory -Destination $movedDirectory
        Remove-Item -LiteralPath $f.delete -Force
        Remove-Item -LiteralPath $Fixture.deleteDirectory -Recurse -Force
        [IO.File]::AppendAllText($renamedStable, ' size-change', [Text.UTF8Encoding]::new($false))
        $createdFile = Join-Path $Fixture.root 'CreatedAfter-Phase05.txt'
        [IO.File]::WriteAllText($createdFile, 'created after initial index', [Text.UTF8Encoding]::new($false))
        $createdDirectory = Join-Path $Fixture.root 'CreatedDirectory-Phase05'
        New-Item -ItemType Directory -Force -Path $createdDirectory | Out-Null
        $createdChild = Join-Path $createdDirectory 'CreatedChild-Phase05.txt'
        [IO.File]::WriteAllText($createdChild, 'created child', [Text.UTF8Encoding]::new($false))
        $deletedAfterCreate = Join-Path $Fixture.root 'CreateThenDelete-Phase05'
        New-Item -ItemType Directory -Force -Path $deletedAfterCreate | Out-Null
        [IO.File]::WriteAllText((Join-Path $deletedAfterCreate 'Gone.txt'), 'gone', [Text.UTF8Encoding]::new($false))
        Remove-Item -LiteralPath $deletedAfterCreate -Recurse -Force
        $unicodeAfter = Join-Path $Fixture.root '新增-中文-Phase05.txt'
        [IO.File]::WriteAllText($unicodeAfter, 'unicode after', [Text.UTF8Encoding]::new($false))

        if (-not $Fixture.hard_link_created) {
            try {
                & fsutil.exe hardlink create $f.hardAlias $f.hardSource 2>&1 | ForEach-Object { Write-Log ([string]$_) }
                $Fixture.hard_link_created = $LASTEXITCODE -eq 0
                if ($Fixture.hard_link_created) {
                    Write-Log 'Hard link mutation created after initial index.'
                }
            }
            catch {
                Write-Log ("Hard link mutation unavailable: {0}" -f $_.Exception.Message)
                $Fixture.hard_link_created = $false
            }
        }

        # Ordinary CatchUp Timeout: 120 seconds (2 minutes)
        $catchup = Wait-Healthy -Label 'mutations-catchup' -TimeoutSeconds 120
        $catchupTelemetry = if ($null -ne $catchup.operation) { $catchup.operation.telemetry } else { $null }
        $catchupResponse = if ($null -ne $catchup.operation) { $catchup.operation.response } else { $null }
        $nonInitialCatchup = $null -ne $catchup.operation -and
            $catchup.operation.operation_kind -eq 'usn_catchup'
        $catchupCompleted = $nonInitialCatchup -and
            $catchupResponse.error -eq 0 -and
            [uint64]$catchupResponse.records_seen -gt 0
        $telemetryIsCompatible = $null -eq $catchupTelemetry -or -not [bool]$catchupTelemetry.initial
        $serviceReadPass = $catchupCompleted -and $telemetryIsCompatible
        $readError = if ($serviceReadPass -or $null -eq $catchupResponse) { $null } else { [int]$catchupResponse.error }
        $readEvidence = if ($null -ne $catchupTelemetry) {
            'non-initial CatchUp telemetry observed'
        }
        elseif ($catchupCompleted) {
            'non-initial CatchUp completed with records_seen>0 and error=0; telemetry file absent because pure internal CatchUp telemetry is suppressed to avoid a same-volume self-catch-up loop'
        }
        else {
            'non-initial CatchUp completion evidence was not available'
        }
        Add-Check -Area 'ServiceAccount' -Name 'FSCTL_READ_USN_JOURNAL' -Status $(if ($serviceReadPass) { 'PASS' } else { 'FAIL' }) -Details $readEvidence -Win32Error $readError
        if ($serviceReadPass -and $null -ne $script:activeAccountRecord) {
            $script:activeAccountRecord.read_usn_journal = 'PASS'
            $script:activeAccountRecord.read_evidence = $readEvidence
            $accountResult = @($results.service_accounts | Where-Object { $_.account -eq $script:activeAccountRecord.account -and $_.label -eq $script:activeAccountRecord.label } | Select-Object -Last 1)
            if ($accountResult.Count -eq 1) {
                $accountResult[0].read_usn_journal = 'PASS'
                $accountResult[0].read_evidence = 'Post-mutation catch-up completed through FSCTL_READ_USN_JOURNAL with error=0.'
            }
        }
        $newStableHit = Find-Hit (Invoke-SearchHits 'RenamedFile-Phase05.txt' -QueryPathPrefix $Fixture.root) 'RenamedFile-Phase05.txt'
        $newMoveHit = Find-Hit (Invoke-SearchHits 'MovedFile-Phase05.txt' -QueryPathPrefix $Fixture.root) 'MovedFile-Phase05.txt'
        $newRenameDirectoryHit = Find-Hit (Invoke-SearchHits 'RenamedDirectory' -QueryPathPrefix $Fixture.root) 'RenamedDirectory'
        $newMoveDirectoryHit = Find-Hit (Invoke-SearchHits 'MovedDirectory' -QueryPathPrefix $Fixture.root) 'MovedDirectory'
        $newLazyChildHit = Find-Hit (Invoke-SearchHits 'LazyChild-Phase05.txt' -QueryPathPrefix $Fixture.root) 'LazyChild-Phase05.txt'
        $deletedHit = Invoke-SearchHits 'DeleteMe-Phase05.txt' -QueryPathPrefix $Fixture.root
        $hardAfter = Invoke-SearchHits 'HardLink' -QueryPathPrefix $Fixture.root
        $createdHit = Find-Hit (Invoke-SearchHits 'CreatedAfter-Phase05.txt' -QueryPathPrefix $Fixture.root) 'CreatedAfter-Phase05.txt'
        $createdDirectoryHit = Find-Hit (Invoke-SearchHits 'CreatedDirectory-Phase05' -QueryPathPrefix $Fixture.root) 'CreatedDirectory-Phase05'
        $deletedDirectoryHits = Invoke-SearchHits 'DeleteDirectory' -QueryPathPrefix $Fixture.root
        $unicodeHit = Find-Hit (Invoke-SearchHits '新增-中文-Phase05.txt' -QueryPathPrefix $Fixture.root) '新增-中文-Phase05.txt'
        $movedChildHit = Find-Hit (Invoke-SearchHits 'MoveChild-Phase05.txt' -QueryPathPrefix $Fixture.root) 'MoveChild-Phase05.txt'

        $checks = [ordered]@{
            create = $null -ne $createdHit
            delete = @($deletedHit).Count -eq 0
            file_rename_file_id_stable = $null -ne $newStableHit -and $newStableHit.identity.key -eq $baseline.stable.identity.key
            file_move_file_id_stable = $null -ne $newMoveHit -and $newMoveHit.identity.key -eq $baseline.move.identity.key
            file_move_parent_id_updated = $null -ne $newMoveHit -and $newMoveHit.parent_identity.key -ne $baseline.move.parent_identity.key
            directory_rename_file_id_stable = $null -ne $newRenameDirectoryHit -and $newRenameDirectoryHit.identity.key -eq $baseline.renameDirectory.identity.key
            directory_move_file_id_stable = $null -ne $newMoveDirectoryHit -and $newMoveDirectoryHit.identity.key -eq $baseline.moveDirectory.identity.key
            lazy_path_resolution = $null -ne $movedChildHit -and $movedChildHit.identity.key -eq $baseline.moveChild.identity.key -and $movedChildHit.full_path -like '*\MovedDirectory\MoveChild-Phase05.txt'
            moved_child_parent_id = $null -ne $movedChildHit -and $movedChildHit.parent_identity.key -eq $newMoveDirectoryHit.identity.key
            hard_link_multiple_names = ($Fixture.hard_link_created -and (@($hardAfter).Count -ge 2) -and (@($hardAfter | Where-Object { $_.identity.key -eq $hardAfter[0].identity.key }).Count -ge 2))
            unicode = $null -ne $unicodeHit
            directory_create = $null -ne $createdDirectoryHit -and $createdDirectoryHit.is_directory
            directory_delete = @($deletedDirectoryHits).Count -eq 0
        }
        foreach ($name in $checks.Keys) {
            Add-Check -Area 'USN' -Name $name -Status $(if ($checks[$name]) { 'PASS' } else { 'FAIL' }) -Details ('result=' + $checks[$name])
        }
        $mutationResult = [ordered]@{
            baseline = $baseline
            catchup = $catchup
            checks = $checks
            paths = [ordered]@{
                renamed_file = $renamedStable
                moved_file = $movedFile
                renamed_directory = $renamedDirectory
                moved_directory = $movedDirectory
                created_file = $createdFile
                created_directory = $createdDirectory
                unicode_file = $unicodeAfter
            }
            hard_link_created = $Fixture.hard_link_created
            symlink_created = $Fixture.symlink_created
        }
        Add-ResultProperty -Name 'mutations' -Value $mutationResult
        return $mutationResult
    }
}

function Invoke-ContinuousWriteStressTest {
    param([hashtable]$Fixture)
    Trace-Stage 'continuous-write-stress' {
        Write-Log 'Starting continuous-write stress test...'
        $initialStatus = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
        $initialUsn = [int64]$initialStatus.last_usn
        $beforeTelemetryIds = @(
            Get-ChildItem -LiteralPath $telemetryRoot -Filter 'operation-*.json' -File -ErrorAction SilentlyContinue |
                ForEach-Object { $_.BaseName -replace '^operation-', '' }
        )

        $stressDir = Join-Path $Fixture.root 'Stress'
        New-Item -ItemType Directory -Force -Path $stressDir | Out-Null

        $jobScript = {
            param($Dir)
            for ($i = 0; $i -lt 300; $i++) {
                $filePath = Join-Path $Dir ("stress_{0}.tmp" -f $i)
                [System.IO.File]::WriteAllText($filePath, "stress data $i")
                Start-Sleep -Milliseconds 15
                if ($i % 3 -eq 0 -and (Test-Path -LiteralPath $filePath)) {
                    Remove-Item -LiteralPath $filePath -Force -ErrorAction SilentlyContinue
                }
            }
        }

        $stressJob = Start-Job -ScriptBlock $jobScript -ArgumentList $stressDir
        $ipcLatencies = [System.Collections.Generic.List[double]]::new()
        $usnHistory = [System.Collections.Generic.List[int64]]::new()
        $deadlockOrTimeout = $false
        $writerCompleted = $false
        $writerTimedOut = $false
        $writerError = $null
        $writerOutput = @()

        try {
            for ($s = 0; $s -lt 25; $s++) {
                $sw = [System.Diagnostics.Stopwatch]::StartNew()
                try {
                    $status = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
                    $sw.Stop()
                    $ipcLatencies.Add($sw.Elapsed.TotalMilliseconds)
                    $usnHistory.Add([int64]$status.last_usn)
                }
                catch {
                    $deadlockOrTimeout = $true
                    Write-Log ("Stress test IPC failure: {0}" -f $_.Exception.Message) -Failure
                }
                Start-Sleep -Milliseconds 200
            }
        }
        finally {
            try {
                $completedJob = Wait-Job -Job $stressJob -Timeout 90
                if ($null -eq $completedJob) {
                    $writerTimedOut = $true
                    throw 'Stress writer exceeded its 90 second bounded completion deadline'
                }
                if ($stressJob.State -ne 'Completed') {
                    throw ("Stress writer ended in unexpected state: {0}" -f $stressJob.State)
                }
                $writerOutput = @(Receive-Job -Job $stressJob -ErrorAction Stop)
                $jobErrors = @($stressJob.ChildJobs | ForEach-Object { @($_.Error) } | Where-Object { $null -ne $_ })
                if ($jobErrors.Count -gt 0) {
                    throw ("Stress writer reported job errors: {0}" -f (($jobErrors | ForEach-Object { $_.ToString() }) -join '; '))
                }
                $writerCompleted = $true
            }
            catch {
                $writerError = $_.Exception.Message
                Write-Log ("Stress writer completion failure: {0}" -f $writerError) -Failure
            }
            finally {
                # Stop-Job is reserved for the timeout path.  A normal writer
                # must be observed as Completed before it is removed.
                if ($writerTimedOut -and $stressJob.State -eq 'Running') {
                    Stop-Job -Job $stressJob -ErrorAction SilentlyContinue
                }
                Remove-Job -Job $stressJob -Force -ErrorAction SilentlyContinue
            }
        }

        $monotonic = $true
        for ($i = 1; $i -lt $usnHistory.Count; $i++) {
            if ($usnHistory[$i] -lt $usnHistory[$i-1]) {
                $monotonic = $false
                break
            }
        }

        $sortedLatencies = $ipcLatencies | Sort-Object
        $median = if ($sortedLatencies.Count -gt 0) { $sortedLatencies[[int]($sortedLatencies.Count * 0.5)] } else { 0 }
        $p95 = if ($sortedLatencies.Count -gt 0) { $sortedLatencies[[int]($sortedLatencies.Count * 0.95)] } else { 0 }
        $maxLat = if ($sortedLatencies.Count -gt 0) { $sortedLatencies[$sortedLatencies.Count - 1] } else { 0 }

        # Stress Convergence Stage Timeout: 120 seconds (2 minutes)
        $postStressHealthy = Wait-Healthy -Label 'post-stress-convergence' -TimeoutSeconds 120
        $finalStatus = $postStressHealthy.response
        $finalUsn = [int64]$finalStatus.last_usn

        # STRESS FINAL SENTINEL TEST (Create -> Converge -> Must Exist -> Delete -> Converge -> Zero Stale Entries)
        $sentinelGuid = [Guid]::NewGuid().ToString('N')
        $sentinelName = "BlinkStress_FinalSentinel_$sentinelGuid.txt"
        $sentinelPath = Join-Path $Fixture.root $sentinelName
        [System.IO.File]::WriteAllText($sentinelPath, "sentinel $sentinelGuid")

        $sentinelCreateHealthy = Wait-Healthy -Label 'stress-sentinel-create' -TimeoutSeconds 120
        # Healthy can be observed before the scheduler has consumed the new
        # USN record.  Poll Search itself so the create assertion proves that
        # the sentinel entered the index rather than merely observing an old
        # Healthy state.
        $sentinelHit = Wait-SearchHit -Name $sentinelName -PathLike $Fixture.root -TimeoutSeconds 120
        $sentinelCreatedValid = $null -ne $sentinelHit

        Remove-Item -LiteralPath $sentinelPath -Force
        $sentinelDeleteHealthy = Wait-Healthy -Label 'stress-sentinel-delete' -TimeoutSeconds 120
        # Require the create phase to have been observed before accepting the
        # delete phase.  Otherwise a stale zero-result query could make both
        # halves look successful when the create USN was never caught up.
        $sentinelDeletedValid = $sentinelCreatedValid -and
            (Wait-SearchEmpty -Name $sentinelName -PathLike $Fixture.root -TimeoutSeconds 120)

        $usnAdvanced = $finalUsn -gt $initialUsn
        $telemetryDeadline = (Get-Date).AddSeconds(10)
        $stressTelemetry = @()
        do {
            $stressTelemetry = @(
                Get-ChildItem -LiteralPath $telemetryRoot -Filter 'operation-*.json' -File -ErrorAction SilentlyContinue |
                    Where-Object { $beforeTelemetryIds -notcontains (($_.BaseName -replace '^operation-', '')) } |
                    ForEach-Object {
                        try { Get-Content -LiteralPath $_.FullName -Raw -Encoding utf8 | ConvertFrom-Json } catch { $null }
                    } |
                    Where-Object { $null -ne $_ -and -not [bool]$_.initial }
            )
            if ($stressTelemetry.Count -eq 0 -and (Get-Date) -lt $telemetryDeadline) {
                Start-Sleep -Milliseconds 250
            }
        } while ($stressTelemetry.Count -eq 0 -and (Get-Date) -lt $telemetryDeadline)
        $catchupBatches = @($stressTelemetry | Where-Object {
            $_.error -eq 0 -and [uint32]$_.catchup_batch_count -gt 0
        } | Sort-Object operation_id)
        $statusPass = $writerCompleted -and (-not $deadlockOrTimeout) -and $monotonic -and $usnAdvanced -and
                      ($finalStatus.index_state -eq 'Healthy') -and
                      $sentinelCreatedValid -and $sentinelDeletedValid

        Add-Check -Area 'Stress' -Name 'Writer_Completed_Bounded' -Status $(if ($writerCompleted) { 'PASS' } else { 'FAIL' }) `
            -Details ("completed={0}, timed_out={1}, error={2}" -f $writerCompleted, $writerTimedOut, $writerError)
        Add-Check -Area 'Stress' -Name 'CatchUp_Fixed_Target_Batch' -Status $(if ($catchupBatches.Count -gt 0) { 'PASS' } else { 'FAIL' }) `
            -Details ("catchup_batch_count={0}; telemetry_count={1}" -f $catchupBatches.Count, $stressTelemetry.Count)

        Add-Check -Area 'Stress' -Name 'Continuous_Write_Livelock_Prevention' `
            -Status $(if ($statusPass) { 'PASS' } else { 'FAIL' }) `
            -Details ("writer_completed={0}, usn_advanced={1} (init={2}, final={3}), monotonic={4}, deadlocks={5}, sentinel_create={6}, sentinel_zero_stale={7}, median={8:F2}ms, P95={9:F2}ms" -f $writerCompleted, $usnAdvanced, $initialUsn, $finalUsn, $monotonic, $deadlockOrTimeout, $sentinelCreatedValid, $sentinelDeletedValid, $median, $p95)

        $sentinelPass = $sentinelCreatedValid -and $sentinelDeletedValid
        Write-Log ("Continuous write stress completed: USN {0}->{1}, sentinel_pass={2}, IPC median={3:F2}ms, P95={4:F2}ms" -f $initialUsn, $finalUsn, $sentinelPass, $median, $p95)
        $stressResult = [ordered]@{
            writer_completed = $writerCompleted
            writer_timed_out = $writerTimedOut
            writer_error = $writerError
            writer_output = $writerOutput
            initial_usn = $initialUsn
            final_usn = $finalUsn
            usn_advanced = $usnAdvanced
            monotonic_usn = $monotonic
            catchup_batch_count = $catchupBatches.Count
            catchup_batches = $catchupBatches
            telemetry_observed = $stressTelemetry.Count
            sentinel_created = $sentinelCreatedValid
            sentinel_zero_stale = $sentinelDeletedValid
            ipc_median_ms = $median
            ipc_p95_ms = $p95
            ipc_max_ms = $maxLat
        }
        Add-ResultProperty -Name 'stress' -Value $stressResult
        return $stressResult
    }
}

function Invoke-RestartRecovery {
    param([hashtable]$Fixture)
    Trace-Stage 'restart-recovery' {
        $before = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
        Stop-InstalledService
        $duringStop = Join-Path $Fixture.root 'CreatedWhileServiceStopped-Phase05.txt'
        [IO.File]::WriteAllText($duringStop, 'created while service stopped', [Text.UTF8Encoding]::new($false))
        $restartStarted = [Diagnostics.Stopwatch]::StartNew()
        Start-Service -Name $serviceName
        Wait-ServiceStatus -Expected 'Running' | Out-Null
        # Restart CatchUp Timeout: 180 seconds (3 minutes)
        $healthy = Wait-Healthy -Label 'restart-catchup' -TimeoutSeconds 180 -RequestTimeoutMs 5000
        $restartStarted.Stop()
        $after = $healthy.response
        $operation = $healthy.operation
        $telemetry = if ($null -ne $operation) { $operation.telemetry } else { $null }
        $noFullRebuild = $null -ne $telemetry -and -not [bool]$telemetry.initial
        $duringHit = Find-Hit (Invoke-SearchHits 'CreatedWhileServiceStopped-Phase05.txt' -QueryPathPrefix $Fixture.root) 'CreatedWhileServiceStopped-Phase05.txt'
        Add-Check -Area 'Recovery' -Name 'BLK5_Load_Journal_Validation' -Status $(if ($after.index_state -eq 'Healthy') { 'PASS' } else { 'FAIL' }) -Details ("state=$($after.index_state), journal=$($after.journal_id)")
        Add-Check -Area 'Recovery' -Name 'USN_Catchup_Without_Full_Rebuild' -Status $(if ($noFullRebuild) { 'PASS' } else { 'FAIL' }) -Details ("telemetry_initial=$($telemetry.initial)")
        Add-Check -Area 'Recovery' -Name 'Changes_During_Stop_Recovered' -Status $(if ($null -ne $duringHit) { 'PASS' } else { 'FAIL' }) -Details $duringStop
        $recovery = [ordered]@{
            before = $before
            after = $after
            restart_query_seconds = $restartStarted.Elapsed.TotalSeconds
            catchup_operation = $operation
            no_full_mft_rebuild = $noFullRebuild
            during_stop_file = $duringStop
            during_stop_hit = $duringHit
        }
        Add-ResultProperty -Name 'restart_recovery' -Value $recovery
        return $recovery
    }
}

function Invoke-ServiceFailureTests {
    param([hashtable]$Fixture)
    Trace-Stage 'service-failure' {
        $stopTimer = [Diagnostics.Stopwatch]::StartNew()
        Stop-InstalledService
        $stopTimer.Stop()
        $localBrowse = $false
        try {
            $localBrowse = @(Get-ChildItem -LiteralPath $Fixture.root -Force -ErrorAction Stop).Count -ge 0
        }
        catch {}
        $disconnectStop = $false
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try {
            $null = Send-PipeRequest -Type 1 -TimeoutMs 1000
        }
        catch {
            $disconnectStop = $true
        }
        $sw.Stop()
        $disconnectStopMs = $sw.Elapsed.TotalMilliseconds
        Add-Check -Area 'ServiceFailure' -Name 'Stop_Disconnect_Bounded' -Status $(if ($disconnectStop -and $disconnectStopMs -lt 5000) { 'PASS' } else { 'FAIL' }) -Details ("elapsed_ms=$disconnectStopMs")
        Add-Check -Area 'ServiceFailure' -Name 'Local_Browse_While_Stopped' -Status $(if ($localBrowse) { 'PASS' } else { 'FAIL' }) -Details $Fixture.root
        Start-Service -Name $serviceName
        Wait-ServiceStatus -Expected 'Running' | Out-Null
        # Service Reconnect Timeout: 120 seconds (2 minutes)
        $null = Wait-Healthy -Label 'after-stop-start' -TimeoutSeconds 120 -RequestTimeoutMs 5000

        $process = Get-Process -Name 'Blink.IndexService' -ErrorAction Stop | Select-Object -First 1
        Stop-Process -Id $process.Id -Force
        Start-Sleep -Milliseconds 500
        $killDisconnect = $false
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try {
            $null = Send-PipeRequest -Type 1 -TimeoutMs 1000
        }
        catch {
            $killDisconnect = $true
        }
        $sw.Stop()
        $killDisconnectMs = $sw.Elapsed.TotalMilliseconds
        Add-Check -Area 'ServiceFailure' -Name 'Kill_Disconnect_Bounded' -Status $(if ($killDisconnect -and $killDisconnectMs -lt 5000) { 'PASS' } else { 'FAIL' }) -Details ("elapsed_ms=$killDisconnectMs")
        Start-Service -Name $serviceName
        Wait-ServiceStatus -Expected 'Running' | Out-Null
        $reconnected = Wait-Healthy -Label 'after-kill-reconnect' -TimeoutSeconds 120 -RequestTimeoutMs 5000
        Add-Check -Area 'ServiceFailure' -Name 'Restart_Reconnect_Healthy' -Status $(if ($reconnected.response.index_state -eq 'Healthy') { 'PASS' } else { 'FAIL' }) -Details $reconnected.response.message
        $failureResult = [ordered]@{
            stop_elapsed_ms = $stopTimer.Elapsed.TotalMilliseconds
            stop_disconnect_bounded = $disconnectStop
            stop_disconnect_ms = $disconnectStopMs
            local_browse = $localBrowse
            kill_disconnect_bounded = $killDisconnect
            kill_disconnect_ms = $killDisconnectMs
            reconnect = $reconnected
            blink_ui_process_launched = $false
            blink_ui_note = 'Blink.exe was not launched by automation; bounded IPC and local browse were used to avoid GUI side effects.'
        }
        Add-ResultProperty -Name 'service_failure' -Value $failureResult
        return $failureResult
    }
}

function Ensure-CrcType {
    if ($null -eq ('Phase05Crc32' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
public static class Phase05Crc32
{
    public static UInt32 Compute(byte[] data, int length)
    {
        UInt32 value = 0xFFFFFFFFu;
        for (int i = 0; i < length; i++)
        {
            value ^= data[i];
            for (int bit = 0; bit < 8; bit++)
            {
                UInt32 mask = (value & 1u) != 0 ? 0xEDB88320u : 0u;
                value = (value >> 1) ^ mask;
            }
        }
        return ~value;
    }
}
'@
    }
}

function Wait-IndexState {
    param(
        [string]$ExpectedState,
        [int]$TimeoutSeconds = 120,
        [int]$RequestTimeoutMs = 5000
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $last = $null
    $lastHeartbeat = Get-Date
    while ((Get-Date) -lt $deadline) {
        try {
            $requestTimeout = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds $RequestTimeoutMs
            if ($requestTimeout -le 0) { break }
            $last = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs $requestTimeout
            if ($last.index_state -eq $ExpectedState) {
                return $last
            }
            if ($last.index_state -notin @('Rebuilding', 'CatchingUp') -and $last.error -ne 997) {
                return $last
            }
            if (((Get-Date) - $lastHeartbeat).TotalSeconds -ge 10) {
                Write-Log ("Waiting for index state {0}: current={1}, error={2}" -f $ExpectedState, $last.index_state, $last.error)
                $lastHeartbeat = Get-Date
            }
        }
        catch {
            Write-Log ("Waiting for index state {0}: transient IPC failure: {1}" -f $ExpectedState, $_.Exception.Message) -Failure
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 500
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    return $last
}

function Invoke-CheckpointInvalidTest {
    param([hashtable]$Fixture)
    Trace-Stage 'checkpoint-invalid-test' {
        $store = Join-Path $serviceDataRoot 'index\d.blink5'
        if (-not (Test-Path -LiteralPath $store -PathType Leaf)) {
            throw "Valid BLK5 store was not found: $store"
        }
        Ensure-CrcType
        $backup = Join-Path $harnessLiveRoot 'valid-store-backup.blink5'
        Copy-Item -LiteralPath $store -Destination $backup -Force
        Stop-InstalledService
        try {
            [byte[]]$bytes = [IO.File]::ReadAllBytes($store)
            if ($bytes.Length -lt 100) { throw 'BLK5 store is unexpectedly short' }
            $oldJournal = [BitConverter]::ToUInt64($bytes, 16)
            $newJournal = [uint64]($oldJournal + 1)
            [byte[]]$journalBytes = [BitConverter]::GetBytes($newJournal)
            [Array]::Copy($journalBytes, 0, $bytes, 16, 8)
            $crc = [Phase05Crc32]::Compute($bytes, $bytes.Length - 4)
            [byte[]]$crcBytes = [BitConverter]::GetBytes([uint32]$crc)
            [Array]::Copy($crcBytes, 0, $bytes, $bytes.Length - 4, 4)
            [IO.File]::WriteAllBytes($store, $bytes)
            Start-Service -Name $serviceName
            Wait-ServiceStatus -Expected 'Running' | Out-Null
            # Loading the modified store is asynchronous.  The first status
            # response is expected to be Busy/997/Rebuilding; acceptance must
            # observe the terminal RebuildRequired state before judging the
            # checksum-valid journal mismatch.
            $invalidResponse = Wait-IndexState -ExpectedState 'RebuildRequired' -TimeoutSeconds 120 -RequestTimeoutMs 5000
            if ($null -eq $invalidResponse) {
                $invalidResponse = [pscustomobject]@{
                    index_state = 'Unknown'
                    error = 1460
                    status = 'Timeout'
                    message = 'Timed out waiting for RebuildRequired'
                }
            }
            # A checksum-valid Journal ID mismatch maps to
            # ERROR_JOURNAL_ENTRY_DELETED (1181).  1178 is
            # ERROR_JOURNAL_DELETE_IN_PROGRESS and is not the service's
            # mismatch result.
            $valid = $invalidResponse.index_state -eq 'RebuildRequired' -and $invalidResponse.error -eq 1181
            Add-Check -Area 'JournalInvalid' -Name 'ChecksumValid_JournalIdMismatch_RebuildRequired' -Status $(if ($valid) { 'PASS' } else { 'FAIL' }) -Details ("state=$($invalidResponse.index_state), error=$($invalidResponse.error)")
            $result = [ordered]@{
                type = 'Synthetic Invalid Checkpoint Test'
                mutated_field = 'journal_id'
                original_journal_id = $oldJournal
                synthetic_journal_id = $newJournal
                response = $invalidResponse
                rebuild_required = $valid
            }
        }
        finally {
            Stop-InstalledService
            Copy-Item -LiteralPath $backup -Destination $store -Force
            Start-Service -Name $serviceName
            Wait-ServiceStatus -Expected 'Running' | Out-Null
            $null = Wait-Healthy -Label 'after-journal-restore' -TimeoutSeconds 120 -RequestTimeoutMs 5000
        }
        Add-ResultProperty -Name 'journal_invalid' -Value $result
        return $result
    }
}

function Invoke-CorruptionTest {
    Trace-Stage 'store-corruption-test' {
        $store = Join-Path $serviceDataRoot 'index\d.blink5'
        $backup = Join-Path $harnessLiveRoot 'valid-store-before-corruption.blink5'
        Copy-Item -LiteralPath $store -Destination $backup -Force
        Stop-InstalledService
        try {
            [byte[]]$bytes = [IO.File]::ReadAllBytes($store)
            $bytes[100] = $bytes[100] -bxor 0x5A
            [IO.File]::WriteAllBytes($store, $bytes)
            Start-Service -Name $serviceName
            Wait-ServiceStatus -Expected 'Running' | Out-Null
            $response = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
            $valid = $response.index_state -eq 'Corrupted'
            Add-Check -Area 'Persistence' -Name 'Corrupted_Store_Not_Healthy' -Status $(if ($valid) { 'PASS' } else { 'FAIL' }) -Details ("state=$($response.index_state), error=$($response.error)")
            $result = [ordered]@{ response = $response; corrupted = $valid }
        }
        finally {
            Stop-InstalledService
            Copy-Item -LiteralPath $backup -Destination $store -Force
            Start-Service -Name $serviceName
            Wait-ServiceStatus -Expected 'Running' | Out-Null
            $null = Wait-Healthy -Label 'after-corruption-restore' -TimeoutSeconds 120 -RequestTimeoutMs 5000
        }
        Add-ResultProperty -Name 'persistence' -Value $result
        return $result
    }
}

function Invoke-DirectVolumeProbe {
    param([switch]$InitializeOnly)
    $stageName = if ($InitializeOnly) { 'initialize-volume-probe-helper' } else { 'direct-volume-probe' }
    Trace-Stage $stageName {
        if ($null -eq ('Phase05VolumeProbe' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public sealed class Phase05VolumeProbeResult
{
    public string file_system;
    public bool volume_handle;
    public int volume_handle_error;
    public bool query_usn_journal;
    public int query_usn_journal_error;
    public ulong journal_id;
    public long first_usn;
    public long next_usn;
    public long lowest_valid_usn;
    public long max_usn;
    public bool enum_usn_data;
    public int enum_usn_data_error;
    public ulong mft_record_count;
    public double enum_seconds;
    public bool read_usn_journal;
    public int read_usn_journal_error;
    public ulong read_record_count;
    public double read_seconds;
    public string mft_input_structure;
    public int mft_input_size_bytes;
    public string read_input_structure;
    public int read_input_size_bytes;
    public ushort min_record_major_version;
    public ushort max_record_major_version;
}

public sealed class Phase05FileIdentityResult
{
    public bool ok;
    public ulong volume_serial;
    public uint volume_serial_low32;
    public string file_id_hex;
    public int error;
}

public static class Phase05VolumeProbe
{
    const uint FILE_READ_ATTRIBUTES = 0x00000080;
    const uint GENERIC_READ = 0x80000000;
    const uint FILE_SHARE_READ = 1;
    const uint FILE_SHARE_WRITE = 2;
    const uint FILE_SHARE_DELETE = 4;
    const uint OPEN_EXISTING = 3;
    const uint FSCTL_READ_USN_JOURNAL = 0x000900BB;
    const uint FSCTL_ENUM_USN_DATA = 0x000900B3;
    const uint FSCTL_QUERY_USN_JOURNAL = 0x000900F4;
    const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
    const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
    const int FileIdInfo = 18;

    [StructLayout(LayoutKind.Sequential)]
    struct MftEnumDataV0
    {
        public ulong Start;
        public long Low;
        public long High;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MftEnumDataV1
    {
        public ulong Start;
        public long Low;
        public long High;
        public ushort MinMajorVersion;
        public ushort MaxMajorVersion;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct ReadUsnJournalDataV0
    {
        public long StartUsn;
        public uint ReasonMask;
        public uint ReturnOnlyOnClose;
        public ulong Timeout;
        public ulong BytesToWaitFor;
        public ulong UsnJournalID;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct ReadUsnJournalDataV1
    {
        public long StartUsn;
        public uint ReasonMask;
        public uint ReturnOnlyOnClose;
        public ulong Timeout;
        public ulong BytesToWaitFor;
        public ulong UsnJournalID;
        public ushort MinMajorVersion;
        public ushort MaxMajorVersion;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code, byte[] input, uint inputSize, byte[] output, uint outputSize, out uint returned, IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int fileInformationClass, byte[] buffer, uint bufferSize);

    static byte[] StructBytes<T>(T value)
    {
        int size = Marshal.SizeOf(typeof(T));
        IntPtr memory = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.StructureToPtr(value, memory, false);
            byte[] bytes = new byte[size];
            Marshal.Copy(memory, bytes, 0, size);
            return bytes;
        }
        finally { Marshal.FreeHGlobal(memory); }
    }

    static int CountRecords(byte[] buffer, int length, int offset, ref ulong count, ushort minMajor, ushort maxMajor)
    {
        while (offset < length)
        {
            if (offset + 4 > length) return 13;
            uint recordLength = BitConverter.ToUInt32(buffer, offset);
            if (recordLength < 8 || recordLength > length - offset) return 13;
            if (offset + 8 > length) return 13;
            ushort major = BitConverter.ToUInt16(buffer, offset + 4);
            if (major < minMajor || major > maxMajor) return 50;
            count++;
            offset += (int)recordLength;
        }
        return 0;
    }

    public static Phase05FileIdentityResult ReadFileIdentity(string path, bool directory)
    {
        var result = new Phase05FileIdentityResult { file_id_hex = string.Empty };
        uint flags = FILE_FLAG_OPEN_REPARSE_POINT;
        if (directory) flags |= FILE_FLAG_BACKUP_SEMANTICS;
        using (SafeFileHandle handle = CreateFileW(
            path,
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            IntPtr.Zero,
            OPEN_EXISTING,
            flags,
            IntPtr.Zero))
        {
            if (handle == null || handle.IsInvalid)
            {
                result.error = Marshal.GetLastWin32Error();
                return result;
            }
            byte[] buffer = new byte[24];
            if (!GetFileInformationByHandleEx(handle, FileIdInfo, buffer, (uint)buffer.Length))
            {
                result.error = Marshal.GetLastWin32Error();
                return result;
            }
            result.volume_serial = BitConverter.ToUInt64(buffer, 0);
            result.volume_serial_low32 = unchecked((uint)result.volume_serial);
            byte[] fileId = new byte[16];
            Array.Copy(buffer, 8, fileId, 0, fileId.Length);
            result.file_id_hex = BitConverter.ToString(fileId).Replace("-", string.Empty);
            result.ok = true;
            result.error = 0;
            return result;
        }
    }

    public static Phase05VolumeProbeResult Run(string root)
    {
        var result = new Phase05VolumeProbeResult();
        string drive = root.TrimEnd('\\', '/');
        if (drive.Length < 2 || drive[1] != ':')
        {
            result.volume_handle_error = 123;
            return result;
        }
        using (SafeFileHandle handle = CreateFileW(@"\\.\" + drive.Substring(0, 2), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero))
        {
            if (handle == null || handle.IsInvalid)
            {
                result.volume_handle_error = Marshal.GetLastWin32Error();
                return result;
            }
            result.volume_handle = true;
            try
            {
                result.file_system = new DriveInfo(drive + "\\").DriveFormat;
            }
            catch
            {
                result.file_system = string.Empty;
            }
            byte[] queryBuffer = new byte[128];
            uint returned;
            if (!DeviceIoControl(handle, FSCTL_QUERY_USN_JOURNAL, null, 0, queryBuffer, (uint)queryBuffer.Length, out returned, IntPtr.Zero))
            {
                result.query_usn_journal_error = Marshal.GetLastWin32Error();
                return result;
            }
            result.query_usn_journal = true;
            result.journal_id = BitConverter.ToUInt64(queryBuffer, 0);
            result.first_usn = BitConverter.ToInt64(queryBuffer, 8);
            result.next_usn = BitConverter.ToInt64(queryBuffer, 16);
            result.lowest_valid_usn = BitConverter.ToInt64(queryBuffer, 24);
            result.max_usn = BitConverter.ToInt64(queryBuffer, 32);

            bool isNtfs = string.Equals(result.file_system, "NTFS", StringComparison.OrdinalIgnoreCase);
            bool isRefs = string.Equals(result.file_system, "ReFS", StringComparison.OrdinalIgnoreCase);
            if (!isNtfs && !isRefs)
            {
                result.enum_usn_data_error = 50;
                result.read_usn_journal_error = 50;
                return result;
            }
            result.min_record_major_version = 2;
            result.max_record_major_version = isNtfs ? (ushort)2 : (ushort)3;
            byte[] enumInputBytes;
            if (isNtfs)
            {
                result.mft_input_structure = "MFT_ENUM_DATA_V0";
                result.mft_input_size_bytes = Marshal.SizeOf(typeof(MftEnumDataV0));
                enumInputBytes = StructBytes(new MftEnumDataV0 { Start = 0, Low = 0, High = long.MaxValue });
            }
            else
            {
                result.mft_input_structure = "MFT_ENUM_DATA_V1";
                result.mft_input_size_bytes = Marshal.SizeOf(typeof(MftEnumDataV1));
                enumInputBytes = StructBytes(new MftEnumDataV1 { Start = 0, Low = 0, High = long.MaxValue, MinMajorVersion = 2, MaxMajorVersion = 3 });
            }
            var enumWatch = Stopwatch.StartNew();
            ulong start = 0;
            bool enumDone = false;
            while (!enumDone)
            {
                Array.Copy(BitConverter.GetBytes(start), 0, enumInputBytes, 0, 8);
                byte[] output = new byte[1024 * 1024];
                if (!DeviceIoControl(handle, FSCTL_ENUM_USN_DATA, enumInputBytes, (uint)enumInputBytes.Length, output, (uint)output.Length, out returned, IntPtr.Zero))
                {
                    int error = Marshal.GetLastWin32Error();
                    if (error == 38 || error == 18)
                    {
                        enumDone = true;
                        result.enum_usn_data = true;
                        break;
                    }
                    result.enum_usn_data_error = error;
                    break;
                }
                if (returned < 8)
                {
                    enumDone = true;
                    result.enum_usn_data = true;
                    break;
                }
                ulong next = BitConverter.ToUInt64(output, 0);
                int parseError = CountRecords(output, (int)returned, 8, ref result.mft_record_count, result.min_record_major_version, result.max_record_major_version);
                if (parseError != 0)
                {
                    result.enum_usn_data_error = parseError;
                    break;
                }
                if (next == start)
                {
                    result.enum_usn_data_error = 13;
                    break;
                }
                start = next;
            }
            enumWatch.Stop();
            result.enum_seconds = enumWatch.Elapsed.TotalSeconds;

            var readWatch = Stopwatch.StartNew();
            byte[] readInputBytes;
            if (isNtfs)
            {
                result.read_input_structure = "READ_USN_JOURNAL_DATA_V0";
                result.read_input_size_bytes = Marshal.SizeOf(typeof(ReadUsnJournalDataV0));
                readInputBytes = StructBytes(new ReadUsnJournalDataV0
                {
                    StartUsn = result.next_usn,
                    ReasonMask = 0xFFFFFFFF,
                    ReturnOnlyOnClose = 0,
                    Timeout = 0,
                    BytesToWaitFor = 0,
                    UsnJournalID = result.journal_id
                });
            }
            else
            {
                result.read_input_structure = "READ_USN_JOURNAL_DATA_V1";
                result.read_input_size_bytes = Marshal.SizeOf(typeof(ReadUsnJournalDataV1));
                readInputBytes = StructBytes(new ReadUsnJournalDataV1
                {
                    StartUsn = result.next_usn,
                    ReasonMask = 0xFFFFFFFF,
                    ReturnOnlyOnClose = 0,
                    Timeout = 0,
                    BytesToWaitFor = 0,
                    UsnJournalID = result.journal_id,
                    MinMajorVersion = 2,
                    MaxMajorVersion = 3
                });
            }
            byte[] readOutput = new byte[1024 * 1024];
            if (!DeviceIoControl(handle, FSCTL_READ_USN_JOURNAL, readInputBytes, (uint)readInputBytes.Length, readOutput, (uint)readOutput.Length, out returned, IntPtr.Zero))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == 38 || error == 18)
                {
                    result.read_usn_journal = true;
                }
                else
                {
                    result.read_usn_journal_error = error;
                }
            }
            else
            {
                result.read_usn_journal = true;
                if (returned >= 8)
                {
                    int parseError = CountRecords(readOutput, (int)returned, 8, ref result.read_record_count, result.min_record_major_version, result.max_record_major_version);
                    if (parseError != 0)
                    {
                        result.read_usn_journal = false;
                        result.read_usn_journal_error = parseError;
                    }
                }
            }
            readWatch.Stop();
            result.read_seconds = readWatch.Elapsed.TotalSeconds;
        }
        return result;
    }
}
'@
        }
        if ($InitializeOnly) {
            return $null
        }
        return [Phase05VolumeProbe]::Run($VolumeRoot)
    }
}

function Ensure-Phase05VolumeProbeType {
    if ($null -eq ('Phase05VolumeProbe' -as [type])) {
        Invoke-DirectVolumeProbe -InitializeOnly | Out-Null
    }
}

function Get-ExpectedSearchIdentity {
    param([string]$Path)
    Ensure-Phase05VolumeProbeType
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    $identity = [Phase05VolumeProbe]::ReadFileIdentity($item.FullName, [bool]$item.PSIsContainer)
    if ($null -eq $identity -or -not $identity.ok) {
        throw "FILE_ID_INFO probe failed for '$($item.FullName)' (Win32=$($identity.error))"
    }
    return [pscustomobject][ordered]@{
        path = $item.FullName
        volume_serial = [uint64]$identity.volume_serial
        volume_serial_low32 = [uint32]$identity.volume_serial_low32
        file_id = $identity.file_id_hex
        key = ('{0:X16}:{1}' -f [uint64]$identity.volume_serial, $identity.file_id_hex)
    }
}

function Test-SearchIdentityMatch {
    param($Hit, $Expected)
    if ($null -eq $Hit -or $null -eq $Hit.identity -or $null -eq $Expected) {
        return $false
    }
    # PowerShell 5.1 parses the unsuffixed hexadecimal literal 0xFFFFFFFF as
    # signed Int32 -1.  Casting that value to UInt64 throws and used to make
    # every known-hit assertion look like a Search failure.  Keep the mask
    # explicitly representable as a positive UInt64.
    $hitSerialLow32 = ([uint64]$Hit.identity.volume_serial) -band [uint64]4294967295
    return [bool]$Hit.identity.valid -and
        ([string]$Hit.identity.file_id).Equals([string]$Expected.file_id, [StringComparison]::OrdinalIgnoreCase) -and
        $hitSerialLow32 -eq [uint64]$Expected.volume_serial_low32
}

function Test-SearchPathWithin {
    param([string]$Path, [string]$Scope)
    if ([string]::IsNullOrWhiteSpace($Path) -or [string]::IsNullOrWhiteSpace($Scope)) {
        return $false
    }
    $normalizedPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $normalizedScope = [IO.Path]::GetFullPath($Scope).TrimEnd('\')
    return $normalizedPath.Equals($normalizedScope, [StringComparison]::OrdinalIgnoreCase) -or
        $normalizedPath.StartsWith($normalizedScope + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Test-SearchPathExact {
    param([string]$Path, [string]$ExpectedPath)
    if ([string]::IsNullOrWhiteSpace($Path) -or [string]::IsNullOrWhiteSpace($ExpectedPath)) {
        return $false
    }
    try {
        $normalizedPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
        $normalizedExpected = [IO.Path]::GetFullPath($ExpectedPath).TrimEnd('\')
        return $normalizedPath.Equals($normalizedExpected, [StringComparison]::OrdinalIgnoreCase)
    }
    catch {
        return $false
    }
}

function Invoke-SearchCorrectness {
    param([hashtable]$Fixture)
    Trace-Stage 'search-correctness' {
        $stableExpected = Get-ExpectedSearchIdentity $Fixture.files.stable
        $renameChildExpected = Get-ExpectedSearchIdentity $Fixture.files.renameChild
        $cases = @(
            [ordered]@{
                name = 'known_exact_filename'
                text = 'StableFile-Phase05.txt'
                prefix = $false
                extension = ''
                path = $Fixture.root
                expected_path = $Fixture.files.stable
                expected = $stableExpected
                outside_path = ''
                expects_hit = $true
            }
            [ordered]@{
                name = 'known_prefix'
                text = 'StableFile'
                prefix = $true
                extension = ''
                path = $Fixture.root
                expected_path = $Fixture.files.stable
                expected = $stableExpected
                outside_path = ''
                expects_hit = $true
            }
            [ordered]@{
                name = 'known_substring'
                text = 'Phase05'
                prefix = $false
                extension = ''
                path = $Fixture.root
                expected_path = $Fixture.files.stable
                expected = $stableExpected
                outside_path = ''
                expects_hit = $true
            }
            [ordered]@{
                name = 'known_extension'
                text = ''
                prefix = $false
                extension = '.txt'
                path = $Fixture.root
                expected_path = $Fixture.files.stable
                expected = $stableExpected
                outside_path = ''
                expects_hit = $true
            }
            [ordered]@{
                name = 'known_directory_scoped'
                text = 'RenameChild-Phase05.txt'
                prefix = $false
                extension = ''
                path = $Fixture.renameDirectory
                expected_path = $Fixture.files.renameChild
                expected = $renameChildExpected
                outside_path = $Fixture.files.outsideScopedChild
                expects_hit = $true
            }
            [ordered]@{
                name = 'known_miss'
                text = 'Blink-Phase05-Known-Miss-20260811'
                prefix = $false
                extension = ''
                path = $Fixture.root
                expected_path = ''
                expected = $null
                outside_path = ''
                expects_hit = $false
            }
        )

        $records = @()
        foreach ($case in $cases) {
            $record = [ordered]@{
                run_id = $runId
                query_type = $case.name
                expectation = $(if ($case.expects_hit) { 'known_hit' } else { 'known_miss' })
                status = 'FAIL'
                result_count = 0
                expected_path = $case.expected_path
                expected_identity = $case.expected
                matching_identity = $null
                scope = $case.path
                scope_valid = $false
                outside_scope_hits = @()
                error = $null
            }
            try {
                $hits = Invoke-SearchHits -QueryText $case.text -QueryPrefix:([bool]$case.prefix) -QueryExtension $case.extension -QueryPathPrefix $case.path
                $record.result_count = @($hits).Count
                $scopeInvalid = @($hits | Where-Object { -not (Test-SearchPathWithin $_.full_path $case.path) })
                # Keep these values as arrays even when exactly one result is
                # returned.  Windows PowerShell 5.1 otherwise unwraps the
                # conditional subexpression and StrictMode makes .Count on a
                # scalar fail, masking a valid known-hit assertion.
                $outsideHits = @(
                    if ([string]::IsNullOrWhiteSpace($case.outside_path)) {
                        @()
                    }
                    else {
                        @($hits | Where-Object { ([IO.Path]::GetFullPath($_.full_path)).Equals([IO.Path]::GetFullPath($case.outside_path), [StringComparison]::OrdinalIgnoreCase) })
                    }
                )
                $record.scope_valid = @($scopeInvalid).Count -eq 0
                $record.outside_scope_hits = @($outsideHits | ForEach-Object { $_.full_path })
                $matching = @($hits | Where-Object { Test-SearchIdentityMatch $_ $case.expected })
                if (@($matching).Count -gt 0) {
                    $record.matching_identity = $matching[0].identity
                }
                $record.status = $(if ($case.expects_hit) {
                    if (@($matching).Count -gt 0 -and $record.scope_valid -and @($outsideHits).Count -eq 0) { 'PASS' } else { 'FAIL' }
                }
                else {
                    if ($record.result_count -eq 0) { 'PASS' } else { 'FAIL' }
                })
            }
            catch {
                $record.error = $_.Exception.Message
            }
            $details = "expectation=$($record.expectation), result_count=$($record.result_count), scope_valid=$($record.scope_valid), outside_scope_hits=$(@($record.outside_scope_hits).Count)"
            if ($null -ne $record.error) {
                $details += ", error=$($record.error)"
            }
            Add-Check -Area 'SearchCorrectness' -Name $case.name -Status $record.status -Details $details
            $records += [pscustomobject]$record
        }
        Add-ResultProperty -Name 'search_correctness' -Value $records
        return $records
    }
}

function ConvertTo-TargetedSearchCandidate {
    param($Hit)
    if ($null -eq $Hit) {
        return $null
    }
    $identity = $null
    $identityProperty = $Hit.PSObject.Properties['identity']
    if ($null -ne $identityProperty -and $null -ne $identityProperty.Value) {
        $rawIdentity = $identityProperty.Value
        $identity = [ordered]@{
            valid = [bool]$rawIdentity.valid
            volume_serial = if ($null -ne $rawIdentity.volume_serial) { [uint64]$rawIdentity.volume_serial } else { $null }
            file_id = [string]$rawIdentity.file_id
            key = [string]$rawIdentity.key
        }
    }
    return [ordered]@{
        name = [string]$Hit.name
        full_path = [string]$Hit.full_path
        identity = $identity
        parent_identity = if ($null -ne $Hit.parent_identity) { [string]$Hit.parent_identity.key } else { $null }
        last_usn = if ($null -ne $Hit.last_usn) { [int64]$Hit.last_usn } else { $null }
        is_directory = [bool]$Hit.is_directory
    }
}

function Get-TargetedWriterState {
    if ($null -eq $script:targetedWriterJob) {
        return 'NotStarted'
    }
    return [string]$script:targetedWriterJob.State
}

function Record-TargetedSearchAttempt {
    param(
        [string]$Stage,
        [string]$QueryText,
        [string]$PathScope,
        $PhaseContext,
        $Response,
        [double]$ElapsedMilliseconds,
        [string]$TransportError = ''
    )
    $contextOperationId = [uint64]0
    $oldLastUsn = $null
    $targetUsn = $null
    $resultingLastUsn = $null
    if ($null -ne $PhaseContext) {
        if ($null -ne $PhaseContext.operation_id) { $contextOperationId = [uint64]$PhaseContext.operation_id }
        if ($null -ne $PhaseContext.old_last_usn) { $oldLastUsn = [int64]$PhaseContext.old_last_usn }
        if ($null -ne $PhaseContext.target_usn) { $targetUsn = [int64]$PhaseContext.target_usn }
        if ($null -ne $PhaseContext.resulting_last_usn) { $resultingLastUsn = [int64]$PhaseContext.resulting_last_usn }
    }
    $responseOperationId = if ($null -ne $Response) { [uint64]$Response.operation_id } else { [uint64]0 }
    $operationId = if ($contextOperationId -ne 0) { $contextOperationId } else { $responseOperationId }
    $candidates = if ($null -ne $Response) {
        @($Response.results | ForEach-Object { ConvertTo-TargetedSearchCandidate $_ })
    }
    else {
        @()
    }
    $record = [ordered]@{
        timestamp_utc = (Get-Date).ToUniversalTime().ToString('o')
        stage = $Stage
        query = [ordered]@{
            text = $QueryText
            prefix = $false
            extension = ''
            path_scope = $PathScope
        }
        status = if ($null -ne $Response) { [string]$Response.status } else { 'TransportError' }
        status_code = if ($null -ne $Response) { [int]$Response.status_code } else { $null }
        error = if ($null -ne $Response) { [int]$Response.error } else { $null }
        message = if ($null -ne $Response) { [string]$Response.message } else { '' }
        response_operation_id = $responseOperationId
        operation_id = $operationId
        old_last_usn = $oldLastUsn
        target_usn = $targetUsn
        resulting_last_usn = $resultingLastUsn
        result_count = @($candidates).Count
        elapsed_ms = $ElapsedMilliseconds
        transport_error = $TransportError
        candidates = $candidates
    }
    $script:targetedSearchAttempts.Add([pscustomobject]$record)
    if ($null -ne $script:targetedAcceptance) {
        $script:targetedAcceptance['search_attempts'] = @($script:targetedSearchAttempts)
        Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
    }
    try {
        [IO.File]::AppendAllText(
            $targetedSearchAttemptsPath,
            (($record | ConvertTo-Json -Depth 20 -Compress) + [Environment]::NewLine),
            [Text.UTF8Encoding]::new($false))
    }
    catch {
        Write-Log ("Unable to append targeted Search attempt artifact: {0}" -f $_.Exception.Message) -Failure
    }
    Save-Results
    Write-Log ("Targeted Search [{0}] status={1}, result_count={2}, operation_id={3}, old_usn={4}, target_usn={5}, resulting_usn={6}" -f
        $Stage, $record.status, $record.result_count, $record.operation_id, $record.old_last_usn, $record.target_usn, $record.resulting_last_usn)
    return [pscustomobject]$record
}

function Invoke-TargetedSearchAttempt {
    param(
        [string]$Stage,
        [string]$QueryText,
        [string]$PathScope,
        $PhaseContext
    )
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $response = Send-PipeRequest -Type 5 -RequestVolumeRoot $VolumeRoot -Text $QueryText -PathPrefix $PathScope -Limit 200 -TimeoutMs 5000
        $stopwatch.Stop()
        $attempt = Record-TargetedSearchAttempt -Stage $Stage -QueryText $QueryText -PathScope $PathScope `
            -PhaseContext $PhaseContext -Response $response -ElapsedMilliseconds $stopwatch.Elapsed.TotalMilliseconds
        return [ordered]@{ response = $response; attempt = $attempt }
    }
    catch {
        $stopwatch.Stop()
        $attempt = Record-TargetedSearchAttempt -Stage $Stage -QueryText $QueryText -PathScope $PathScope `
            -PhaseContext $PhaseContext -Response $null -ElapsedMilliseconds $stopwatch.Elapsed.TotalMilliseconds `
            -TransportError $_.Exception.Message
        return [ordered]@{ response = $null; attempt = $attempt }
    }
}

function Find-TargetedSentinelHit {
    param(
        [object[]]$Hits,
        [string]$Name,
        [string]$PathScope,
        [string]$ExpectedPath,
        $ExpectedIdentity
    )
    foreach ($hit in @($Hits)) {
        if ($null -eq $hit) { continue }
        if (-not ([string]$hit.name).Equals($Name, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if (-not (Test-SearchPathWithin -Path ([string]$hit.full_path) -Scope $PathScope)) { continue }
        if (-not (Test-SearchPathExact -Path ([string]$hit.full_path) -ExpectedPath $ExpectedPath)) { continue }
        if (Test-SearchIdentityMatch -Hit $hit -Expected $ExpectedIdentity) {
            return $hit
        }
    }
    return $null
}

function Wait-TargetedSearchHit {
    param(
        [string]$Stage,
        [string]$Name,
        [string]$PathScope,
        [string]$ExpectedPath,
        $ExpectedIdentity,
        $PhaseContext,
        [int]$TimeoutSeconds = 120
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastAttempt = $null
    while ((Get-Date) -lt $deadline) {
        $run = Invoke-TargetedSearchAttempt -Stage $Stage -QueryText $Name -PathScope $PathScope -PhaseContext $PhaseContext
        $lastAttempt = $run.attempt
        if ($null -ne $run.response) {
            if ($run.response.status -eq 'Ok') {
                $hit = Find-TargetedSentinelHit -Hits @($run.response.results) -Name $Name -PathScope $PathScope `
                    -ExpectedPath $ExpectedPath -ExpectedIdentity $ExpectedIdentity
                if ($null -ne $hit) {
                    return [ordered]@{ success = $true; hit = $hit; last_attempt = $lastAttempt }
                }
            }
            elseif ($run.response.status -ne 'Busy' -or $run.response.error -ne 997) {
                Write-Log ("Targeted Search hit polling received terminal status: status={0}, error={1}, state={2}" -f
                    $run.response.status, $run.response.error, $run.response.index_state) -Failure
                break
            }
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 250
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    Write-Log ("Targeted Search hit convergence failed: name={0}, path_scope={1}, last_result_count={2}" -f
        $Name, $PathScope, $(if ($null -ne $lastAttempt) { $lastAttempt.result_count } else { 0 })) -Failure
    return [ordered]@{ success = $false; hit = $null; last_attempt = $lastAttempt }
}

function Wait-TargetedSearchEmpty {
    param(
        [string]$Stage,
        [string]$Name,
        [string]$PathScope,
        $PhaseContext,
        [int]$TimeoutSeconds = 120
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastAttempt = $null
    while ((Get-Date) -lt $deadline) {
        $run = Invoke-TargetedSearchAttempt -Stage $Stage -QueryText $Name -PathScope $PathScope -PhaseContext $PhaseContext
        $lastAttempt = $run.attempt
        if ($null -ne $run.response) {
            if ($run.response.status -eq 'Ok' -and @($run.response.results).Count -eq 0) {
                return [ordered]@{ success = $true; last_attempt = $lastAttempt }
            }
            if ($run.response.status -ne 'Ok' -and ($run.response.status -ne 'Busy' -or $run.response.error -ne 997)) {
                Write-Log ("Targeted Search empty polling received terminal status: status={0}, error={1}, state={2}" -f
                    $run.response.status, $run.response.error, $run.response.index_state) -Failure
                break
            }
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 250
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    Write-Log ("Targeted Search empty convergence failed: name={0}, path_scope={1}, last_result_count={2}" -f
        $Name, $PathScope, $(if ($null -ne $lastAttempt) { $lastAttempt.result_count } else { 0 })) -Failure
    return [ordered]@{ success = $false; last_attempt = $lastAttempt }
}

function Invoke-TargetedCatchUp {
    param(
        [string]$Label,
        [int64]$OldLastUsn,
        [int]$TimeoutSeconds = 120
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $statusSamples = [System.Collections.Generic.List[PSCustomObject]]::new()
    $lastStatus = $null
    $operation = $null
    $transportFailureStarted = $null
    $writerStateAtStart = Get-TargetedWriterState
    while ((Get-Date) -lt $deadline) {
        try {
            $remainingMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 5000
            if ($remainingMs -le 0) { break }
            $lastStatus = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs $remainingMs
            $statusSamples.Add([pscustomobject][ordered]@{
                timestamp_utc = (Get-Date).ToUniversalTime().ToString('o')
                status = $lastStatus.status
                index_state = $lastStatus.index_state
                operation_id = [uint64]$lastStatus.operation_id
                last_usn = [int64]$lastStatus.last_usn
                error = [int]$lastStatus.error
            })
            $transportFailureStarted = $null
        }
        catch {
            if ($null -eq $transportFailureStarted) {
                $transportFailureStarted = Get-Date
                Write-Log ("Targeted CatchUp status transport failure [{0}]: {1}" -f $Label, $_.Exception.Message) -Failure
            }
            $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 250
            if ($sleepMs -le 0) { break }
            Start-Sleep -Milliseconds $sleepMs
            continue
        }

        $runningOperation = $lastStatus.operation_id -ne 0 -and
            ($lastStatus.index_state -in @('CatchingUp', 'Rebuilding') -or $lastStatus.error -eq 997)
        if ($runningOperation) {
            $remainingSeconds = [int][math]::Ceiling(($deadline - (Get-Date)).TotalSeconds)
            if ($remainingSeconds -le 0) { break }
            $operation = Wait-IndexOperation -OperationId $lastStatus.operation_id -Label $Label `
                -TimeoutSeconds $remainingSeconds -AbsoluteDeadline $deadline
            $telemetry = $operation.telemetry
            $targetUsn = $null
            if ($null -ne $telemetry -and $null -ne $telemetry.catchup_target_usn) {
                $targetUsn = [int64]$telemetry.catchup_target_usn
            }
            elseif ($null -ne $operation.response) {
                $targetUsn = [int64]$operation.response.last_usn
            }
            $resultingUsn = [int64]$operation.response.last_usn
            return [ordered]@{
                label = $Label
                operation_id = [uint64]$lastStatus.operation_id
                operation_observed = $true
                old_last_usn = $OldLastUsn
                target_usn = $targetUsn
                resulting_last_usn = $resultingUsn
                completed = $operation.response.error -eq 0 -and $operation.response.index_state -eq 'Healthy'
                error = [int]$operation.response.error
                response = $operation.response
                operation = $operation
                telemetry = $telemetry
                writer_state_at_start = $writerStateAtStart
                writer_state_at_end = Get-TargetedWriterState
                status_samples = @($statusSamples)
            }
        }

        if ($lastStatus.index_state -eq 'Healthy' -and [int64]$lastStatus.last_usn -gt $OldLastUsn) {
            return [ordered]@{
                label = $Label
                operation_id = [uint64]$lastStatus.operation_id
                operation_observed = $false
                old_last_usn = $OldLastUsn
                target_usn = [int64]$lastStatus.last_usn
                resulting_last_usn = [int64]$lastStatus.last_usn
                completed = $true
                error = [int]$lastStatus.error
                response = $lastStatus
                operation = $null
                telemetry = $null
                writer_state_at_start = $writerStateAtStart
                writer_state_at_end = Get-TargetedWriterState
                status_samples = @($statusSamples)
            }
        }
        if ($lastStatus.index_state -in @('RebuildRequired', 'Corrupted', 'Unavailable')) {
            return [ordered]@{
                label = $Label
                operation_id = [uint64]$lastStatus.operation_id
                operation_observed = $false
                old_last_usn = $OldLastUsn
                target_usn = $null
                resulting_last_usn = [int64]$lastStatus.last_usn
                completed = $false
                error = [int]$lastStatus.error
                response = $lastStatus
                operation = $null
                telemetry = $null
                writer_state_at_start = $writerStateAtStart
                writer_state_at_end = Get-TargetedWriterState
                status_samples = @($statusSamples)
            }
        }
        $sleepMs = Get-RemainingMilliseconds -Deadline $deadline -CapMilliseconds 250
        if ($sleepMs -le 0) { break }
        Start-Sleep -Milliseconds $sleepMs
    }
    return [ordered]@{
        label = $Label
        operation_id = if ($null -ne $lastStatus) { [uint64]$lastStatus.operation_id } else { [uint64]0 }
        operation_observed = $false
        old_last_usn = $OldLastUsn
        target_usn = $null
        resulting_last_usn = if ($null -ne $lastStatus) { [int64]$lastStatus.last_usn } else { $null }
        completed = $false
        error = if ($null -ne $lastStatus) { [int]$lastStatus.error } else { $null }
        response = $lastStatus
        operation = $null
        telemetry = $null
        writer_state_at_start = $writerStateAtStart
        writer_state_at_end = Get-TargetedWriterState
        status_samples = @($statusSamples)
        timeout_seconds = $TimeoutSeconds
    }
}

function Start-TargetedContinuousWriter {
    param([string]$Directory, [int]$DurationSeconds = 90)
    if (Test-Path -LiteralPath $Directory) {
        throw "Refusing to overwrite targeted writer directory: $Directory"
    }
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    $writerScript = {
        param($WriterDirectory, $WriterDurationSeconds)
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        $created = 0
        $deleted = 0
        $iteration = 0
        while ($stopwatch.Elapsed.TotalSeconds -lt $WriterDurationSeconds) {
            $path = Join-Path $WriterDirectory ("writer_{0:D5}.tmp" -f $iteration)
            [IO.File]::WriteAllText($path, ("continuous writer {0}" -f $iteration), [Text.UTF8Encoding]::new($false))
            $created++
            if ($iteration -ge 3 -and $iteration % 3 -eq 0) {
                $oldPath = Join-Path $WriterDirectory ("writer_{0:D5}.tmp" -f ($iteration - 3))
                if (Test-Path -LiteralPath $oldPath) {
                    Remove-Item -LiteralPath $oldPath -Force -ErrorAction SilentlyContinue
                    $deleted++
                }
            }
            $iteration++
            Start-Sleep -Milliseconds 50
        }
        [pscustomobject][ordered]@{
            completed = $true
            created = $created
            deleted = $deleted
            iterations = $iteration
            duration_seconds = $stopwatch.Elapsed.TotalSeconds
        }
    }
    $script:targetedWriterJob = Start-Job -ScriptBlock $writerScript -ArgumentList $Directory, $DurationSeconds
    Write-Log ("Targeted continuous writer started: directory={0}, duration_limit={1}s, job_id={2}" -f
        $Directory, $DurationSeconds, $script:targetedWriterJob.Id)
    return $script:targetedWriterJob
}

function Stop-TargetedContinuousWriter {
    if ($null -eq $script:targetedWriterJob) {
        return [ordered]@{ started = $false; state = 'NotStarted'; stopped_by_harness = $false; error = $null }
    }
    $job = $script:targetedWriterJob
    $stoppedByHarness = $false
    try {
        if ($job.State -eq 'Running') {
            Stop-Job -Job $job -ErrorAction SilentlyContinue
            $stoppedByHarness = $true
        }
        $output = @(Receive-Job -Job $job -ErrorAction SilentlyContinue)
        $errors = @($job.ChildJobs | ForEach-Object { @($_.Error) } | Where-Object { $null -ne $_ })
        $result = [ordered]@{
            started = $true
            state = [string]$job.State
            stopped_by_harness = $stoppedByHarness
            completed = $job.State -eq 'Completed'
            output = $output
            errors = @($errors | ForEach-Object { $_.ToString() })
            error = if ($errors.Count -gt 0) { ($errors | ForEach-Object { $_.ToString() }) -join '; ' } else { $null }
        }
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
        $script:targetedWriterJob = $null
        return $result
    }
    catch {
        $message = $_.Exception.Message
        try { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue } catch {}
        $script:targetedWriterJob = $null
        return [ordered]@{
            started = $true
            state = 'CleanupError'
            stopped_by_harness = $stoppedByHarness
            completed = $false
            output = @()
            errors = @($message)
            error = $message
        }
    }
}

function Prepare-TargetedStore {
    if ([string]::IsNullOrWhiteSpace($ExistingStorePath)) {
        throw 'Targeted mode requires -ExistingStorePath pointing to a valid BLK5 file.'
    }
    if ($TargetedServiceAccount -ne 'LocalSystem') {
        throw 'Targeted mode requires explicit -TargetedServiceAccount LocalSystem; it does not silently test or switch accounts.'
    }
    $serviceExecutable = Join-Path $repositoryRoot ("x64\{0}\Blink.IndexService.exe" -f $Configuration)
    if (-not (Test-Path -LiteralPath $serviceExecutable -PathType Leaf)) {
        throw "Targeted mode does not build binaries. Service executable is missing: $serviceExecutable"
    }
    $sourceItem = Get-Item -LiteralPath $ExistingStorePath -Force -ErrorAction Stop
    if ($sourceItem.PSIsContainer -or $sourceItem.Extension -ne '.blink5') {
        throw "ExistingStorePath must be a BLK5 file: $ExistingStorePath"
    }
    $sourcePath = $sourceItem.FullName
    $destinationPath = Join-Path $serviceDataRoot 'index\d.blink5'
    $destinationFullPath = [IO.Path]::GetFullPath($destinationPath)
    if ($sourcePath.Equals($destinationFullPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Targeted mode refuses to use the destination BLK5 as its own source.'
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
    $copied = Get-Item -LiteralPath $destinationPath -Force
    if ($copied.Length -ne $sourceItem.Length) {
        throw "Copied BLK5 size mismatch: source=$($sourceItem.Length), destination=$($copied.Length)"
    }
    return [ordered]@{
        source_path = $sourcePath
        source_size_bytes = [uint64]$sourceItem.Length
        copied_path = $destinationFullPath
        copied_size_bytes = [uint64]$copied.Length
        service_executable = $serviceExecutable
        initial_mft_build_executed = $false
    }
}

function Invoke-TargetedSentinelAcceptance {
    return Trace-Stage 'targeted-sentinel-acceptance' {
        $script:targetedAcceptance = [ordered]@{
            mode = 'targeted_sentinel'
            full_acceptance_executed = $false
            initial_mft_build_executed = $false
            service_account = $TargetedServiceAccount
            existing_store_path = $ExistingStorePath
            copied_store = $null
            fixture_root = $testRoot
            search_attempts_artifact = $targetedSearchAttemptsPath
            healthy_before_create = $null
            writer = $null
            sentinel = $null
            create_catchup = $null
            delete_catchup = $null
            search_attempts = @()
            result = 'NOT_EXECUTED'
        }
        Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
        Set-Content -LiteralPath $targetedSearchAttemptsPath -Value '' -Encoding utf8

        $script:targetedAcceptance['copied_store'] = Prepare-TargetedStore
        Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
        Write-Log ("Targeted mode: Initial MFT build NOT EXECUTED; copied existing BLK5 from {0} to {1}" -f
            $script:targetedAcceptance.copied_store.source_path, $script:targetedAcceptance.copied_store.copied_path)

        Install-TestService -Account $TargetedServiceAccount -DataRoot $serviceDataRoot -HardenedCandidate:$HardenedLocalSystemCandidate
        $healthy = Wait-Healthy -Label 'targeted-store-load' -TimeoutSeconds 180 -RequestTimeoutMs 5000
        $script:targetedAcceptance['healthy_before_create'] = $healthy.response
        Add-Check -Area 'TargetedSentinel' -Name 'BLK5_Load_Healthy' -Status $(if ($healthy.response.index_state -eq 'Healthy') { 'PASS' } else { 'FAIL' }) `
            -Details ("state={0}, journal={1}, operation_id={2}" -f $healthy.response.index_state, $healthy.response.journal_id, $healthy.response.operation_id)
        if ($HardenedLocalSystemCandidate -and -not (Test-HardenedSecurityEvidence)) {
            throw 'Hardened LocalSystem security evidence did not pass.'
        }

        if (Test-Path -LiteralPath $testRoot) {
            throw "Refusing to overwrite existing targeted fixture directory: $testRoot"
        }
        Set-FixtureOwnershipState -State 'creating'
        New-Item -ItemType Directory -Path $fixtureRootBase -Force | Out-Null
        New-Item -ItemType Directory -Path $testRoot | Out-Null
        $script:fixtureOwnership.created_by_run = $true
        $script:fixtureOwnership.created_utc = (Get-Date).ToUniversalTime().ToString('o')
        Set-FixtureOwnershipState -State 'created'
        $sentinelDirectory = Join-Path $testRoot 'Sentinel'
        $writerDirectory = Join-Path $testRoot 'ContinuousWriter'
        New-Item -ItemType Directory -Force -Path $sentinelDirectory | Out-Null
        $writerJob = Start-TargetedContinuousWriter -Directory $writerDirectory -DurationSeconds 90
        try {
            $beforeCreate = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
            $sentinelGuid = [Guid]::NewGuid().ToString('N')
            $sentinelName = "BlinkStress_TargetedSentinel_$sentinelGuid.txt"
            $sentinelPath = Join-Path $sentinelDirectory $sentinelName
            [IO.File]::WriteAllText($sentinelPath, ("Blink targeted sentinel {0}" -f $sentinelGuid), [Text.UTF8Encoding]::new($false))
            $expectedIdentity = Get-ExpectedSearchIdentity -Path $sentinelPath
            $afterCreate = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
            $createCatchup = Invoke-TargetedCatchUp -Label 'targeted-sentinel-create-catchup' -OldLastUsn ([int64]$beforeCreate.last_usn) -TimeoutSeconds 120
            $createContext = [ordered]@{
                operation_id = [uint64]$createCatchup.operation_id
                old_last_usn = [int64]$createCatchup.old_last_usn
                target_usn = $createCatchup.target_usn
                resulting_last_usn = $createCatchup.resulting_last_usn
            }
            $createSearch = Wait-TargetedSearchHit -Stage 'targeted-sentinel-create-search' -Name $sentinelName -PathScope $sentinelDirectory `
                -ExpectedPath $sentinelPath -ExpectedIdentity $expectedIdentity -PhaseContext $createContext -TimeoutSeconds 120
            $script:targetedAcceptance['create_catchup'] = $createCatchup
            $script:targetedAcceptance['sentinel'] = [ordered]@{
                name = $sentinelName
                path = $sentinelPath
                expected_identity = $expectedIdentity
                create = [ordered]@{
                    before_usn = [int64]$beforeCreate.last_usn
                    after_create_before_catchup_usn = [int64]$afterCreate.last_usn
                    catchup = $createCatchup
                    search = $createSearch
                    matched_identity = if ($null -ne $createSearch.hit) { ConvertTo-TargetedSearchCandidate $createSearch.hit } else { $null }
                }
            }
            Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
            Save-Results

            Remove-Item -LiteralPath $sentinelPath -Force
            $afterDelete = Send-PipeRequest -Type 2 -RequestVolumeRoot $VolumeRoot -TimeoutMs 5000
            $deleteOldLastUsn = if ($null -ne $createCatchup.resulting_last_usn) {
                [int64]$createCatchup.resulting_last_usn
            }
            else {
                [int64]$afterCreate.last_usn
            }
            $deleteCatchup = Invoke-TargetedCatchUp -Label 'targeted-sentinel-delete-catchup' -OldLastUsn $deleteOldLastUsn -TimeoutSeconds 120
            $deleteContext = [ordered]@{
                operation_id = [uint64]$deleteCatchup.operation_id
                old_last_usn = [int64]$deleteCatchup.old_last_usn
                target_usn = $deleteCatchup.target_usn
                resulting_last_usn = $deleteCatchup.resulting_last_usn
            }
            $deleteSearch = Wait-TargetedSearchEmpty -Stage 'targeted-sentinel-delete-search' -Name $sentinelName -PathScope $sentinelDirectory `
                -PhaseContext $deleteContext -TimeoutSeconds 120
            $script:targetedAcceptance['delete_catchup'] = $deleteCatchup
            $script:targetedAcceptance.sentinel.delete = [ordered]@{
                after_delete_before_catchup_usn = [int64]$afterDelete.last_usn
                catchup = $deleteCatchup
                search = $deleteSearch
            }
            Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
            Save-Results
        }
        finally {
            $script:targetedAcceptance['writer'] = Stop-TargetedContinuousWriter
            Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
            Save-Results
        }

        $createHit = $null
        if ($null -ne $script:targetedAcceptance.sentinel -and
            $null -ne $script:targetedAcceptance.sentinel.create.search.hit) {
            $createHit = $script:targetedAcceptance.sentinel.create.search.hit
        }
        $createIdentityPass = $null -ne $createHit -and
            (Test-SearchIdentityMatch -Hit $createHit -Expected $script:targetedAcceptance.sentinel.expected_identity)
        $createPathPass = $null -ne $createHit -and
            (Test-SearchPathExact -Path ([string]$createHit.full_path) -ExpectedPath $script:targetedAcceptance.sentinel.path)
        $createPass = $null -ne $script:targetedAcceptance.sentinel -and
            $script:targetedAcceptance.sentinel.create.catchup.completed -and
            $script:targetedAcceptance.sentinel.create.catchup.operation_observed -and
            $null -ne $script:targetedAcceptance.sentinel.create.catchup.target_usn -and
            $script:targetedAcceptance.sentinel.create.catchup.writer_state_at_start -eq 'Running' -and
            $script:targetedAcceptance.sentinel.create.search.success -and
            $createIdentityPass -and
            $createPathPass
        $deletePass = $null -ne $script:targetedAcceptance.sentinel -and
            $script:targetedAcceptance.sentinel.create.search.success -and
            $script:targetedAcceptance.sentinel.delete.catchup.completed -and
            $script:targetedAcceptance.sentinel.delete.catchup.operation_observed -and
            $null -ne $script:targetedAcceptance.sentinel.delete.catchup.target_usn -and
            $script:targetedAcceptance.sentinel.delete.catchup.writer_state_at_start -eq 'Running' -and
            $script:targetedAcceptance.sentinel.delete.search.success
        $writerPass = $null -ne $script:targetedAcceptance.writer -and
            [string]::IsNullOrWhiteSpace([string]$script:targetedAcceptance.writer.error)
        Add-Check -Area 'TargetedSentinel' -Name 'Continuous_Writer_Bounded' -Status $(if ($writerPass) { 'PASS' } else { 'FAIL' }) `
            -Details ("state={0}, stopped_by_harness={1}, error={2}" -f $script:targetedAcceptance.writer.state, $script:targetedAcceptance.writer.stopped_by_harness, $script:targetedAcceptance.writer.error)
        Add-Check -Area 'TargetedSentinel' -Name 'Create_CatchUp_Search_Convergence' -Status $(if ($createPass) { 'PASS' } else { 'FAIL' }) `
            -Details ("name={0}, path={1}, path_match={2}, identity_match={3}, expected_identity={4}, matched_wire_identity={5}, catchup_completed={6}, search_hit={7}" -f
                $script:targetedAcceptance.sentinel.name, $script:targetedAcceptance.sentinel.path,
                $createPathPass, $createIdentityPass, $script:targetedAcceptance.sentinel.expected_identity.key,
                $(if ($null -ne $createHit) { $createHit.identity.key } else { $null }),
                $script:targetedAcceptance.sentinel.create.catchup.completed, $script:targetedAcceptance.sentinel.create.search.success)
        Add-Check -Area 'TargetedSentinel' -Name 'Delete_CatchUp_Search_Convergence' -Status $(if ($deletePass) { 'PASS' } else { 'FAIL' }) `
            -Details ("delete_catchup_completed={0}, search_empty={1}, create_was_observed={2}" -f
                $script:targetedAcceptance.sentinel.delete.catchup.completed, $script:targetedAcceptance.sentinel.delete.search.success,
                $script:targetedAcceptance.sentinel.create.search.success)

        $targetedPass = $createPass -and $deletePass -and $writerPass
        $script:targetedAcceptance['result'] = if ($targetedPass) { 'PASS' } else { 'FAIL' }
        Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
        Save-Results
        Write-Log ("Targeted sentinel acceptance result: {0}; full Phase 0.5 result remains NOT READY" -f $script:targetedAcceptance.result)
        if (-not $targetedPass) {
            $script:exitCode = 2
        }
        return $script:targetedAcceptance
    }
}

function Invoke-PrepareWindowsRestartAcceptance {
    return Trace-Stage 'windows-restart-prepare' {
        if ([string]::IsNullOrWhiteSpace($ExistingStorePath)) {
            throw 'Prepare mode requires -ExistingStorePath pointing to a valid BLK5 file.'
        }
        if (Test-Path -LiteralPath $rebootStatePath -PathType Leaf) {
            throw "Refusing to overwrite an existing Windows Restart state: $rebootStatePath"
        }

        $script:restartAcceptance = [ordered]@{
            mode = 'windows_restart_acceptance'
            phase = 'prepare'
            run_id = $runId
            status = 'PREPARING'
            service_account = 'LocalSystem'
            volume_root = $VolumeRoot
            service_data_root = $serviceDataRoot
            source_store_path = $ExistingStorePath
            state_path = $rebootStatePath
            prepare_boot = $null
            checkpoint = $null
            service_registration_before_stop = $null
            service_registration_after_stop = $null
            sentinel = $null
            manual_action = 'Restart Windows manually, then run -CompleteWindowsRestartAcceptance -RunId <RunId>.'
        }
        Add-ResultProperty -Name 'windows_restart_acceptance' -Value $script:restartAcceptance
        Set-Content -LiteralPath $targetedSearchAttemptsPath -Value '' -Encoding utf8

        $prepareBoot = Get-BootEvidence
        $script:restartAcceptance.prepare_boot = $prepareBoot
        $seed = Copy-ExistingStoreSeed -SourcePath $ExistingStorePath
        $script:restartAcceptance.seed_copy = $seed
        Write-Log ("Windows Restart Prepare: copied read-only BLK5 seed from {0} to {1}; source remains untouched" -f $seed.source_path, $seed.copied_path)

        Install-TestService -Account 'LocalSystem' -DataRoot $serviceDataRoot
        $healthy = Wait-Healthy -Label 'windows-restart-prepare-load' -TimeoutSeconds 180 -RequestTimeoutMs 5000
        if ($healthy.response.index_state -ne 'Healthy') {
            throw "Prepare BLK5 load did not reach Healthy: $($healthy.response.index_state)"
        }
        $checkpointIdentity = Get-ExpectedSearchIdentity -Path $VolumeRoot
        $registrationBeforeStop = Get-ServiceRegistrationEvidence
        $operationId = [uint64]$healthy.response.operation_id
        $telemetryRecords = @(Get-ServiceTelemetryRecords)
        if ($telemetryRecords.Count -gt 0) {
            $operationId = [math]::Max($operationId, [uint64]$telemetryRecords[-1].operation.operation_id)
        }
        $checkpoint = [ordered]@{
            journal_id = [uint64]$healthy.response.journal_id
            last_applied_usn = [int64]$healthy.response.last_usn
            operation_id = [uint64]$operationId
            identity = $checkpointIdentity
            index_state = [string]$healthy.response.index_state
            response = $healthy.response
        }
        $script:restartAcceptance.checkpoint = $checkpoint
        $script:restartAcceptance.service_registration_before_stop = $registrationBeforeStop
        Add-Check -Area 'WindowsRestart' -Name 'Prepare_BLK5_Load_Healthy' -Status 'PASS' `
            -Details ("state={0}, journal={1}, last_usn={2}, operation_id={3}" -f $healthy.response.index_state, $healthy.response.journal_id, $healthy.response.last_usn, $operationId)

        Stop-InstalledService
        $registrationAfterStop = Get-ServiceRegistrationEvidence
        $script:restartAcceptance.service_registration_after_stop = $registrationAfterStop
        if (-not $registrationAfterStop.exists -or $registrationAfterStop.state -ne 'Stopped') {
            throw "Prepare could not leave Blink.IndexService registered and stopped: state=$($registrationAfterStop.state)"
        }
        if ([string]$registrationAfterStop.start_name -notmatch 'LocalSystem') {
            throw "Prepare service account changed unexpectedly: $($registrationAfterStop.start_name)"
        }
        Add-Check -Area 'WindowsRestart' -Name 'Prepare_Service_Stopped_Registration_Retained' -Status 'PASS' `
            -Details ("state={0}, start_mode={1}, start_name={2}" -f $registrationAfterStop.state, $registrationAfterStop.start_mode, $registrationAfterStop.start_name)

        if (Test-Path -LiteralPath $testRoot) {
            throw "Refusing to reuse an existing reboot fixture: $testRoot"
        }
        Set-FixtureOwnershipState -State 'creating'
        New-Item -ItemType Directory -Path $fixtureRootBase -Force | Out-Null
        New-Item -ItemType Directory -Path $testRoot | Out-Null
        $script:fixtureOwnership.created_by_run = $true
        $script:fixtureOwnership.created_utc = (Get-Date).ToUniversalTime().ToString('o')
        Set-FixtureOwnershipState -State 'created'
        $sentinelDirectory = Join-Path $testRoot 'Sentinel'
        New-Item -ItemType Directory -Force -Path $sentinelDirectory | Out-Null
        $sentinelGuid = [Guid]::NewGuid().ToString('N')
        $sentinelName = "Blink_RebootSentinel_$sentinelGuid.txt"
        $sentinelPath = Join-Path $sentinelDirectory $sentinelName
        [IO.File]::WriteAllText($sentinelPath, ("Blink Windows restart sentinel {0}" -f $sentinelGuid), [Text.UTF8Encoding]::new($false))
        $sentinelIdentity = Get-ExpectedSearchIdentity -Path $sentinelPath
        $journalAfterCreate = $null
        $journalQueryStatus = 'NOT_AVAILABLE'
        try {
            $journalAfterCreate = Get-VolumeJournalEvidence
            $journalQueryStatus = 'PASS'
        }
        catch {
            $journalAfterCreate = [ordered]@{
                status = 'NOT_AVAILABLE'
                error = $_.Exception.Message
            }
            Write-Log ("Prepare sentinel creation USN probe unavailable: {0}" -f $_.Exception.Message) -Failure
        }
        $script:restartAcceptance.sentinel = [ordered]@{
            name = $sentinelName
            path = $sentinelPath
            created_utc = (Get-Date).ToUniversalTime().ToString('o')
            expected_identity = $sentinelIdentity
            creation_usn = $null
            creation_usn_status = 'NOT_RESOLVED_EXACTLY'
            journal_after_creation = $journalAfterCreate
        }
        Add-Check -Area 'WindowsRestart' -Name 'Prepare_RebootSentinel_Created' -Status 'PASS' `
            -Details ("path={0}, identity={1}, journal_probe={2}" -f $sentinelPath, $sentinelIdentity.key, $journalQueryStatus)

        $state = [ordered]@{
            schema_version = 1
            mode = 'windows_restart_acceptance'
            run_id = $runId
            status = 'PreparedWaitingForManualRestart'
            created_utc = (Get-Date).ToUniversalTime().ToString('o')
            updated_utc = $null
            computer_name = $env:COMPUTERNAME
            volume_root = $VolumeRoot
            target_volume_root = $script:targetVolumeRoot
            service_name = $serviceName
            service_account = 'LocalSystem'
            service_data_root = $serviceDataRoot
            store_path = Join-Path $serviceDataRoot 'index\d.blink5'
            source_store_path = $ExistingStorePath
            prepare_boot = $prepareBoot
            checkpoint = $checkpoint
            service_registration_before_stop = $registrationBeforeStop
            service_registration_after_stop = $registrationAfterStop
            sentinel = $script:restartAcceptance.sentinel
            complete = $null
            manual_restart_required = $true
            initial_mft_build_executed_in_prepare = $false
        }
        Save-RebootState -State $state
        Add-Check -Area 'WindowsRestart' -Name 'Prepare_RebootState_Written' -Status 'PASS' -Details $rebootStatePath
        $script:restartAcceptance.status = 'PREPARED_WAITING_FOR_MANUAL_RESTART'
        $script:restartAcceptance.state = $state
        $results.windows_restart_acceptance = $script:restartAcceptance
        Save-Results
        Write-Host ''
        Write-Host 'MANUAL ACTION REQUIRED: Windows Restart Acceptance Prepare completed.'
        Write-Host ("RunId: {0}" -f $runId)
        Write-Host ("State: {0}" -f $rebootStatePath)
        Write-Host 'The service is registered and stopped. Reboot Windows manually now; this script will not restart the machine.'
        Write-Host ("After Windows starts, run: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-phase05-elevated-acceptance.ps1 -CompleteWindowsRestartAcceptance -RunId {0} -Configuration {1} -KeepAcceptanceData" -f $runId, $Configuration)
        return $script:restartAcceptance
    }
}

function Invoke-CompleteWindowsRestartAcceptance {
    return Trace-Stage 'windows-restart-complete' {
        $state = $null
        $currentBoot = $null
        try {
            $state = Load-RebootState
            if ([string]$state.status -notin @('PreparedWaitingForManualRestart', 'CompleteFail')) {
                throw "Windows Restart state is not ready for manual completion/retry: $($state.status)"
            }
            if ([string]$state.status -eq 'CompleteFail') {
                Write-Log 'Manual retry of a previous failed Windows Restart completion; prior attempt artifacts are preserved'
            }
            $currentBoot = Get-BootEvidence
            $preparedBoot = [datetime]::Parse([string]$state.prepare_boot.last_boot_time_utc).ToUniversalTime()
            $observedBoot = [datetime]::Parse([string]$currentBoot.last_boot_time_utc).ToUniversalTime()
            $tickCountReset = [uint64]$currentBoot.tick_count_64 -lt [uint64]$state.prepare_boot.tick_count_64
            $rebootProven = $observedBoot -gt $preparedBoot
            Add-Check -Area 'WindowsRestart' -Name 'Complete_Actual_Windows_Reboot_Proven' -Status $(if ($rebootProven) { 'PASS' } else { 'FAIL' }) `
                -Details ("prepared_boot={0}, observed_boot={1}, prepared_tick={2}, observed_tick={3}, tick_reset={4}" -f $preparedBoot.ToString('o'), $observedBoot.ToString('o'), $state.prepare_boot.tick_count_64, $currentBoot.tick_count_64, $tickCountReset)
            if (-not $rebootProven) {
                throw 'Windows Restart Acceptance requires a later OS boot session; a service restart is not sufficient.'
            }

            if ([string]$state.service_data_root -ne [string]$serviceDataRoot) {
                throw "State service DataRoot mismatch: state=$($state.service_data_root), current=$serviceDataRoot"
            }
            if (-not (Test-Path -LiteralPath $state.store_path -PathType Leaf)) {
                throw "Prepared BLK5 is missing: $($state.store_path)"
            }
            if (-not (Test-Path -LiteralPath $state.sentinel.path -PathType Leaf)) {
                throw "Prepared RebootSentinel is missing: $($state.sentinel.path)"
            }
            $registration = Get-ServiceRegistrationEvidence
            if (-not $registration.exists) {
                throw 'Blink.IndexService registration is absent after Windows restart.'
            }
            if ([string]$registration.start_name -notmatch 'LocalSystem') {
                throw "Blink.IndexService is not registered as LocalSystem: $($registration.start_name)"
            }
            if ([string]$registration.path_name -notlike "*$($state.service_data_root)*") {
                throw 'Blink.IndexService registration does not point to the prepared Service DataRoot.'
            }
            Start-Service -Name $serviceName
            $script:restartServiceStartedByComplete = $true
            Wait-ServiceStatus -Expected 'Running' | Out-Null
            Add-Check -Area 'WindowsRestart' -Name 'Complete_Service_Started_In_New_Boot' -Status 'PASS' `
                -Details ("state={0}, pid={1}, boot={2}" -f (Get-ServiceRegistrationEvidence).state, (Get-ServiceRegistrationEvidence).process_id, $currentBoot.last_boot_time_utc)

            $healthy = Wait-Healthy -Label 'windows-restart-load' -TimeoutSeconds 180 -RequestTimeoutMs 5000
            if ($healthy.response.index_state -ne 'Healthy') {
                throw "Post-reboot persisted load did not reach Healthy: $($healthy.response.index_state)"
            }
            $journalMatches = [uint64]$healthy.response.journal_id -eq [uint64]$state.checkpoint.journal_id
            $usnContinuity = [int64]$healthy.response.last_usn -ge [int64]$state.checkpoint.last_applied_usn
            Add-Check -Area 'WindowsRestart' -Name 'Complete_Journal_Continuity' -Status $(if ($journalMatches -and $usnContinuity) { 'PASS' } else { 'FAIL' }) `
                -Details ("journal_expected={0}, journal_observed={1}, last_usn_before={2}, last_usn_observed={3}" -f $state.checkpoint.journal_id, $healthy.response.journal_id, $state.checkpoint.last_applied_usn, $healthy.response.last_usn)
            if (-not $journalMatches) {
                throw "Post-reboot Journal ID mismatch: expected=$($state.checkpoint.journal_id), actual=$($healthy.response.journal_id)"
            }
            if (-not $usnContinuity) {
                throw 'Post-reboot LastAppliedUSN moved backwards.'
            }

            $minimumRecoveryUsn = [int64]$state.checkpoint.last_applied_usn
            $journalAfterNextUsnProperty = if ($null -ne $state.sentinel.journal_after_creation) {
                $state.sentinel.journal_after_creation.PSObject.Properties['next_usn']
            }
            else {
                $null
            }
            if ($null -ne $journalAfterNextUsnProperty -and $null -ne $journalAfterNextUsnProperty.Value) {
                $minimumRecoveryUsn = [math]::Max($minimumRecoveryUsn, [int64]$journalAfterNextUsnProperty.Value)
            }
            $recoveryTelemetry = Wait-RestartRecoveryTelemetry -MinimumOperationId ([uint64]$state.checkpoint.operation_id) `
                -MinimumCompletedUsn $minimumRecoveryUsn -TimeoutSeconds 180
            $noFullMftRebuild = $recoveryTelemetry.operation.initial -eq $false -and
                [int]$recoveryTelemetry.operation.error -eq 0 -and
                [int64]$recoveryTelemetry.operation.mft_records -eq 0
            Add-Check -Area 'WindowsRestart' -Name 'Complete_USN_CatchUp_No_Full_MFT_Rebuild' -Status $(if ($noFullMftRebuild) { 'PASS' } else { 'FAIL' }) `
                -Details ("operation_id={0}, initial={1}, mft_records={2}, catchup_completed_usn={3}" -f $recoveryTelemetry.operation.operation_id, $recoveryTelemetry.operation.initial, $recoveryTelemetry.operation.mft_records, $recoveryTelemetry.operation.catchup_completed_usn)
            if (-not $noFullMftRebuild) {
                throw 'Post-reboot recovery was not proven to be a non-initial USN CatchUp.'
            }

            $phaseContext = [ordered]@{
                operation_id = [uint64]$recoveryTelemetry.operation.operation_id
                old_last_usn = [int64]$recoveryTelemetry.operation.catchup_start_usn
                target_usn = [int64]$recoveryTelemetry.operation.catchup_target_usn
                resulting_last_usn = [int64]$recoveryTelemetry.operation.catchup_completed_usn
            }
            $expectedIdentity = [pscustomobject][ordered]@{
                path = [string]$state.sentinel.expected_identity.path
                volume_serial = [uint64]$state.sentinel.expected_identity.volume_serial
                volume_serial_low32 = [uint32]$state.sentinel.expected_identity.volume_serial_low32
                file_id = [string]$state.sentinel.expected_identity.file_id
                key = [string]$state.sentinel.expected_identity.key
            }
            $search = Wait-TargetedSearchHit -Stage 'windows-restart-sentinel-search' -Name ([string]$state.sentinel.name) `
                -PathScope (Split-Path -Parent ([string]$state.sentinel.path)) -ExpectedPath ([string]$state.sentinel.path) `
                -ExpectedIdentity $expectedIdentity -PhaseContext $phaseContext -TimeoutSeconds 120
            $hit = $search.hit
            $identityPass = $null -ne $hit -and (Test-SearchIdentityMatch -Hit $hit -Expected $expectedIdentity)
            $pathPass = $null -ne $hit -and (Test-SearchPathExact -Path ([string]$hit.full_path) -ExpectedPath ([string]$state.sentinel.path))
            $sentinelPass = $search.success -and $identityPass -and $pathPass
            $searchResultCount = if ($null -ne $search.last_attempt) { [int]$search.last_attempt.result_count } else { 0 }
            Add-Check -Area 'WindowsRestart' -Name 'Complete_RebootSentinel_Search_Identity_Path' -Status $(if ($sentinelPass) { 'PASS' } else { 'FAIL' }) `
                -Details ("search_success={0}, path_match={1}, identity_match={2}, result_count={3}" -f $search.success, $pathPass, $identityPass, $searchResultCount)
            if (-not $sentinelPass) {
                throw 'Post-reboot RebootSentinel Search did not converge to the expected path and identity.'
            }

            $script:restartAcceptance = [ordered]@{
                mode = 'windows_restart_acceptance'
                phase = 'complete'
                run_id = $runId
                status = 'PASS'
                prepare_boot = $state.prepare_boot
                complete_boot = $currentBoot
                service_registration = $registration
                service_data_root = $state.service_data_root
                checkpoint = $state.checkpoint
                load = $healthy
                recovery_telemetry = $recoveryTelemetry
                no_full_mft_rebuild = $noFullMftRebuild
                reboot_sentinel = [ordered]@{
                    name = $state.sentinel.name
                    path = $state.sentinel.path
                    expected_identity = $expectedIdentity
                    search = $search
                    path_match = $pathPass
                    identity_match = $identityPass
                }
                search_attempts = @($script:targetedSearchAttempts)
                state_path = $rebootStatePath
            }
            $state.status = 'CompletePass'
            $state.manual_restart_required = $false
            $state.complete = $script:restartAcceptance
            Save-RebootState -State $state
            $script:restartCompletePass = $true
            $results.windows_restart_acceptance = $script:restartAcceptance
            Add-ResultProperty -Name 'windows_restart_acceptance' -Value $script:restartAcceptance
            Save-Results
            Write-Log 'Windows Restart Acceptance complete: PASS; persisted BLK5 + non-initial USN CatchUp + sentinel identity/path verified'
            return $script:restartAcceptance
        }
        catch {
            $failureMessage = $_.Exception.Message
            if ($null -ne $state) {
                try {
                    $state.status = 'CompleteFail'
                    $state.complete = [ordered]@{
                        status = 'FAIL'
                        error = $failureMessage
                        observed_boot = if ($null -ne $currentBoot) { $currentBoot } else { $null }
                        search_attempts = @($script:targetedSearchAttempts)
                        state_path = $rebootStatePath
                    }
                    Save-RebootState -State $state
                }
                catch {
                    Write-Log ("Unable to persist failed Windows Restart state: {0}" -f $_.Exception.Message) -Failure
                }
            }
            $script:restartAcceptance = [ordered]@{
                mode = 'windows_restart_acceptance'
                phase = 'complete'
                run_id = $runId
                status = 'FAIL'
                error = $failureMessage
                state_path = $rebootStatePath
                search_attempts = @($script:targetedSearchAttempts)
            }
            $results.windows_restart_acceptance = $script:restartAcceptance
            Add-ResultProperty -Name 'windows_restart_acceptance' -Value $script:restartAcceptance
            Save-Results
            throw
        }
    }
}

function Invoke-AccountInitialProbe {
    param([ValidateSet('LocalService', 'LocalSystem')][string]$Account, [string]$DataRoot, [string]$Label)
    return Trace-Stage ("account-probe-" + $Account.ToLowerInvariant()) {
        $record = [ordered]@{
            account = $Account
            label = $Label
            install = 'NOT_EXECUTED'
            volume_handle = 'NOT_EXECUTED'
            query_usn_journal = 'NOT_EXECUTED'
            enum_usn_data = 'NOT_EXECUTED'
            read_usn_journal = 'NOT_EXECUTED'
            failure_api = $null
            win32_error = $null
            initial = $null
            telemetry_observation = $null
            read_evidence = $null
        }
        try {
            Install-TestService -Account $Account -DataRoot $DataRoot
            $record.install = 'PASS'
            $initial = Start-InitialIndex
            $record.initial = $initial
            $telemetry = $initial.operation.telemetry
            if ($initial.operation.response.error -eq 0) {
                $record.volume_handle = 'PASS'
                $record.query_usn_journal = 'PASS'
                $record.enum_usn_data = 'PASS'
                $record.read_usn_journal = if ($null -ne $telemetry -and [uint64]$telemetry.usn_records -gt 0) { 'PASS' } else { 'NOT_EXECUTED' }
                if ($record.read_usn_journal -eq 'PASS') {
                    $record.read_evidence = "Initial BuildInitial consumed $($telemetry.usn_records) USN records with error=0; post-mutation incremental catch-up remains pending."
                }
            }
            else {
                $stage = if ($null -ne $telemetry -and -not [string]::IsNullOrWhiteSpace($telemetry.failure_stage)) { [string]$telemetry.failure_stage } else { 'FSCTL_QUERY_USN_JOURNAL' }
                switch -Wildcard ($stage) {
                    'Volume Handle*' {
                        $record.volume_handle = 'FAIL'; $record.query_usn_journal = 'PASS'; $record.enum_usn_data = 'NOT_EXECUTED'; $record.read_usn_journal = 'NOT_EXECUTED'
                    }
                    'FSCTL_QUERY_USN_JOURNAL' {
                        $record.volume_handle = 'PASS'; $record.query_usn_journal = 'FAIL'; $record.enum_usn_data = 'NOT_EXECUTED'; $record.read_usn_journal = 'NOT_EXECUTED'
                    }
                    'FSCTL_ENUM_USN_DATA' {
                        $record.volume_handle = 'PASS'; $record.query_usn_journal = 'PASS'; $record.enum_usn_data = 'FAIL'; $record.read_usn_journal = 'NOT_EXECUTED'
                    }
                    'FSCTL_READ_USN_JOURNAL' {
                        $record.volume_handle = 'PASS'; $record.query_usn_journal = 'PASS'; $record.enum_usn_data = 'PASS'; $record.read_usn_journal = 'FAIL'
                    }
                    default {
                        $record.volume_handle = 'FAIL'; $record.query_usn_journal = 'NOT_EXECUTED'; $record.enum_usn_data = 'NOT_EXECUTED'; $record.read_usn_journal = 'NOT_EXECUTED'
                    }
                }
                $record.failure_api = $stage
                $record.win32_error = [int]$initial.operation.response.error
                Add-Check -Area 'ServiceAccount' -Name ("$Account/$($record.failure_api)") -Status 'FAIL' -Details 'Initial operation failed; API stage inferred from service telemetry' -Win32Error $record.win32_error
            }
        }
        catch {
            $record.failure_api = 'SCM/Service/IPC'
            $native_error = Get-NativeErrorFromException -Exception $_.Exception
            $diagnostic = Get-IpcRejectionDiagnostic
            if (-not [string]::IsNullOrWhiteSpace($diagnostic)) {
                $record.failure_api = 'IPC/GetClientIdentity'
            }
            $record.win32_error = $native_error
            $details = $_.Exception.Message
            if (-not [string]::IsNullOrWhiteSpace($diagnostic)) {
                $details = "$details; service diagnostic: $diagnostic"
                $match = [regex]::Match($diagnostic, 'win32_error=(\d+)')
                if ($match.Success) {
                    $record.win32_error = [int]$match.Groups[1].Value
                }
            }
            $telemetryObservation = Get-LatestServiceTelemetry
            if ($null -ne $telemetryObservation) {
                $record.telemetry_observation = [ordered]@{
                    status = 'TELEMETRY_ONLY'
                    reason = 'Acceptance client did not capture a complete IPC response; service-side operation telemetry is retained for diagnosis and is not accepted as the client result.'
                    source = $telemetryObservation.path
                    operation = $telemetryObservation.operation
                }
                Write-Log ("{0}: retained telemetry-only observation from {1}; it does not replace the failed client acceptance response" -f $Account, $telemetryObservation.path) -Failure
            }
            Add-Check -Area 'ServiceAccount' -Name "$Account/Initial" -Status 'FAIL' -Details $details -Win32Error $record.win32_error
        }
        finally {
            if ($script:serviceInstalledByRun) {
                try { Remove-InstalledService } catch { Write-Log ("Account probe cleanup failed: {0}" -f $_.Exception.Message) -Failure }
            }
        }
        $results.service_accounts = @($results.service_accounts) + [pscustomobject]$record
        return [pscustomobject]$record
    }
}

function Ask-LocalSystem {
    param([bool]$Required)
    $prompt = if ($Required) {
        'LocalService did not complete volume operations. Type LOCAL_SYSTEM to explicitly test LocalSystem, or anything else to stop'
    }
    else {
        'LocalService completed. Type LOCAL_SYSTEM to explicitly run the optional LocalSystem probe, or anything else to keep LocalService'
    }
    $answer = Read-Host $prompt
    return $answer -ceq 'LOCAL_SYSTEM'
}

function Write-ManualRestartInstructions {
    $text = @"
# Blink Phase 0.5 Windows Restart Acceptance

This file is generated by the elevated acceptance script. The script deliberately does not reboot Windows.

1. Run the acceptance script in an Administrator High Integrity PowerShell with -KeepAcceptanceData -KeepService and complete the service/BLK5 run.
2. Before reboot, confirm a valid BLK5 exists under the retained run's service data root and record Journal ID / Last Applied USN from phase05-results.json.
3. Stop only the Blink client if it is running; do not delete the BLK5.
4. Reboot Windows manually when safe.
5. After Windows starts, run Get-Service Blink.IndexService; if necessary run Start-Service Blink.IndexService. Do not run StartInitialIndex.
6. Launch the normal-user Blink.exe, use the IndexService status/search action for D:, and confirm the existing BLK5 loads, Journal ID validates, USN Catch-up completes, Search becomes Healthy, and no initial MFT operation is reported.
7. Set Windows Restart Acceptance to PASS only after recording those post-reboot observations. Otherwise leave it NOT EXECUTED.

No automatic shutdown or restart is performed by this tool.
"@
    Set-Content -LiteralPath $manualRestartPath -Value $text -Encoding utf8
}

function Test-FinalResult {
    $failed = @($results.checks | Where-Object { $_.status -eq 'FAIL' }).Count -gt 0
    $successfulAccount = @($results.service_accounts | Where-Object {
        $_.install -eq 'PASS' -and $_.initial -ne $null -and $_.initial.operation.response.error -eq 0
    }).Count -gt 0
    $recoveryPass = $null -ne $results.restart_recovery -and $results.restart_recovery.no_full_mft_rebuild
    $mutationPass = $null -ne $results.mutations -and @($results.mutations.checks.Values | Where-Object { -not $_ }).Count -eq 0
    $journalPass = $null -ne $results.journal_invalid -and $results.journal_invalid.rebuild_required
    $persistencePass = $null -ne $results.persistence -and $results.persistence.corrupted
    $failurePass = $null -ne $results.service_failure -and $results.service_failure.stop_disconnect_bounded -and $results.service_failure.kill_disconnect_bounded
    if ($successfulAccount -and -not $failed -and $recoveryPass -and $mutationPass -and $journalPass -and $persistencePass -and $failurePass) {
        $results.final_result = 'READY WITH CONDITIONS'
        $results.phase1_recommendation = 'DO NOT PROCEED'
    }
    else {
        $results.final_result = 'NOT READY'
        $results.phase1_recommendation = 'DO NOT PROCEED'
        $script:exitCode = 2
    }
}

$fixture = $null
try {
    Write-Log ("Starting elevated Blink Phase 0.5 acceptance run: {0}" -f $runId)
    Write-Log ("Target volume: {0} | Live harness root: {1} | Service DataRoot: {2}" -f $VolumeRoot, $harnessLiveRoot, $serviceDataRoot)

    if ($TargetedSentinelAcceptance) {
        if ($TargetedServiceAccount -ne 'LocalSystem') {
            throw 'Targeted mode requires explicit -TargetedServiceAccount LocalSystem.'
        }
        $null = Invoke-TargetedSentinelAcceptance
    }
    elseif ($PrepareWindowsRestartAcceptance -or $CompleteWindowsRestartAcceptance) {
        $volume = Get-Volume -DriveLetter $VolumeRoot.TrimEnd('\').Substring(0, 1)
        if ($volume.FileSystem -ne 'NTFS') {
            throw "Target volume is not NTFS: $($volume.FileSystem)"
        }
        Write-Log ("Windows Restart Acceptance target volume {0}: NTFS, label={1}, free={2}" -f $VolumeRoot, $volume.FileSystemLabel, $volume.SizeRemaining)
        if ($PrepareWindowsRestartAcceptance) {
            $null = Invoke-PrepareWindowsRestartAcceptance
        }
        else {
            $null = Invoke-CompleteWindowsRestartAcceptance
        }
    }
    else {
        $volume = Get-Volume -DriveLetter $VolumeRoot.TrimEnd('\').Substring(0, 1)
        if ($volume.FileSystem -ne 'NTFS') {
            throw "Target volume is not NTFS: $($volume.FileSystem)"
        }
        Write-Log ("Target volume {0}: NTFS, label={1}, free={2}" -f $VolumeRoot, $volume.FileSystemLabel, $volume.SizeRemaining)

    $directProbe = Invoke-DirectVolumeProbe
    Add-ResultProperty -Name 'volume_probe' -Value $directProbe
    foreach ($field in @('volume_handle', 'query_usn_journal', 'enum_usn_data', 'read_usn_journal')) {
        $status = if ($directProbe.$field) { 'PASS' } else { 'FAIL' }
        $errorField = $field + '_error'
        Add-Check -Area 'DirectVolume' -Name $field -Status $status -Details ("mft_records={0}, read_records={1}" -f $directProbe.mft_record_count, $directProbe.read_record_count) -Win32Error $(if ($status -eq 'FAIL') { [int]$directProbe.$errorField } else { $null })
    }

    try { Stop-InstalledService } catch {}
    try { Remove-InstalledService } catch {}
    try { Start-Process -FilePath "taskkill.exe" -ArgumentList "/F /IM Blink.IndexService.exe /T" -NoNewWindow -Wait -ErrorAction SilentlyContinue } catch {}

    Trace-Stage 'build-debug' {
        Invoke-Phase05LoggedCommand -FilePath 'powershell.exe' -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repositoryRoot 'build.ps1'), '-Configuration', 'Debug', '-Action', 'Build') -LogAction $script:phase05LogAction
    }
    Trace-Stage 'build-release' {
        Invoke-Phase05LoggedCommand -FilePath 'powershell.exe' -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repositoryRoot 'build.ps1'), '-Configuration', 'Release', '-Action', 'Build') -LogAction $script:phase05LogAction
    }

    $fixture = New-Fixture

    $localData = Join-Path $runRoot 'service-data-localservice'
    $localTelemetry = Join-Path $localData 'telemetry'
    $telemetryRoot = $localTelemetry

    $localRecord = Invoke-AccountInitialProbe -Account 'LocalService' -DataRoot $localData -Label 'localservice-initial'
    $initialRecord = $localRecord.initial

    if ($null -eq $initialRecord -or $initialRecord.operation.response.error -ne 0) {
        if ($localRecord.failure_api -eq 'SCM/Service/IPC' -or $localRecord.failure_api -like 'IPC/*') {
            throw 'LocalService IPC transport failed before a volume operation. LocalSystem fallback is not applicable; fix the IPC/service boundary and rerun.'
        }
        $useLocalSystem = $TestLocalSystem -or (Ask-LocalSystem -Required $true)
        if (-not $useLocalSystem) {
            Add-Check -Area 'ServiceAccount' -Name 'LocalSystem' -Status 'NOT_EXECUTED' -Details 'LocalService failed and explicit LocalSystem consent was not supplied'
            throw 'LocalService failed and LocalSystem was not explicitly selected'
        }
        $systemData = Join-Path $runRoot 'service-data-localsystem'
        $telemetryRoot = Join-Path $systemData 'telemetry'
        $systemRecord = Invoke-AccountInitialProbe -Account 'LocalSystem' -DataRoot $systemData -Label 'localsystem-initial'
        if ($null -eq $systemRecord.initial -or $systemRecord.initial.operation.response.error -ne 0) {
            throw 'Both LocalService and explicitly selected LocalSystem failed initial indexing'
        }
        $script:activeAccountRecord = $systemRecord
        $serviceDataRoot = $systemData
    }
    else {
        $script:activeAccountRecord = $localRecord
        $serviceDataRoot = $localData
        $optionalSystem = $TestLocalSystem -or (Ask-LocalSystem -Required $false)
        if ($optionalSystem) {
            $systemData = Join-Path $runRoot 'service-data-localsystem'
            $telemetryRoot = Join-Path $systemData 'telemetry'
            $systemRecord = Invoke-AccountInitialProbe -Account 'LocalSystem' -DataRoot $systemData -Label 'localsystem-probe'
            if ($null -eq $systemRecord.initial -or $systemRecord.initial.operation.response.error -ne 0) {
                Write-Log 'Explicit LocalSystem probe failed; LocalService remains the active test account' -Failure
            }
            $telemetryRoot = Join-Path $serviceDataRoot 'telemetry'
        }
    }

    if ($null -eq $script:activeAccountRecord) {
        throw 'No service account completed the initial index'
    }

    $results.service_data_root = $serviceDataRoot
    Install-TestService -Account $script:activeAccountRecord.account -DataRoot $serviceDataRoot

    $results.service_account_decision = [ordered]@{
        recommended_prototype_account = $script:activeAccountRecord.account
        production_account_status = 'UNRESOLVED'
    }

    $initialRecord = $script:activeAccountRecord.initial
    $initialTelemetry = $initialRecord.operation.telemetry
    $initialSummary = [ordered]@{
        service_account = $script:activeAccountRecord.account
        file_count = $initialRecord.operation.response.files
        directory_count = $initialRecord.operation.response.directories
        mft_record_count = if ($null -ne $initialTelemetry) { $initialTelemetry.mft_records } else { $null }
        mft_enumeration_seconds = if ($null -ne $initialTelemetry) { $initialTelemetry.mft_enumeration_seconds } else { $null }
        parent_tree_build_seconds = if ($null -ne $initialTelemetry) { $initialTelemetry.parent_tree_build_seconds } else { $null }
        usn_record_count = if ($null -ne $initialTelemetry) { $initialTelemetry.usn_records } else { $null }
        usn_read_seconds = if ($null -ne $initialTelemetry) { $initialTelemetry.usn_read_seconds } else { $null }
        total_initial_index_seconds = if ($null -ne $initialTelemetry -and $initialTelemetry.operation_seconds -gt 0) { $initialTelemetry.operation_seconds } else { $initialRecord.operation.wall_seconds }
        client_observation_seconds = $initialRecord.operation.wall_seconds
        build_seconds = if ($null -ne $initialTelemetry) { $initialTelemetry.build_seconds } else { $null }
        cpu_seconds = $initialRecord.operation.cpu_seconds
        average_cpu_percent = $initialRecord.operation.average_cpu_percent
        peak_working_set_bytes = $initialRecord.operation.peak_working_set_bytes
        blk5_save_seconds = if ($null -ne $initialTelemetry) { $initialTelemetry.save_seconds } else { $null }
        blk5_size_bytes = if ($null -ne $initialTelemetry) { $initialTelemetry.index_bytes } else { $initialRecord.operation.response.index_bytes }
        store_path = Join-Path $serviceDataRoot 'index\d.blink5'
        raw_operation = $initialRecord
    }
    Add-ResultProperty -Name 'initial_index' -Value $initialSummary
    Add-Check -Area 'InitialIndex' -Name 'MFT_Initial_Index' -Status $(if ($initialRecord.operation.response.error -eq 0) { 'PASS' } else { 'FAIL' }) -Details ("files={0}, dirs={1}, records={2}" -f $initialRecord.operation.response.files, $initialRecord.operation.response.directories, $initialRecord.operation.response.records_seen) -Win32Error $initialRecord.operation.response.error

    # Initial Store Load Stage Timeout: 180 seconds (3 minutes)
    $searchReady = Wait-Healthy -Label 'initial-store-load' -TimeoutSeconds 180 -RequestTimeoutMs 5000
    Add-Check -Area 'Persistence' -Name 'BLK5_Load_Before_Search' -Status $(if ($searchReady.response.index_state -eq 'Healthy') { 'PASS' } else { 'FAIL' }) -Details ("state={0}, journal={1}" -f $searchReady.response.index_state, $searchReady.response.journal_id)

    Invoke-SearchBenchmark -PathPrefix $fixture.root
    $null = Invoke-SearchCorrectness -Fixture $fixture
    $null = Invoke-Mutations -Fixture $fixture
    $null = Invoke-ContinuousWriteStressTest -Fixture $fixture
    $null = Invoke-RestartRecovery -Fixture $fixture
    $null = Invoke-ServiceFailureTests -Fixture $fixture

    $null = Wait-Healthy -Label 'before-persistence-tests' -TimeoutSeconds 120 -RequestTimeoutMs 5000
    $null = Invoke-CheckpointInvalidTest -Fixture $fixture
    $null = Invoke-CorruptionTest

        Add-Check -Area 'WindowsRestart' -Name 'Manual_Only' -Status 'NOT_EXECUTED' -Details $manualRestartPath
    }
}
catch {
    $script:exitCode = 2
    $results.failures = @($results.failures) + [pscustomobject][ordered]@{
        area = 'Execution'
        name = 'AcceptanceRun'
        details = $_.Exception.Message
        win32_error = [int]$_.Exception.HResult
    }
    if ($TargetedSentinelAcceptance -and $null -ne $script:targetedAcceptance) {
        $script:targetedAcceptance['result'] = 'FAIL'
        Add-ResultProperty -Name 'targeted_sentinel_acceptance' -Value $script:targetedAcceptance
    }
    Write-Log ("Acceptance run stopped: {0}" -f $_.Exception.Message) -Failure
}
finally {
    if ($PrepareWindowsRestartAcceptance) {
        if ($script:serviceInstalledByRun) {
            try { Stop-InstalledService } catch { Write-Log ("Prepare cleanup service stop failed: {0}" -f $_.Exception.Message) -Failure }
            Write-Log 'Windows Restart Prepare retains the stopped Blink.IndexService registration created by this run for the manual reboot step'
        }
        elseif ($script:serviceCreationAttemptedByRun) {
            try { Remove-InstalledService } catch { Write-Log ("Windows Restart Prepare failed-install service cleanup failed: {0}" -f $_.Exception.Message) -Failure }
            Write-Log 'Windows Restart Prepare removed the service registration created by this failed run'
        }
        else {
            Write-Log 'Windows Restart Prepare did not install Blink.IndexService; any pre-existing service registration was left untouched'
        }
    }
    elseif ($CompleteWindowsRestartAcceptance) {
        if ($script:restartCompletePass) {
            try { Remove-InstalledService } catch { Write-Log ("Successful Windows Restart cleanup service removal failed: {0}" -f $_.Exception.Message) -Failure }
        }
        else {
            if ($script:restartServiceStartedByComplete -or $null -ne (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) {
                try { Stop-InstalledService } catch { Write-Log ("Failed Windows Restart service stop after failure: {0}" -f $_.Exception.Message) -Failure }
            }
            Write-Log 'Failed Windows Restart completion retains the service registration and fixture for diagnosis; no automatic rerun was attempted'
        }
    }
    elseif ($KeepService) {
        if ($script:serviceInstalledByRun) {
            Write-Log 'Keeping the final acceptance service registration as requested'
        }
    }
    else {
        if ($script:serviceInstalledByRun) {
            try {
                Stop-InstalledService
            }
            catch {
                Write-Log ("Cleanup service stop failed: {0}" -f $_.Exception.Message) -Failure
            }
            try { Remove-InstalledService } catch { Write-Log ("Cleanup service removal failed: {0}" -f $_.Exception.Message) -Failure }
        }
        elseif ($script:serviceCreationAttemptedByRun) {
            try { Remove-InstalledService } catch { Write-Log ("Failed-install service cleanup failed: {0}" -f $_.Exception.Message) -Failure }
            Write-Log 'Acceptance removed the service registration created by this failed run'
        }
        elseif ($TargetedSentinelAcceptance) {
            Write-Log 'Targeted run did not create Blink.IndexService; leaving the pre-existing service registration untouched'
        }
    }

    if (Test-Path -LiteralPath $testRoot) {
        if (Test-CurrentRunOwnedFixture) {
            $targetedFailure = $TargetedSentinelAcceptance -and (
                $script:exitCode -ne 0 -or
                @($results.failures).Count -gt 0 -or
                ($null -ne $script:targetedAcceptance -and [string]$script:targetedAcceptance.result -eq 'FAIL'))
            $restartFailure = ($PrepareWindowsRestartAcceptance -or $CompleteWindowsRestartAcceptance) -and
                ($PrepareWindowsRestartAcceptance -or -not $script:restartCompletePass)
            if ($KeepAcceptanceData -or $targetedFailure -or $restartFailure) {
                $reason = if ($PrepareWindowsRestartAcceptance) { 'manual Windows restart acceptance is pending' }
                    elseif ($CompleteWindowsRestartAcceptance) { 'failed Windows restart completion retained for diagnosis' }
                    elseif ($targetedFailure) { 'failed targeted acceptance retained for diagnosis' }
                    else { 'retained by -KeepAcceptanceData' }
                Set-FixtureOwnershipState -State 'retained' -RetainedPath $testRoot -Reason $reason
                Write-Log ("Keeping acceptance fixture: {0}; ownership manifest: {1}" -f $testRoot, $fixtureOwnershipPath)
            }
            else {
                Remove-Item -LiteralPath $testRoot -Recurse -Force
                Set-FixtureOwnershipState -State 'cleaned' -RetainedPath $null -Reason 'completed historical fixture'
                Write-Log ("Removed acceptance fixture: {0}" -f $testRoot)
            }
        }
        else {
            Set-FixtureOwnershipState -State 'unresolved' -RetainedPath $testRoot -Reason 'existing path was not created and registered by this run; refused cleanup'
            Write-Log ("Refusing fixture cleanup because current run does not own the path: {0}" -f $testRoot) -Failure
        }
    }

    # Keep the per-run service-data directory with the exported artifacts.
    # It contains the BLK5/telemetry needed to diagnose a failed privileged
    # run and to perform the separate manual Windows-restart acceptance.  The
    # acceptance root is project-local on D:, and is not user data; only the
    # owned test fixture is removed above when -KeepAcceptanceData is absent.

    if ($TargetedSentinelAcceptance) {
        # A targeted run reports its own PASS/FAIL, but it must never be
        # mistaken for a complete Phase 0.5 result or a Phase 1 approval.
        $results.final_result = 'NOT READY'
        $results.phase1_recommendation = 'DO NOT PROCEED'
    }
    elseif ($PrepareWindowsRestartAcceptance -or $CompleteWindowsRestartAcceptance) {
        # Prepare/Complete are a single acceptance slice.  They deliberately
        # do not recompute or reclassify the complete Phase 0.5 result.
        $results.final_result = 'NOT READY'
        $results.phase1_recommendation = 'DO NOT PROCEED'
    }
    else {
        Write-ManualRestartInstructions
        try { Test-FinalResult } catch { Write-Log ("Final result calculation failed: {0}" -f $_.Exception.Message) -Failure; $script:exitCode = 2 }
    }
    $results.artifact_export = 'PENDING'
    $results.artifact_export_error = $null
    Save-Results
    Write-Log ("Final result: {0}; live_artifacts: {1}" -f $results.final_result, $harnessLiveRoot)

    # Flush-LogBuffer MUST come AFTER all Write-Log calls and BEFORE artifact copy.
    # This ensures the log files on disk are complete before we copy them to the project directory.
    Flush-LogBuffer

    # COPY FINALIZED ARTIFACTS FROM %TEMP% (harnessLiveRoot) TO PROJECT DIR (.blink-data\acceptance\)
    # No Write-Log calls are permitted after this point — the log files are finalized.
    try {
        Write-Host ('{0} Copying final acceptance artifacts to project directory: {1}' -f (Get-Date).ToString('s'), $artifactRunRoot)
        New-Item -ItemType Directory -Force -Path $artifactRunRoot -ErrorAction SilentlyContinue | Out-Null
        $harnessItems = @(Get-ChildItem -Path $harnessLiveRoot -Force -ErrorAction Stop)
        foreach ($item in $harnessItems) {
            Copy-Item -LiteralPath $item.FullName -Destination $artifactRunRoot -Recurse -Force
        }
        Copy-Item -LiteralPath $logPath -Destination (Join-Path $acceptanceRoot 'phase05-acceptance.log') -Force
        Copy-Item -LiteralPath $failurePath -Destination (Join-Path $acceptanceRoot 'phase05-failures.log') -Force
        Copy-Item -LiteralPath $resultsPath -Destination (Join-Path $acceptanceRoot 'phase05-results.json') -Force
        Copy-Item -LiteralPath $benchmarkPath -Destination (Join-Path $acceptanceRoot 'phase05-benchmark.csv') -Force
        if ($acceptanceMode -eq 'full_phase05') {
            Copy-Item -LiteralPath $manualRestartPath -Destination (Join-Path $acceptanceRoot 'windows-restart-manual.md') -Force
        }
        elseif ($acceptanceMode -eq 'targeted_sentinel' -and (Test-Path -LiteralPath $targetedSearchAttemptsPath -PathType Leaf)) {
            Copy-Item -LiteralPath $targetedSearchAttemptsPath -Destination (Join-Path $acceptanceRoot 'phase05-targeted-search-attempts.jsonl') -Force
        }
        $results.artifact_export = 'PASS'
        $results.artifact_export_error = $null
        Save-Results
        Copy-Item -LiteralPath $resultsPath -Destination (Join-Path $artifactRunRoot 'phase05-results.json') -Force
        Copy-Item -LiteralPath $resultsPath -Destination (Join-Path $acceptanceRoot 'phase05-results.json') -Force
    }
    catch {
        $artifactError = $_.Exception.Message
        $results.artifact_export = 'FAIL'
        $results.artifact_export_error = $artifactError
        try {
            Save-Results
            Copy-Item -LiteralPath $resultsPath -Destination (Join-Path $acceptanceRoot 'phase05-results.json') -Force -ErrorAction Stop
        }
        catch {
            $artifactError = "$artifactError; final status export failed: $($_.Exception.Message)"
            $results.artifact_export_error = $artifactError
        }
        Write-Host ('{0} Artifact copy failed: {1}' -f (Get-Date).ToString('s'), $artifactError)
        if ($script:exitCode -eq 0) { $script:exitCode = 3 }
    }

    # The canonical artifacts have already been copied to D:.  Remove only
    # this run's temporary off-target directory in the default mode; retain it
    # for -KeepAcceptanceData or an export failure so diagnostics remain
    # available without touching unrelated user files.
    if (-not $KeepAcceptanceData -and $results.artifact_export -eq 'PASS' -and (Test-Path -LiteralPath $harnessLiveRoot)) {
        try {
            Remove-Item -LiteralPath $harnessLiveRoot -Recurse -Force -ErrorAction Stop
            Write-Host ('{0} Removed temporary live harness root: {1}' -f (Get-Date).ToString('s'), $harnessLiveRoot)
        }
        catch {
            Write-Host ('{0} Temporary live harness cleanup failed: {1}' -f (Get-Date).ToString('s'), $_.Exception.Message)
            if ($script:exitCode -eq 0) { $script:exitCode = 3 }
        }
    }
}
exit $script:exitCode
