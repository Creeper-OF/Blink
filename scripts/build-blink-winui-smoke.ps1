param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [ValidateSet('M3', 'M4', 'R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'G0', 'G1', 'GS0')]
    [string] $Mode = 'M3'
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'src\Blink.WinUiSmoke\Blink.WinUiSmoke.vcxproj'
$buildRoot = Join-Path $projectRoot '.build\winui-smoke'
$nugetRoot = Join-Path $projectRoot '.nuget\packages'
$httpCache = Join-Path $buildRoot 'nuget-http-cache'
$scratch = Join-Path $buildRoot 'nuget-scratch'
$plugins = Join-Path $buildRoot 'nuget-plugins'
$tempRoot = Join-Path $buildRoot 'temp'

foreach ($directory in @($buildRoot, $httpCache, $scratch, $plugins, $tempRoot)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Smoke project not found: $projectPath"
}

$msbuildCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
    'D:\DEV\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
)
$msbuildPath = $msbuildCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $msbuildPath) {
    throw 'MSBuild with the v143 toolset was not found.'
}

$env:NUGET_PACKAGES = $nugetRoot
$env:NUGET_HTTP_CACHE_PATH = $httpCache
$env:NUGET_SCRATCH = $scratch
$env:NUGET_PLUGINS_CACHE_PATH = $plugins
$env:TEMP = $tempRoot
$env:TMP = $tempRoot

$arguments = @(
    $projectPath,
    '/t:Restore;Rebuild',
    '/m',
    '/nr:false',
    '/nologo',
    '/v:minimal',
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/p:PlatformToolset=v143',
    '/p:WindowsTargetPlatformVersion=10.0.26100.0',
    '/p:WindowsTargetPlatformMinVersion=10.0.26100.0',
    '/p:WindowsPackageType=None',
    '/p:AppxPackage=false',
    ("/p:WindowsAppSDKSelfContained={0}" -f ((@('M4', 'GS0') -contains $Mode).ToString().ToLowerInvariant())),
    '/p:WindowsAppSdkUndockedRegFreeWinRTInitialize=true',
    ("/p:SmokeOutputSuffix=-{0}" -f $Mode.ToLowerInvariant()),
    ("/p:SmokeDeclarativeResources={0}" -f ((@('M4', 'G0', 'G1', 'GS0') -contains $Mode).ToString().ToLowerInvariant())),
    ("/p:SmokeGeneratedStartup={0}" -f ((@('G0', 'G1', 'GS0') -contains $Mode).ToString().ToLowerInvariant())),
    ("/p:SmokeGeneratedMainWrapper={0}" -f (($Mode -eq 'G1').ToString().ToLowerInvariant())),
    "/p:RestorePackagesPath=$nugetRoot",
    "/p:NuGetPackageRoot=$nugetRoot"
)

Write-Output ("Building Blink.WinUiSmoke {0} with {1}" -f $Configuration, $msbuildPath)
& $msbuildPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Blink.WinUiSmoke build failed with exit code $LASTEXITCODE."
}

$outputDirectory = Join-Path $projectRoot ("src\Blink.WinUiSmoke\x64\{0}-{1}" -f $Configuration, $Mode.ToLowerInvariant())
$smokeExe = Join-Path $outputDirectory 'Blink.WinUiSmoke.exe'
if (-not (Test-Path -LiteralPath $smokeExe -PathType Leaf)) {
    throw "Build completed without the expected executable: $smokeExe"
}

Write-Output ("SmokeExe = {0}" -f $smokeExe)
Write-Output ("Configuration = {0}" -f $Configuration)
