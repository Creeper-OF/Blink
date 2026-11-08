[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

$projectRoot = Split-Path -Parent $PSScriptRoot
$blinkExe = Join-Path $projectRoot 'src\Blink\x64\Release\Blink.exe'

if (-not (Test-Path -LiteralPath $blinkExe -PathType Leaf)) {
    Write-Error ("Release Blink.exe not found. Expected path: {0}" -f $blinkExe)
    exit 1
}

Write-Output ("Starting existing Release Blink.exe: {0}" -f $blinkExe)
Start-Process -FilePath $blinkExe -WorkingDirectory (Split-Path -Parent $blinkExe)
