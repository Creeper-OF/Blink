#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('LocalService', 'NetworkService', 'LocalSystem')]
    [string]$Account = 'LocalService',
    [string]$DataRoot = '',
    [string]$TelemetryRoot = '',
    [string]$AuthorizedClientSid = '',
    [switch]$HardenedLocalSystemCandidate,
    [switch]$Start
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null
$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path
$serviceName = 'Blink.IndexService'
$serviceExecutable = Join-Path $repositoryRoot "x64\$Configuration\Blink.IndexService.exe"

function Resolve-RepositoryPath([string]$value) {
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $null
    }
    if ([IO.Path]::IsPathRooted($value)) {
        return [IO.Path]::GetFullPath($value)
    }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $value))
}

function Test-PathWithin([string]$candidate, [string]$parent) {
    $candidateFull = ([IO.Path]::GetFullPath($candidate)).TrimEnd('\') + '\'
    $parentFull = ([IO.Path]::GetFullPath($parent)).TrimEnd('\') + '\'
    return $candidateFull.StartsWith($parentFull, [StringComparison]::OrdinalIgnoreCase)
}

$dataRoot = Resolve-RepositoryPath $(if ([string]::IsNullOrWhiteSpace($DataRoot)) { '.blink-data' } else { $DataRoot })
$resolvedTelemetryRoot = Resolve-RepositoryPath $TelemetryRoot

if (-not (Test-PathWithin $dataRoot $repositoryRoot)) {
    throw "DataRoot must remain inside the Blink repository: $dataRoot"
}
if ($null -ne $resolvedTelemetryRoot -and -not (Test-PathWithin $resolvedTelemetryRoot $dataRoot)) {
    throw "TelemetryRoot must remain inside DataRoot: $resolvedTelemetryRoot"
}

function Invoke-NativeUtf8Command {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,
        [Parameter(Mandatory)]
        [string]$Arguments
    )

    # icacls/sc.exe use the system OEM code page when stdout is redirected.
    # Decode that byte stream here, then emit Unicode through the explicitly
    # UTF-8-configured PowerShell console so the parent harness receives UTF-8.
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
            [Console]::Out.Flush()
        }
        if (-not [string]::IsNullOrEmpty($stderr)) {
            [Console]::Error.Write($stderr)
            [Console]::Error.Flush()
        }
        if ($process.ExitCode -ne 0) {
            throw "$FilePath failed with exit code $($process.ExitCode)"
        }
    }
    finally {
        $process.Dispose()
    }
}

function ConvertTo-WindowsProcessArgument([string]$value) {
    if ($null -eq $value -or $value.Length -eq 0) {
        return '""'
    }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $value.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (2 * $slashes + 1)))
            [void]$builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) {
            [void]$builder.Append(('\' * $slashes))
            $slashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($slashes -gt 0) {
        [void]$builder.Append(('\' * (2 * $slashes)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-ScCommand {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )
    $argumentString = (($Arguments | ForEach-Object { ConvertTo-WindowsProcessArgument $_ }) -join ' ')
    Invoke-NativeUtf8Command -FilePath 'sc.exe' -Arguments $argumentString
}

if (-not ('BlinkServiceConfig.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace BlinkServiceConfig
{
    public sealed class Snapshot
    {
        public uint ServiceSidType { get; set; }
        public string[] RequiredPrivileges { get; set; }
    }

    public static class Native
    {
        private const uint ScManagerConnect = 0x0001;
        private const uint ServiceQueryConfig = 0x0001;
        private const uint ServiceChangeConfig = 0x0002;
        private const uint ServiceConfigServiceSidInfo = 5;
        private const uint ServiceConfigRequiredPrivilegesInfo = 6;

        [StructLayout(LayoutKind.Sequential)]
        private struct ServiceSidInfo
        {
            public uint ServiceSidType;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RequiredPrivilegesInfo
        {
            public IntPtr RequiredPrivileges;
        }

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, EntryPoint = "OpenSCManagerW", ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr OpenSCManager(
            string machineName,
            string databaseName,
            uint desiredAccess);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, EntryPoint = "OpenServiceW", ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr OpenService(
            IntPtr manager,
            string serviceName,
            uint desiredAccess);

        [DllImport("advapi32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseServiceHandle(IntPtr handle);

        [DllImport("advapi32.dll", EntryPoint = "ChangeServiceConfig2W", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ChangeServiceConfig2(
            IntPtr service,
            uint infoLevel,
            IntPtr info);

        [DllImport("advapi32.dll", EntryPoint = "QueryServiceConfig2W", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryServiceConfig2(
            IntPtr service,
            uint infoLevel,
            IntPtr buffer,
            uint bufferSize,
            out uint bytesNeeded);

        private static void ThrowLastError(string operation)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), operation);
        }

        private static IntPtr OpenTargetService(string serviceName, out IntPtr manager)
        {
            manager = OpenSCManager(null, null, ScManagerConnect);
            if (manager == IntPtr.Zero)
            {
                ThrowLastError("OpenSCManagerW");
            }
            IntPtr service = OpenService(
                manager,
                serviceName,
                ServiceQueryConfig | ServiceChangeConfig);
            if (service == IntPtr.Zero)
            {
                CloseServiceHandle(manager);
                manager = IntPtr.Zero;
                ThrowLastError("OpenServiceW");
            }
            return service;
        }

        public static void Configure(
            string serviceName,
            uint serviceSidType,
            string[] requiredPrivileges)
        {
            IntPtr manager;
            IntPtr service = OpenTargetService(serviceName, out manager);
            try
            {
                IntPtr sidInfoMemory = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(ServiceSidInfo)));
                try
                {
                    ServiceSidInfo sidInfo = new ServiceSidInfo { ServiceSidType = serviceSidType };
                    Marshal.StructureToPtr(sidInfo, sidInfoMemory, false);
                    if (!ChangeServiceConfig2(service, ServiceConfigServiceSidInfo, sidInfoMemory))
                    {
                        ThrowLastError("ChangeServiceConfig2W(SERVICE_CONFIG_SERVICE_SID_INFO)");
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(sidInfoMemory);
                }

                string multiSz = string.Join("\0", requiredPrivileges ?? new string[0]) + "\0\0";
                IntPtr multiSzMemory = Marshal.StringToHGlobalUni(multiSz);
                IntPtr privilegeInfoMemory = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(RequiredPrivilegesInfo)));
                try
                {
                    RequiredPrivilegesInfo privilegeInfo = new RequiredPrivilegesInfo
                    {
                        RequiredPrivileges = multiSzMemory
                    };
                    Marshal.StructureToPtr(privilegeInfo, privilegeInfoMemory, false);
                    if (!ChangeServiceConfig2(service, ServiceConfigRequiredPrivilegesInfo, privilegeInfoMemory))
                    {
                        ThrowLastError("ChangeServiceConfig2W(SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO)");
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(privilegeInfoMemory);
                    Marshal.FreeHGlobal(multiSzMemory);
                }
            }
            finally
            {
                CloseServiceHandle(service);
                CloseServiceHandle(manager);
            }
        }

        private static IntPtr QueryBuffer(IntPtr service, uint infoLevel, out uint bufferSize)
        {
            QueryServiceConfig2(service, infoLevel, IntPtr.Zero, 0, out bufferSize);
            int error = Marshal.GetLastWin32Error();
            if (bufferSize == 0 && error != 122)
            {
                throw new Win32Exception(error, "QueryServiceConfig2W");
            }
            IntPtr buffer = Marshal.AllocHGlobal((int)bufferSize);
            uint ignoredBytesNeeded;
            if (!QueryServiceConfig2(service, infoLevel, buffer, bufferSize, out ignoredBytesNeeded))
            {
                Marshal.FreeHGlobal(buffer);
                ThrowLastError("QueryServiceConfig2W");
            }
            return buffer;
        }

        private static string[] ReadMultiSz(IntPtr pointer, uint byteLimit)
        {
            var values = new List<string>();
            var current = new System.Text.StringBuilder();
            for (int offset = 0; offset + 1 < byteLimit; offset += 2)
            {
                char value = (char)Marshal.ReadInt16(pointer, offset);
                if (value != '\0')
                {
                    current.Append(value);
                    continue;
                }
                if (current.Length == 0)
                {
                    break;
                }
                values.Add(current.ToString());
                current.Clear();
            }
            return values.ToArray();
        }

        public static Snapshot Query(string serviceName)
        {
            IntPtr manager;
            IntPtr service = OpenTargetService(serviceName, out manager);
            try
            {
                uint sidBufferSize;
                IntPtr sidBuffer = QueryBuffer(service, ServiceConfigServiceSidInfo, out sidBufferSize);
                uint sidType;
                try
                {
                    sidType = Marshal.PtrToStructure<ServiceSidInfo>(sidBuffer).ServiceSidType;
                }
                finally
                {
                    Marshal.FreeHGlobal(sidBuffer);
                }

                uint privilegeBufferSize;
                IntPtr privilegeBuffer = QueryBuffer(service, ServiceConfigRequiredPrivilegesInfo, out privilegeBufferSize);
                string[] privileges;
                try
                {
                    IntPtr multiSz = Marshal.ReadIntPtr(privilegeBuffer);
                    privileges = multiSz == IntPtr.Zero
                        ? new string[0]
                        : ReadMultiSz(multiSz, privilegeBufferSize);
                }
                finally
                {
                    Marshal.FreeHGlobal(privilegeBuffer);
                }
                return new Snapshot
                {
                    ServiceSidType = sidType,
                    RequiredPrivileges = privileges
                };
            }
            finally
            {
                CloseServiceHandle(service);
                CloseServiceHandle(manager);
            }
        }
    }
}
'@ -Language CSharp
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$groups = (whoami /groups | Out-String)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
    $groups -notmatch 'S-1-16-(12288|16384)(?!\d)') {
    throw 'Blink.IndexService installation requires an Administrator High Integrity PowerShell.'
}

if (-not (Test-Path -LiteralPath $serviceExecutable -PathType Leaf)) {
    throw "Service executable is missing: $serviceExecutable. Build with .\build.ps1 first."
}

if ($HardenedLocalSystemCandidate -and $Account -ne 'LocalSystem') {
    throw '-HardenedLocalSystemCandidate is currently supported only with -Account LocalSystem.'
}

if ([string]::IsNullOrWhiteSpace($AuthorizedClientSid)) {
    $AuthorizedClientSid = $identity.User.Value
}
try {
    $authorizedSidObject = New-Object System.Security.Principal.SecurityIdentifier($AuthorizedClientSid)
    if (-not $authorizedSidObject.IsAccountSid()) {
        throw "SID is not an account SID: $AuthorizedClientSid"
    }
}
catch {
    throw "AuthorizedClientSid is invalid: $AuthorizedClientSid. $($_.Exception.Message)"
}

New-Item -ItemType Directory -Force -Path (Join-Path $dataRoot 'index') | Out-Null
if ($null -ne $resolvedTelemetryRoot) {
    New-Item -ItemType Directory -Force -Path $resolvedTelemetryRoot | Out-Null
}

$accountName = switch ($Account) {
    'LocalService' { 'NT AUTHORITY\LocalService' }
    'NetworkService' { 'NT AUTHORITY\NetworkService' }
    'LocalSystem' { 'LocalSystem' }
}
$binaryPath = '"{0}" --data-root "{1}"' -f $serviceExecutable, $dataRoot
if ($null -ne $resolvedTelemetryRoot) {
    $binaryPath += ' --acceptance-telemetry-root "{0}"' -f $resolvedTelemetryRoot
}
$binaryPath += ' --authorized-client-sid "{0}"' -f $AuthorizedClientSid
if ($HardenedLocalSystemCandidate) {
    $binaryPath += ' --hardened-security'
}
$existing = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -eq $existing) {
    Invoke-ScCommand -Arguments @(
        'create', $serviceName, 'binPath=', $binaryPath, 'start=', 'demand',
        'obj=', $accountName, 'DisplayName=', 'Blink Privileged Index Service')
} else {
    Invoke-ScCommand -Arguments @(
        'config', $serviceName, 'binPath=', $binaryPath, 'start=', 'demand', 'obj=', $accountName)
}

$requiredPrivileges = @()
$serviceSidTypeName = 'NONE_OR_UNCHANGED'
$serviceSidValue = ''
$serviceConfigSnapshot = $null
if ($HardenedLocalSystemCandidate) {
    $requiredPrivileges = @('SeImpersonatePrivilege')
    [BlinkServiceConfig.Native]::Configure(
        $serviceName,
        1,
        $requiredPrivileges)
    $serviceConfigSnapshot = [BlinkServiceConfig.Native]::Query($serviceName)
    if ($serviceConfigSnapshot.ServiceSidType -ne 1 -or
        $serviceConfigSnapshot.RequiredPrivileges -notcontains 'SeImpersonatePrivilege') {
        throw 'Hardened LocalSystem candidate configuration did not round-trip through QueryServiceConfig2.'
    }
    $serviceSidTypeName = 'UNRESTRICTED'

    try {
        $serviceSidAccount = New-Object System.Security.Principal.NTAccount('NT SERVICE\Blink.IndexService')
        $serviceSidValue = $serviceSidAccount.Translate([System.Security.Principal.SecurityIdentifier]).Value
    }
    catch {
        Write-Warning "Service SID name is configured but its numeric SID is not resolvable until SCM materializes the service token: $($_.Exception.Message)"
    }

    # Keep the native sc.exe representation in the acceptance evidence as a
    # second, independently readable view of the same QueryServiceConfig2
    # state. Invoke-NativeUtf8Command decodes legacy OEM output explicitly.
    Invoke-ScCommand -Arguments @('qsidtype', $serviceName)
    Invoke-ScCommand -Arguments @('qprivs', $serviceName)
}

$securityDirectory = Join-Path $dataRoot 'security'
New-Item -ItemType Directory -Force -Path $securityDirectory | Out-Null
$securityEvidencePath = Join-Path $securityDirectory 'installer-service-config.json'
$securityEvidence = [ordered]@{
    schema = 'blink.service-security.v1'
    service_name = $serviceName
    logon_account = $accountName
    hardened_local_system_candidate = [bool]$HardenedLocalSystemCandidate
    service_sid_name = 'NT SERVICE\Blink.IndexService'
    service_sid_type_requested = $serviceSidTypeName
    service_sid_value_observed = $serviceSidValue
    required_privileges_requested = @($requiredPrivileges)
    authorized_client_sid = $AuthorizedClientSid
    pipe_remote_policy = 'PIPE_REJECT_REMOTE_CLIENTS'
    pipe_client_rights = @('FILE_READ_DATA','FILE_WRITE_DATA','FILE_READ_ATTRIBUTES','FILE_WRITE_ATTRIBUTES','READ_CONTROL','SYNCHRONIZE')
    pipe_server_extra_right = 'FILE_CREATE_PIPE_INSTANCE'
    query_service_config2 = if ($null -eq $serviceConfigSnapshot) { $null } else {
        [ordered]@{
            service_sid_type = $serviceConfigSnapshot.ServiceSidType
            required_privileges = @($serviceConfigSnapshot.RequiredPrivileges)
        }
    }
}
$securityEvidenceJson = $securityEvidence | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($securityEvidencePath, $securityEvidenceJson, [Text.UTF8Encoding]::new($false))

if ($Account -ne 'LocalSystem') {
    $grant = '{0}:(OI)(CI)M' -f $accountName
    $nativeArguments = '"{0}" /grant "{1}" /T' -f $dataRoot, $grant
    Invoke-NativeUtf8Command -FilePath 'icacls.exe' -Arguments $nativeArguments
}

Write-Host "Installed $serviceName as $accountName."
Write-Host "Data root: $dataRoot"
Write-Host "Authorized client SID: $AuthorizedClientSid"
Write-Host "Service security evidence: $securityEvidencePath"
if ($HardenedLocalSystemCandidate) {
    Write-Host 'Hardened LocalSystem candidate: Service SID type=UNRESTRICTED; RequiredPrivileges=SeImpersonatePrivilege'
}
if ($null -ne $resolvedTelemetryRoot) {
    Write-Host "Acceptance telemetry root: $resolvedTelemetryRoot"
}
Write-Host 'Direct volume access is tested only after the service is started; an access-denied result is reported and is not silently upgraded.'

if ($Start) {
    Start-Service -Name $serviceName
    Write-Host "Started $serviceName."
}
