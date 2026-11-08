[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [ValidateSet('Build', 'Restore')]
    [string] $Action = 'Build',

    [ValidateRange(1, 64)]
    [int] $MaxCpuCount = 1
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$buildJunctionPath = $null

try {
    $repositoryRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
    $localPackageRoot = Join-Path $repositoryRoot '.nuget\packages'
    $localHttpCache = Join-Path $repositoryRoot '.nuget\http-cache'
    $localNuGetScratch = Join-Path $repositoryRoot '.nuget\scratch'
    $localPluginCache = Join-Path $repositoryRoot '.nuget\plugins-cache'
    $localBuildTemp = Join-Path $repositoryRoot '.build\tmp'
    $localRuntimeData = Join-Path $repositoryRoot '.blink-data'

    foreach ($directory in @(
        $localPackageRoot,
        $localHttpCache,
        $localNuGetScratch,
        $localPluginCache,
        $localBuildTemp,
        $localRuntimeData
    )) {
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
        }
    }

    # MIDL and a few other native build tools still receive paths through
    # legacy ANSI boundaries.  When the repository path contains non-ASCII
    # characters, build through a temporary ASCII junction.  The junction
    # points at this repository, so all outputs and caches remain project-local.
    $msbuildRepositoryRoot = $repositoryRoot
    $requiresAsciiBuildRoot = $false
    foreach ($character in $repositoryRoot.ToCharArray()) {
        if ([int][char]$character -gt 127) {
            $requiresAsciiBuildRoot = $true
            break
        }
    }
    if ($requiresAsciiBuildRoot) {
        $volumeRoot = [System.IO.Path]::GetPathRoot($repositoryRoot)
        $buildJunctionPath = Join-Path $volumeRoot ('BlinkBuildAscii-' + [Guid]::NewGuid().ToString('N'))
        if (Test-Path -LiteralPath $buildJunctionPath) {
            throw "Refusing to replace existing build junction path: $buildJunctionPath"
        }
        New-Item -ItemType Junction -Path $buildJunctionPath -Target $repositoryRoot | Out-Null
        $junctionItem = Get-Item -LiteralPath $buildJunctionPath -Force
        if ((-not ($junctionItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) -or
            $junctionItem.Target -ne $repositoryRoot) {
            [System.IO.Directory]::Delete($buildJunctionPath)
            $buildJunctionPath = $null
            throw "Build junction target verification failed."
        }
        $msbuildRepositoryRoot = $buildJunctionPath
    }

    $solutionPath = Join-Path $msbuildRepositoryRoot 'Blink.sln'
    $msbuildPackageRoot = Join-Path $msbuildRepositoryRoot '.nuget\packages'
    $msbuildHttpCache = Join-Path $msbuildRepositoryRoot '.nuget\http-cache'
    $msbuildNuGetScratch = Join-Path $msbuildRepositoryRoot '.nuget\scratch'
    $msbuildPluginCache = Join-Path $msbuildRepositoryRoot '.nuget\plugins-cache'
    $msbuildBuildTemp = Join-Path $msbuildRepositoryRoot '.build\tmp'
    $msbuildRuntimeData = Join-Path $msbuildRepositoryRoot '.blink-data'

    # These variables are scoped to this PowerShell process and its MSBuild child.
    # They do not change the user's global environment after this script exits.
    $env:NUGET_PACKAGES = $msbuildPackageRoot
    $env:NUGET_HTTP_CACHE_PATH = $msbuildHttpCache
    $env:NUGET_SCRATCH = $msbuildNuGetScratch
    $env:NUGET_PLUGINS_CACHE_PATH = $msbuildPluginCache
    $env:TEMP = $msbuildBuildTemp
    $env:TMP = $msbuildBuildTemp
    $env:BLINK_DATA_ROOT = $msbuildRuntimeData

$msbuildPath = $null

# Blink keeps the MSVC v143 toolset policy. Prefer the installed VS18 MSBuild
# because its NuGet resolver handles the native PackageReference graph used by
# the WinUI product; the project toolset remains v143.
$preferredInstallRoots = @(
    'D:\DEV\C++\BuildTools',
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\BuildTools'),
    'D:\DEV\Microsoft Visual Studio\2022\BuildTools',
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\BuildTools'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Community'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Professional'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Professional'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Enterprise'),
    (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Enterprise')
)
foreach ($installRoot in $preferredInstallRoots) {
    $candidate = Join-Path $installRoot 'MSBuild\Current\Bin\MSBuild.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $msbuildPath = $candidate
        break
    }
}

if ([string]::IsNullOrWhiteSpace($msbuildPath)) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswherePath -PathType Leaf) {
        $installationPath = (& $vswherePath -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
        if ($null -eq $installationPath) {
            $installationPath = (& $vswherePath -version '[17.0,18.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
        }
        if ($null -ne $installationPath) {
            $installationPath = $installationPath.ToString().Trim()
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $msbuildPath = $candidate
            }
        }
    }
}

if ([string]::IsNullOrWhiteSpace($msbuildPath)) {
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        $msbuildPath = $command.Source
    }
}

if ([string]::IsNullOrWhiteSpace($msbuildPath)) {
    throw 'MSBuild.exe was not found. Install Visual Studio Build Tools with the C++ workload.'
}

    if (-not (Test-Path -LiteralPath $solutionPath -PathType Leaf)) {
        throw "Solution not found: $solutionPath"
    }

    # /restore reevaluates imports before building. Chaining Restore;Rebuild
    # retains stale Pkg* paths from the previous temporary ASCII junction.
    $target = if ($Action -eq 'Restore') { 'Restore' } else { 'Rebuild' }
    $packageRootWithTrailingSlash = "$msbuildPackageRoot\"
    $arguments = @(
        $solutionPath,
        "/t:$target",
        "/m:$MaxCpuCount",
        "/p:CL_MPCount=$MaxCpuCount",
        '/nr:false',
        "/p:Configuration=$Configuration",
        '/p:Platform=x64',
        "/p:RestorePackagesPath=$msbuildPackageRoot",
        "/p:NuGetPackageRoot=$packageRootWithTrailingSlash",
        '/v:minimal'
    )
    if ($Action -eq 'Build') {
        $arguments += '/restore'
    }

    Write-Host "Repository: $repositoryRoot"
    Write-Host "MSBuild root: $msbuildRepositoryRoot"
    Write-Host "MSBuild: $msbuildPath"
    Write-Host "NuGet packages: $localPackageRoot"
    Write-Host "NuGet HTTP cache: $localHttpCache"
    Write-Host "Build temp: $localBuildTemp"
    Write-Host "Action: $Action ($Configuration|x64)"
    Write-Host "Project/compiler parallelism: $MaxCpuCount"

    & $msbuildPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE"
    }

    $testSolutionPath = Join-Path $msbuildRepositoryRoot 'tests\Blink.Phase05.Tests\Blink.Phase05.Tests.vcxproj'
    if (Test-Path -LiteralPath $testSolutionPath -PathType Leaf) {
        $testArguments = @(
            $testSolutionPath,
            "/t:$target",
            "/m:$MaxCpuCount",
            "/p:CL_MPCount=$MaxCpuCount",
            '/nr:false',
            "/p:Configuration=$Configuration",
            '/p:Platform=x64',
            "/p:RestorePackagesPath=$msbuildPackageRoot",
            "/p:NuGetPackageRoot=$packageRootWithTrailingSlash",
            '/v:minimal'
        )
        if ($Action -eq 'Build') {
            $testArguments += '/restore'
        }
        & $msbuildPath @testArguments
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed to build Phase05.Tests with exit code $LASTEXITCODE"
        }
    }
}
finally {
    if ($null -ne $buildJunctionPath -and (Test-Path -LiteralPath $buildJunctionPath)) {
        $junctionItem = Get-Item -LiteralPath $buildJunctionPath -Force
        if (($junctionItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -and
            $junctionItem.Target -eq $repositoryRoot) {
            [System.IO.Directory]::Delete($buildJunctionPath)
        }
        else {
            Write-Warning "Build junction was not removed because target verification failed: $buildJunctionPath"
        }
    }
}
