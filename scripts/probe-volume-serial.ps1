[CmdletBinding()]
param(
    [string]$VolumeRoot = 'D:\',
    [string]$ProjectRoot = '',
    [string]$DataRoot = '',
    [string]$OutputPath = ''
)

[Console]::InputEncoding  = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
chcp.com 65001 > $null

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($DataRoot)) {
    $DataRoot = Join-Path $ProjectRoot '.blink-data'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot '.blink-data\acceptance\volume-serial-probe.json'
}

if (-not ('BlinkVolumeSerialNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using System.Text;

public static class BlinkVolumeSerialNative
{
    private const uint FILE_READ_ATTRIBUTES = 0x00000080;
    private const uint FILE_SHARE_READ = 0x00000001;
    private const uint FILE_SHARE_WRITE = 0x00000002;
    private const uint FILE_SHARE_DELETE = 0x00000004;
    private const uint OPEN_EXISTING = 3;
    private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
    private const int FileIdInfo = 18;

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct ByHandleFileInformation
    {
        public uint FileAttributes;
        public long CreationTime;
        public long LastAccessTime;
        public long LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool GetVolumeInformationW(
        string rootPathName,
        StringBuilder volumeNameBuffer,
        int volumeNameSize,
        out uint volumeSerialNumber,
        out uint maximumComponentLength,
        out uint fileSystemFlags,
        StringBuilder fileSystemNameBuffer,
        int fileSystemNameSize);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        IntPtr securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetFileInformationByHandle(
        SafeFileHandle handle,
        out ByHandleFileInformation information);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetFileInformationByHandleEx(
        IntPtr handle,
        int fileInformationClass,
        IntPtr fileInformation,
        uint bufferSize);

    public static bool TryGetVolumeInformation(
        string rootPathName,
        out uint volumeSerialNumber,
        out string fileSystemName)
    {
        uint maximumComponentLength;
        uint fileSystemFlags;
        var volumeName = new StringBuilder(256);
        var fileSystem = new StringBuilder(256);
        var ok = GetVolumeInformationW(
            rootPathName,
            volumeName,
            volumeName.Capacity,
            out volumeSerialNumber,
            out maximumComponentLength,
            out fileSystemFlags,
            fileSystem,
            fileSystem.Capacity);
        fileSystemName = fileSystem.ToString();
        return ok;
    }

    public static SafeFileHandle OpenMetadata(string path)
    {
        return CreateFileW(
            path,
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            IntPtr.Zero,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            IntPtr.Zero);
    }

    public static bool TryGetFileIdVolumeSerial(
        SafeFileHandle handle,
        out string volumeSerialHex,
        out uint low32,
        out uint high32)
    {
        volumeSerialHex = null;
        low32 = 0;
        high32 = 0;
        var size = 24;
        var buffer = Marshal.AllocHGlobal(size);
        try
        {
            if (!GetFileInformationByHandleEx(
                    handle.DangerousGetHandle(),
                    FileIdInfo,
                    buffer,
                    (uint)size))
            {
                return false;
            }
            var volumeSerialNumber = unchecked((ulong)Marshal.ReadInt64(buffer));
            volumeSerialHex = volumeSerialNumber.ToString("X16");
            low32 = unchecked((uint)volumeSerialNumber);
            high32 = unchecked((uint)(volumeSerialNumber >> 32));
            return true;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    public static bool TryGetByHandleVolumeSerial(
        SafeFileHandle handle,
        out uint volumeSerialNumber)
    {
        ByHandleFileInformation information;
        var ok = GetFileInformationByHandle(handle, out information);
        volumeSerialNumber = information.VolumeSerialNumber;
        return ok;
    }
}
'@
}

function Format-Hex32 {
    param([uint32]$Value)
    return ('0x{0:X8}' -f $Value)
}

function Format-Hex64 {
    param([uint64]$Value)
    return ('0x{0:X16}' -f $Value)
}

function Get-SerialRecord {
    param([string]$Path)

    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    $fullPath = $item.FullName
    $root = [System.IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw "Could not resolve volume root for '$fullPath'"
    }

    [uint32]$getVolumeSerial = 0
    $fileSystemName = ''
    $volumeOk = [BlinkVolumeSerialNative]::TryGetVolumeInformation(
        $root,
        [ref]$getVolumeSerial,
        [ref]$fileSystemName)
    $volumeError = if ($volumeOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }

    $handle = [BlinkVolumeSerialNative]::OpenMetadata($fullPath)
    if ($null -eq $handle -or $handle.IsInvalid) {
        $handleError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($null -ne $handle) { $handle.Dispose() }
        throw "CreateFile(FILE_READ_ATTRIBUTES) failed for '$fullPath' (Win32=$handleError)"
    }

    try {
        [string]$fileIdSerialHex = ''
        [uint32]$fileIdLow32 = 0
        [uint32]$fileIdHigh32 = 0
        $fileIdOk = [BlinkVolumeSerialNative]::TryGetFileIdVolumeSerial(
            $handle,
            [ref]$fileIdSerialHex,
            [ref]$fileIdLow32,
            [ref]$fileIdHigh32)
        $fileIdError = if ($fileIdOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }

        [uint32]$byHandleSerial = 0
        $byHandleOk = [BlinkVolumeSerialNative]::TryGetByHandleVolumeSerial($handle, [ref]$byHandleSerial)
        $byHandleError = if ($byHandleOk) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }

        $low32MatchesGetVolume = $volumeOk -and $fileIdOk -and $fileIdLow32 -eq $getVolumeSerial
        $low32MatchesByHandle = $byHandleOk -and $fileIdOk -and $fileIdLow32 -eq $byHandleSerial

        return [ordered]@{
            path = $fullPath
            is_directory = [bool]$item.PSIsContainer
            volume_root = $root
            file_system = $fileSystemName
            get_volume_information = [ordered]@{
                ok = $volumeOk
                serial_decimal = $getVolumeSerial
                serial_hex = if ($volumeOk) { Format-Hex32 $getVolumeSerial } else { $null }
                win32_error = $volumeError
            }
            file_id_info = [ordered]@{
                ok = $fileIdOk
                volume_serial_hex = if ($fileIdOk) { '0x' + $fileIdSerialHex } else { $null }
                low32_hex = if ($fileIdOk) { Format-Hex32 $fileIdLow32 } else { $null }
                high32_hex = if ($fileIdOk) { Format-Hex32 $fileIdHigh32 } else { $null }
                win32_error = $fileIdError
            }
            by_handle_file_information = [ordered]@{
                ok = $byHandleOk
                serial_decimal = $byHandleSerial
                serial_hex = if ($byHandleOk) { Format-Hex32 $byHandleSerial } else { $null }
                win32_error = $byHandleError
            }
            mapping = [ordered]@{
                low32_equals_get_volume_information = $low32MatchesGetVolume
                low32_equals_by_handle = $low32MatchesByHandle
                canonical_32_bit_mapping = $low32MatchesGetVolume -and $low32MatchesByHandle
            }
        }
    }
    finally {
        $handle.Dispose()
    }
}

$resolvedDataRoot = (Resolve-Path -LiteralPath $DataRoot -ErrorAction Stop).Path
$firstChild = Get-ChildItem -LiteralPath $resolvedDataRoot -Force -ErrorAction Stop |
    Sort-Object FullName |
    Select-Object -First 1
if ($null -eq $firstChild) {
    throw "DataRoot '$resolvedDataRoot' has no child file or directory to probe"
}

$targets = @(
    (Resolve-Path -LiteralPath $VolumeRoot -ErrorAction Stop).Path,
    $resolvedDataRoot,
    $firstChild.FullName
)
$records = @($targets | ForEach-Object { Get-SerialRecord $_ })
$mappingPassed = @($records | Where-Object { -not $_.mapping.canonical_32_bit_mapping }).Count -eq 0

Write-Host 'Blink Volume Serial Mapping Probe'
Write-Host ('Volume root input: {0}' -f $VolumeRoot)
Write-Host ('DataRoot: {0}' -f $resolvedDataRoot)
Write-Host ('Child target: {0}' -f $firstChild.FullName)
foreach ($record in $records) {
    Write-Host ''
    Write-Host ('Target: {0}' -f $record.path)
    Write-Host ('File system: {0}' -f $record.file_system)
    Write-Host ('GetVolumeInformationW DWORD = {0}' -f $record.get_volume_information.serial_hex)
    Write-Host ('FILE_ID_INFO 64             = {0}' -f $record.file_id_info.volume_serial_hex)
    Write-Host ('FILE_ID_INFO low32          = {0}' -f $record.file_id_info.low32_hex)
    Write-Host ('FILE_ID_INFO high32         = {0}' -f $record.file_id_info.high32_hex)
    Write-Host ('BY_HANDLE DWORD             = {0}' -f $record.by_handle_file_information.serial_hex)
    Write-Host ('low32 == GetVolumeInformationW = {0}' -f $(if ($record.mapping.low32_equals_get_volume_information) { 'PASS' } else { 'FAIL' }))
    Write-Host ('low32 == BY_HANDLE           = {0}' -f $(if ($record.mapping.low32_equals_by_handle) { 'PASS' } else { 'FAIL' }))
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$json = $records | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText($OutputPath, $json, [System.Text.UTF8Encoding]::new($false))
Write-Host ''
Write-Host ('Mapping result: {0}' -f $(if ($mappingPassed) { 'PASS' } else { 'FAIL' }))
Write-Host ('JSON: {0}' -f $OutputPath)
if (-not $mappingPassed) {
    exit 2
}
