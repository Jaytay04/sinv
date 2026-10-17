param([Parameter(Mandatory=$true)][string]$RepoRoot,[Parameter(Mandatory=$true)][string]$OutputBoundary,[string]$SnapshotDirectory,[string]$ReferenceReceiptPath,[switch]$LibraryOnly)
$auditLibraryOnly=$LibraryOnly
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRepo = [IO.Path]::GetFullPath($RepoRoot)
Set-Location -LiteralPath $taskRepo
. (Join-Path $taskRepo 'tools\efi_inventory\shim-chain-collector.ps1') -LibraryOnly
if($ReferenceReceiptPath){
    $referenceReceipt=[IO.Path]::GetFullPath($ReferenceReceiptPath)
    Assert-NoReparseAncestors $referenceReceipt $OutputBoundary
    if(-not(Test-Path -LiteralPath $referenceReceipt -PathType Leaf) -or (Test-ReparsePoint $referenceReceipt)){throw 'The reference receipt must be an existing plain file inside the handoff package.'}
    $script:ShimReceiptPath=$referenceReceipt
}
if (-not $auditLibraryOnly) {
    Assert-Admin
    if (-not $SnapshotDirectory) {
        $SnapshotDirectory = Join-Path $PSScriptRoot ('recovery.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
    }
    $SnapshotDirectory = [IO.Path]::GetFullPath($SnapshotDirectory)
    Assert-NoReparseAncestors $SnapshotDirectory $OutputBoundary
    if (Test-Path -LiteralPath $SnapshotDirectory) { throw 'Snapshot destination must be new.' }
    New-Item -ItemType Directory -Path $SnapshotDirectory | Out-Null
    $taskTemp=Join-Path $SnapshotDirectory 'temporary-verifier-files'
    New-Item -ItemType Directory -Path $taskTemp | Out-Null
    $env:TEMP=$taskTemp
    $env:TMP=$taskTemp
    $auditLog = Join-Path $SnapshotDirectory 'audit.log'
}
$mountedEsp = $null
$exitCode = 1
function Save-Json($Value,[string]$Name) {
    [IO.File]::WriteAllText((Join-Path $SnapshotDirectory $Name),($Value | ConvertTo-Json -Depth 15),[Text.Encoding]::UTF8)
}
function Write-Audit([string]$Message) {
    Add-Content -LiteralPath $auditLog -Value $Message
}
function Initialize-ReadyFirmwareReader {
if ('ReadyToArmFirmwareRead' -as [type]) { return }
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class ReadyToArmFirmwareRead {
    [StructLayout(LayoutKind.Sequential)] struct LUID { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)] struct TOKEN_PRIVILEGES { public uint Count; public LUID Luid; public uint Attributes; }
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr p, uint a, out IntPtr t);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool LookupPrivilegeValue(string s, string n, out LUID l);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool AdjustTokenPrivileges(IntPtr t, bool d, ref TOKEN_PRIVILEGES p, uint n, IntPtr q, IntPtr r);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern uint GetFirmwareEnvironmentVariableEx(string n,string g,byte[] b,uint s,out uint a);
    public static void Enable() {
        IntPtr t;
        if (!OpenProcessToken(GetCurrentProcess(),0x28,out t)) throw new Win32Exception();
        try {
            LUID l;
            if (!LookupPrivilegeValue(null,"SeSystemEnvironmentPrivilege",out l)) throw new Win32Exception();
            TOKEN_PRIVILEGES p = new TOKEN_PRIVILEGES { Count=1, Luid=l, Attributes=2 };
            if (!AdjustTokenPrivileges(t,false,ref p,0,IntPtr.Zero,IntPtr.Zero) || Marshal.GetLastWin32Error()!=0) throw new Win32Exception();
        } finally { CloseHandle(t); }
    }
    public static byte[] Read(string name,string guid,out uint attributes,out int error) {
        byte[] b = new byte[1024*1024];
        uint n = GetFirmwareEnvironmentVariableEx(name,guid,b,(uint)b.Length,out attributes);
        if(n==0) { error=Marshal.GetLastWin32Error(); return null; }
        error=0;
        Array.Resize(ref b,(int)n);
        return b;
    }
}
'@
}
$globalGuid = '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
$imageGuid = '{D719B2CB-3D3A-4596-A3BC-DAD00E67656F}'
$shimGuid = '{605DAB50-E046-4300-ABB6-3DD810DD8B23}'
$firmwareRecords = [Collections.Generic.List[object]]::new()
function Read-VariableRecord([string]$Name,[string]$Guid) {
    [uint32]$attributes=0
    [int]$readError=0
    $bytes=[ReadyToArmFirmwareRead]::Read($Name,$Guid,[ref]$attributes,[ref]$readError)
    $record=[ordered]@{name=$Name;guid=$Guid;attributes=$attributes;present=($null -ne $bytes);win32Error=$readError;size=0;sha256=$null;base64=$null}
    if($null -ne $bytes) {
        $record.size=$bytes.Length
        $record.base64=[Convert]::ToBase64String($bytes)
        $hash=[Security.Cryptography.SHA256]::Create()
        try { $record.sha256=[BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-','') }
        finally { $hash.Dispose() }
    }
    $object=[pscustomobject]$record
    $firmwareRecords.Add($object)
    return $object
}
function Read-ReadyBootEntries([object]$Order,[object]$Next,[object]$Current,[string]$Guid) {
    if(-not $Order.present -or $Order.win32Error -ne 0){throw 'BootOrder could not be read safely.'}
    $orderBytes=[Convert]::FromBase64String($Order.base64)
    if($orderBytes.Length -eq 0 -or ($orderBytes.Length % 2) -ne 0){throw 'BootOrder is malformed.'}
    $references=@{}
    for($i=0;$i -lt $orderBytes.Length;$i+=2){
        $number=[int][BitConverter]::ToUInt16($orderBytes,$i)
        if(-not $references.ContainsKey($number)){$references[$number]=@()}
        if($references[$number] -notcontains 'BootOrder'){$references[$number]+='BootOrder'}
    }
    foreach($selection in @($Next,$Current)) {
        if(-not $selection.present){
            if($selection.win32Error -ne 203){throw ($selection.name+' could not be read safely (Win32 '+$selection.win32Error+').')}
            continue
        }
        if($selection.win32Error -ne 0){throw ($selection.name+' returned inconsistent read status.')}
        $selectionBytes=[Convert]::FromBase64String($selection.base64)
        if($selectionBytes.Length -ne 2){throw ($selection.name+' is malformed.')}
        $number=[int][BitConverter]::ToUInt16($selectionBytes,0)
        if(-not $references.ContainsKey($number)){$references[$number]=@()}
        if($references[$number] -notcontains $selection.name){$references[$number]+=$selection.name}
    }
    foreach($number in @($references.Keys | Sort-Object)) {
        $record=Read-VariableRecord ('Boot{0:X4}' -f $number) $Guid
        $sources=@($references[$number])
        $required=($sources -contains 'BootOrder' -or $sources -contains 'BootNext')
        if(-not $record.present){
            # BootCurrent describes this boot, even if Collect removed its one-time option.
            # A missing historical target is evidence to preserve, not an option to recreate.
            if($required -or $record.win32Error -ne 203){
                throw ('Referenced boot entry unreadable: '+$record.name+' (via '+($sources -join ',')+'; Win32 '+$record.win32Error+').')
            }
            Write-Audit ('NOTE: '+$record.name+' is absent (Win32 203), referenced only by BootCurrent; preserving the current state.')
            [pscustomobject]@{variable=$record.name;present=$false;win32Error=$record.win32Error;referencedBy=$sources;description=$null;active=$false;unicodeData=$null}
            continue
        }
        if($record.win32Error -ne 0){throw ('Inconsistent boot entry read status: '+$record.name)}
        $bytes=[Convert]::FromBase64String($record.base64)
        if($bytes.Length -lt 8){throw ('Malformed boot entry: '+$record.name)}
        $end=6
        while($end+1 -lt $bytes.Length -and ($bytes[$end] -ne 0 -or $bytes[$end+1] -ne 0)){$end+=2}
        if($end+1 -ge $bytes.Length -or [BitConverter]::ToUInt16($bytes,4) -gt $bytes.Length-($end+2)){
            throw ('Malformed boot entry description or device path: '+$record.name)
        }
        $description=[Text.Encoding]::Unicode.GetString($bytes,6,$end-6)
        [pscustomobject]@{variable=$record.name;present=$true;win32Error=0;referencedBy=$sources;description=$description;active=(([BitConverter]::ToUInt32($bytes,0) -band 1) -ne 0);unicodeData=[Text.Encoding]::Unicode.GetString($bytes,6,$bytes.Length-6)}
    }
}
function Assert-UnchangedVariables {
    foreach($record in $firmwareRecords) {
        [uint32]$attributes=0
        [int]$readError=0
        $bytes=[ReadyToArmFirmwareRead]::Read($record.name,$record.guid,[ref]$attributes,[ref]$readError)
        $encoded=if($null -ne $bytes){[Convert]::ToBase64String($bytes)}else{$null}
        if($readError -ne $record.win32Error -or $attributes -ne $record.attributes -or $encoded -cne $record.base64) {
            throw ('Firmware changed during snapshot: '+$record.name)
        }
    }
}
function Test-WritableDirectory([string]$Directory) {
    $probe=Join-Path $Directory ('.hypersinv-write-probe.'+[Guid]::NewGuid().ToString('N'))
    $data=[Text.Encoding]::ASCII.GetBytes('Hypersinv reversible preflight write probe')
    try {
        $stream=[IO.File]::Open($probe,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $stream.Write($data,0,$data.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        if([Convert]::ToBase64String([IO.File]::ReadAllBytes($probe)) -cne [Convert]::ToBase64String($data)) { throw 'Write probe readback mismatch.' }
    } finally { if(Test-Path -LiteralPath $probe -PathType Leaf){Remove-Item -LiteralPath $probe -Force} }
}
if ($auditLibraryOnly) { return }
try {
    Write-Audit ('START '+[DateTime]::UtcNow.ToString('o'))
    $secureBoot=[bool](Confirm-SecureBootUEFI)
    if(-not $secureBoot){throw 'Secure Boot is not enabled.'}
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $order=Read-VariableRecord 'BootOrder' $globalGuid
    $next=Read-VariableRecord 'BootNext' $globalGuid
    $current=Read-VariableRecord 'BootCurrent' $globalGuid
    $bootEntries=@(Read-ReadyBootEntries $order $next $current $globalGuid)
    foreach($name in @('SecureBoot','SetupMode','AuditMode','DeployedMode','PK','KEK')){$null=Read-VariableRecord $name $globalGuid}
    foreach($name in @('db','dbx')){$null=Read-VariableRecord $name $imageGuid}
    foreach($name in @('MokListRT','MokListXRT','MokSBStateRT','SbatLevelRT','SbatLevel','SbatPolicy','MokIgnoreDB')){$null=Read-VariableRecord $name $shimGuid}
    Save-Json $firmwareRecords.ToArray() 'firmware-variables.json'
    Save-Json $bootEntries 'boot-entries.json'
    $bcdBefore=@(& bcdedit.exe /enum firmware /v)
    if($LASTEXITCODE -ne 0){throw 'Firmware BCD enumeration failed.'}
    [IO.File]::WriteAllLines((Join-Path $SnapshotDirectory 'bcd-firmware.txt'),[string[]]$bcdBefore)
    $bcdAll=@(& bcdedit.exe /enum all /v)
    if($LASTEXITCODE -ne 0){throw 'BCD enumeration failed.'}
    [IO.File]::WriteAllLines((Join-Path $SnapshotDirectory 'bcd-all.txt'),[string[]]$bcdAll)
    $exportOutput=@(& bcdedit.exe /export (Join-Path $SnapshotDirectory 'system-bcd.export'))
    Write-Audit ($exportOutput -join "`n")
    if($LASTEXITCODE -ne 0){throw 'BCD export failed.'}
    $hostRecord=[ordered]@{
        baseBoard=Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer,Product,Version
        bios=Get-CimInstance Win32_BIOS | Select-Object Manufacturer,SMBIOSBIOSVersion,ReleaseDate
        secureBoot=$secureBoot
        disks=Get-Disk | Select-Object Number,FriendlyName,UniqueId,PartitionStyle,IsBoot,IsSystem
        partitions=Get-Partition | Select-Object DiskNumber,PartitionNumber,Guid,GptType,Offset,Size,IsBoot,IsSystem,AccessPaths
        volumes=Get-Volume | Select-Object DriveLetter,FileSystemLabel,FileSystem,DriveType,Size,SizeRemaining,Path,UniqueId
        bitLocker=Get-BitLockerVolume -MountPoint $env:SystemDrive | Select-Object MountPoint,VolumeStatus,ProtectionStatus,EncryptionPercentage
    }
    Save-Json $hostRecord 'host-state.json'
    $mountedEsp=Mount-Esp
    $stage=Get-MountedShimStageStatus $mountedEsp.EspRoot
    $receipt=Read-ShimReceipt
    Assert-ShimReceipt $receipt $stage $mountedEsp.VolumeGuid ''
    if($next.present){throw 'BootNext already exists; snapshot saved but chain is not ready to arm.'}
    if((Test-Path -LiteralPath $stage.Paths.Report) -or (Test-Path -LiteralPath $stage.Paths.ChainStatus)){throw 'Prior output exists; cannot declare ready.'}
    $espBackup=Join-Path $SnapshotDirectory 'ESP'
    New-Item -ItemType Directory -Path $espBackup | Out-Null
    $layout=[Collections.Generic.List[object]]::new()
    $items=@(Get-ChildItem -LiteralPath $mountedEsp.EspRoot -Force)
    $skipped=@()
    foreach($rootItem in $items) {
        if($rootItem.Name -eq 'System Volume Information'){$skipped+='System Volume Information (filesystem metadata)';continue}
        $tree=if($rootItem.PSIsContainer){@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)}else{@($rootItem)}
        foreach($item in $tree) {
            if(Test-ReparsePoint $item.FullName){throw ('ESP contains reparse point: '+$item.FullName)}
            $relative=$item.FullName.Substring($mountedEsp.EspRoot.Length)
            $destination=Join-Path $espBackup $relative
            if($item.PSIsContainer){
                New-Item -ItemType Directory -Path $destination -Force | Out-Null
                $layout.Add([pscustomobject]@{path=$relative;directory=$true;size=0;sha256=$null})
            }else{
                if($relative -match '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$') {
                    $layout.Add([pscustomobject]@{path=$relative;directory=$false;size=$item.Length;sha256=$null;backup='system-bcd.export (bcdedit consistent export; live hive/logs are locked)'})
                    continue
                }
                $sourceHash=Get-Sha256 $item.FullName
                Copy-Item -LiteralPath $item.FullName -Destination $destination
                if((Get-Sha256 $destination) -cne $sourceHash -or (Get-Sha256 $item.FullName) -cne $sourceHash){throw ('ESP snapshot changed: '+$relative)}
                $layout.Add([pscustomobject]@{path=$relative;directory=$false;size=$item.Length;sha256=$sourceHash})
            }
        }
    }
    Save-Json $layout.ToArray() 'esp-layout.json'
    $captureDir=Join-Path $SnapshotDirectory 'capture-write-probe'
    Assert-NoReparseAncestors $captureDir $OutputBoundary
    New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
    Test-WritableDirectory $captureDir
    Test-WritableDirectory $mountedEsp.EspRoot
    $scanner=Join-Path $script:InventoryBuild 'efi_inventory.exe'
    if(-not (Test-Path -LiteralPath $scanner -PathType Leaf)){throw 'Collector merge scanner missing.'}
    $null=Get-InstallTool
    Assert-UnchangedVariables
    $bcdAfter=@(& bcdedit.exe /enum firmware /v)
    if($LASTEXITCODE -ne 0){throw 'Mounted firmware BCD enumeration failed.'}
    [IO.File]::WriteAllLines((Join-Path $SnapshotDirectory 'bcd-firmware-mounted.txt'),[string[]]$bcdAfter)
    if(-not (Confirm-SecureBootUEFI) -or (Get-BootManagerHash $mountedEsp.EspRoot) -cne $stage.BootHash){throw 'Secure Boot or Windows Boot Manager changed.'}
    $summary=[ordered]@{
        schema=1;capturedUtc=[DateTime]::UtcNow.ToString('o');repoRoot=$taskRepo;volumeGuid=$mountedEsp.VolumeGuid
        secureBoot=$true;bootNextPresent=$false;bootCurrent=$current;bootOrder=$order;bootEntries=$bootEntries
        bootManagerSha256=$stage.BootHash;stageDirectory='EFI\HypersinvShimInventory'
        stageFiles=$stage.Sources.Files;manifestSha256=Get-Sha256 (Join-Path $stage.Paths.StageRoot 'manifest.sha256')
        espReportPath='\HypersinvEfiInventory.txt';chainStatusPath='\EFI\HypersinvShimInventory\chain-status.txt'
        captureDirectory=$captureDir;espWriteProbePassed=$true;captureWriteProbePassed=$true;scannerSha256=Get-Sha256 $scanner
        installerSha256=Get-Sha256 (Get-InstallTool);espSnapshotFileCount=@($layout | Where-Object {-not $_.directory}).Count
        skippedEspMetadata=$skipped;firmwareAndBootManagerUnchanged=$true;readyToArm=$true
    }
    Save-Json $summary 'snapshot.json'
    foreach($evidence in @('shim-chain-stage.json','shim-chain-build.log','path-b-collector.tests.ps1.log','shim-chain-collector.tests.ps1.log')) {
        Copy-Item -LiteralPath (Join-Path $script:InventoryBuild $evidence) -Destination (Join-Path $SnapshotDirectory $evidence)
    }
    Copy-Item -LiteralPath (Join-Path $script:SignedDirectory 'signed-source.json') -Destination (Join-Path $SnapshotDirectory 'signed-source.json')
    Write-Audit ('PASS: live stage, Secure Boot ON, BootNext absent, full boot snapshot, both write probes, unchanged firmware and Boot Manager. Snapshot='+$SnapshotDirectory)
    $exitCode=0
}catch {
    $failureText='FAILED: '+($_ | Out-String)
    Write-Audit $failureText
    [Console]::Error.WriteLine($failureText)
    [Console]::Error.WriteLine('Snapshot audit: '+$auditLog)
    # Keep the observed boot variables even when preflight stops early.
    Save-Json $firmwareRecords.ToArray() 'firmware-variables.partial.json'
}finally {
    if($mountedEsp){
        Dismount-Esp $mountedEsp
        if(Test-Path -LiteralPath $mountedEsp.EspRoot){
            Write-Audit 'FAILED: temporary ESP mount was not removed.'
            $exitCode=1
        }
    }
    if($exitCode -eq 0){
        $bcdUnmounted=@(& bcdedit.exe /enum firmware /v)
        if($LASTEXITCODE -ne 0){
            Write-Audit 'FAILED: firmware BCD enumeration after ESP unmount failed.'
            $exitCode=1
        }else{
            [IO.File]::WriteAllLines((Join-Path $SnapshotDirectory 'bcd-firmware-after.txt'),[string[]]$bcdUnmounted)
            if(($bcdUnmounted -join "`n") -cne ($bcdBefore -join "`n")){
                Write-Audit 'FAILED: firmware BCD listing changed between unmounted baseline and unmounted readback.'
                $exitCode=1
            }
        }
    }
    Write-Audit ('EXIT='+$exitCode)
    [IO.File]::WriteAllText((Join-Path $SnapshotDirectory 'exit-code.txt'),[string]$exitCode)
}
exit $exitCode
