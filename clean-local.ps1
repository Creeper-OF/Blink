<#
.SYNOPSIS
Removes disposable local build and restore caches.
.DESCRIPTION
Keeps runtime data, current binaries, verification records, recovery snapshots,
IDE settings and restored packages. Use -WhatIf to preview the exact targets.
.PARAMETER IncludePackages
Also removes restored NuGet packages. A later build will need package restore.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [switch] $IncludePackages
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
$repositoryPrefix = $repositoryRoot.TrimEnd('\') + '\'
$relativeTargets = @(
    '.build\tmp',
    '.build\nuget-http-cache',
    '.build\nuget-scratch',
    '.nuget\http-cache',
    '.nuget\scratch',
    '.nuget\plugins-cache'
)
if ($IncludePackages) {
    $relativeTargets += '.nuget\packages'
}

if (-not $WhatIfPreference) {
    $inUse = @(Get-Process -Name @(
        'MSBuild', 'cl', 'link', 'midl', 'csc', 'nuget',
        'Blink.Phase0.Tests', 'Blink.Phase05.Tests',
        'Blink.Phase1.Tests', 'Blink.Transfer.Tests'
    ) -ErrorAction SilentlyContinue)
    if ($inUse.Count) {
        throw ('Build or test processes are running; wait before cleaning: ' +
            (($inUse | Select-Object -ExpandProperty ProcessName -Unique) -join ', '))
    }
}

foreach ($relativeTarget in $relativeTargets) {
    $target = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $relativeTarget))
    if (-not $target.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a path outside the repository: $target"
    }

    if (Test-Path -LiteralPath $target) {
        if ((Get-Item -LiteralPath $target -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to clean a redirected cache path: $target"
        }
        if ($PSCmdlet.ShouldProcess($target, 'Remove local cache')) {
            Remove-Item -LiteralPath $target -Recurse -Force
            Write-Host "Removed $target"
        }
    }
}
