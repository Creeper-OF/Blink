[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$VolumeRoot = 'D:\',
    [Parameter(Mandatory)]
    [string]$ExistingStorePath,
    [switch]$ReplaceExistingService,
    [switch]$KeepAcceptanceData
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
    $highIntegrity = $groups -match 'High Mandatory Level|System Mandatory Level'
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or -not $highIntegrity) {
        Write-Error 'Blink security acceptance requires an Administrator High Integrity PowerShell. This script will not self-elevate and will not run tests.'
        exit 740
    }
    return $identity
}

$identity = Assert-AdministratorHighIntegrity
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runner = Join-Path $PSScriptRoot 'run-phase05-elevated-acceptance.ps1'
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "Targeted acceptance runner is missing: $runner"
}
if (-not (Test-Path -LiteralPath $ExistingStorePath -PathType Leaf)) {
    throw "ExistingStorePath is missing: $ExistingStorePath"
}

Write-Host 'Blink Phase 0.5 Security Acceptance is a targeted manual run.'
Write-Host 'Initial MFT build is not requested; ExistingStorePath is copied to a new run DataRoot.'
Write-Host ("Authorized client SID: {0}" -f $identity.User.Value)

$runnerArguments = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $runner,
    '-Configuration', $Configuration,
    '-VolumeRoot', $VolumeRoot,
    '-TargetedSentinelAcceptance',
    '-TargetedServiceAccount', 'LocalSystem',
    '-HardenedLocalSystemCandidate',
    '-ExistingStorePath', $ExistingStorePath
)
if ($ReplaceExistingService) {
    $runnerArguments += '-ReplaceExistingService'
}
if ($KeepAcceptanceData) {
    $runnerArguments += '-KeepAcceptanceData'
}

& powershell.exe @runnerArguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "Targeted security acceptance runner failed with exit code $exitCode."
}
Write-Host 'Targeted security acceptance runner completed. Its slice result is not a recomputation of full Phase 0.5.'
