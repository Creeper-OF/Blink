[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null

# Shared Phase 0.5 harness helpers.
#
# Aside from required console encoding/code-page initialization, this file
# intentionally contains no top-level business I/O. Both the production
# acceptance harness and its smoke test dot-source it so the smoke test calls
# the exact same process/stream/event cleanup implementation.

function Invoke-Phase05NativeOemCommand {
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
        return [pscustomobject][ordered]@{
            exit_code = $process.ExitCode
            stdout = $stdout
            stderr = $stderr
        }
    }
    finally {
        $process.Dispose()
    }
}

function Invoke-Phase05LoggedCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [Parameter(Mandatory)]
        [scriptblock]$LogAction,
        [string]$StartLabel = 'Build',
        [int]$HeartbeatSeconds = 10
    )

    $cmdStr = "{0} {1}" -f $FilePath, ($Arguments -join ' ')
    & $LogAction ("{0} start: {1}" -f $StartLabel, $cmdStr) $false
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $pinfo = New-Object System.Diagnostics.ProcessStartInfo
    $pinfo.FileName = $FilePath
    $pinfo.Arguments = $Arguments -join ' '
    $pinfo.RedirectStandardOutput = $true
    $pinfo.RedirectStandardError = $true
    $pinfo.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $pinfo.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
    $pinfo.UseShellExecute = $false
    $pinfo.CreateNoWindow = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $pinfo
    $outputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
    $stdoutSub = $null
    $stderrSub = $null
    $exit = $null

    $outAction = {
        if (-not [string]::IsNullOrWhiteSpace($EventArgs.Data)) {
            $Event.MessageData.Enqueue([string]$EventArgs.Data)
        }
    }

    try {
        $stdoutSub = Register-ObjectEvent -InputObject $process -EventName 'OutputDataReceived' -Action $outAction -MessageData $outputQueue
        $stderrSub = Register-ObjectEvent -InputObject $process -EventName 'ErrorDataReceived' -Action $outAction -MessageData $outputQueue
        $process.Start() | Out-Null
        $process.BeginOutputReadLine()
        $process.BeginErrorReadLine()

        $lastHeartbeat = Get-Date
        while (-not $process.HasExited) {
            $line = $null
            while ($outputQueue.TryDequeue([ref]$line)) {
                & $LogAction $line $false
            }
            if (((Get-Date) - $lastHeartbeat).TotalSeconds -ge $HeartbeatSeconds) {
                & $LogAction ("{0} heartbeat [{1}]: running for {2:F0}s..." -f $StartLabel, $FilePath, $sw.Elapsed.TotalSeconds) $false
                $lastHeartbeat = Get-Date
            }
            Start-Sleep -Milliseconds 250
        }

        $process.WaitForExit()
        # Event callbacks can be queued just after the process exits.
        Start-Sleep -Milliseconds 100
        $line = $null
        while ($outputQueue.TryDequeue([ref]$line)) {
            & $LogAction $line $false
        }
        $exit = $process.ExitCode
    }
    finally {
        if ($null -ne $stdoutSub) {
            Unregister-Event -SubscriptionId $stdoutSub.Id -ErrorAction SilentlyContinue
            Remove-Job -Id $stdoutSub.Id -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $stderrSub) {
            Unregister-Event -SubscriptionId $stderrSub.Id -ErrorAction SilentlyContinue
            Remove-Job -Id $stderrSub.Id -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $process) {
            $process.Dispose()
        }
    }

    $sw.Stop()
    & $LogAction ("{0} end: {1} (elapsed={2:F2}s, exitCode={3})" -f $StartLabel, $FilePath, $sw.Elapsed.TotalSeconds, $exit) $false
    if ($exit -ne 0) {
        throw ("{0} failed with exit code {1}" -f $FilePath, $exit)
    }
}
