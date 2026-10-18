[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [string]$Action = 'Probe',
    [string]$ExpectedProbeVerdict = 'NOT_MET'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$expectedRunRoot = [IO.Path]::GetFullPath((Join-Path $env:ProgramData 'UEFILoaderRunner\runs')).TrimEnd('\') + '\'
$root = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\')
$result = [ordered]@{
    SchemaVersion = 1
    RunId = Split-Path -Leaf $root
    Status = 'FAIL'
    Message = 'Read-only SMBIOS and EFI diagnostics did not complete.'
    Action = $Action
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$metadata = [ordered]@{
    schema = 'combined-vm-smbios-boot-diagnostic.v1'
    lifecycleId = '__LIFECYCLE_ID__'
    startedUtc = [DateTime]::UtcNow.ToString('o')
    status = 'FAIL'
    observations = [ordered]@{}
    reader = [ordered]@{}
    firmwareVariables = [ordered]@{}
    esp = [ordered]@{}
    errors = @()
}
$output = [Collections.Generic.List[string]]::new()
$stateRoot = '__LIFECYCLE_STATE_ROOT__'
$readerPath = Join-Path $stateRoot 'assets\c0ef07ac4698644758f2384bc8524cb87fb9946fe04972f47c3f6bd30a80a74c\smbios_windows_readback.exe'
$readerExpectedSha256 = '__CAPTURE_READER_SHA256__'
$firmwareGuid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Read-EspEvidence([string]$EspRoot, [string]$RelativePath, [switch]$IncludeText) {
    $path = Join-Path $EspRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return [ordered]@{ path = $RelativePath; exists = $false; bytes = 0; sha256 = $null; text = $null }
    }
    $item = Get-Item -LiteralPath $path -ErrorAction Stop
    if ($item.Length -gt 524288) { throw "ESP diagnostic file exceeds the 512 KiB read limit: $RelativePath" }
    $bytes = [IO.File]::ReadAllBytes($path)
    $record = [ordered]@{
        path = $RelativePath
        exists = $true
        bytes = [long]$bytes.Length
        sha256 = (Get-Sha256 $path)
        text = $null
    }
    if ($IncludeText) {
        $record.text = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)
    }
    return $record
}

try {
    if (-not $root.StartsWith($expectedRunRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $root -PathType Container)) {
        throw 'Diagnostic RunRoot is outside the authenticated guest mailbox run directory.'
    }
    if ($Action -cne 'Probe' -or $ExpectedProbeVerdict -cne 'NOT_MET') {
        throw 'Unexpected fixed guest gate action or probe verdict.'
    }

    $computerRows = @(Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop)
    $productRows = @(Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop)
    $expectedDeviceId = [string]([char]92 + [char]92 + '.' + [char]92 + 'PHYSICALDRIVE0')
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq $expectedDeviceId })
    $disks = @(Get-Disk -Number 0 -ErrorAction Stop)
    if ($computerRows.Count -ne 1 -or $productRows.Count -ne 1 -or $diskRows.Count -ne 1 -or $disks.Count -ne 1) {
        throw ('VM guard query cardinality failed: computer=' + $computerRows.Count + '; product=' + $productRows.Count + '; disk=' + $diskRows.Count + '; Get-Disk=' + $disks.Count)
    }
    $diskPnp = [string]$diskRows[0].PNPDeviceID
    if ($diskPnp -cne 'SCSI\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\5&25A13950&0&000000' -or
        [string]$diskRows[0].Model -notmatch '(?i)VMware' -or -not $disks[0].IsBoot -or -not $disks[0].IsSystem) {
        throw ('Exact disposable VMware boot-disk guard failed: pnp=' + $diskPnp + '; model=' + [string]$diskRows[0].Model + '; isBoot=' + [string]$disks[0].IsBoot + '; isSystem=' + [string]$disks[0].IsSystem)
    }
    $metadata.observations.machineIdentity = [ordered]@{
        win32ComputerSystem = [ordered]@{ manufacturer = [string]$computerRows[0].Manufacturer; model = [string]$computerRows[0].Model }
        win32ComputerSystemProduct = [ordered]@{
            vendor = [string]$productRows[0].Vendor
            name = [string]$productRows[0].Name
            uuid = [string]$productRows[0].UUID
            identifyingNumber = [string]$productRows[0].IdentifyingNumber
        }
        disk = [ordered]@{
            deviceId = [string]$diskRows[0].DeviceID
            pnpDeviceId = $diskPnp
            model = [string]$diskRows[0].Model
            serialNumberWmi = ([string]$diskRows[0].SerialNumber).Trim()
            number = [int]$disks[0].Number
            isBoot = [bool]$disks[0].IsBoot
            isSystem = [bool]$disks[0].IsSystem
        }
    }
    $metadata.observations.identityComparison = [ordered]@{
        expectedOriginal = [ordered]@{ manufacturer = 'VMware, Inc.'; model = 'VMware20,1'; uuid = 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' }
        expectedPatched = [ordered]@{ manufacturer = 'NORTHLAB'; model = 'NODE'; uuid = 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9' }
    }

    if (-not (Test-Path -LiteralPath $readerPath -PathType Leaf)) { throw "Pinned SMBIOS reader is missing in the guest cache: $readerPath" }
    $readerActualSha256 = Get-Sha256 $readerPath
    if ($readerActualSha256 -cne $readerExpectedSha256) { throw "Pinned SMBIOS reader hash mismatch: expected=$readerExpectedSha256 actual=$readerActualSha256" }
    $capturePath = Join-Path $root 'smbios-current.json'
    $readerOutput = @(& $readerPath --capture --output $capturePath 2>&1 | ForEach-Object { [string]$_ })
    $readerExitCode = $LASTEXITCODE
    $output.AddRange([string[]]$readerOutput)
    $metadata.reader = [ordered]@{
        path = $readerPath
        sha256 = $readerActualSha256
        outputPath = $capturePath
        exitCode = [int]$readerExitCode
        output = @($readerOutput)
    }
    if ($readerExitCode -ne 0 -or -not (Test-Path -LiteralPath $capturePath -PathType Leaf)) {
        throw ('SMBIOS native/WMI capture failed: exit=' + $readerExitCode + '; output=' + ($readerOutput -join ' | '))
    }
    $captureText = [IO.File]::ReadAllText($capturePath)
    $capture = $captureText | ConvertFrom-Json -ErrorAction Stop
    if ($capture.schema -cne 'smbios-windows-capture.v1' -or $capture.mode -cne 'capture') {
        throw ('SMBIOS capture schema/mode mismatch: schema=' + [string]$capture.schema + '; mode=' + [string]$capture.mode)
    }
    $fieldRows = [ordered]@{}
    foreach ($field in @('system_serial', 'system_vendor', 'system_product', 'board_serial', 'board_vendor', 'board_product', 'system_uuid')) {
        $property = $capture.fields.PSObject.Properties[$field]
        if ($null -eq $property) { $fieldRows[$field] = [ordered]@{ present = $false } ; continue }
        $fieldRows[$field] = [ordered]@{
            present = $true
            observedNative = [string]$property.Value.observedNative
            observedWmi = [string]$property.Value.observedWmi
            nativeMatchesWmi = [bool]$property.Value.nativeMatchesWmi
            matches = ([string]$property.Value.observedNative -ceq [string]$property.Value.observedWmi)
        }
    }
    $metadata.observations.smbios = [ordered]@{
        schema = [string]$capture.schema
        result = [string]$capture.result
        machineIdentity = $capture.machineIdentity
        fields = $fieldRows
        rawCapture = $capture
        captureSha256 = (Get-Sha256 $capturePath)
        captureBytes = [long](Get-Item -LiteralPath $capturePath).Length
    }

    $secureBoot = $null
    $secureBootError = $null
    try { $secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { $secureBootError = $_.Exception.ToString() }
    $metadata.observations.secureBoot = [ordered]@{ enabled = $secureBoot; error = $secureBootError }
    if (-not ('CombinedVmDiagnosticFirmwareReader' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class CombinedVmDiagnosticFirmwareReader {
    private const uint TOKEN_QUERY = 0x0008;
    private const uint TOKEN_ADJUST_PRIVILEGES = 0x0020;
    private const uint SE_PRIVILEGE_ENABLED = 0x00000002;
    private const int ERROR_NOT_ALL_ASSIGNED = 1300;
    [StructLayout(LayoutKind.Sequential)] private struct Luid { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)] private struct LuidAndAttributes { public Luid Luid; public uint Attributes; }
    [StructLayout(LayoutKind.Sequential)] private struct TokenPrivileges { public uint PrivilegeCount; public LuidAndAttributes Privileges; }
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="SetLastError")] private static extern void ClearLastError(uint error);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="LookupPrivilegeValueW")] private static extern bool LookupPrivilegeValue(string systemName, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges newState, uint bufferLength, out TokenPrivileges previousState, out uint returnLength);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="GetFirmwareEnvironmentVariableExW")] private static extern uint Get(string name, string guid, byte[] buffer, uint size, out uint attributes);
    public static object Read(string name, string guid) {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, out token)) {
            int error = Marshal.GetLastWin32Error();
            return new { name=name, supported=false, win32Error=error, win32ErrorText=new Win32Exception(error).Message, size=0, attributes=(uint?)null, bytesHex=(string)null, privilegeEnabled=false, privilegeRestored=true, privilegeRestoreError=0 };
        }
        try {
            Luid privilege;
            if (!LookupPrivilegeValue(null, "SeSystemEnvironmentPrivilege", out privilege)) {
                int error = Marshal.GetLastWin32Error();
                return new { name=name, supported=false, win32Error=error, win32ErrorText=new Win32Exception(error).Message, size=0, attributes=(uint?)null, bytesHex=(string)null, privilegeEnabled=false, privilegeRestored=true, privilegeRestoreError=0 };
            }
            TokenPrivileges requested = new TokenPrivileges();
            requested.PrivilegeCount=1; requested.Privileges.Luid=privilege; requested.Privileges.Attributes=SE_PRIVILEGE_ENABLED;
            TokenPrivileges previous; uint previousLength;
            ClearLastError(0);
            bool adjusted=AdjustTokenPrivileges(token,false,ref requested,(uint)Marshal.SizeOf(typeof(TokenPrivileges)),out previous,out previousLength);
            int privilegeError=Marshal.GetLastWin32Error();
            bool privilegeEnabled=adjusted && privilegeError!=ERROR_NOT_ALL_ASSIGNED;
            uint size=0, attributes=0; int readError=0; byte[] actual=new byte[0];
            if (privilegeEnabled) {
                byte[] buffer=new byte[4096]; ClearLastError(0); size=Get(name,guid,buffer,(uint)buffer.Length,out attributes);
                readError=size==0 ? Marshal.GetLastWin32Error() : 0;
                actual=size==0 ? new byte[0] : new byte[(int)size]; if (size!=0) Array.Copy(buffer,actual,(int)size);
            }
            int restoreError=0;
            if (adjusted) {
                TokenPrivileges ignored; uint ignoredLength; ClearLastError(0);
                if (!AdjustTokenPrivileges(token,false,ref previous,(uint)Marshal.SizeOf(typeof(TokenPrivileges)),out ignored,out ignoredLength)) restoreError=Marshal.GetLastWin32Error();
                else if (Marshal.GetLastWin32Error()==ERROR_NOT_ALL_ASSIGNED) restoreError=ERROR_NOT_ALL_ASSIGNED;
            }
            int finalError=restoreError!=0 ? restoreError : (privilegeError!=0 ? privilegeError : readError);
            bool supported=finalError==0 && size!=0;
            return new { name=name, supported=supported, win32Error=finalError, win32ErrorText=finalError==0 ? null : new Win32Exception(finalError).Message,
                size=supported ? (int)size : 0, attributes=supported ? (uint?)attributes : null,
                bytesHex=supported ? BitConverter.ToString(actual).Replace("-","") : (string)null,
                privilegeEnabled=privilegeEnabled, privilegeRestored=restoreError==0, privilegeRestoreError=restoreError };
        } finally { CloseHandle(token); }
    }
}
'@
    }
    foreach ($name in @('SecureBoot', 'BootNext', 'BootOrder', 'Boot0006')) {
        $metadata.firmwareVariables[$name] = [CombinedVmDiagnosticFirmwareReader]::Read($name, $firmwareGuid)
    }
    $bcdOutput = @(& (Join-Path $env:SystemRoot 'System32\bcdedit.exe') /enum firmware /v 2>&1 | ForEach-Object { [string]$_ })
    $bcdExit = $LASTEXITCODE
    $metadata.observations.bcdFirmware = [ordered]@{ exitCode = [int]$bcdExit; output = @($bcdOutput) }

    $localPathB = Join-Path $stateRoot 'assets\f8c356e7099f4bbe06fb52f98bc795ca6628e638cb283ddeb2ba3e1e7e48c639\path-b-collector.ps1'
    $inventoryState = Join-Path $stateRoot 'pathb-diagnostic\inventory'
    $metadata.esp.localPathBState = [ordered]@{ path = $inventoryState; exists = Test-Path -LiteralPath $inventoryState -PathType Container }
    foreach ($relativePath in @('path-b-stage.json')) {
        $localPath = Join-Path $inventoryState $relativePath
        if (Test-Path -LiteralPath $localPath -PathType Leaf) {
            $metadata.esp.localPathBState[$relativePath] = [ordered]@{ sha256 = Get-Sha256 $localPath; text = [IO.File]::ReadAllText($localPath) }
        } else { $metadata.esp.localPathBState[$relativePath] = [ordered]@{ exists = $false } }
    }
    $capturesPath = Join-Path $inventoryState 'captures'
    $metadata.esp.priorCollectorCaptures = @(
        Get-ChildItem -LiteralPath $capturesPath -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc | ForEach-Object {
            [ordered]@{ name = $_.Name; bytes = [long]$_.Length; sha256 = (Get-Sha256 $_.FullName) }
        }
    )

    $espRoot = 'Z:\'
    $mountvol = Join-Path $env:SystemRoot 'System32\mountvol.exe'
    if (Test-Path -LiteralPath $espRoot) {
        throw 'Reserved ESP diagnostic mount letter Z: is already in use; refusing to inspect or displace it.'
    }
    $mountOutput = @(& $mountvol 'Z:' '/S' 2>&1 | ForEach-Object { [string]$_ })
    $mountExit = $LASTEXITCODE
    $metadata.esp.mount = [ordered]@{ command = 'mountvol Z: /S'; exitCode = [int]$mountExit; output = @($mountOutput); mounted = ($mountExit -eq 0) }
    if ($mountExit -eq 0) {
        try {
            if (-not (Test-Path -LiteralPath $espRoot -PathType Container)) {
                $metadata.esp.readError = 'mountvol returned success but Z:\\ is not accessible.'
            } else {
                $guidOutput = @(& $mountvol 'Z:' '/L' 2>&1 | ForEach-Object { [string]$_ })
                $metadata.esp.volumeGuidOutput = @($guidOutput)
                $bootManagerPath = Join-Path $espRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
                $metadata.esp.bootManager = if (Test-Path -LiteralPath $bootManagerPath -PathType Leaf) {
                    [ordered]@{ path = 'EFI\Microsoft\Boot\bootmgfw.efi'; bytes = [long](Get-Item -LiteralPath $bootManagerPath).Length; sha256 = Get-Sha256 $bootManagerPath }
                } else { [ordered]@{ path = 'EFI\Microsoft\Boot\bootmgfw.efi'; exists = $false } }
                $metadata.esp.files = @(
                    (Read-EspEvidence $espRoot 'HypersinvSmbiosPatch.txt' -IncludeText),
                    (Read-EspEvidence $espRoot 'HypersinvEfiInventory.txt' -IncludeText),
                    (Read-EspEvidence $espRoot 'EFI\HypersinvInventory\manifest.sha256' -IncludeText),
                    (Read-EspEvidence $espRoot 'EFI\HypersinvInventory\loader.efi'),
                    (Read-EspEvidence $espRoot 'EFI\HypersinvInventory\inventory.efi'),
                    (Read-EspEvidence $espRoot 'EFI\HypersinvShimInventory\chain-status.txt' -IncludeText),
                    (Read-EspEvidence $espRoot 'EFI\HypersinvShimInventory\chain-diagnostic.txt' -IncludeText)
                )
            }
        }
        catch { $metadata.esp.readError = $_.Exception.ToString() }
        finally {
            $dismountOutput = @(& $mountvol 'Z:' '/D' 2>&1 | ForEach-Object { [string]$_ })
            $dismountExit = $LASTEXITCODE
            $metadata.esp.dismount = [ordered]@{ command = 'mountvol Z: /D'; exitCode = [int]$dismountExit; output = @($dismountOutput); dismounted = ($dismountExit -eq 0) }
            if ($dismountExit -ne 0) { throw ('Could not remove temporary diagnostic ESP mount Z:; exit=' + $dismountExit + '; output=' + ($dismountOutput -join ' ')) }
        }
    } else {
        $metadata.esp.readError = 'ESP mount failed; no ESP contents were read.'
    }
    if (Test-Path -LiteralPath $localPathB -PathType Leaf) { $metadata.esp.pathBSourceSha256 = Get-Sha256 $localPathB }

    $metadata.completedUtc = [DateTime]::UtcNow.ToString('o')
    $metadata.status = 'PASS'
    $result.Status = 'PASS'
    $result.Message = 'Read-only fresh SMBIOS, guest identity, EFI variables, BCD, and ESP diagnostic completed.'
}
catch {
    $failure = $_.Exception.ToString()
    $metadata.errors += @($failure)
    $result.Message = $failure
}
finally {
    $metadata.completedUtc = [DateTime]::UtcNow.ToString('o')
    $metadataJson = $metadata | ConvertTo-Json -Depth 30
    [IO.File]::WriteAllText((Join-Path $root 'kernel-metadata.json'), $metadataJson + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'stdout.txt'), (($output.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'stderr.txt'), $(if ($result.Status -eq 'PASS') { '' } else { [string]$result.Message + [Environment]::NewLine }), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'result.json'), ($result | ConvertTo-Json -Depth 12) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
if ($result.Status -cne 'PASS') { exit 1 }
Write-Output $result.Message
