#Requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$KeepData)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null
$ErrorActionPreference = 'Stop'
$serviceName = 'Blink.IndexService'

function Invoke-NativeUtf8Command {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,
        [Parameter(Mandatory)]
        [string]$Arguments
    )
    $nativeEncoding = [Text.Encoding]::GetEncoding(
        [Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
    $pinfo = [Diagnostics.ProcessStartInfo]::new()
    $pinfo.FileName = $FilePath
    $pinfo.Arguments = $Arguments
    $pinfo.UseShellExecute = $false
    $pinfo.CreateNoWindow = $true
    $pinfo.RedirectStandardOutput = $true
    $pinfo.RedirectStandardError = $true
    $pinfo.StandardOutputEncoding = $nativeEncoding
    $pinfo.StandardErrorEncoding = $nativeEncoding
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $pinfo
    try {
        if (-not $process.Start()) {
            throw "Failed to start native command: $FilePath"
        }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if (-not [string]::IsNullOrEmpty($stdout)) {
            [Console]::Out.Write($stdout)
        }
        if (-not [string]::IsNullOrEmpty($stderr)) {
            [Console]::Error.Write($stderr)
        }
        if ($process.ExitCode -ne 0) {
            throw "$FilePath failed with exit code $($process.ExitCode)"
        }
    }
    finally {
        $process.Dispose()
    }
}

$existing = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -ne $existing) {
    if ($existing.Status -ne 'Stopped') {
        Stop-Service -Name $serviceName -Force
    }
    Invoke-NativeUtf8Command -FilePath 'sc.exe' -Arguments ('delete "{0}"' -f $serviceName)
}

if (-not $KeepData) {
    $dataRoot = Join-Path $PSScriptRoot '.blink-data'
    if (Test-Path -LiteralPath $dataRoot) {
        Remove-Item -LiteralPath $dataRoot -Recurse -Force
        Write-Host "Removed service data: $dataRoot"
    }
}
