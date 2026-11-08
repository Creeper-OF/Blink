[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$AuthorizedClientSid
)

$ErrorActionPreference = 'Stop'
$enableExitCode = 1
$transcriptStarted = $false

function Test-RepositoryPath([string]$Candidate, [string]$RepositoryRoot) {
    if (-not [IO.Path]::IsPathRooted($Candidate)) { return $false }
    $candidatePath = [IO.Path]::GetFullPath($Candidate).TrimEnd('\') + '\'
    $rootPath = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\') + '\'
    return $candidatePath.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)
}

function Wait-ServiceState([System.ServiceProcess.ServiceControllerStatus]$Status) {
    $service = Get-Service -Name 'Blink.IndexService' -ErrorAction Stop
    try { $service.WaitForStatus($Status, [TimeSpan]::FromSeconds(30)) }
    finally { $service.Dispose() }
}

try {
    # The UI performs the one UAC request and passes its original user's SID.
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $enableExitCode = 5
        throw 'Enabling the index service requires the UAC-approved administrator process.'
    }
    $enableExitCode = 87
    $sid = [Security.Principal.SecurityIdentifier]::new($AuthorizedClientSid)
    if (-not $sid.IsAccountSid()) { throw 'AuthorizedClientSid must identify the original Blink user account.' }

    $repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $logRoot = Join-Path $repositoryRoot '.blink-data\logs'
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    Start-Transcript -Path (Join-Path $logRoot 'index-service-setup.log') -Force | Out-Null
    $transcriptStarted = $true
    [Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    $OutputEncoding = [Console]::OutputEncoding
    $installer = Join-Path $repositoryRoot 'install-index-service.ps1'
    $serviceExecutable = Join-Path $repositoryRoot "x64\$Configuration\Blink.IndexService.exe"
    if (-not (Test-Path -LiteralPath $serviceExecutable -PathType Leaf) -and
        -not $PSBoundParameters.ContainsKey('Configuration')) {
        $debugExecutable = Join-Path $repositoryRoot 'x64\Debug\Blink.IndexService.exe'
        if (Test-Path -LiteralPath $debugExecutable -PathType Leaf) {
            $Configuration = 'Debug'
            $serviceExecutable = $debugExecutable
        }
    }
    $enableExitCode = 2
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf) -or
        -not (Test-Path -LiteralPath $serviceExecutable -PathType Leaf)) {
        throw "The Blink index service build or installer is missing for $Configuration."
    }

    $serviceName = 'Blink.IndexService'
    $dataRoot = Join-Path $repositoryRoot '.blink-data'
    $enableExitCode = 1
    $existing = Get-CimInstance -ClassName Win32_Service -Filter "Name='Blink.IndexService'" -ErrorAction Stop
    if ($null -ne $existing) {
        $enableExitCode = 183
        $image = [regex]::Match($existing.PathName, '^\s*(?:"(?<image>[^"]+)"|(?<image>\S+))(?<arguments>.*)$')
        if (-not $image.Success) { throw 'The existing Blink.IndexService command cannot be identified.' }
        $existingExecutable = $image.Groups['image'].Value
        $roots = [regex]::Matches($image.Groups['arguments'].Value,
            '(?:^|\s)--data-root\s+(?:"(?<path>[^"]+)"|(?<path>\S+))(?=\s|$)')
        if ($roots.Count -ne 1 -or
            -not (Test-RepositoryPath $existingExecutable $repositoryRoot) -or
            -not [IO.Path]::GetFileName($existingExecutable).Equals('Blink.IndexService.exe', [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-RepositoryPath $roots[0].Groups['path'].Value $repositoryRoot)) {
            throw 'Blink.IndexService belongs to another location; its registration and data have been left unchanged.'
        }
        # Preserve an existing project-owned store instead of silently changing it.
        $dataRoot = [IO.Path]::GetFullPath($roots[0].Groups['path'].Value)
    }

    $desiredCommand = '"{0}" --data-root "{1}" --authorized-client-sid "{2}" --hardened-security' -f
        $serviceExecutable, $dataRoot, $sid.Value
    $configure = $null -eq $existing -or $existing.StartName -ne 'LocalSystem' -or $existing.StartMode -eq 'Disabled' -or
        -not $existing.PathName.Trim().Equals($desiredCommand, [StringComparison]::OrdinalIgnoreCase)
    $enableExitCode = 1
    if ($configure) {
        if ($null -ne $existing -and $existing.State -ne 'Stopped') {
            Stop-Service -Name $serviceName -ErrorAction Stop
            Wait-ServiceState ([System.ServiceProcess.ServiceControllerStatus]::Stopped)
        }
        & $installer -Configuration $Configuration -Account LocalSystem -DataRoot $dataRoot `
            -AuthorizedClientSid $sid.Value -HardenedLocalSystemCandidate
    }
    Set-Service -Name $serviceName -StartupType Automatic -ErrorAction Stop
    $service = Get-Service -Name $serviceName -ErrorAction Stop
    try {
        if ($service.Status -eq [System.ServiceProcess.ServiceControllerStatus]::StopPending) {
            $service.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(30))
            $service.Refresh()
        }
        if ($service.Status -eq [System.ServiceProcess.ServiceControllerStatus]::Paused) {
            Resume-Service -Name $serviceName -ErrorAction Stop
        }
        elseif ($service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running -and
            $service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::StartPending) {
            Start-Service -Name $serviceName -ErrorAction Stop
        }
        $service.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Running, [TimeSpan]::FromSeconds(30))
    }
    finally { $service.Dispose() }
    [Console]::WriteLine("Blink.IndexService is running. Data root: $dataRoot")
    exit 0
}
catch {
    $failure = $_.Exception.GetBaseException()
    if ($failure -is [System.ServiceProcess.TimeoutException] -or $failure -is [TimeoutException]) {
        $enableExitCode = 1460
    }
    elseif ($failure -is [ComponentModel.Win32Exception]) {
        $enableExitCode = $failure.NativeErrorCode
    }
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit $enableExitCode
}
finally {
    if ($transcriptStarted) { Stop-Transcript -ErrorAction SilentlyContinue | Out-Null }
}
