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
$lifecycleId = '__LIFECYCLE_ID__'
$stateRoot = '__LIFECYCLE_STATE_ROOT__'
$fixtureManifestSha256 = '__FIXTURE_MANIFEST_SHA256__'
$fixtureBuildId = '__FIXTURE_BUILD_ID__'
$expectedVmxUuid = 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1'
$expectedDiskPnp = 'SCSI\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\5&25A13950&0&000000'
$expectedDiskSerial = '59F5_4E30_1778_CC3A_000C_2967_3817_6676.'
$expectedBootOrder = '050000000100020003000400'
$expectedBootManager = '25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2'
$pathBAssetSha256 = 'F8C356E7099F4BBE06FB52F98BC795CA6628E638CB283DDEB2BA3E1E7E48C639'
$loaderAssetSha256 = '24B321ECAABB33EC3DAD346F2E8941F0E035D87056712128410F75598FFFFE6A'
$installerAssetSha256 = 'DC52141BEB419FB8754A03640D2D3507A3D7521290728EAAF84A3DCE2F671949'
$collectorAssetSha256 = 'BD86CA73EFACE5B2195DDF0AEEA4BE9BBB2BCD2DF778D77F22DF2D9B82AB60F9'
$firmwareGuid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}'

$result = [ordered]@{
    SchemaVersion = 1
    RunId = Split-Path -Leaf $root
    Status = 'FAIL'
    Message = 'EFI-only Path-B stage/arm did not complete.'
    Action = $Action
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$metadata = [ordered]@{
    schema = 'combined-vm-pathb-diagnostic-stage-arm.v1'
    lifecycleId = $lifecycleId
    fixture = [ordered]@{ manifestSha256 = $fixtureManifestSha256; buildId = $fixtureBuildId; collectorSha256 = $collectorAssetSha256 }
    startedUtc = [DateTime]::UtcNow.ToString('o')
    status = 'FAIL'
    vm = [ordered]@{ expectedUuid = $expectedVmxUuid; observedUuid = $null; manufacturer = $null; model = $null; diskPnp = $null; diskSerial = $null }
    firmware = [ordered]@{ secureBoot = $null; secureBootVariable = $null; bootNextBefore = $null; bootNextAfterStage = $null; bootNextAfterArm = $null; boot0006Before = $null; armedEntryName = $null; armedEntryAfterArm = $null; bootOrderBefore = $null; bootOrderAfterStage = $null; bootOrderAfterArm = $null }
    esp = [ordered]@{ before = $null; afterStage = $null; afterArm = $null }
    sources = @()
    pathB = [ordered]@{ source = $null; workspace = $null; stageOutput = @(); stageExitCode = $null; armOutput = @(); armExitCode = $null; receipt = $null }
    errors = @()
    rebootRequested = $false
}
$output = [Collections.Generic.List[string]]::new()

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Assert-FileHash([string]$Path, [string]$Expected, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required cached input is missing: $Name ($Path)" }
    $actual = Get-Sha256 $Path
    $metadata.sources += @([ordered]@{ name = $Name; path = $Path; sha256 = $actual; expectedSha256 = $Expected; matches = ($actual -ceq $Expected) })
    if ($actual -cne $Expected) { throw "Cached input hash mismatch: $Name expected=$Expected actual=$actual" }
}

if (-not ('CombinedVmPathBFirmwareReader' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class CombinedVmPathBFirmwareReader {
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
            TokenPrivileges previous; uint previousLength; ClearLastError(0);
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

function Read-Firmware([string]$Name) {
    return [CombinedVmPathBFirmwareReader]::Read($Name, $firmwareGuid)
}

function Get-EspState {
    $rootLetter = 'Z:'
    if (Test-Path -LiteralPath ($rootLetter + '\')) { throw 'Reserved ESP mount Z: is already in use.' }
    $mountOutput = @(& (Join-Path $env:SystemRoot 'System32\mountvol.exe') $rootLetter '/S' 2>&1 | ForEach-Object { [string]$_ })
    $mountExit = $LASTEXITCODE
    if ($mountExit -ne 0 -or -not (Test-Path -LiteralPath ($rootLetter + '\') -PathType Container)) {
        throw ('ESP mount failed: exit=' + $mountExit + '; output=' + ($mountOutput -join ' | '))
    }
    try {
        $bootManager = Join-Path ($rootLetter + '\') 'EFI\Microsoft\Boot\bootmgfw.efi'
        if (-not (Test-Path -LiteralPath $bootManager -PathType Leaf)) { throw 'Windows Boot Manager is missing from the ESP.' }
        $stageRoot = Join-Path ($rootLetter + '\') 'EFI\HypersinvInventory'
        $stageFiles = @()
        if (Test-Path -LiteralPath $stageRoot -PathType Container) {
            $stageFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Force | ForEach-Object {
                [ordered]@{ name = $_.Name; bytes = [long]$_.Length; sha256 = Get-Sha256 $_.FullName }
            })
        }
        $reportFiles = @('HypersinvSmbiosPatch.txt', 'HypersinvEfiInventory.txt') | ForEach-Object {
            $p = Join-Path ($rootLetter + '\') $_
            if (Test-Path -LiteralPath $p -PathType Leaf) { [ordered]@{ name = $_; exists = $true; bytes = [long](Get-Item -LiteralPath $p).Length; sha256 = Get-Sha256 $p; text = [IO.File]::ReadAllText($p) } }
            else { [ordered]@{ name = $_; exists = $false; bytes = 0; sha256 = $null; text = $null } }
        }
        $volumeOutput = @(& (Join-Path $env:SystemRoot 'System32\mountvol.exe') $rootLetter '/L' 2>&1 | ForEach-Object { [string]$_ })
        return [ordered]@{
            volumeGuidOutput = @($volumeOutput)
            bootManagerSha256 = Get-Sha256 $bootManager
            stageFiles = @($stageFiles)
            stageReportFiles = @($reportFiles)
            stageDirectoryExists = Test-Path -LiteralPath $stageRoot -PathType Container
        }
    }
    finally {
        $dismountOutput = @(& (Join-Path $env:SystemRoot 'System32\mountvol.exe') $rootLetter '/D' 2>&1 | ForEach-Object { [string]$_ })
        if ($LASTEXITCODE -ne 0) { throw ('ESP dismount failed: ' + ($dismountOutput -join ' | ')) }
    }
}

try {
    if (-not $root.StartsWith($expectedRunRoot, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $root -PathType Container)) {
        throw 'RunRoot is outside the authenticated guest mailbox run directory.'
    }
    if ($Action -cne 'Probe' -or $ExpectedProbeVerdict -cne 'NOT_MET') { throw 'Unexpected fixed guest gate action or probe verdict.' }
    $identity = @(Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop)
    $product = @(Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop)
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq '\\.\PHYSICALDRIVE0' })
    if ($identity.Count -ne 1 -or $product.Count -ne 1 -or $diskRows.Count -ne 1) { throw 'VM identity/disk query cardinality changed.' }
    $metadata.vm.manufacturer = [string]$identity[0].Manufacturer
    $metadata.vm.model = [string]$identity[0].Model
    $metadata.vm.observedUuid = [string]$product[0].UUID
    $metadata.vm.diskPnp = [string]$diskRows[0].PNPDeviceID
    $metadata.vm.diskSerial = ([string]$diskRows[0].SerialNumber).Trim()
    if ($metadata.vm.manufacturer -cne 'VMware, Inc.' -or $metadata.vm.model -cne 'VMware20,1' -or $metadata.vm.observedUuid -cne $expectedVmxUuid) {
        throw ('Unexpected VM identity; expected VMware, Inc./VMware20,1/' + $expectedVmxUuid + '; actual=' + $metadata.vm.manufacturer + '/' + $metadata.vm.model + '/' + $metadata.vm.observedUuid)
    }
    if ($metadata.vm.diskPnp -cne $expectedDiskPnp -or $metadata.vm.diskSerial -cne $expectedDiskSerial) {
        throw ('Unexpected VM boot disk; expected=' + $expectedDiskPnp + '/' + $expectedDiskSerial + '; actual=' + $metadata.vm.diskPnp + '/' + $metadata.vm.diskSerial)
    }
    $secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop)
    $metadata.firmware.secureBoot = $secureBoot
    $metadata.firmware.secureBootVariable = Read-Firmware 'SecureBoot'
    $metadata.firmware.bootNextBefore = Read-Firmware 'BootNext'
    $metadata.firmware.bootOrderBefore = Read-Firmware 'BootOrder'
    $metadata.firmware.boot0006Before = Read-Firmware 'Boot0006'
    if ($secureBoot -ne $false -or $metadata.firmware.secureBootVariable.bytesHex -cne '00') { throw 'Secure Boot was not already OFF at the diagnostic baseline; refusing to continue.' }
    if ($metadata.firmware.bootNextBefore.supported -ne $false -or [int]$metadata.firmware.bootNextBefore.win32Error -ne 203) { throw 'BootNext was not absent before the diagnostic stage.' }
    if ($metadata.firmware.boot0006Before.supported -ne $false -or [int]$metadata.firmware.boot0006Before.win32Error -ne 203) { throw 'Boot0006 was not absent before the diagnostic stage.' }
    if ($metadata.firmware.bootOrderBefore.bytesHex -cne $expectedBootOrder) { throw ('BootOrder baseline mismatch: expected=' + $expectedBootOrder + ' actual=' + [string]$metadata.firmware.bootOrderBefore.bytesHex) }
    if ($metadata.firmware.bootOrderBefore.privilegeRestored -ne $true) { throw 'BootOrder reader privilege restoration failed.' }

    $bcdOutput = @(& (Join-Path $env:SystemRoot 'System32\bcdedit.exe') /enum '{current}' /v 2>&1 | ForEach-Object { [string]$_ })
    $bcdExit = $LASTEXITCODE
    if ($bcdExit -ne 0) { throw ('bcdedit current-object read failed: exit=' + $bcdExit + '; output=' + ($bcdOutput -join ' | ')) }
    if (@($bcdOutput | Where-Object { $_ -match '^\s*testsigning\s+Yes\s*$' }).Count -gt 0) { throw 'Current BCD object has testsigning enabled; refusing to change policy or proceed.' }
    $metadata.bcdCurrent = [ordered]@{ exitCode = $bcdExit; output = @($bcdOutput); testsigningYes = $false }

    $localRoot = Join-Path $stateRoot 'pathb-diagnostic'
    if (Test-Path -LiteralPath $localRoot) { throw "Diagnostic workspace already exists; refusing to overwrite it: $localRoot" }
    $assetRoot = Join-Path $stateRoot 'assets'
    $pathBSource = Join-Path $assetRoot ('f8c356e7099f4bbe06fb52f98bc795ca6628e638cb283ddeb2ba3e1e7e48c639\path-b-collector.ps1')
    $loaderSource = Join-Path $assetRoot ('24b321ecaabb33ec3dad346f2e8941f0e035d87056712128410f75598ffffe6a\inventory_loader.efi')
    $installerSource = Join-Path $assetRoot ('dc52141beb419fb8754a03640d2d3507a3d7521290728eaaf84a3dce2f671949\ib-install.exe')
    $collectorSource = Join-Path $assetRoot ('bd86ca73eface5b2195ddf0aeea4be9bbb2bcd2df778d77f22df2d9b82ab60f9\smbios-inventory.efi')
    Assert-FileHash $pathBSource $pathBAssetSha256 'path-b-collector.ps1'
    Assert-FileHash $loaderSource $loaderAssetSha256 'inventory_loader.efi'
    Assert-FileHash $installerSource $installerAssetSha256 'ib-install.exe'
    Assert-FileHash $collectorSource $collectorAssetSha256 'r5 inventory.efi'
    $metadata.pathB.source = [ordered]@{ path = $pathBSource; sha256 = $pathBAssetSha256 }

    $metadata.esp.before = Get-EspState
    if ($metadata.esp.before.bootManagerSha256 -cne $expectedBootManager) { throw ('Boot Manager hash mismatch before stage: expected=' + $expectedBootManager + ' actual=' + $metadata.esp.before.bootManagerSha256) }
    if ($metadata.esp.before.stageDirectoryExists -or @($metadata.esp.before.stageReportFiles | Where-Object exists -eq $true).Count -gt 0) {
        throw 'Path-B stage or prior collector report is already present before the diagnostic.'
    }

    $loaderBuild = Join-Path $localRoot 'loader'
    $inventoryBuild = Join-Path $localRoot 'inventory'
    $inventoryEfi = Join-Path $inventoryBuild 'efi'
    New-Item -ItemType Directory -Path $loaderBuild,$inventoryEfi -Force | Out-Null
    $pathBLocal = Join-Path $localRoot 'path-b-collector.ps1'
    Copy-Item -LiteralPath $pathBSource -Destination $pathBLocal
    Copy-Item -LiteralPath $loaderSource -Destination (Join-Path $loaderBuild 'inventory_loader.efi')
    Copy-Item -LiteralPath $installerSource -Destination (Join-Path $loaderBuild 'ib-install.exe')
    Copy-Item -LiteralPath $collectorSource -Destination (Join-Path $inventoryEfi 'inventory.efi')
    Assert-FileHash (Join-Path $loaderBuild 'inventory_loader.efi') $loaderAssetSha256 'local inventory_loader.efi'
    Assert-FileHash (Join-Path $loaderBuild 'ib-install.exe') $installerAssetSha256 'local ib-install.exe'
    Assert-FileHash (Join-Path $inventoryEfi 'inventory.efi') $collectorAssetSha256 'local r5 inventory.efi'
    if ((Get-Sha256 $pathBLocal) -cne $pathBAssetSha256) { throw 'Local Path-B script hash changed after staging.' }
    $metadata.pathB.workspace = $localRoot

    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $commonArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$pathBLocal,'-LoaderBuildDir',$loaderBuild,'-InventoryBuildDir',$inventoryBuild,'-MountLetter','Z')
    $stageOutput = @(& $powershell @commonArgs -Mode Stage 2>&1 | ForEach-Object { [string]$_ })
    $metadata.pathB.stageExitCode = [int]$LASTEXITCODE
    $metadata.pathB.stageOutput = @($stageOutput)
    $output.AddRange([string[]]$stageOutput)
    if ($metadata.pathB.stageExitCode -ne 0) { throw ('Path-B Stage failed: exit=' + $metadata.pathB.stageExitCode + '; output=' + ($stageOutput -join ' | ')) }
    $receiptPath = Join-Path $inventoryBuild 'path-b-stage.json'
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'Path-B Stage returned success without writing its stage receipt.' }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $metadata.pathB.receipt = [ordered]@{ path = $receiptPath; sha256 = Get-Sha256 $receiptPath; volumeGuid = [string]$receipt.volumeGuid; loaderSha256 = [string]$receipt.loaderSha256; inventorySha256 = [string]$receipt.inventorySha256; bootManagerSha256 = [string]$receipt.bootManagerSha256; stageDirectory = [string]$receipt.stageDirectory }
    if ([string]$receipt.loaderSha256 -cne $loaderAssetSha256 -or [string]$receipt.inventorySha256 -cne $collectorAssetSha256 -or [string]$receipt.bootManagerSha256 -cne $expectedBootManager) {
        throw 'Path-B receipt hashes do not match the approved loader, r5 payload, and original Windows Boot Manager.'
    }
    $metadata.esp.afterStage = Get-EspState
    $metadata.firmware.bootNextAfterStage = Read-Firmware 'BootNext'
    $metadata.firmware.bootOrderAfterStage = Read-Firmware 'BootOrder'
    if ($metadata.esp.afterStage.bootManagerSha256 -cne $expectedBootManager -or $metadata.firmware.bootNextAfterStage.supported -ne $false -or $metadata.firmware.bootOrderAfterStage.bytesHex -cne $expectedBootOrder) {
        throw 'Path-B Stage altered a protected baseline value or unexpectedly armed BootNext.'
    }
    $staged = @($metadata.esp.afterStage.stageFiles)
    if ($staged.Count -ne 3) { throw ('Path-B Stage file count mismatch: expected=3 actual=' + $staged.Count) }
    $expectedStageHashes = @{
        'loader.efi' = $loaderAssetSha256
        'inventory.efi' = $collectorAssetSha256
    }
    foreach ($name in @('loader.efi','inventory.efi')) {
        $match = @($staged | Where-Object { [string]$_.name -ceq $name })
        if ($match.Count -ne 1 -or [string]$match[0].sha256 -cne $expectedStageHashes[$name]) {
            $actualHash = if ($match.Count -eq 1) { [string]$match[0].sha256 } else { '<missing-or-duplicate>' }
            throw "Staged ESP file mismatch: $name expected=$($expectedStageHashes[$name]) actual=$actualHash"
        }
    }
    $manifestFile = @($staged | Where-Object { [string]$_.name -ceq 'manifest.sha256' })
    if ($manifestFile.Count -ne 1) { throw 'The exact owned manifest.sha256 file is missing or duplicated after staging.' }
    $metadata.esp.afterStageManifestSha256 = [string]$manifestFile[0].sha256

    $armOutput = @(& $powershell @commonArgs -Mode Arm 2>&1 | ForEach-Object { [string]$_ })
    $metadata.pathB.armExitCode = [int]$LASTEXITCODE
    $metadata.pathB.armOutput = @($armOutput)
    $output.AddRange([string[]]$armOutput)
    if ($metadata.pathB.armExitCode -ne 0) { throw ('Path-B Arm failed: exit=' + $metadata.pathB.armExitCode + '; output=' + ($armOutput -join ' | ')) }
    $metadata.esp.afterArm = Get-EspState
    $metadata.firmware.bootNextAfterArm = Read-Firmware 'BootNext'
    $metadata.firmware.bootOrderAfterArm = Read-Firmware 'BootOrder'
    if ($metadata.esp.afterArm.bootManagerSha256 -cne $expectedBootManager -or $metadata.firmware.bootOrderAfterArm.bytesHex -cne $expectedBootOrder) {
        throw 'Path-B Arm changed Windows Boot Manager or BootOrder.'
    }
    if ($metadata.firmware.bootNextAfterArm.supported -ne $true -or [int]$metadata.firmware.bootNextAfterArm.size -lt 2 -or $metadata.firmware.bootNextAfterArm.privilegeRestored -ne $true) {
        throw 'Path-B Arm returned success but BootNext did not contain a readable one-shot boot entry.'
    }
    $bootNextHex = [string]$metadata.firmware.bootNextAfterArm.bytesHex
    if ($bootNextHex -notmatch '\A[0-9A-Fa-f]{4}\z') { throw "BootNext does not contain an exact 16-bit entry number: $bootNextHex" }
    $bootEntryIndex = [Convert]::ToInt32(($bootNextHex.Substring(2,2) + $bootNextHex.Substring(0,2)), 16)
    $metadata.firmware.armedEntryName = 'Boot' + $bootEntryIndex.ToString('X4')
    $metadata.firmware.armedEntryAfterArm = Read-Firmware ([string]$metadata.firmware.armedEntryName)
    if ($metadata.firmware.armedEntryAfterArm.supported -ne $true -or [int]$metadata.firmware.armedEntryAfterArm.win32Error -ne 0) {
        throw ('BootNext references an unreadable firmware entry: ' + [string]$metadata.firmware.armedEntryName + '; win32Error=' + [string]$metadata.firmware.armedEntryAfterArm.win32Error)
    }
    if ($metadata.esp.afterArm.stageFiles.Count -ne 3) { throw 'Path-B stage file count after arm is not exactly the three owned files.' }
    $result.Status = 'PASS'
    $result.Message = 'Path-B staged the pinned r5 diagnostic payload and armed one BootNext entry; no policy or NIC/storage driver changes were made.'
    $metadata.status = 'PASS'
}
catch {
    $failure = $_.Exception.ToString()
    $metadata.errors += @($failure)
    $result.Message = $failure
}
finally {
    $metadata.completedUtc = [DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText((Join-Path $root 'kernel-metadata.json'), ($metadata | ConvertTo-Json -Depth 24) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'stdout.txt'), (($output.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'stderr.txt'), $(if ($result.Status -eq 'PASS') { '' } else { [string]$result.Message + [Environment]::NewLine }), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'result.json'), ($result | ConvertTo-Json -Depth 8) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
if ($result.Status -cne 'PASS') { exit 1 }
Write-Output $result.Message
