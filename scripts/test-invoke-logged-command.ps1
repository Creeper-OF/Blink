[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'phase05-harness-common.ps1')

$script:sameVolumeLogging = $false
$script:smokeLog = [System.Collections.Generic.List[string]]::new()
$logPath = Join-Path $env:TEMP 'blink-smoke-test.log'
Set-Content -LiteralPath $logPath -Value '' -Encoding utf8

function Write-Log {
    param([string]$Message, [switch]$Failure)
    $line = ('{0} {1}' -f (Get-Date).ToString('s'), $Message)
    Add-Content -LiteralPath $logPath -Value $line -Encoding utf8
    $script:smokeLog.Add($line)
    Write-Host $line
}

$script:smokeLogAction = {
    param([string]$Message, [bool]$Failure)
    Write-Log -Message $Message -Failure:$Failure
}

Write-Host ''
Write-Host '=== TEST 1: stdout + stderr + heartbeat + exit 0 ==='
$script:smokeLog.Clear()
$subBefore = @(Get-EventSubscriber).Count
$test1Pass = $false
try {
    Invoke-Phase05LoggedCommand -FilePath 'powershell.exe' -Arguments @(
        '-NoProfile', '-Command',
        '$u=[System.Text.UTF8Encoding]::new($false); [Console]::InputEncoding=$u; [Console]::OutputEncoding=$u; $OutputEncoding=$u; $text=''Blink中文路径测试_内蒙古_QWQ''; Write-Output $text; $Host.UI.WriteErrorLine($text); Write-Output STDOUT-LINE-1; Write-Output STDOUT-LINE-2; $Host.UI.WriteErrorLine(''STDERR-LINE-1''); Start-Sleep -Seconds 12; Write-Output STDOUT-AFTER-SLEEP; exit 0'
    ) -LogAction $script:smokeLogAction
    $test1Pass = $true
}
catch {
    Write-Host "TEST 1 UNEXPECTED THROW: $_"
}
$subAfter = @(Get-EventSubscriber).Count
$logText = ($script:smokeLog -join [Environment]::NewLine)
$hasStdout1 = $logText -match 'STDOUT-LINE-1'
$hasStdout2 = $logText -match 'STDOUT-LINE-2'
$hasStderr = $logText -match 'STDERR-LINE-1'
$hasChineseStdout = $logText.Contains('Blink中文路径测试_内蒙古_QWQ')
$hasChineseNoMojibake = -not $logText.Contains('???') -and -not $logText.Contains('锟')
$hasHeartbeat = $logText -match 'Build heartbeat'
$hasBuildEnd = $logText -match 'exitCode=0'
$noResidual = $subAfter -eq $subBefore

Write-Host ''
Write-Host "  stdout captured:     $hasStdout1 AND $hasStdout2"
Write-Host "  stderr captured:     $hasStderr"
Write-Host "  Chinese stdout/stderr: $hasChineseStdout"
Write-Host "  UTF-8 text intact:   $hasChineseNoMojibake"
Write-Host "  heartbeat emitted:   $hasHeartbeat"
Write-Host "  build end logged:    $hasBuildEnd"
Write-Host "  no throw:            $test1Pass"
Write-Host "  no residual subs:    $noResidual (before=$subBefore, after=$subAfter)"
$test1Result = $hasStdout1 -and $hasStdout2 -and $hasStderr -and $hasChineseStdout -and $hasChineseNoMojibake -and $hasHeartbeat -and $hasBuildEnd -and $test1Pass -and $noResidual
Write-Host "  TEST 1 RESULT:       $(if ($test1Result) { 'PASS' } else { 'FAIL' })"

Write-Host ''
Write-Host '=== TEST 2: exit non-zero -> must throw ==='
$script:smokeLog.Clear()
$subBefore2 = @(Get-EventSubscriber).Count
$test2Threw = $false
try {
    Invoke-Phase05LoggedCommand -FilePath 'powershell.exe' -Arguments @(
        '-NoProfile', '-Command',
        'Write-Output BEFORE-FAIL; exit 42'
    ) -LogAction $script:smokeLogAction
}
catch {
    $test2Threw = $true
    Write-Host "  Caught expected throw: $_"
}
$subAfter2 = @(Get-EventSubscriber).Count
$logText2 = ($script:smokeLog -join [Environment]::NewLine)
$hasBeforeFail = $logText2 -match 'BEFORE-FAIL'
$hasBuildEnd2 = $logText2 -match 'exitCode=42'
$noResidual2 = $subAfter2 -eq $subBefore2

Write-Host "  threw on non-zero:   $test2Threw"
Write-Host "  stdout captured:     $hasBeforeFail"
Write-Host "  exitCode in log:     $hasBuildEnd2"
Write-Host "  no residual subs:    $noResidual2 (before=$subBefore2, after=$subAfter2)"
$test2Result = $test2Threw -and $hasBeforeFail -and $hasBuildEnd2 -and $noResidual2
Write-Host "  TEST 2 RESULT:       $(if ($test2Result) { 'PASS' } else { 'FAIL' })"

Write-Host ''
Write-Host '=== OVERALL ==='
if ($test1Result -and $test2Result) {
    Write-Host '  ALL SMOKE TESTS PASSED'
    $exitCode = 0
} else {
    Write-Host '  SMOKE TEST FAILURES DETECTED'
    $exitCode = 1
}

Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
exit $exitCode
