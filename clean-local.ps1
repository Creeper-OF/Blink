[CmdletBinding(SupportsShouldProcess)]
param(
    [switch] $IncludePackages
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
$repositoryPrefix = $repositoryRoot.TrimEnd('\') + '\'
$relativeTargets = @(
    '.build',
    '.vs',
    '.blink-data',
    '.nuget\http-cache',
    '.nuget\scratch',
    '.nuget\plugins-cache'
)
if ($IncludePackages) {
    $relativeTargets += '.nuget\packages'
}

foreach ($relativeTarget in $relativeTargets) {
    $target = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $relativeTarget))
    if (-not $target.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a path outside the repository: $target"
    }

    if (Test-Path -LiteralPath $target) {
        if ($PSCmdlet.ShouldProcess($target, 'Remove local cache')) {
            Remove-Item -LiteralPath $target -Recurse -Force
            Write-Host "Removed $target"
        }
    }
}
