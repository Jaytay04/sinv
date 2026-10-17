Set-StrictMode -Version 2.0

if (-not ('StorageHostReadNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class StorageHostReadNative {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool DeviceIoControl(IntPtr handle, uint code, byte[] input, uint inputLength, byte[] output, uint outputLength, out uint returned, IntPtr overlapped);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="SetLastError")]
    private static extern void ClearLastError(uint error);
    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();
    [DllImport("advapi32.dll", SetLastError=true)]
    private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool LookupPrivilegeValueW(string systemName, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError=true)]
    private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges newState, uint bufferLength, out TokenPrivileges previousState, out uint returnLength);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="GetFirmwareEnvironmentVariableExW")]
    private static extern uint GetFirmwareEnvironmentVariableEx(string name, string guid, byte[] buffer, uint bufferLength, out uint attributes);
    [DllImport("ntdll.dll")]
    private static extern int NtQuerySystemInformation(int informationClass, ref CodeIntegrityInformation information, int informationLength, out int returnLength);

    [StructLayout(LayoutKind.Sequential)]
    private struct CodeIntegrityInformation { public uint Length; public uint Options; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Luid { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)]
    private struct LuidAndAttributes { public Luid Luid; public uint Attributes; }
    [StructLayout(LayoutKind.Sequential)]
    private struct TokenPrivileges { public uint PrivilegeCount; public LuidAndAttributes Privileges; }

    public static string ReadStorageSerial(string path) {
        IntPtr handle = CreateFileW(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (handle == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateFileW(" + path + ") failed");
        try {
            byte[] query = new byte[12];
            byte[] descriptor = new byte[8192];
            uint returned;
            if (!DeviceIoControl(handle, 0x002D1400, query, (uint)query.Length, descriptor, (uint)descriptor.Length, out returned, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "IOCTL_STORAGE_QUERY_PROPERTY failed");
            if (returned < 36) throw new InvalidOperationException("StorageDeviceProperty descriptor shorter than 36 bytes: " + returned);
            uint size = BitConverter.ToUInt32(descriptor, 4);
            uint offset = BitConverter.ToUInt32(descriptor, 24);
            if (size < 36 || size > returned || offset < 36 || offset >= size)
                throw new InvalidOperationException("StorageDeviceProperty serial offset/size is malformed: offset=" + offset + " size=" + size + " returned=" + returned);
            uint end = offset;
            while (end < size && descriptor[end] != 0) end++;
            if (end == size) throw new InvalidOperationException("StorageDeviceProperty serial is not NUL terminated");
            return Encoding.ASCII.GetString(descriptor, (int)offset, (int)(end - offset));
        } finally { CloseHandle(handle); }
    }

    public static object ReadSecureBootVariable() {
        const string guid = "{8be4df61-93ca-11d2-aa0d-00e098032b8c}";
        const uint TOKEN_QUERY = 0x0008;
        const uint TOKEN_ADJUST_PRIVILEGES = 0x0020;
        const uint SE_PRIVILEGE_ENABLED = 0x00000002;
        const int ERROR_NOT_ALL_ASSIGNED = 1300;
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, out token)) {
            int error = Marshal.GetLastWin32Error();
            return new { supported = false, win32Error = error, win32ErrorText = new Win32Exception(error).Message, size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null, value = (string)null, privilegeEnabled = false, privilegeError = error };
        }
        try {
            Luid privilege;
            if (!LookupPrivilegeValueW(null, "SeSystemEnvironmentPrivilege", out privilege)) {
                int error = Marshal.GetLastWin32Error();
                return new { supported = false, win32Error = error, win32ErrorText = new Win32Exception(error).Message, size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null, value = (string)null, privilegeEnabled = false, privilegeError = error };
            }
            TokenPrivileges requested = new TokenPrivileges();
            requested.PrivilegeCount = 1;
            requested.Privileges.Luid = privilege;
            requested.Privileges.Attributes = SE_PRIVILEGE_ENABLED;
            TokenPrivileges previous;
            uint previousLength;
            ClearLastError(0);
            if (!AdjustTokenPrivileges(token, false, ref requested, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out previous, out previousLength)) {
                int error = Marshal.GetLastWin32Error();
                return new { supported = false, win32Error = error, win32ErrorText = new Win32Exception(error).Message, size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null, value = (string)null, privilegeEnabled = false, privilegeError = error };
            }
            int privilegeError = Marshal.GetLastWin32Error();
            if (privilegeError == ERROR_NOT_ALL_ASSIGNED) {
                return new { supported = false, win32Error = privilegeError, win32ErrorText = new Win32Exception(privilegeError).Message, size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null, value = (string)null, privilegeEnabled = false, privilegeError = privilegeError };
            }
            bool wasEnabled = (previous.Privileges.Attributes & SE_PRIVILEGE_ENABLED) != 0;
            byte[] buffer = new byte[256];
            uint attributes = 0;
            ClearLastError(0);
            uint size = GetFirmwareEnvironmentVariableEx("SecureBoot", guid, buffer, (uint)buffer.Length, out attributes);
            int readError = size == 0 ? Marshal.GetLastWin32Error() : 0;
            byte[] actual = size == 0 ? new byte[0] : new byte[(int)size];
            if (size != 0) Array.Copy(buffer, actual, (int)size);
            int restoreError = 0;
            if (!wasEnabled) {
                TokenPrivileges ignored;
                uint ignoredLength;
                ClearLastError(0);
                if (!AdjustTokenPrivileges(token, false, ref previous, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out ignored, out ignoredLength)) restoreError = Marshal.GetLastWin32Error();
                else if (Marshal.GetLastWin32Error() == ERROR_NOT_ALL_ASSIGNED) restoreError = ERROR_NOT_ALL_ASSIGNED;
            }
            if (restoreError != 0) {
                return new { supported = false, win32Error = restoreError, win32ErrorText = "Failed to restore prior token privilege state: " + new Win32Exception(restoreError).Message, size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null, value = (string)null, privilegeEnabled = true, privilegeError = 0, privilegeRestored = false, privilegeRestoreError = restoreError };
            }
            if (readError != 0) {
                return new { supported = false, win32Error = readError, win32ErrorText = new Win32Exception(readError).Message, size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null, value = (string)null, privilegeEnabled = true, privilegeError = 0, privilegeRestored = true, privilegeRestoreError = 0 };
            }
            return new { supported = true, win32Error = 0, win32ErrorText = (string)null, size = size, attributes = (uint?)attributes,
                bytesHex = BitConverter.ToString(actual).Replace("-", ""), bytesBase64 = Convert.ToBase64String(actual),
                value = actual.Length == 1 ? actual[0].ToString("X2") : (string)null, privilegeEnabled = true, privilegeError = 0, privilegeRestored = true, privilegeRestoreError = 0 };
        } finally { CloseHandle(token); }
    }

    public static object ReadCodeIntegrityOptions() {
        CodeIntegrityInformation value = new CodeIntegrityInformation();
        value.Length = (uint)Marshal.SizeOf(typeof(CodeIntegrityInformation));
        int returned;
        int status = NtQuerySystemInformation(103, ref value, (int)value.Length, out returned);
        return new { ntStatus = "0x" + unchecked((uint)status).ToString("X8"), success = status >= 0, options = status >= 0 ? (uint?)value.Options : null, returnLength = returned };
    }
}
'@
}

function Invoke-StorageHostCapture {
    param([Parameter(Mandatory=$true)][string]$Program,[Parameter(Mandatory=$true)][string[]]$Arguments,[switch]$RequireSuccess)
    $prior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& $Program @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $prior }
    $capture = [pscustomobject]@{ program = $Program; arguments = $Arguments; exitCode = $exitCode; output = $output }
    if ($RequireSuccess -and $exitCode -ne 0) { throw ($Program + ' ' + ($Arguments -join ' ') + ' exited ' + $exitCode + ': ' + ($output -join ' | ')) }
    return $capture
}

function Get-StorageHostBcdElement {
    param([Parameter(Mandatory=$true)][string[]]$Lines)
    return Get-StorageHostTestSigningElement -BcdLines $Lines
}

function Get-StorageHostServiceState {
    $service = Get-CimInstance Win32_SystemDriver -Filter "Name='client_storage_filter'" -ErrorAction SilentlyContinue
    $registryPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\client_storage_filter'
    $registry = $null
    if (Test-Path -LiteralPath $registryPath) {
        $item = Get-ItemProperty -LiteralPath $registryPath
        $properties=[ordered]@{}
        foreach($name in @('Start','Type','ImagePath','Group','ErrorControl')){
            $property=$item.PSObject.Properties[$name]
            $key=$name.Substring(0,1).ToLowerInvariant()+$name.Substring(1)
            if($null -ne $property){$properties[$key]=$property.Value}else{$properties[$key]=$null}
        }
        $registry=$properties
    }
    return [ordered]@{ present = ($null -ne $service); services = @($service | Select-Object Name,State,StartMode,PathName); registry = $registry }
}

function Get-StorageHostCertificateMembership {
    param([Parameter(Mandatory=$true)][string]$Thumbprint)
    $result = [ordered]@{}
    foreach ($storeName in @('Root','TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($storeName,'LocalMachine')
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        try { $result[$storeName] = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $Thumbprint }).Count -gt 0 }
        finally { $store.Close() }
    }
    return $result
}

function Get-StorageHostInventory {
    param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
    if (-not (Test-Path -LiteralPath $EvidenceDirectory -PathType Container)) { throw "Evidence directory does not exist: $EvidenceDirectory" }
    $os = Get-CimInstance Win32_OperatingSystem
    $machine = Get-CimInstance Win32_ComputerSystem
    $product = Get-CimInstance Win32_ComputerSystemProduct
    $version = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $disks = @(Get-CimInstance Win32_DiskDrive | Where-Object { $_.DeviceID -ceq '\\.\PHYSICALDRIVE0' })
    if ($disks.Count -ne 1) { throw ('Expected one PHYSICALDRIVE0 WMI record; actual count=' + $disks.Count) }
    $disk = Get-Disk -Number 0 -ErrorAction Stop
    $pnpId = [string]$disks[0].PNPDeviceID
    $hardwareIds = @()
    $hardwareIdError = $null
    try { $hardwareIds = @((Get-PnpDeviceProperty -InstanceId $pnpId -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction Stop).Data | ForEach-Object { [string]$_ }) }
    catch { $hardwareIdError = $_.Exception.Message }

    $nativeSerial = [StorageHostReadNative]::ReadStorageSerial('\\.\PHYSICALDRIVE0')
    $wmiSerial = ([string]$disks[0].SerialNumber).Trim()
    $pnpStack = Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\pnputil.exe') -Arguments @('/enum-devices','/instanceid',$pnpId,'/stack')
    $stackLines = @(Get-StorageHostStackLines -PnpUtilOutput $pnpStack.output)
    $secureBoot = $null
    $secureBootError = $null
    try { $secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) }
    catch { $secureBootError = $_.Exception.Message }
    $bcd = Invoke-StorageHostCapture -Program (Join-Path $env:SystemRoot 'System32\bcdedit.exe') -Arguments @('/enum','{current}','/v')
    $testSigning = Get-StorageHostBcdElement -Lines $bcd.output
    $filePath = Join-Path $EvidenceDirectory ('file-io.' + [Guid]::NewGuid().ToString('N') + '.bin')
    $fileIo = $null
    $bytes = New-Object byte[] (1MB)
    $random = [Random]::new(61417)
    $random.NextBytes($bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $expectedHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')
        $stream = [IO.FileStream]::new($filePath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,4096,[IO.FileOptions]::WriteThrough)
        try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        $actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash
        $fileIo = [ordered]@{ bytes = $bytes.Length; expectedSha256 = $expectedHash; actualSha256 = $actualHash; match = ($expectedHash -ceq $actualHash); writeThrough = $true }
    } finally {
        $sha.Dispose()
        if (Test-Path -LiteralPath $filePath) { Remove-Item -LiteralPath $filePath -Force }
    }

    return [ordered]@{
        schema = 'storage-host-inventory.v1'
        capturedUtc = [DateTime]::UtcNow.ToString('o')
        machine = [ordered]@{ manufacturer = [string]$machine.Manufacturer; model = [string]$machine.Model; uuid = [string]$product.UUID }
        windows = [ordered]@{ build = [string]$version.CurrentBuildNumber; ubr = [int]$version.UBR; displayVersion = [string]$version.DisplayVersion; lastBootUpTime = $os.LastBootUpTime.ToUniversalTime().ToString('o') }
        memory = [ordered]@{ totalMiB = [math]::Round($os.TotalVisibleMemorySize / 1024); freeMiB = [math]::Round($os.FreePhysicalMemory / 1024) }
        disk = [ordered]@{ number = [int]$disk.Number; isBoot = [bool]$disk.IsBoot; isSystem = [bool]$disk.IsSystem; pnpDeviceId = $pnpId; hardwareIds = $hardwareIds; hardwareIdReadError = $hardwareIdError; serial = $nativeSerial; wmiSerial = $wmiSerial; devicePath = '\\.\PHYSICALDRIVE0'; nativeSource = 'CreateFileW + IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceProperty'; wmiSource = 'Fresh Win32_DiskDrive CIM query' }
        pnpStack = [ordered]@{ exitCode = $pnpStack.exitCode; rawLines = $pnpStack.output; stackLines = $stackLines; filterPresent = (Test-StorageHostStackContainsFilter -PnpUtilOutput $pnpStack.output); source = 'pnputil /enum-devices /instanceid /stack; Device Stack section only' }
        policy = [ordered]@{ secureBoot = $secureBoot; secureBootReadError = $secureBootError; secureBootVariable = [StorageHostReadNative]::ReadSecureBootVariable(); bcdCurrentExitCode = $bcd.exitCode; bcdCurrentLines = $bcd.output; testSigning = $testSigning; codeIntegrity = [StorageHostReadNative]::ReadCodeIntegrityOptions() }
        storageFilter = Get-StorageHostServiceState
        fileIo = $fileIo
        hostSettingsChanged = $false
    }
}

function Write-StorageHostInventory {
    param([Parameter(Mandatory=$true)][string]$EvidenceDirectory,[Parameter(Mandatory=$true)][string]$Name)
    $inventory = Get-StorageHostInventory -EvidenceDirectory $EvidenceDirectory
    $path = Join-Path $EvidenceDirectory $Name
    [IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $inventory -Depth 12),[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ path = $path; inventory = $inventory; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}
