param(
    [ValidateSet('Build','Stage','Status','Arm','Collect','Disarm')]
    [string]$Mode = 'Status',
    [string]$LoaderBuildDir = '.codex-build/efi_inventory_loader',
    [string]$InventoryBuildDir = '.codex-build/efi_inventory',
    [ValidatePattern('^[A-Za-z]$')]
    [string]$MountLetter = 'S',
    [string]$TestRoot,
    [switch]$LibraryOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([IO.Path]::IsPathRooted($LoaderBuildDir)) {
    $script:LoaderBuild = [IO.Path]::GetFullPath($LoaderBuildDir)
} else {
    $script:LoaderBuild = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $LoaderBuildDir))
}
if ([IO.Path]::IsPathRooted($InventoryBuildDir)) {
    $script:InventoryBuild = [IO.Path]::GetFullPath($InventoryBuildDir)
} else {
    $script:InventoryBuild = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $InventoryBuildDir))
}
$script:MountLetter = $MountLetter.ToUpperInvariant()
$script:ReceiptPath = Join-Path $script:InventoryBuild 'path-b-stage.json'
$script:OwnedNames = @('loader.efi','inventory.efi','manifest.sha256')
$script:EspGuidType = '{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}'
$script:BootManagerRelative = 'EFI\Microsoft\Boot\bootmgfw.efi'
$script:ReportName = 'HypersinvEfiInventory.txt'

function Resolve-RepoPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $Path))
}

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'This operation requires an elevated PowerShell window.'
    }
}

function Test-ReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $item = Get-Item -LiteralPath $Path -Force
    return (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Assert-NoReparseAncestors([string]$Path, [string]$Boundary) {
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetFullPath($Boundary).TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside its approved root: $full"
    }
    $current = $full
    while ($current.Length -ge $root.Length) {
        if (Test-Path -LiteralPath $current) {
            if (Test-ReparsePoint $current) { throw "Refusing reparse point in staging path: $current" }
        }
        $parentInfo = [IO.Directory]::GetParent($current)
        if (-not $parentInfo) { break }
        $current = $parentInfo.FullName
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-EfiApplication([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing EFI application: $Path" }
    if (Test-ReparsePoint $Path) { throw "EFI source is a reparse point: $Path" }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Invalid DOS/PE image header: $Path"
    }
    $peOffset = [BitConverter]::ToUInt32($bytes, 0x3C)
    if ($peOffset -gt ($bytes.Length - 24)) { throw "Truncated PE header: $Path" }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "Missing PE signature: $Path"
    }
    $machine = [BitConverter]::ToUInt16($bytes, [int]$peOffset + 4)
    $optionalSize = [BitConverter]::ToUInt16($bytes, [int]$peOffset + 20)
    $optionalOffset = [int]$peOffset + 24
    if ($machine -ne 0x8664 -or $optionalSize -lt 70 -or
        ($optionalOffset + $optionalSize) -gt $bytes.Length) {
        throw "EFI image must be a complete x64 PE32+ application: $Path"
    }
    $magic = [BitConverter]::ToUInt16($bytes, $optionalOffset)
    $subsystem = [BitConverter]::ToUInt16($bytes, $optionalOffset + 68)
    if ($magic -ne 0x20B -or $subsystem -ne 10) {
        throw "EFI image must use PE32+ and EFI_APPLICATION subsystem: $Path"
    }
}

function Get-StageSources {
    $loader = Join-Path $script:LoaderBuild 'inventory_loader.efi'
    $collector = Join-Path (Join-Path $script:InventoryBuild 'efi') 'inventory.efi'
    Assert-NoReparseAncestors $loader ([IO.Path]::GetPathRoot($loader))
    Assert-NoReparseAncestors $collector ([IO.Path]::GetPathRoot($collector))
    Assert-EfiApplication $loader
    Assert-EfiApplication $collector
    return @{
        Loader = $loader
        Collector = $collector
        LoaderSize = (Get-Item -LiteralPath $loader).Length
        CollectorSize = (Get-Item -LiteralPath $collector).Length
        LoaderHash = Get-Sha256 $loader
        CollectorHash = Get-Sha256 $collector
    }
}

function Assert-TestRoot([string]$Path) {
    if (-not $Path) { throw '-TestRoot requires an existing local non-root directory.' }
    $full = [IO.Path]::GetFullPath($Path)
    $uncPrefix = [string][char]92 + [string][char]92
    if ($full.StartsWith($uncPrefix)) { throw '-TestRoot must be a local directory.' }
    $driveRoot = [IO.Path]::GetPathRoot($full)
    if ($full.TrimEnd([char]92) -eq $driveRoot.TrimEnd([char]92)) {
        throw '-TestRoot cannot be a drive root.'
    }
    if (-not (Test-Path -LiteralPath $full -PathType Container)) {
        throw "-TestRoot must already exist: $full"
    }
    if (Test-ReparsePoint $full) { throw '-TestRoot cannot be a reparse point.' }
    Assert-NoReparseAncestors $full $driveRoot
    return $full
}
function Get-BootManagerHash([string]$EspRoot) {
    $path = Join-Path $EspRoot $script:BootManagerRelative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows Boot Manager is missing from the ESP: $path"
    }
    if (Test-ReparsePoint $path) { throw "Windows Boot Manager is a reparse point: $path" }
    return Get-Sha256 $path
}

function Assert-StageDirectory([string]$EspRoot, [switch]$AllowReport) {
    $efiRoot = Join-Path $EspRoot 'EFI'
    $stageRoot = Join-Path $efiRoot 'HypersinvInventory'
    Assert-NoReparseAncestors $efiRoot $EspRoot
    if ((Test-Path -LiteralPath $efiRoot) -and
        -not (Test-Path -LiteralPath $efiRoot -PathType Container)) {
        throw 'ESP EFI path exists but is not a directory.'
    }
    if (Test-Path -LiteralPath $stageRoot) {
        if (-not (Test-Path -LiteralPath $stageRoot -PathType Container)) {
            throw 'Inventory stage path exists but is not a directory.'
        }
        Assert-NoReparseAncestors $stageRoot $EspRoot
        $actual = @(Get-ChildItem -LiteralPath $stageRoot -Force)
        $unknown = @($actual | Where-Object { $script:OwnedNames -notcontains $_.Name })
        if ($unknown.Count -gt 0) {
            throw "Unknown content in owned inventory stage directory: $($unknown[0].Name)"
        }
        foreach ($item in $actual) {
            if ($item.PSIsContainer -or (Test-ReparsePoint $item.FullName)) {
                throw "Owned stage entries must be regular files: $($item.FullName)"
            }
        }
    }
    $report = Join-Path $EspRoot $script:ReportName
    if (Test-Path -LiteralPath $report) {
        if (-not $AllowReport) {
            throw "Existing root inventory report must be collected first: $report"
        }
        if (-not (Test-Path -LiteralPath $report -PathType Leaf) -or (Test-ReparsePoint $report)) {
            throw "Existing root inventory report is not a safe regular file: $report"
        }
    }
    return @{ EfiRoot = $efiRoot; StageRoot = $stageRoot; Report = $report }
}
function New-Manifest([hashtable]$Sources) {
    $nl = [string][char]13 + [char]10
    return ('schema=1' + $nl +
        ('loader.efi size={0} sha256={1}' -f $Sources.LoaderSize,$Sources.LoaderHash) + $nl +
        ('inventory.efi size={0} sha256={1}' -f $Sources.CollectorSize,$Sources.CollectorHash) + $nl)
}

function Assert-ExistingStageOwned([string]$StageRoot) {
    $actual = @(Get-ChildItem -LiteralPath $StageRoot -Force)
    if ($actual.Count -ne $script:OwnedNames.Count) {
        throw 'Existing inventory stage is incomplete or contains unowned files; refusing replacement.'
    }
    foreach ($name in $script:OwnedNames) {
        $path = Join-Path $StageRoot $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Test-ReparsePoint $path)) {
            throw "Existing inventory stage is incomplete or unsafe: $path"
        }
    }
    $loaderPath = Join-Path $StageRoot 'loader.efi'
    $collectorPath = Join-Path $StageRoot 'inventory.efi'
    Assert-EfiApplication $loaderPath
    Assert-EfiApplication $collectorPath
    $prior = @{
        LoaderSize = (Get-Item -LiteralPath $loaderPath).Length
        LoaderHash = Get-Sha256 $loaderPath
        CollectorSize = (Get-Item -LiteralPath $collectorPath).Length
        CollectorHash = Get-Sha256 $collectorPath
    }
    $expected = New-Manifest $prior
    $manifest = [IO.File]::ReadAllText((Join-Path $StageRoot 'manifest.sha256'), [Text.Encoding]::ASCII)
    if ($manifest -cne $expected) {
        throw 'Existing inventory stage does not match its exact schema=1 manifest; refusing replacement.'
    }
    return $prior
}

function Save-LocalStageBackup([string]$StageRoot, [hashtable]$Prior) {
    New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
    Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
    $name = 'path-b-backup.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
    $backup = Join-Path $script:InventoryBuild $name
    Assert-NoReparseAncestors $backup $script:InventoryBuild
    New-Item -ItemType Directory -Path $backup | Out-Null
    try {
        foreach ($fileName in $script:OwnedNames) {
            Copy-Item -LiteralPath (Join-Path $StageRoot $fileName) -Destination (Join-Path $backup $fileName)
        }
        Assert-StageFiles $backup $Prior
        return $backup
    } catch {
        if (Test-Path -LiteralPath $backup) { Remove-OwnedStage $backup }
        throw
    }
}

function Assert-StageFiles([string]$StageRoot, [hashtable]$Sources) {
    foreach ($name in $script:OwnedNames) {
        $path = Join-Path $StageRoot $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Test-ReparsePoint $path)) {
            throw "Missing or unsafe staged file: $path"
        }
    }
    Assert-EfiApplication (Join-Path $StageRoot 'loader.efi')
    Assert-EfiApplication (Join-Path $StageRoot 'inventory.efi')
    if ((Get-Item -LiteralPath (Join-Path $StageRoot 'loader.efi')).Length -ne $Sources.LoaderSize -or
        (Get-Sha256 (Join-Path $StageRoot 'loader.efi')) -ne $Sources.LoaderHash) {
        throw 'Staged loader.efi does not match the build artifact.'
    }
    if ((Get-Item -LiteralPath (Join-Path $StageRoot 'inventory.efi')).Length -ne $Sources.CollectorSize -or
        (Get-Sha256 (Join-Path $StageRoot 'inventory.efi')) -ne $Sources.CollectorHash) {
        throw 'Staged inventory.efi does not match the build artifact.'
    }
    $expected = New-Manifest $Sources
    $manifest = [IO.File]::ReadAllText((Join-Path $StageRoot 'manifest.sha256'), [Text.Encoding]::ASCII)
    if ($manifest -cne $expected) {
        throw 'Staged manifest does not match the exact schema=1 format.'
    }
}
function Write-Receipt([string]$VolumeGuid, [string]$EspRoot, [string]$BootHash, [hashtable]$Sources, [string]$TestRootValue) {
    New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
    Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
    if (Test-ReparsePoint $script:ReceiptPath) { throw "Stage receipt is a reparse point: $script:ReceiptPath" }
    $receipt = [ordered]@{
        schema = 1
        stagedUtc = [DateTime]::UtcNow.ToString('o')
        volumeGuid = $VolumeGuid
        testRoot = $TestRootValue
        espRoot = $EspRoot
        bootManagerSha256 = $BootHash
        loaderSize = $Sources.LoaderSize
        loaderSha256 = $Sources.LoaderHash
        inventorySize = $Sources.CollectorSize
        inventorySha256 = $Sources.CollectorHash
        stageDirectory = Join-Path (Join-Path $EspRoot 'EFI') 'HypersinvInventory'
    }
    $json = $receipt | ConvertTo-Json -Depth 4
    $temp = $script:ReceiptPath + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    Assert-NoReparseAncestors $temp $script:InventoryBuild
    [IO.File]::WriteAllText($temp, $json + [Environment]::NewLine, [Text.Encoding]::UTF8)
    Move-Item -LiteralPath $temp -Destination $script:ReceiptPath -Force
}
function Read-Receipt {
    Assert-NoReparseAncestors $script:ReceiptPath ([IO.Path]::GetPathRoot($script:ReceiptPath))
    if (Test-ReparsePoint $script:ReceiptPath) { throw "Stage receipt is a reparse point: $script:ReceiptPath" }
    if (-not (Test-Path -LiteralPath $script:ReceiptPath -PathType Leaf)) {
        throw "No Path B stage receipt exists at $script:ReceiptPath. Run Stage first."
    }
    $receipt = Get-Content -LiteralPath $script:ReceiptPath -Raw | ConvertFrom-Json
    if ($receipt.schema -ne 1 -or -not $receipt.volumeGuid -or -not $receipt.stageDirectory -or
        [string]$receipt.bootManagerSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        [string]$receipt.loaderSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        [string]$receipt.inventorySha256 -notmatch '^[A-Fa-f0-9]{64}$') {
        throw "Invalid Path B stage receipt: $script:ReceiptPath"
    }
    return $receipt
}

function Invoke-MountEsp {
    Assert-Admin
    $mount = '{0}:' -f $script:MountLetter
    if (Test-Path -LiteralPath ($mount + [string][char]92)) {
        throw "Mount letter $mount is already in use."
    }
    $mountvol = Join-Path $env:SystemRoot ('System32' + [IO.Path]::DirectorySeparatorChar + 'mountvol.exe')
    & $mountvol $mount '/S'
    if ($LASTEXITCODE -ne 0) {
        throw "mountvol failed to mount the system ESP (exit $LASTEXITCODE)."
    }
    try {
        $lines = & $mountvol $mount '/L'
        if ($LASTEXITCODE -ne 0) {
            throw "mountvol could not resolve the volume GUID for $mount."
        }
        $volumeGuid = $null
        $volumePrefix = [string][char]92 + [string][char]92 + '?' + [string][char]92 + 'Volume{'
        foreach ($line in $lines) {
            $candidate = ([string]$line).Trim()
            if ($candidate.StartsWith($volumePrefix) -and $candidate.EndsWith([string][char]92)) {
                $guidStart = $candidate.IndexOf('{')
                $guidEnd = $candidate.IndexOf('}')
                $parsedGuid = [Guid]::Empty
                if ($guidStart -ge 0 -and $guidEnd -gt $guidStart -and
                    [Guid]::TryParse($candidate.Substring($guidStart + 1,$guidEnd - $guidStart - 1),[ref]$parsedGuid)) {
                    $volumeGuid = $candidate
                    break
                }
            }
        }
        if (-not $volumeGuid) {
            throw "Could not parse the exact volume GUID for $mount."
        }
        # mountvol /S can create a DOS mount without populating DriveLetter.
        # Match the actual mounted volume GUID to exactly one partition.
        $partitions = @(Get-Partition | Where-Object { @($_.AccessPaths) -contains $volumeGuid })
        if ($partitions.Count -ne 1 -or $partitions[0].GptType -ne $script:EspGuidType) {
            throw "Mounted volume $volumeGuid does not resolve to exactly one GPT EFI System Partition."
        }
        $espRoot = $mount + [string][char]92
        Assert-NoReparseAncestors (Join-Path $espRoot 'EFI') $espRoot
        return @{
            EspRoot = $espRoot
            VolumeGuid = $volumeGuid
            MountPath = $espRoot
            Mountvol = $mountvol
        }
    } catch {
        & $mountvol $mount '/D' | Out-Null
        throw
    }
}
function Dismount-Esp($Mount) {
    if ($Mount -and $Mount.Mountvol) {
        & $Mount.Mountvol $Mount.MountLetterColon '/D' | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Warning "Could not dismount temporary ESP mount $($Mount.MountLetterColon)." }
    }
}

function Mount-Esp {
    $mount = Invoke-MountEsp
    $mount.MountLetterColon = '{0}:' -f $script:MountLetter
    return $mount
}

function Remove-OwnedStage([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return }
    $items = @(Get-ChildItem -LiteralPath $Path -Force)
    foreach ($item in $items) {
        if ($script:OwnedNames -notcontains $item.Name -or $item.PSIsContainer -or (Test-ReparsePoint $item.FullName)) {
            throw "Refusing to remove unowned stage content: $($item.FullName)"
        }
        Remove-Item -LiteralPath $item.FullName -Force
    }
    Remove-Item -LiteralPath $Path -Force
}

function Stage-ToRoot([string]$EspRoot, [string]$VolumeGuid, [string]$TestRootValue) {
    $sources = Get-StageSources
    $bootBefore = Get-BootManagerHash $EspRoot
    $paths = Assert-StageDirectory $EspRoot
    $efiRoot = $paths.EfiRoot
    $stageRoot = $paths.StageRoot
    if (-not (Test-Path -LiteralPath $efiRoot)) { New-Item -ItemType Directory -Path $efiRoot | Out-Null }

    $localBackup = $null
    if (Test-Path -LiteralPath $stageRoot) {
        $prior = Assert-ExistingStageOwned $stageRoot
        $localBackup = Save-LocalStageBackup $stageRoot $prior
    }

    $token = [Guid]::NewGuid().ToString('N')
    $tempRoot = Join-Path $efiRoot ('.HypersinvInventory.stage-' + $token)
    $backupRoot = Join-Path $efiRoot ('.HypersinvInventory.backup-' + $token)
    Assert-NoReparseAncestors $tempRoot $EspRoot
    Assert-NoReparseAncestors $backupRoot $EspRoot
    Assert-NoReparseAncestors $stageRoot $EspRoot
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    $newMoved = $false
    $oldMoved = $false
    try {
        Copy-Item -LiteralPath $sources.Loader -Destination (Join-Path $tempRoot 'loader.efi')
        Copy-Item -LiteralPath $sources.Collector -Destination (Join-Path $tempRoot 'inventory.efi')
        [IO.File]::WriteAllText((Join-Path $tempRoot 'manifest.sha256'), (New-Manifest $sources), [Text.Encoding]::ASCII)
        Assert-StageFiles $tempRoot $sources
        if (Test-Path -LiteralPath $stageRoot) {
            Move-Item -LiteralPath $stageRoot -Destination $backupRoot
            $oldMoved = $true
        }
        Move-Item -LiteralPath $tempRoot -Destination $stageRoot
        $newMoved = $true
        Assert-NoReparseAncestors $stageRoot $EspRoot
        Assert-StageFiles $stageRoot $sources
        $bootAfter = Get-BootManagerHash $EspRoot
        if ($bootAfter -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed during staging.' }
        Write-Receipt $VolumeGuid $EspRoot $bootBefore $sources $TestRootValue
        if ($oldMoved) {
            try { Remove-OwnedStage $backupRoot }
            catch { Write-Warning "Staging succeeded but ESP backup cleanup failed: $($_.Exception.Message)" }
        }
        Write-Output "STAGED $stageRoot"
        if ($localBackup) { Write-Output "PRESERVED previous stage at $localBackup" }
        Write-Output "  loader.efi sha256=$($sources.LoaderHash)"
        Write-Output "  inventory.efi sha256=$($sources.CollectorHash)"
        Write-Output '  BootNext and Secure Boot were not changed.'
    } catch {
        if ($newMoved -and (Test-Path -LiteralPath $stageRoot)) { Remove-OwnedStage $stageRoot }
        if ($oldMoved -and (Test-Path -LiteralPath $backupRoot) -and -not (Test-Path -LiteralPath $stageRoot)) {
            Move-Item -LiteralPath $backupRoot -Destination $stageRoot
        }
        if (Test-Path -LiteralPath $tempRoot) { Remove-OwnedStage $tempRoot }
        throw
    }
}
function Get-InstallTool {
    $path = Join-Path $script:LoaderBuild 'ib-install.exe'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing installer: $path. Run Build first." }
    return $path
}

function Invoke-OneShot([ValidateSet('bootnext','off')][string]$OneShotMode, [string]$VolumeGuid) {
    $installer = Get-InstallTool
    & $installer oneshot --mode $OneShotMode --profile inventory --esp-volume $VolumeGuid
    if ($LASTEXITCODE -ne 0) { throw "ib-install oneshot --mode $OneShotMode failed (exit $LASTEXITCODE)." }
}

function Invoke-ArmWithRollback([scriptblock]$ArmAction, [scriptblock]$VerifyAction, [scriptblock]$RollbackAction) {
    # ib-install owns cleanup when its transaction itself fails. Only issue our
    # explicit "off" action after a successful set whose verification failed.
    & $ArmAction
    try {
        & $VerifyAction
    } catch {
        $verificationFailure = $_.Exception.Message
        $cleanupFailure = $null
        try { & $RollbackAction }
        catch { $cleanupFailure = $_.Exception.Message }
        if ($cleanupFailure) {
            throw "Arming verification failed: $verificationFailure. Clearing the owned one-time boot state also failed: $cleanupFailure"
        }
        throw "Arming verification failed; the owned one-time boot state was cleared. $verificationFailure"
    }
}

function Get-MountedStageStatus([string]$EspRoot, [switch]$AllowReport) {
    $sources = Get-StageSources
    $paths = Assert-StageDirectory $EspRoot -AllowReport:$AllowReport
    Assert-StageFiles $paths.StageRoot $sources
    $bootHash = Get-BootManagerHash $EspRoot
    return @{ Paths = $paths; Sources = $sources; BootHash = $bootHash }
}

function Assert-StatusReceipt($Receipt, [hashtable]$Status, [string]$ExpectedVolumeGuid, [string]$ExpectedTestRoot) {
    if ($Receipt.volumeGuid -cne $ExpectedVolumeGuid) {
        throw 'Current ESP volume differs from the staged receipt.'
    }
    if ($Receipt.loaderSha256 -cne $Status.Sources.LoaderHash -or
        $Receipt.inventorySha256 -cne $Status.Sources.CollectorHash) {
        throw 'Current build files differ from the staged receipt; run Build and Stage again.'
    }
    if ($Receipt.bootManagerSha256 -cne $Status.BootHash) {
        throw 'Windows Boot Manager SHA-256 differs from the staged receipt.'
    }
    $receiptStage = [IO.Path]::GetFullPath([string]$Receipt.stageDirectory)
    $actualStage = [IO.Path]::GetFullPath([string]$Status.Paths.StageRoot)
    if ($receiptStage -cne $actualStage) {
        throw 'Staged directory path differs from the Path B receipt.'
    }
    if ($ExpectedTestRoot) {
        $receiptTestRoot = [IO.Path]::GetFullPath([string]$Receipt.testRoot)
        if ($receiptTestRoot -cne [IO.Path]::GetFullPath($ExpectedTestRoot)) {
            throw 'Test root differs from the Path B receipt.'
        }
    } elseif ($Receipt.testRoot) {
        throw 'A host-test receipt cannot be used to verify a live ESP.'
    }
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    if (-not $Executable) { throw 'Required CMake/CTest executable was not found.' }
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed (exit $LASTEXITCODE)." }
}

function Assert-CMakeCache([string]$BuildDirectory, [hashtable]$ExpectedValues, [string[]]$PathValues) {
    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { throw "CMake did not create its cache: $cache" }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $cache) {
        $equals = $line.IndexOf('=')
        $colon = $line.IndexOf(':')
        if ($equals -le 0 -or $colon -le 0 -or $colon -ge $equals) { continue }
        $values[$line.Substring(0,$colon)] = $line.Substring($equals + 1)
    }
    foreach ($name in $ExpectedValues.Keys) {
        if (-not $values.ContainsKey($name)) { throw "CMake cache is missing required setting $name in $cache" }
        $expected = [string]$ExpectedValues[$name]
        $actual = [string]$values[$name]
        if ($PathValues -contains $name) {
            $expected = [IO.Path]::GetFullPath($expected)
            $actual = [IO.Path]::GetFullPath($actual)
            if (-not [string]::Equals($expected,$actual,[StringComparison]::OrdinalIgnoreCase)) {
                throw "CMake cache $name mismatch in $cache. Expected '$expected'; found '$actual'."
            }
        } elseif ($actual -cne $expected) {
            throw "CMake cache $name mismatch in $cache. Expected '$expected'; found '$actual'."
        }
    }
}

function Invoke-Build {
    $systemRoot = [Environment]::GetEnvironmentVariable('SystemRoot')
    if (-not $systemRoot) { $systemRoot = [Environment]::GetEnvironmentVariable('windir') }
    $systemDriveRoot = if ($systemRoot) { [IO.Path]::GetPathRoot($systemRoot) } else { $null }
    $programFilesRoots = @(
        [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
        [Environment]::GetEnvironmentVariable('ProgramFiles')
        [Environment]::GetEnvironmentVariable('ProgramW6432')
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
        [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
        if ($systemDriveRoot) { Join-Path $systemDriveRoot 'Program Files (x86)' }
        if ($systemDriveRoot) { Join-Path $systemDriveRoot 'Program Files' }
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } |
        ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique

    $vswhere = $null
    foreach ($programFilesRoot in $programFilesRoots) {
        $candidate = Join-Path $programFilesRoot 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $vswhere = $candidate
            break
        }
    }
    $installations = @()
    if ($vswhere) {
        $identified = @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
        if ($LASTEXITCODE -ne 0 -or $identified.Count -eq 0) {
            $identified = @(& $vswhere -products '*' -property installationPath 2>$null)
        }
        $installations += $identified
    }
    # vswhere can omit installations in restricted/partial environments. Probe
    # only known Visual Studio version/edition directory levels as a fallback.
    foreach ($programFilesRoot in $programFilesRoots) {
        $vsRoot = Join-Path $programFilesRoot 'Microsoft Visual Studio'
        if (-not (Test-Path -LiteralPath $vsRoot -PathType Container)) { continue }
        foreach ($version in (Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction SilentlyContinue)) {
            $installations += @(Get-ChildItem -LiteralPath $version.FullName -Directory -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName)
        }
    }
    $installations = @($installations | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } |
        ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique)
    $vcvars = $null
    foreach ($installation in $installations) {
        if (-not $installation) { continue }
        $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $candidate) {
            $vcvars = $candidate
            break
        }
    }
    if (-not $vcvars) {
        throw 'No x64 MSVC installation with vcvars64.bat was found.'
    }

    $ninja = $null
    foreach ($buildDirectory in @($script:InventoryBuild,$script:LoaderBuild)) {
        $cache = Join-Path $buildDirectory 'CMakeCache.txt'
        if (Test-Path -LiteralPath $cache) {
            $cacheLine = Get-Content -LiteralPath $cache |
                Where-Object { $_ -match '^CMAKE_MAKE_PROGRAM:FILEPATH=' } |
                Select-Object -First 1
            if ($cacheLine) {
                $candidate = $cacheLine.Substring($cacheLine.IndexOf('=') + 1)
                if (Test-Path -LiteralPath $candidate) { $ninja = $candidate; break }
            }
        }
    }
    if (-not $ninja) {
        $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
        if ($command) { $ninja = $command.Source }
    }
    if (-not $ninja) {
        foreach ($installation in $installations) {
            if (-not $installation) { continue }
            $candidate = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
            if (Test-Path -LiteralPath $candidate) {
                $ninja = $candidate
                break
            }
        }
    }
    if (-not $ninja) {
        throw 'Ninja was not found on PATH, in the existing inventory build cache, or in Visual Studio.'
    }

    $commandProcessor = [Environment]::GetEnvironmentVariable('ComSpec')
    if (-not $commandProcessor) {
        $commandProcessor = Join-Path ([Environment]::SystemDirectory) 'cmd.exe'
    }
    if (-not (Test-Path -LiteralPath $commandProcessor -PathType Leaf)) {
        throw "Windows command processor was not found: $commandProcessor"
    }
    $envLines = & $commandProcessor /d /s /c ('call "' + $vcvars + '" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed (exit $LASTEXITCODE)." }
    foreach ($line in $envLines) {
        $equals = $line.IndexOf('=')
        if ($equals -le 0) { continue }
        $name = $line.Substring(0, $equals)
        if ($name.StartsWith('=')) { continue }
        [Environment]::SetEnvironmentVariable($name, $line.Substring($equals + 1), 'Process')
    }
    $ninjaDir = Split-Path -Parent $ninja
    if (($env:PATH -split ';') -notcontains $ninjaDir) {
        $env:PATH = $ninjaDir + ';' + $env:PATH
    }
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $ctest = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if (-not $cmake -or -not $ctest) { throw 'CMake or CTest was not found on PATH.' }
    $versionOutput = & $cmake.Source --version
    $versionText = $versionOutput -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -or $versionText -notmatch 'cmake version\s+(\d+)\.(\d+)\.(\d+)') {
        throw 'Could not determine the installed CMake version.'
    }
    $cmakeMajor = [int]$Matches[1]
    $cmakeMinor = [int]$Matches[2]
    if ($cmakeMajor -lt 3 -or ($cmakeMajor -eq 3 -and $cmakeMinor -lt 24)) {
        throw 'Build requires CMake 3.24 or newer for isolated --fresh configuration.'
    }
    $compilerCommand = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $compilerCommand -or -not $compilerCommand.Source) { throw 'MSVC cl.exe was not found after loading vcvars64.bat.' }
    $compiler = [IO.Path]::GetFullPath($compilerCommand.Source)

    New-Item -ItemType Directory -Path $script:LoaderBuild -Force | Out-Null
    New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
    Invoke-Checked $cmake.Source @('--fresh','-S',(Join-Path $script:RepoRoot 'components/uefi-loader'),'-B',$script:LoaderBuild,'-G','Ninja',('-DCMAKE_MAKE_PROGRAM:FILEPATH=' + $ninja),'-DIB_STATIC_RUNTIME:BOOL=ON','-DIB_QEMU_DEBUG_PORTS:BOOL=OFF',('-DCMAKE_C_COMPILER:FILEPATH=' + $compiler),'-DCMAKE_BUILD_TYPE:STRING=Release')
    Assert-CMakeCache $script:LoaderBuild @{
        CMAKE_BUILD_TYPE = 'Release'
        IB_STATIC_RUNTIME = 'ON'
        IB_QEMU_DEBUG_PORTS = 'OFF'
        CMAKE_C_COMPILER = $compiler
        CMAKE_MAKE_PROGRAM = $ninja
    } @('CMAKE_C_COMPILER','CMAKE_MAKE_PROGRAM')
    Invoke-Checked $cmake.Source @('--build',$script:LoaderBuild)
    Invoke-Checked $cmake.Source @('--build',$script:LoaderBuild,'--target','inventory_loader')
    Invoke-Checked $ctest.Source @('--test-dir',$script:LoaderBuild,'--output-on-failure')

    Invoke-Checked $cmake.Source @('--fresh','-S',(Join-Path $script:RepoRoot 'tools/efi_inventory'),'-B',$script:InventoryBuild,'-G','Ninja',('-DCMAKE_MAKE_PROGRAM:FILEPATH=' + $ninja),('-DCMAKE_CXX_COMPILER:FILEPATH=' + $compiler),'-DCMAKE_BUILD_TYPE:STRING=Release')
    Assert-CMakeCache $script:InventoryBuild @{
        CMAKE_BUILD_TYPE = 'Release'
        CMAKE_CXX_COMPILER = $compiler
        CMAKE_MAKE_PROGRAM = $ninja
    } @('CMAKE_CXX_COMPILER','CMAKE_MAKE_PROGRAM')
    Invoke-Checked $cmake.Source @('--build',$script:InventoryBuild)
    Invoke-Checked $ctest.Source @('--test-dir',$script:InventoryBuild,'--output-on-failure')
    Write-Output 'Build and host CTest completed. No ESP was mounted and no firmware state was changed.'
}
function Invoke-Status {
    if ($TestRoot) {
        $espRoot = Assert-TestRoot $TestRoot
        $receipt = Read-Receipt
        $status = Get-MountedStageStatus $espRoot -AllowReport
        Assert-StatusReceipt $receipt $status 'TEST-ROOT' $espRoot
        Write-Output "STAGED AND VERIFIED $($status.Paths.StageRoot)"
        Write-Output "  loader.efi sha256=$($status.Sources.LoaderHash)"
        Write-Output "  inventory.efi sha256=$($status.Sources.CollectorHash)"
        Write-Output "  bootmgfw.efi sha256=$($status.BootHash)"
        Write-Output '  Status does not indicate whether BootNext is armed.'
        return
    }
    $receipt = Read-Receipt
    $mount = Mount-Esp
    try {
        $status = Get-MountedStageStatus $mount.EspRoot -AllowReport
        Assert-StatusReceipt $receipt $status $mount.VolumeGuid ''
        Write-Output "STAGED AND VERIFIED $($status.Paths.StageRoot)"
        Write-Output "  loader.efi sha256=$($status.Sources.LoaderHash)"
        Write-Output "  inventory.efi sha256=$($status.Sources.CollectorHash)"
        Write-Output "  bootmgfw.efi sha256=$($status.BootHash)"
        Write-Output '  Status does not indicate whether BootNext is armed.'
    } finally { Dismount-Esp $mount }
}

function Invoke-Arm {
    Assert-Admin
    try { $secureBoot = Confirm-SecureBootUEFI }
    catch { throw "Cannot verify Secure Boot state; refusing to arm. $($_.Exception.Message)" }
    if ($secureBoot -ne $false) { throw 'Secure Boot must be disabled before arming the unsigned inventory loader.' }
    $receipt = Read-Receipt
    $mount = Mount-Esp
    try {
        if ($receipt.volumeGuid -cne $mount.VolumeGuid) { throw 'Current system ESP volume differs from the staged receipt.' }
        $status = Get-MountedStageStatus $mount.EspRoot
        if ($receipt.loaderSha256 -cne $status.Sources.LoaderHash -or
            $receipt.inventorySha256 -cne $status.Sources.CollectorHash) { throw 'Staged files no longer match the receipt.' }
        $bootBefore = Get-BootManagerHash $mount.EspRoot
        if ($receipt.bootManagerSha256 -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 differs from the staged receipt.' }
        if (Test-Path -LiteralPath (Join-Path $mount.EspRoot $script:ReportName)) {
            throw 'An earlier inventory report is still present on the ESP; collect it before arming again.'
        }
        Invoke-ArmWithRollback {
            Invoke-OneShot 'bootnext' $mount.VolumeGuid
        } {
            if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) {
                throw 'Windows Boot Manager SHA-256 changed while arming.'
            }
        } {
            Invoke-OneShot 'off' $mount.VolumeGuid
            if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) {
                throw 'Windows Boot Manager SHA-256 changed during rollback.'
            }
        }
        Write-Output "ARMED one-time inventory boot on $($mount.VolumeGuid)"
        Write-Output 'No reboot was started. Reboot when ready; the one-time entry is selected by BootNext.'
    } finally { Dismount-Esp $mount }
}

function Invoke-Disarm {
    Assert-Admin
    $mount = Mount-Esp
    try {
        $bootBefore = Get-BootManagerHash $mount.EspRoot
        Invoke-OneShot 'off' $mount.VolumeGuid
        if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed while disarming.' }
        Write-Output 'Owned inventory BootNext/Boot#### state cleared. Staged files were preserved.'
    } finally { Dismount-Esp $mount }
}

function Invoke-Collect {
    Assert-Admin
    $receipt = Read-Receipt
    $mount = Mount-Esp
    try {
        if ($receipt.volumeGuid -cne $mount.VolumeGuid) { throw 'Current system ESP volume differs from the staged receipt.' }
        $report = Join-Path $mount.EspRoot $script:ReportName
        if (-not (Test-Path -LiteralPath $report -PathType Leaf) -or (Test-ReparsePoint $report)) {
            throw "No safe collector report exists at $report."
        }
        $size = (Get-Item -LiteralPath $report).Length
        if ($size -lt 20) { throw "Collector report is too small to be valid ($size bytes)." }
        Assert-NoReparseAncestors $script:InventoryBuild ([IO.Path]::GetPathRoot($script:InventoryBuild))
        New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
        $captureDir = Join-Path $script:InventoryBuild 'captures'
        if ((Test-Path -LiteralPath $captureDir) -and
            -not (Test-Path -LiteralPath $captureDir -PathType Container)) {
            throw "Capture path exists but is not a directory: $captureDir"
        }
        Assert-NoReparseAncestors $captureDir $script:InventoryBuild
        New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
        $suffix = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
        $capture = Join-Path $captureDir ('HypersinvEfiInventory.' + $suffix + '.txt')
        $full = Join-Path $captureDir ('HypersinvInventory.' + $suffix + '.txt')
        Assert-NoReparseAncestors $capture $captureDir
        Assert-NoReparseAncestors $full $captureDir
        $before = Get-Sha256 $report
        Copy-Item -LiteralPath $report -Destination $capture
        $copiedHash = Get-Sha256 $capture
        $afterCopy = Get-Sha256 $report
        if ($before -cne $copiedHash -or $before -cne $afterCopy) {
            throw 'Report changed while it was being copied; it has been left on the ESP.'
        }
        $scanner = Join-Path $script:InventoryBuild 'efi_inventory.exe'
        if (-not (Test-Path -LiteralPath $scanner -PathType Leaf)) { throw "Missing scanner: $scanner. Run Build first." }
        & $scanner scan $capture $full
        if ($LASTEXITCODE -ne 0) { throw "efi_inventory scan failed (exit $LASTEXITCODE); report remains on the ESP." }
        if (-not (Test-Path -LiteralPath $full -PathType Leaf) -or (Get-Item -LiteralPath $full).Length -lt 20) {
            throw 'Scanner did not create a complete local inventory report; ESP report was preserved.'
        }
        $readback = Get-Sha256 $capture
        if ($readback -cne $before) { throw 'Local capture read-back hash does not match the ESP report.' }
        $bootBefore = Get-BootManagerHash $mount.EspRoot
        Invoke-OneShot 'off' $mount.VolumeGuid
        if ((Get-BootManagerHash $mount.EspRoot) -cne $bootBefore) { throw 'Windows Boot Manager SHA-256 changed during cleanup.' }
        if ((Get-Sha256 $report) -cne $before) { throw 'ESP report changed before cleanup; preserving it.' }
        Remove-Item -LiteralPath $report -Force
        Write-Output "COLLECTED $capture"
        Write-Output "SCANNED   $full"
        Write-Output "  source sha256=$before"
        Write-Output '  Owned inventory BootNext/Boot#### state cleared; unchanged ESP report removed.'
    } finally { Dismount-Esp $mount }
}

if (-not $LibraryOnly) {
    switch ($Mode) {
        'Build' { Invoke-Build }
        'Stage' {
            if ($TestRoot) {
                $testEsp = Assert-TestRoot $TestRoot
                Stage-ToRoot $testEsp 'TEST-ROOT' $testEsp
            } else {
                Assert-Admin
                $mount = Mount-Esp
                try { Stage-ToRoot $mount.EspRoot $mount.VolumeGuid '' }
                finally { Dismount-Esp $mount }
            }
        }
        'Status' { Invoke-Status }
        'Arm' {
            if ($TestRoot) { throw '-TestRoot is allowed only for Stage and Status; it never arms firmware.' }
            Invoke-Arm
        }
        'Collect' {
            if ($TestRoot) { throw '-TestRoot is allowed only for Stage and Status; it never collects from firmware.' }
            Invoke-Collect
        }
        'Disarm' {
            if ($TestRoot) { throw '-TestRoot is allowed only for Stage and Status; it never changes firmware state.' }
            Invoke-Disarm
        }
    }
}
