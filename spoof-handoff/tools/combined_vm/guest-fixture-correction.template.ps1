[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [string]$Action = 'Probe',
    [string]$ExpectedProbeVerdict = 'NOT_MET'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$configBytes = [Convert]::FromBase64String('__CONFIG_BASE64__')
try { $config = [Text.Encoding]::UTF8.GetString($configBytes) | ConvertFrom-Json -ErrorAction Stop }
finally { [Array]::Clear($configBytes, 0, $configBytes.Length) }
$runRootFull = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\')
$expectedRunRoot = [IO.Path]::GetFullPath((Join-Path $env:ProgramData 'UEFILoaderRunner\runs')).TrimEnd('\') + '\'
$stateRoot = [string]$config.stateRoot
$mode = [string]$config.mode
$internalAction = [string]$config.internalAction
$firmwareGuid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}'
$espLetter = 'Z:'
$efiStagePath = 'EFI\HypersinvInventory'
$correctionRelative = 'EFI\HypersinvInventory\fixture-correction'
$preludeRelative = '\EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi'
$loaderRelative = '\EFI\HypersinvInventory\loader.efi'
$bootManagerRelative = 'EFI\Microsoft\Boot\bootmgfw.efi'
$systemEspGuid = '{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}'
$metadata = [ordered]@{
    schema = 'combined-vm-fixture-correction-action.v1'
    internalAction = $internalAction
    mode = $mode
    startedUtc = [DateTime]::UtcNow.ToString('o')
    completedUtc = $null
    status = 'FAIL'
    vm = [ordered]@{ manufacturer = $null; model = $null; uuid = $null; diskPnp = $null; diskSerial = $null }
    bootIdentity = $null
    firmware = [ordered]@{ secureBoot = $null; secureBootVariable = $null; bootOrderBefore = $null; bootOrderAfter = $null; bootNextBefore = $null; bootNextAfter = $null; bootCurrent = $null; standardBootEntry = $null; customBootEntry = $null }
    esp = [ordered]@{ before = $null; afterStageArm = $null; afterCapture = $null; correctionEvidence = @() }
    windowsReadback = $null
    collectorReport = $null
    efi = $null
    sourcePins = @()
    capturedFiles = @()
    rawEvidenceBundle = $null
    errors = @()
    errorDetails = @()
    cleanup = [ordered]@{ attempted = $false; errors = @() }
}
$result = [ordered]@{
    SchemaVersion = 1
    RunId = Split-Path -Leaf $runRootFull
    Status = 'FAIL'
    Message = 'Fixture correction action did not complete.'
    Action = $Action
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$output = [Collections.Generic.List[string]]::new()
$pathBScript = $null
$loaderBuild = $null
$inventoryBuild = $null
$firmwareModulePath = $null
$bootOptionModulePath = $null
$script:fixtureJournal = $null
$script:fixtureJournalPath = $null
$script:fixtureJournalOwnedThisInvocation = $false
$script:recoveryAttempted = $false

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Install-VerifiedReadOnlyAsset([string]$SourcePath, [string]$DestinationPath, [string]$ExpectedSha256) {
    if ([string]::IsNullOrWhiteSpace($SourcePath) -or [string]::IsNullOrWhiteSpace($DestinationPath)) {
        throw 'Asset source and destination paths must be non-empty.'
    }
    $source = [string]$SourcePath
    if ($source.EndsWith('\', [StringComparison]::Ordinal) -or $source.EndsWith('/', [StringComparison]::Ordinal)) {
        throw "Read-only asset path must identify a file without a trailing separator: $source"
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Read-only HGFS asset is not reachable: $source"
    }
    $sourceHash = Get-Sha256 $source
    if ($sourceHash -cne $ExpectedSha256.ToUpperInvariant()) {
        throw "Read-only HGFS asset hash mismatch: expected=$ExpectedSha256 actual=$sourceHash path=$source"
    }

    if (Test-Path -LiteralPath $DestinationPath) {
        if (-not (Test-Path -LiteralPath $DestinationPath -PathType Leaf)) {
            throw "Existing local asset cache is not a file: $DestinationPath"
        }
        $cachedHash = Get-Sha256 $DestinationPath
        if ($cachedHash -cne $ExpectedSha256.ToUpperInvariant()) {
            throw "Existing local cached asset differs: $DestinationPath"
        }
    } else {
        Copy-Item -LiteralPath $source -Destination $DestinationPath -ErrorAction Stop
    }

    $destinationHash = Get-Sha256 $DestinationPath
    if ($destinationHash -cne $ExpectedSha256.ToUpperInvariant()) {
        throw "Local asset copy hash mismatch: expected=$ExpectedSha256 actual=$destinationHash path=$DestinationPath"
    }
}

function Get-Sha256FromBytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Get-FixtureErrorDetail([System.Management.Automation.ErrorRecord]$ErrorRecord, [string]$Phase) {
    if ($null -eq $ErrorRecord) { throw 'Cannot create fixture error detail from a null error record.' }
    $positionMessage = $null
    if ($null -ne $ErrorRecord.InvocationInfo) { $positionMessage = [string]$ErrorRecord.InvocationInfo.PositionMessage }
    return [ordered]@{
        phase = $Phase
        exceptionType = $ErrorRecord.Exception.GetType().FullName
        message = $ErrorRecord.Exception.Message
        scriptStackTrace = [string]$ErrorRecord.ScriptStackTrace
        positionMessage = $positionMessage
    }
}

function Get-FixtureJournalPath([string]$ModeName) {
    if ($ModeName -notin @('correction-only','corrected-collector')) { throw "Invalid fixture journal mode: $ModeName" }
    return Join-Path $stateRoot ($ModeName + '.journal.json')
}

function Ensure-FixtureJournalProperties($Journal) {
    if ($null -eq $Journal) { throw 'Cannot normalize a null fixture journal.' }
    $defaults = [ordered]@{
        captureStartedUtc = $null
        stageExitCode = $null
        armExitCode = $null
        cleanupCompletedUtc = $null
        updatedUtc = $null
    }
    foreach ($name in $defaults.Keys) {
        if ($Journal -is [System.Collections.IDictionary]) {
            if (-not $Journal.Contains($name)) { $Journal[$name] = $defaults[$name] }
        } elseif ($null -eq $Journal.PSObject.Properties[$name]) {
            $Journal | Add-Member -MemberType NoteProperty -Name $name -Value $defaults[$name]
        }
    }
    return $Journal
}

function Save-FixtureJournal {
    if ($null -eq $script:fixtureJournal -or [string]::IsNullOrWhiteSpace([string]$script:fixtureJournalPath)) { throw 'Fixture journal is not initialized.' }
    Ensure-FixtureJournalProperties $script:fixtureJournal | Out-Null
    $script:fixtureJournal.updatedUtc = [DateTime]::UtcNow.ToString('o')
    $json = ($script:fixtureJournal | ConvertTo-Json -Depth 24) + [Environment]::NewLine
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $temporary = $script:fixtureJournalPath + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    $backup = $script:fixtureJournalPath + '.' + [Guid]::NewGuid().ToString('N') + '.bak'
    $stream = $null
    try {
        $stream = [IO.FileStream]::new($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
        $stream.Dispose()
        $stream = $null
        if (Test-Path -LiteralPath $script:fixtureJournalPath -PathType Leaf) {
            [IO.File]::Replace($temporary, $script:fixtureJournalPath, $backup)
        } else {
            [IO.File]::Move($temporary, $script:fixtureJournalPath)
        }
        $actual = Get-Content -LiteralPath $script:fixtureJournalPath -Raw -ErrorAction Stop
        if ($actual -cne $json) { throw 'Fixture journal readback differed from its durable write.' }
        if (Test-Path -LiteralPath $backup -PathType Leaf) { Remove-Item -LiteralPath $backup -Force -ErrorAction Stop }
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue }
        [Array]::Clear($bytes, 0, $bytes.Length)
    }
}

function Invoke-JournaledFixtureMutation([string]$Operation, [string]$CompletedState, [scriptblock]$Mutation) {
    if ($null -eq $script:fixtureJournal) { throw "Cannot journal fixture mutation without an intent journal: $Operation" }
    $script:fixtureJournal.pendingMutation = $Operation
    $script:fixtureJournal.state = 'MUTATION_INTENT'
    Save-FixtureJournal
    $value = & $Mutation
    $completed = @($script:fixtureJournal.completedMutations)
    if ($completed -cnotcontains $Operation) { $completed += $Operation }
    $script:fixtureJournal.completedMutations = $completed
    $script:fixtureJournal.pendingMutation = $null
    $script:fixtureJournal.state = $CompletedState
    Save-FixtureJournal
    return $value
}

function Assert-Hash([string]$Path, [string]$Expected, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required file is missing: $Name ($Path)" }
    $actual = Get-Sha256 $Path
    $metadata.sourcePins += @([ordered]@{ name = $Name; path = $Path; sha256 = $actual; expectedSha256 = $Expected; matches = ($actual -ceq $Expected) })
    if ($actual -cne $Expected) { throw "Pinned file hash mismatch: $Name expected=$Expected actual=$actual" }
}

function Assert-RunRoot {
    if (-not $runRootFull.StartsWith($expectedRunRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $runRootFull -PathType Container)) {
        throw 'RunRoot is outside the authenticated guest mailbox run directory.'
    }
    $item = Get-Item -LiteralPath $runRootFull -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'RunRoot is a reparse point.' }
    if ($Action -cne 'Probe' -or $ExpectedProbeVerdict -cne 'NOT_MET') { throw 'Unexpected fixed guest gate arguments.' }
}

function Import-EmbeddedModule([string]$Base64, [string]$Leaf) {
    $path = Join-Path $runRootFull $Leaf
    if (Test-Path -LiteralPath $path) { throw "Module destination already exists: $path" }
    $bytes = [Convert]::FromBase64String($Base64)
    try { [IO.File]::WriteAllBytes($path, $bytes) } finally { [Array]::Clear($bytes, 0, $bytes.Length) }
    Import-Module -Name $path -Force -ErrorAction Stop
    return $path
}

function Read-Variable([string]$Name) {
    $value = Read-CombinedFirmwareVariable -Name $Name -Guid $firmwareGuid
    if ($value.success -ne $true -or $value.supported -ne $true -or $value.privilegeEnabled -ne $true -or
        $value.privilegeRestored -ne $true -or [int]$value.win32Error -ne 0) {
        throw "Firmware read failed: variable=$Name status=$($value.win32Error) restoreStatus=$($value.privilegeRestoreError)"
    }
    return $value
}

function Read-VariableOrAbsent([string]$Name) {
    $value = Read-CombinedFirmwareVariable -Name $Name -Guid $firmwareGuid
    if ($value.success -eq $true -and $value.supported -eq $true -and $value.privilegeRestored -eq $true -and [int]$value.win32Error -eq 0) { return $value }
    if ($value.supported -eq $false -and [int]$value.win32Error -eq 203 -and $value.privilegeRestored -eq $true) { return $value }
    throw "Firmware read failed: variable=$Name status=$($value.win32Error) restoreStatus=$($value.privilegeRestoreError)"
}

function Write-Variable([string]$Name, [byte[]]$Bytes, [uint32]$Attributes = 7) {
    $value = Write-CombinedFirmwareVariable -Name $Name -Bytes $Bytes -Attributes $Attributes -Guid $firmwareGuid
    if ($value.success -ne $true -or $value.privilegeRestored -ne $true -or [int]$value.win32Error -ne 0) {
        throw "Firmware write failed: variable=$Name status=$($value.win32Error) restoreStatus=$($value.privilegeRestoreError)"
    }
}

function Remove-VariableExact([string]$Name, [byte[]]$ExpectedBytes, [uint32]$ExpectedAttributes) {
    $current = Read-Variable -Name $Name
    if (-not (Test-EfiVariableRecordEquals -Actual $current -ExpectedBytes $ExpectedBytes -ExpectedAttributes $ExpectedAttributes)) {
        throw "Refusing to remove a firmware variable whose exact bytes/attributes differ: $Name"
    }
    $deleted = Remove-CombinedFirmwareVariable -Name $Name -Attributes $ExpectedAttributes -Guid $firmwareGuid
    if ($deleted.success -ne $true -or $deleted.privilegeRestored -ne $true -or [int]$deleted.win32Error -ne 0) {
        throw "Firmware variable deletion failed: variable=$Name status=$($deleted.win32Error) restoreStatus=$($deleted.privilegeRestoreError)"
    }
    $absent = Read-VariableOrAbsent -Name $Name
    if ($absent.supported -ne $false -or [int]$absent.win32Error -ne 203) { throw "Firmware variable remains after deletion: $Name" }
}

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Guest action requires the authenticated LocalSystem worker.' }
    if ($identity.User.Value -cne 'S-1-5-18') { throw "Guest action must run as LocalSystem; actual SID=$($identity.User.Value)" }
}

function Get-ModeValue($Object, [string]$ModeName, [string]$Label) {
    if ($ModeName -notin @('original','replacement')) { throw "Unsupported SMBIOS value mode: $ModeName" }
    $property = $Object.PSObject.Properties[$ModeName]
    if ($null -eq $property -or [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "SMBIOS fixture configuration is missing $Label.$ModeName."
    }
    return [string]$property.Value
}

function Get-Identity([string]$ExpectedMode = 'original') {
    $systemRows = @(Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop)
    $productRows = @(Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop)
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq '\\.\PHYSICALDRIVE0' })
    if ($systemRows.Count -ne 1 -or $productRows.Count -ne 1 -or $diskRows.Count -ne 1) { throw 'VM identity or boot-disk query cardinality changed.' }
    $identity = [ordered]@{
        manufacturer = [string]$systemRows[0].Manufacturer
        model = [string]$systemRows[0].Model
        uuid = [string]$productRows[0].UUID
        diskPnp = [string]$diskRows[0].PNPDeviceID
        diskSerial = ([string]$diskRows[0].SerialNumber).Trim()
        productVendor = [string]$productRows[0].Vendor
        productName = [string]$productRows[0].Name
        productIdentifyingNumber = [string]$productRows[0].IdentifyingNumber
    }
    $expectedVendor = Get-ModeValue $config.smbios.system_vendor $ExpectedMode 'system_vendor'
    $expectedProduct = Get-ModeValue $config.smbios.system_product $ExpectedMode 'system_product'
    $expectedUuid = Get-ModeValue $config.smbios.system_uuid $ExpectedMode 'system_uuid'
    $metadata.vm.manufacturer = $identity.manufacturer
    $metadata.vm.model = $identity.model
    $metadata.vm.uuid = $identity.uuid
    $metadata.vm.diskPnp = $identity.diskPnp
    $metadata.vm.diskSerial = $identity.diskSerial
    if ($identity.manufacturer -cne $expectedVendor -or $identity.model -cne $expectedProduct -or
        $identity.productVendor -cne $expectedVendor -or $identity.productName -cne $expectedProduct -or
        $identity.uuid -cne $expectedUuid -or $identity.diskPnp -cne [string]$config.diskPnp) {
        throw ('Unexpected phase or physical VM identity; expected=' + $expectedVendor + '/' + $expectedProduct + '/' + $expectedUuid + '/' + [string]$config.diskPnp +
            '; actual=ComputerSystem:' + $identity.manufacturer + '/' + $identity.model + ' ComputerSystemProduct:' + $identity.productVendor + '/' + $identity.productName + '/' + $identity.uuid +
            ' disk=' + $identity.diskPnp)
    }
    return $identity
}

function Assert-PhaseIdentity($Identity, [string]$ExpectedMode) {
    $expectedVendor = Get-ModeValue $config.smbios.system_vendor $ExpectedMode 'system_vendor'
    $expectedProduct = Get-ModeValue $config.smbios.system_product $ExpectedMode 'system_product'
    $expectedUuid = Get-ModeValue $config.smbios.system_uuid $ExpectedMode 'system_uuid'
    if ($Identity.manufacturer -cne $expectedVendor -or $Identity.model -cne $expectedProduct -or
        $Identity.productVendor -cne $expectedVendor -or $Identity.productName -cne $expectedProduct -or
        $Identity.uuid -cne $expectedUuid) {
        throw ('SMBIOS phase identity mismatch expected=' + $expectedVendor + '/' + $expectedProduct + '/' + $expectedUuid +
            ' actual=ComputerSystem:' + $Identity.manufacturer + '/' + $Identity.model + ' ComputerSystemProduct:' +
            $Identity.productVendor + '/' + $Identity.productName + '/' + $Identity.uuid)
    }
}

function Get-BootIdentity {
    $osRows = @(Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop)
    if ($osRows.Count -ne 1 -or $null -eq $osRows[0].LastBootUpTime) { throw 'Windows LastBootUpTime was not uniquely readable.' }
    $boot = ([DateTime]$osRows[0].LastBootUpTime).ToUniversalTime().ToString('o')
    $key = [string]$config.vmUuid + '|' + $boot
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $bootId = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($key)))).Replace('-', '') }
    finally { $sha.Dispose() }
    return [ordered]@{ bootId = $bootId; windowsLastBootUpTimeUtc = $boot; lastBootUpTimeUtc = $boot }
}

function Normalize-MountvolVolumeGuid([string[]]$Lines) {
    $matches = [Collections.Generic.List[string]]::new()
    $pattern = '\A\\\\\?\\Volume\{(?<guid>[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\}\\\z'
    foreach ($line in @($Lines)) {
        $candidate = ([string]$line).Trim()
        if ($candidate.Length -eq 0) { continue }
        $match = [regex]::Match($candidate, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (-not $match.Success) { continue }
        $parsedGuid = [Guid]::Empty
        if (-not [Guid]::TryParseExact($match.Groups['guid'].Value, 'D', [ref]$parsedGuid)) { continue }
        $matches.Add(('\\?\Volume{' + $parsedGuid.ToString('D') + '}\'))
    }
    if ($matches.Count -ne 1) { throw "mountvol /L did not return exactly one volume GUID path; validMatches=$($matches.Count)." }
    return $matches[0]
}

function Mount-Esp {
    if (Test-Path -LiteralPath ($espLetter + '\')) { throw 'Reserved ESP mount Z: is already in use.' }
    $mountvol = Join-Path $env:SystemRoot 'System32\mountvol.exe'
    $mountLines = @(& $mountvol $espLetter '/S' 2>&1 | ForEach-Object { [string]$_ })
    $mountExitCode = [int]$LASTEXITCODE
    if ($mountExitCode -ne 0 -or -not (Test-Path -LiteralPath ($espLetter + '\') -PathType Container)) {
        throw ('ESP mount failed: exit=' + $mountExitCode + '; output=' + ($mountLines -join ' | '))
    }
    try {
        $volumeLines = @(& $mountvol $espLetter '/L' 2>&1 | ForEach-Object { [string]$_ })
        $volumeExitCode = [int]$LASTEXITCODE
        if ($volumeExitCode -ne 0) { throw ('mountvol /L failed: exit=' + $volumeExitCode + '; output=' + ($volumeLines -join ' | ')) }
        $volumeGuid = Normalize-MountvolVolumeGuid -Lines $volumeLines
        return [pscustomobject]@{ root = $espLetter + '\'; volumeGuid = $volumeGuid; mounted = $true }
    } catch {
        $mountError = $_
        $dismountLines = @(& $mountvol $espLetter '/D' 2>&1 | ForEach-Object { [string]$_ })
        $dismountExitCode = [int]$LASTEXITCODE
        if ($dismountExitCode -ne 0) {
            throw ('ESP volume identity resolution failed: ' + $mountError.Exception.Message + '; dismount failed: exit=' + $dismountExitCode + '; output=' + ($dismountLines -join ' | '))
        }
        throw $mountError
    }
}

function Dismount-Esp($Mount) {
    if ($null -eq $Mount) { throw 'Cannot dismount the ESP without the mount handle owned by this action.' }
    if ($Mount.mounted -ne $true) { return }
    if ([string]$Mount.root -cne ($espLetter + '\')) { throw 'Refusing to dismount an ESP mount handle with an unexpected root.' }
    if (-not (Test-Path -LiteralPath ($espLetter + '\') -PathType Container)) { throw 'Owned ESP mount disappeared before dismount.' }
    $volumeLines = @(& (Join-Path $env:SystemRoot 'System32\mountvol.exe') $espLetter '/L' 2>&1 | ForEach-Object { [string]$_ })
    $volumeExitCode = [int]$LASTEXITCODE
    if ($volumeExitCode -ne 0) { throw ('Cannot verify ESP volume before dismount: exit=' + $volumeExitCode + '; output=' + ($volumeLines -join ' | ')) }
    $actualVolumeGuid = Normalize-MountvolVolumeGuid -Lines $volumeLines
    if ($actualVolumeGuid -ine [string]$Mount.volumeGuid) { throw ('Refusing to dismount a different volume: expected=' + [string]$Mount.volumeGuid + ' actual=' + $actualVolumeGuid) }
    $lines = @(& (Join-Path $env:SystemRoot 'System32\mountvol.exe') $espLetter '/D' 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) { throw ('ESP dismount failed: ' + ($lines -join ' | ')) }
    if (Test-Path -LiteralPath ($espLetter + '\')) { throw 'ESP mount letter remains present after dismount.' }
    $Mount.mounted = $false
}

function Test-EspMountLetterInUse {
    return (Test-Path -LiteralPath ($espLetter + '\'))
}

function Get-SafeFileRecord([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Expected EFI evidence file is missing: $Path" }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "EFI evidence file is a reparse point: $Path" }
    $before = Get-Sha256 $Path
    $bytes = [IO.File]::ReadAllBytes($Path)
    $after = Get-Sha256 $Path
    $contentHash = Get-Sha256FromBytes $bytes
    if ($before -cne $after -or $before -cne $contentHash -or $bytes.Length -ne [long]$item.Length) { throw "EFI evidence changed while read: $Path" }
    return [pscustomobject]@{ name = $Name; path = $Path; bytes = [long]$bytes.Length; sha256 = $before; content = $bytes }
}

function Assert-OwnedEspDirectory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Owned ESP directory path is not a safe directory: $Label ($Path)"
    }
    return $true
}

function Get-EspSnapshot([string]$EspRoot) {
    $bootManager = Join-Path $EspRoot $bootManagerRelative
    if (-not (Test-Path -LiteralPath $bootManager -PathType Leaf)) { throw 'Windows Boot Manager is missing from ESP.' }
    $stageRoot = Join-Path $EspRoot $efiStagePath
    $stageRootExists = Assert-OwnedEspDirectory $stageRoot 'Path-B stage'
    $correctionRoot = Join-Path $EspRoot $correctionRelative
    $correctionDirectoryExists = Assert-OwnedEspDirectory $correctionRoot 'fixture correction'
    $stageFiles = @()
    $extraStageItems = @()
    if ($stageRootExists) {
        foreach ($entry in @(Get-ChildItem -LiteralPath $stageRoot -Force)) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point under Path-B stage: $($entry.FullName)" }
            if ($entry.PSIsContainer) { $extraStageItems += $entry.Name }
            else { $stageFiles += [ordered]@{ name = $entry.Name; bytes = [long]$entry.Length; sha256 = Get-Sha256 $entry.FullName } }
        }
    }
    $patch = Join-Path $EspRoot 'HypersinvSmbiosPatch.txt'
    $inventory = Join-Path $EspRoot 'HypersinvEfiInventory.txt'
    $reports = @()
    foreach ($path in @($patch,$inventory)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $record = Get-SafeFileRecord $path ([IO.Path]::GetFileName($path))
            $reports += [ordered]@{ name = $record.name; bytes = $record.bytes; sha256 = $record.sha256 }
        }
    }
    return [ordered]@{
        bootManagerSha256 = Get-Sha256 $bootManager
        stageRootExists = $stageRootExists
        stageDirectoryExists = $stageRootExists
        stageFiles = @($stageFiles)
        extraStageItems = @($extraStageItems)
        stageReportFiles = @($reports)
        correctionDirectoryExists = $correctionDirectoryExists
    }
}

function Get-PartitionFacts([string]$EspRoot, [string]$VolumeGuid) {
    if ([string]::IsNullOrWhiteSpace($VolumeGuid)) { throw 'Mounted ESP volume GUID is empty.' }
    $normalizedVolumeGuid = Normalize-MountvolVolumeGuid -Lines @($VolumeGuid)
    $partitionRows = @(Get-Partition -ErrorAction Stop | Where-Object { @($_.AccessPaths) -contains $normalizedVolumeGuid })
    if ($partitionRows.Count -ne 1) { throw "Mounted ESP volume did not resolve to exactly one partition; matches=$($partitionRows.Count)." }
    $partition = $partitionRows[0]
    if ([string]$partition.GptType -ine $systemEspGuid) { throw "Mounted ESP partition type mismatch: $($partition.GptType)" }
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -Filter ('Index=' + [int]$partition.DiskNumber) -ErrorAction Stop)
    if ($diskRows.Count -ne 1 -or [int]$diskRows[0].BytesPerSector -le 0) { throw 'ESP physical sector size was not uniquely readable.' }
    $sectorSize = [uint64]$diskRows[0].BytesPerSector
    if ([uint64]$partition.Offset % $sectorSize -ne 0 -or [uint64]$partition.Size % $sectorSize -ne 0) { throw 'ESP partition offset/size is not aligned to its physical sector size.' }
    $guidText = ([string]$partition.Guid).Trim('{}')
    $partitionGuid = [Guid]::Empty
    if (-not [Guid]::TryParse($guidText, [ref]$partitionGuid)) { throw "ESP partition GUID is invalid: $guidText" }
    return [ordered]@{
        diskNumber = [uint32]$partition.DiskNumber
        partitionNumber = [uint32]$partition.PartitionNumber
        partitionGuid = $partitionGuid.ToString('D')
        startLba = [uint64]([uint64]$partition.Offset / $sectorSize)
        sizeLba = [uint64]([uint64]$partition.Size / $sectorSize)
        sectorSize = [uint32]$sectorSize
    }
}

function Get-BootOptionIndex([string]$VariableName, $Record) {
    if ([string]$Record.bytesHex -notmatch '\A(?:[0-9A-Fa-f]{2})+\z') { throw "$VariableName has malformed hex bytes." }
    $bytes = [Convert]::FromBase64String([string]$Record.bytesBase64)
    if ($bytes.Length -ne 2) { throw "$VariableName must contain exactly two bytes; actual=$($bytes.Length)." }
    return [int][BitConverter]::ToUInt16($bytes, 0)
}

function Get-FreeBootNumber([int[]]$ExcludeNumbers = @()) {
    for ($number = 65535; $number -ge 0; $number--) {
        if ($ExcludeNumbers -contains $number) { continue }
        $name = ConvertTo-EfiBootVariableName -Number $number
        $record = Read-VariableOrAbsent $name
        if ($record.supported -eq $false -and [int]$record.win32Error -eq 203) { return $number }
    }
    throw 'No unused Boot#### variable number was found.'
}

function Get-StageSources {
    $local = Join-Path $stateRoot 'pathb'
    $loader = Join-Path $local 'loader\inventory_loader.efi'
    $collector = Join-Path $local 'inventory\efi\inventory.efi'
    $installer = Join-Path $local 'loader\ib-install.exe'
    $scanner = Join-Path $local 'inventory\efi_inventory.exe'
    foreach ($pair in @(
        @($loader,[string]$config.assets.inventoryLoaderSha256,'Path-B inventory_loader.efi'),
        @($collector,[string]$config.assets.collectorSha256,'Path-B inventory.efi'),
        @($installer,[string]$config.assets.ibInstallSha256,'Path-B ib-install.exe'),
        @($scanner,[string]$config.assets.efiInventorySha256,'Path-B efi_inventory.exe')
    )) { Assert-Hash ([string]$pair[0]) ([string]$pair[1]) ([string]$pair[2]) }
    return [ordered]@{
        localRoot = $local
        loaderBuild = (Split-Path -Parent $loader)
        inventoryBuild = (Split-Path -Parent (Split-Path -Parent $collector))
        loader = $loader
        loaderBytes = [long](Get-Item -LiteralPath $loader -ErrorAction Stop).Length
        loaderSha256 = [string]$config.assets.inventoryLoaderSha256
        collector = $collector
        collectorBytes = [long](Get-Item -LiteralPath $collector -ErrorAction Stop).Length
        collectorSha256 = [string]$config.assets.collectorSha256
        installer = $installer
        scanner = $scanner
    }
}

function Get-PathBManifestText($Stage) {
    $newline = [string][char]13 + [char]10
    return ('schema=1' + $newline +
        ('loader.efi size={0} sha256={1}' -f [long]$Stage.loaderBytes,[string]$Stage.loaderSha256) + $newline +
        ('inventory.efi size={0} sha256={1}' -f [long]$Stage.collectorBytes,[string]$Stage.collectorSha256) + $newline)
}

function Invoke-PathB([string]$ModeName, $Stage) {
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $args = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$pathBScript,'-Mode',$ModeName,'-LoaderBuildDir',[string]$Stage.loaderBuild,'-InventoryBuildDir',[string]$Stage.inventoryBuild,'-MountLetter','Z')
    $lines = @(& $ps @args 2>&1 | ForEach-Object { [string]$_ })
    $code = [int]$LASTEXITCODE
    $output.AddRange([string[]]$lines)
    if ($code -ne 0) { throw ("Path-B $ModeName failed exit=" + $code + '; output=' + ($lines -join ' | ')) }
    return [pscustomobject]@{ exitCode = $code; output = @($lines) }
}

function Invoke-PathBWithEspHandoff {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('Stage','Arm','Disarm','Collect')][string]$ModeName,
        [Parameter(Mandatory = $true)]$Stage,
        $Mount,
        [switch]$RemountAfter,
        [string]$ExpectedVolumeGuid,
        [string]$ExpectedPartitionGuid,
        [string]$ExpectedBootManagerSha256
    )
    if ($null -ne $Mount -and $Mount.mounted -eq $true) { Dismount-Esp $Mount }
    if (Test-EspMountLetterInUse) {
        throw "Path-B $ModeName refused because ESP mount letter $espLetter is still in use or is not owned by this action."
    }

    $pathBResult = $null
    $pathBFailure = $null
    try { $pathBResult = Invoke-PathB $ModeName $Stage }
    catch { $pathBFailure = $_ }

    if (Test-EspMountLetterInUse) {
        $mountFailure = "Path-B $ModeName returned while ESP mount letter $espLetter remains in use; refusing to treat the handoff as complete."
        if ($null -ne $pathBFailure) {
            throw ($pathBFailure.Exception.Message + '; ' + $mountFailure)
        }
        throw ($mountFailure + ' exit=' + [int]$pathBResult.exitCode + '.')
    }
    if ($null -ne $pathBFailure) { throw $pathBFailure }

    $result = [pscustomobject]@{ pathBResult = $pathBResult; mount = $null; partitionFacts = $null }
    if (-not $RemountAfter.IsPresent) { return $result }
    if ([string]::IsNullOrWhiteSpace($ExpectedVolumeGuid) -or [string]::IsNullOrWhiteSpace($ExpectedPartitionGuid)) {
        throw 'Path-B remount requires the journaled ESP volume and partition identities.'
    }

    $remount = $null
    try {
        $remount = Mount-Esp
        if ([string]$remount.volumeGuid -ine $ExpectedVolumeGuid) {
            throw ('ESP volume changed across Path-B handoff: expected=' + $ExpectedVolumeGuid + ' actual=' + [string]$remount.volumeGuid)
        }
        $partitionFacts = Get-PartitionFacts ([string]$remount.root) ([string]$remount.volumeGuid)
        if ([string]$partitionFacts.partitionGuid -ine $ExpectedPartitionGuid) {
            throw ('ESP partition changed across Path-B handoff: expected=' + $ExpectedPartitionGuid + ' actual=' + [string]$partitionFacts.partitionGuid)
        }
        if (-not [string]::IsNullOrWhiteSpace($ExpectedBootManagerSha256) -and
            (Get-Sha256 (Join-Path ([string]$remount.root) $bootManagerRelative)) -cne $ExpectedBootManagerSha256) {
            throw 'Windows Boot Manager hash changed across Path-B handoff.'
        }
        $result.mount = $remount
        $result.partitionFacts = $partitionFacts
        return $result
    } catch {
        $handoffError = $_
        if ($null -ne $remount -and $remount.mounted -eq $true) {
            try { Dismount-Esp $remount }
            catch { throw ($handoffError.Exception.Message + '; additionally failed to dismount the revalidated ESP: ' + $_.Exception.Message) }
        }
        throw $handoffError
    }
}

function Invoke-CapturePathBCleanup([string]$ModeName, $Stage, $Mount) {
    $action = if ($ModeName -ceq 'correction-only') { 'Disarm' } else { 'Collect' }
    $operation = if ($ModeName -ceq 'correction-only') { 'capture_pathb_disarm' } else { 'capture_pathb_collect' }
    Invoke-JournaledFixtureMutation $operation 'CLEANING' {
        Invoke-PathBWithEspHandoff -ModeName $action -Stage $Stage -Mount $Mount | Out-Null
    } | Out-Null
    return $action
}

function Get-Readback([string]$ExpectedMode) {
    $exe = Join-Path $stateRoot 'smbios_windows_readback.exe'
    Assert-Hash $exe ([string]$config.assets.smbiosReaderSha256) 'SMBIOS readback executable'
    $jsonPath = Join-Path $runRootFull 'smbios-capture.json'
    if (Test-Path -LiteralPath $jsonPath) { throw 'SMBIOS capture output already exists in this mailbox run.' }
    $lines = @(& $exe '--capture' '--output' $jsonPath 2>&1 | ForEach-Object { [string]$_ })
    $code = [int]$LASTEXITCODE
    $output.AddRange([string[]]$lines)
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $jsonPath -PathType Leaf)) { throw ('SMBIOS reader capture failed exit=' + $code + '; output=' + ($lines -join ' | ')) }
    $capture = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$capture.schema -cne 'smbios-windows-capture.v1' -or [string]$capture.result -cne 'PASS' -or @($capture.errors).Count -ne 0) { throw 'SMBIOS readback JSON schema/result is not PASS.' }
    $identity = Get-Identity $ExpectedMode
    Assert-PhaseIdentity $identity $ExpectedMode
    $expectedMap = @{}
    foreach ($member in $config.smbios.PSObject.Properties) { $expectedMap[$member.Name] = Get-ModeValue $member.Value $ExpectedMode $member.Name }
    $fieldResults = [ordered]@{}
    foreach ($key in @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')) {
        $field = $capture.fields.PSObject.Properties[$key]
        if ($null -eq $field) { throw "SMBIOS capture is missing field $key." }
        $native = [string]$field.Value.observedNative
        $wmi = [string]$field.Value.observedWmi
        if ($field.Value.nativeMatchesWmi -ne $true -or $native -cne $expectedMap[$key] -or $wmi -cne $expectedMap[$key]) {
            throw ('SMBIOS readback mismatch mode=' + $ExpectedMode + ' field=' + $key + ' expected=' + $expectedMap[$key] + ' native=' + $native + ' WMI=' + $wmi)
        }
        $fieldResults[$key] = [ordered]@{ expected = $expectedMap[$key]; native = $native; wmi = $wmi; nativeMatchesWmi = $true }
    }
    $captureHash = Get-Sha256 $jsonPath
    $metaPath = Join-Path $runRootFull 'machine-identity.json'
    $identity | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $metaPath -Encoding utf8
    return [ordered]@{
        schema = [string]$capture.schema
        path = $jsonPath
        sha256 = $captureHash
        bytes = [long](Get-Item -LiteralPath $jsonPath).Length
        expectedMode = $ExpectedMode
        identityPath = $metaPath
        identitySha256 = Get-Sha256 $metaPath
        fields = $fieldResults
        native = $capture.native
        wmi = $capture.wmi
    }
}

function Parse-TextReport([byte[]]$Bytes) {
    $text = [Text.Encoding]::ASCII.GetString($Bytes)
    if ($text -match '[^\x09\x0A\x0D\x20-\x7E]') { throw 'EFI report contains non-ASCII bytes.' }
    $values = [ordered]@{}
    foreach ($line in ($text -split "`r?`n")) {
        if ([string]::IsNullOrEmpty($line)) { continue }
        $equals = $line.IndexOf('=')
        if ($equals -le 0) { continue }
        $key = $line.Substring(0, $equals)
        if ($values.Contains($key)) { throw "EFI report contains a duplicate key: $key" }
        $values[$key] = $line.Substring($equals + 1)
    }
    return [pscustomobject]@{ text = $text; values = $values }
}

function Assert-EfiReport($Report, [string]$ExpectedMode) {
    $v = $Report.values
    foreach ($required in @('schema','mode','result','phase','mode_delete_status','configuration_publish_observed','configuration_publish_status','child_load_observed','child_load_status','child_start_status_observed','child_target','eps2_changed_offsets','table_bytes_identical_before_after','eps3_raw_unchanged','smbios2_table_bytes','smbios2_structure_count_before','smbios2_structure_count_after','smbios2_max_structure_before','smbios2_max_structure_measured','smbios2_max_structure_after')) {
        if (-not $v.Contains($required)) { throw "Prelude report is missing required field $required." }
    }
    if ($v.schema -cne 'combined-vm-smbios2-ep-correction.v1' -or $v.mode -cne $ExpectedMode -or $v.result -cne 'prepared' -or $v.phase -cne 'ready_to_chainload') {
        throw ('Prelude report phase/mode mismatch: schema=' + $v.schema + ' mode=' + $v.mode + ' result=' + $v.result + ' phase=' + $v.phase)
    }
    if ($v.mode_delete_status -cne '0x0000000000000000' -or $v.configuration_publish_observed -cne 'true' -or
        $v.configuration_publish_status -cne '0x0000000000000000' -or $v.child_load_observed -cne 'true' -or
        $v.child_load_status -cne '0x0000000000000000' -or $v.child_start_status_observed -cne 'false') {
        throw ('Prelude status evidence mismatch mode=' + $ExpectedMode + ' mode_delete=' + $v.mode_delete_status + ' publish=' + $v.configuration_publish_observed + '/' + $v.configuration_publish_status + ' child_load=' + $v.child_load_observed + '/' + $v.child_load_status + ' child_start_observed=' + $v.child_start_status_observed)
    }
    # The report records EFI file paths relative to the filesystem root without
    # the leading slash used by LoadImage's device path. Keep this exact string
    # check separate from the journaled absolute EFI path checks.
    $expectedTarget = if ($ExpectedMode -ceq 'correction-only') { 'EFI\Microsoft\Boot\bootmgfw.efi' } else { 'EFI\HypersinvInventory\loader.efi' }
    if ($v.child_target -cne $expectedTarget) { throw "Prelude child target mismatch: expected=$expectedTarget actual=$($v.child_target)" }
    if ($v.table_bytes_identical_before_after -cne 'yes' -or $v.eps3_raw_unchanged -cne 'yes') { throw 'Prelude report does not assert table/EPS3 preservation.' }
    $allowedOffsetReports = @('none_noop','4','8','9','4,8','4,9','8,9','4,8,9')
    if ($v.eps2_changed_offsets -cnotin $allowedOffsetReports) {
        throw ('Unexpected EPS2 offsets changed: expected=a canonical subset of {4,8,9} or none_noop actual=' + [string]$v.eps2_changed_offsets)
    }
    return $v
}

function Get-EfiLoadImageEvidence($ReportValues, [string]$ExpectedMode) {
    if ($ExpectedMode -ceq 'correction-only') {
        $expectedTarget = 'EFI\Microsoft\Boot\bootmgfw.efi'
        $statusPhase = 'LoadImage(bootmgfw.efi)'
    } elseif ($ExpectedMode -ceq 'corrected-collector') {
        $expectedTarget = 'EFI\HypersinvInventory\loader.efi'
        $statusPhase = 'LoadImage(loader.efi)'
    } else { throw "Unsupported EFI child mode for LoadImage provenance: $ExpectedMode" }
    if ([string]$ReportValues.mode -cne $ExpectedMode -or [string]$ReportValues.child_target -cne $expectedTarget) {
        throw "Prelude child target/mode mismatch: expectedMode=$ExpectedMode actualMode=$([string]$ReportValues.mode) expectedTarget=$expectedTarget actualTarget=$([string]$ReportValues.child_target)"
    }
    return [pscustomobject]@{ childTarget = $expectedTarget; statusPhase = $statusPhase }
}

function Test-Checksum([byte[]]$Bytes, [int]$Start, [int]$Length) {
    $sum = 0
    for ($i = $Start; $i -lt ($Start + $Length); $i++) { $sum = ($sum + [int]$Bytes[$i]) -band 0xff }
    return $sum -eq 0
}

function Measure-Smbios2Table([byte[]]$Table) {
    if ($null -eq $Table -or $Table.Length -lt 6 -or $Table.Length -gt 1048576) {
        throw 'SMBIOS2 table size is outside the accepted 6-byte through 1-MiB bounds.'
    }

    $offset = 0
    $count = 0
    $maximum = 0
    $typeCounts = @{}
    $sawEnd = $false
    while ($offset -lt $Table.Length) {
        $remaining = $Table.Length - $offset
        if ($remaining -lt 4) { throw "SMBIOS structure header is truncated at offset $offset." }
        $type = [int]$Table[$offset]
        $formattedLength = [int]$Table[$offset + 1]
        if ($formattedLength -lt 4 -or $formattedLength -gt $remaining) {
            throw "SMBIOS formatted structure length is invalid at offset $offset (length=$formattedLength remaining=$remaining)."
        }
        if (($type -eq 1 -and $formattedLength -lt 24) -or ($type -eq 2 -and $formattedLength -lt 8) -or ($type -eq 127 -and $formattedLength -lt 4)) {
            throw "SMBIOS Type $type formatted structure is shorter than its required header at offset $offset."
        }

        $terminator = -1
        $searchEnd = $Table.Length - 1
        for ($index = $offset + $formattedLength; $index -lt $searchEnd; $index++) {
            if ($Table[$index] -eq 0 -and $Table[$index + 1] -eq 0) {
                $terminator = $index
                break
            }
        }
        if ($terminator -lt 0) { throw "SMBIOS string-set double-NUL terminator is missing at offset $offset." }

        $structureBytes = $terminator + 2 - $offset
        if ($structureBytes -lt $formattedLength + 2) { throw "SMBIOS structure extent is invalid at offset $offset." }
        $count++
        if ($structureBytes -gt $maximum) { $maximum = $structureBytes }
        if (-not $typeCounts.ContainsKey($type)) { $typeCounts[$type] = 0 }
        $typeCounts[$type]++

        $offset += $structureBytes
        if ($type -eq 127) {
            if ($offset -ne $Table.Length) { throw 'SMBIOS Type 127 end marker is not the final table structure.' }
            $sawEnd = $true
            break
        }
    }
    if (-not $sawEnd) { throw 'SMBIOS table is missing its final Type 127 end marker.' }
    if ($count -lt 1 -or $maximum -lt 4) { throw 'SMBIOS table walk produced no complete structures.' }

    return [pscustomobject]@{
        structureCount = $count
        maximumStructureBytes = $maximum
        tableBytes = $Table.Length
        type1Count = [int]($typeCounts[1])
        type2Count = [int]($typeCounts[2])
        type127Count = [int]($typeCounts[127])
    }
}

function Assert-EpsAndTable($Records, $ReportValues) {
    $byName = @{}
    foreach ($record in $Records) { $byName[$record.name] = [byte[]]$record.content }
    $before = $byName['eps2-before.bin']; $after = $byName['eps2-after.bin']
    if ($before.Length -ne 31 -or $after.Length -ne 31 -or [Text.Encoding]::ASCII.GetString($before,0,4) -cne '_SM_' -or [Text.Encoding]::ASCII.GetString($after,0,4) -cne '_SM_') { throw 'SMBIOS2 entry point evidence must be two exact 31-byte _SM_ records.' }
    if ($before[5] -ne 31 -or $after[5] -ne 31 -or -not (Test-Checksum $before 0 31) -or -not (Test-Checksum $before 16 15) -or -not (Test-Checksum $after 0 31) -or -not (Test-Checksum $after 16 15)) { throw 'SMBIOS2 entry-point length/checksum validation failed.' }
    $changedOffsets = [Collections.Generic.List[int]]::new()
    for ($index = 0; $index -lt 31; $index++) {
        if ($before[$index] -eq $after[$index]) { continue }
        if ($index -notin @(4,8,9)) { throw "Prelude changed a forbidden SMBIOS2 EPS byte at offset $index." }
        $changedOffsets.Add($index)
    }
    $actualOffsets = if ($changedOffsets.Count -eq 0) { 'none_noop' } else { $changedOffsets -join ',' }
    if ([string]$ReportValues.eps2_changed_offsets -cne $actualOffsets) {
        throw ('SMBIOS2 EPS changed-offset report mismatch: expected=' + $actualOffsets + ' actual=' + [string]$ReportValues.eps2_changed_offsets)
    }
    $eps3Before = $byName['eps3-before.bin']; $eps3After = $byName['eps3-after.bin']
    if (($eps3Before.Length -ne 0 -and $eps3Before.Length -ne 24) -or $eps3After.Length -ne $eps3Before.Length) { throw 'SMBIOS3 entry-point records have unexpected sizes.' }
    if ([Convert]::ToBase64String($eps3Before) -cne [Convert]::ToBase64String($eps3After)) { throw 'SMBIOS3 entry point changed during correction.' }
    if ($eps3After.Length -eq 24 -and (-not (Test-Checksum $eps3After 0 24) -or [Text.Encoding]::ASCII.GetString($eps3After,0,5) -cne '_SM3_')) { throw 'SMBIOS3 entry-point signature/checksum validation failed.' }
    $tableBefore = $byName['table-before.bin']; $tableAfter = $byName['table-after.bin']
    if ($tableBefore.Length -le 0 -or [Convert]::ToBase64String($tableBefore) -cne [Convert]::ToBase64String($tableAfter)) { throw 'SMBIOS table bytes are empty or changed during correction.' }
    $walk = Measure-Smbios2Table $tableAfter
    $epsTableBytes = [int][BitConverter]::ToUInt16($after, 22)
    $epsStructureCount = [int][BitConverter]::ToUInt16($after, 28)
    $epsMaximumStructureBytes = [int][BitConverter]::ToUInt16($after, 8)
    $reportedTableBytes = [int]$ReportValues.smbios2_table_bytes
    $reportedCountBefore = [int]$ReportValues.smbios2_structure_count_before
    $reportedCountAfter = [int]$ReportValues.smbios2_structure_count_after
    $reportedMaximumBefore = [int]$ReportValues.smbios2_max_structure_before
    $reportedMaximumMeasured = [int]$ReportValues.smbios2_max_structure_measured
    $reportedMaximumAfter = [int]$ReportValues.smbios2_max_structure_after
    if ($epsTableBytes -ne $tableAfter.Length -or $reportedTableBytes -ne $tableAfter.Length) {
        throw "SMBIOS2 table length mismatch: EPS=$epsTableBytes report=$reportedTableBytes actual=$($tableAfter.Length)."
    }
    if ($epsStructureCount -ne $walk.structureCount -or $reportedCountBefore -ne $epsStructureCount -or $reportedCountAfter -ne $walk.structureCount) {
        throw "SMBIOS2 structure-count mismatch: EPS=$epsStructureCount reportBefore=$reportedCountBefore reportAfter=$reportedCountAfter walked=$($walk.structureCount)."
    }
    if ($epsMaximumStructureBytes -ne $walk.maximumStructureBytes -or $reportedMaximumBefore -ne [int][BitConverter]::ToUInt16($before, 8) -or $reportedMaximumMeasured -ne $walk.maximumStructureBytes -or $reportedMaximumAfter -ne $epsMaximumStructureBytes) {
        throw "SMBIOS2 maximum complete structure mismatch: EPS=$epsMaximumStructureBytes reportBefore=$reportedMaximumBefore reportMeasured=$reportedMaximumMeasured reportAfter=$reportedMaximumAfter walked=$($walk.maximumStructureBytes) tableBytes=$($tableAfter.Length)."
    }
    if ($walk.type1Count -ne 1 -or $walk.type2Count -ne 1 -or $walk.type127Count -ne 1) {
        throw "SMBIOS2 walk cardinality mismatch: Type1=$($walk.type1Count) Type2=$($walk.type2Count) Type127=$($walk.type127Count)."
    }
}

function New-RawEvidenceBundle([object[]]$Files) {
    Add-Type -AssemblyName System.IO.Compression
    $memory = [IO.MemoryStream]::new()
    try {
        $zip = [IO.Compression.ZipArchive]::new($memory, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($file in $Files) {
                if ([string]$file.archiveLeaf -notmatch '\Af\.[0-9]{4}\.[A-Za-z0-9]{1,10}\z') { throw 'Unsafe raw evidence archive name.' }
                $path = Join-Path $runRootFull ([string]$file.archiveLeaf)
                if ((Get-Sha256 $path) -cne [string]$file.sha256) { throw 'Raw evidence changed before archive creation.' }
                $entry = $zip.CreateEntry([string]$file.archiveLeaf, [IO.Compression.CompressionLevel]::Optimal)
                $stream = $entry.Open(); $input = [IO.File]::OpenRead($path)
                try { $input.CopyTo($stream) } finally { $input.Dispose(); $stream.Dispose() }
            }
        } finally { $zip.Dispose() }
        $bytes = $memory.ToArray()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') } finally { $sha.Dispose() }
        return [ordered]@{ format = 'zip'; bytes = $bytes.Length; sha256 = $hash; fileCount = $Files.Count; base64 = [Convert]::ToBase64String($bytes) }
    } finally { $memory.Dispose() }
}

function Add-CapturedFile([string]$Source, [string]$Name, [string]$Extension, $ValidatedRecord) {
    if ($null -eq $ValidatedRecord -or $null -eq $ValidatedRecord.PSObject.Properties['content']) { throw "Captured file requires a content-bearing validated record: $Name" }
    $sourceFullPath = [IO.Path]::GetFullPath($Source)
    $recordFullPath = [IO.Path]::GetFullPath([string]$ValidatedRecord.path)
    if (-not [string]::Equals($sourceFullPath, $recordFullPath, [StringComparison]::OrdinalIgnoreCase) -or [string]$ValidatedRecord.name -cne $Name) {
        throw "Validated evidence identity does not match archive request: expectedPath=$sourceFullPath actualPath=$recordFullPath expectedName=$Name actualName=$([string]$ValidatedRecord.name)"
    }
    $validatedBytes = [byte[]]$ValidatedRecord.content
    $validatedSha = Get-Sha256FromBytes $validatedBytes
    if ($validatedBytes.Length -ne [long]$ValidatedRecord.bytes -or $validatedSha -cne [string]$ValidatedRecord.sha256) {
        throw "Validated evidence record is internally inconsistent: name=${Name} expectedBytes=$([long]$ValidatedRecord.bytes) actualBytes=$($validatedBytes.Length) expectedSha256=$([string]$ValidatedRecord.sha256) actualSha256=$validatedSha"
    }
    $destinationLeaf = 'f.' + (@($metadata.capturedFiles).Count + 1).ToString('D4') + '.' + $Extension
    $destination = Join-Path $runRootFull $destinationLeaf
    if (Test-Path -LiteralPath $destination) { throw "Evidence destination already exists: $destination" }
    $stream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($validatedBytes, 0, $validatedBytes.Length) } finally { $stream.Dispose() }
    $destinationHash = Get-Sha256 $destination
    $destinationBytes = [long](Get-Item -LiteralPath $destination -ErrorAction Stop).Length
    if ($validatedSha -cne $destinationHash -or $destinationBytes -ne [long]$ValidatedRecord.bytes) { throw "Validated evidence archive mismatch for ${Name}: expectedBytes=$([long]$ValidatedRecord.bytes) actualBytes=$destinationBytes expectedSha256=$validatedSha actualSha256=$destinationHash" }
    $record = [ordered]@{ name = $Name; guestPath = $Source; archiveLeaf = $destinationLeaf; path = $destination; bytes = $destinationBytes; sha256 = $destinationHash }
    $metadata.capturedFiles += @($record)
    return $record
}

function Get-CanonicalBindingMap {
    $map = [ordered]@{}
    foreach ($key in @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')) {
        $map['SMBIOS:' + $key] = Get-ModeValue $config.smbios.PSObject.Properties[$key].Value 'replacement' $key
    }
    return $map
}

function Read-CollectorPatch([string]$EspRoot) {
    $path = Join-Path $EspRoot 'HypersinvSmbiosPatch.txt'
    $record = Get-SafeFileRecord $path 'HypersinvSmbiosPatch.txt'
    $textReport = Parse-TextReport ([byte[]]$record.content)
    $v = $textReport.values
    if ($v.result -cne 'applied' -or [int]$v.mask -ne 127 -or [string]$v.build_id -cne [string]$config.collectorBuildId -or $v.entries -notmatch '\A[1-9][0-9]*\z') {
        throw ('SMBIOS collector patch report mismatch: result=' + [string]$v.result + ' mask=' + [string]$v.mask + ' build_id=' + [string]$v.build_id + ' entries=' + [string]$v.entries)
    }
    $values = Get-CanonicalBindingMap
    $fieldRows = @($textReport.text -split "`r?`n" | Where-Object { $_ -match '^field\t' })
    if ($fieldRows.Count -ne ([int]$v.entries * 7)) { throw "Collector field report count mismatch: expected=$([int]$v.entries * 7) actual=$($fieldRows.Count)" }
    foreach ($line in $fieldRows) {
        $parts = $line.Split("`t")
        $bindingId = 'SMBIOS:' + $parts[2]
        if ($parts.Length -ne 5 -or -not $values.Contains($bindingId)) { throw "Malformed or unexpected collector field row: $line" }
        if ($parts[4] -cne $values[$bindingId]) { throw ('Collector replacement mismatch field=' + $parts[2] + ' expected=' + $values[$bindingId] + ' actual=' + $parts[4]) }
    }
    $result = [ordered]@{
        schema = 'combined-vm-smbios-patch-report.v1'
        path = $path
        sha256 = $record.sha256
        bytes = $record.bytes
        status = 'PASS'
        result = [string]$v.result
        mask = [int]$v.mask
        buildId = [string]$v.build_id
        entries = [int]$v.entries
        values = $values
        efiStatusSource = 'prelude.report.child_load_status'
        efiStatusPhase = if ($mode -ceq 'corrected-collector') { 'LoadImage(loader.efi)' } else { 'LoadImage(bootmgfw.efi)' }
        efiStatus = $null
        startStatusObserved = $false
    }
        return [pscustomobject]@{ record = $record; content = [byte[]]$record.content; metadata = $result }
}

function Remove-CorrectionDirectoryExact([string]$EspRoot, [string[]]$ExpectedNames) {
    $path = Join-Path $EspRoot $correctionRelative
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { return }
    $items = @(Get-ChildItem -LiteralPath $path -Force)
    if ($items.Count -ne $ExpectedNames.Count) { throw "Refusing to remove correction directory with unexpected file count: expected=$($ExpectedNames.Count) actual=$($items.Count)" }
    foreach ($item in $items) {
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $ExpectedNames -cnotcontains $item.Name) {
            throw "Refusing to remove unexpected correction subtree content: $($item.FullName)"
        }
    }
    foreach ($item in $items) {
        $operation = 'capture_remove_correction_' + $item.Name
        Invoke-JournaledFixtureMutation $operation 'CLEANING' { Remove-Item -LiteralPath $item.FullName -Force -ErrorAction Stop } | Out-Null
    }
    Invoke-JournaledFixtureMutation 'capture_remove_correction_directory' 'CLEANING' { Remove-Item -LiteralPath $path -Force -ErrorAction Stop } | Out-Null
}

function Read-FixtureJournal([string]$ModeName) {
    $path = Get-FixtureJournalPath $ModeName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    $item = Get-Item -LiteralPath $path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Fixture journal is a reparse point: $path" }
    $journal = Get-Content -LiteralPath $path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    Ensure-FixtureJournalProperties $journal | Out-Null
    if ([string]$journal.schema -cne 'combined-vm-fixture-correction-journal.v2' -or
        [string]$journal.lifecycleId -cne [string]$config.lifecycleId -or
        [string]$journal.vmUuid -cne [string]$config.vmUuid -or [string]$journal.mode -cne $ModeName) {
        throw "Fixture journal identity does not match lifecycle/VM/mode: $path"
    }
    if ([string]$journal.state -notin @('PREPARING','MUTATION_INTENT','PATHB_STAGED','PATHB_ARMED','CUSTOM_ENTRY_RESERVED','CORRECTION_DIRECTORY_CREATED','PRELUDE_WRITTEN','MODE_FLAG_WRITTEN','CUSTOM_BOOT_ENTRY_WRITTEN','CUSTOM_BOOT_NEXT_WRITTEN','ARMED','CAPTURE_IN_PROGRESS','CLEANING','RECOVERING','CAPTURED_AND_CLEANED','RECOVERY_CLEANED')) {
        throw "Fixture journal has an unsupported state: $($journal.state)"
    }
    $customAllocated = $null -ne $journal.customBootNumber
    if (($customAllocated -and ([int]$journal.customBootNumber -lt 0 -or [int]$journal.customBootNumber -gt 65535 -or
            [string]$journal.customBootName -cne (ConvertTo-EfiBootVariableName -Number ([int]$journal.customBootNumber)) -or
            [string]::IsNullOrWhiteSpace([string]$journal.customBootBytes) -or $null -eq $journal.customBootAttributes)) -or
        (-not $customAllocated -and ($null -ne $journal.customBootName -or $null -ne $journal.customBootBytes -or $null -ne $journal.customBootAttributes)) -or
        (-not $customAllocated -and [string]$journal.state -notin @('PREPARING','MUTATION_INTENT','PATHB_STAGED','PATHB_ARMED','RECOVERING')) -or
        [string]$journal.customPreludeRelative -cne $preludeRelative -or [string]$journal.preludeSha256 -cne [string]$config.preludeSha256) {
        throw "Fixture journal contains invalid custom boot ownership facts: $path"
    }
    return $journal
}

function Get-PendingFixtureJournal {
    $pending = [Collections.Generic.List[object]]::new()
    foreach ($candidateMode in @('correction-only','corrected-collector')) {
        $candidate = Read-FixtureJournal $candidateMode
        if ($null -ne $candidate -and [string]$candidate.state -notin @('CAPTURED_AND_CLEANED','RECOVERY_CLEANED')) { $pending.Add($candidate) }
    }
    if ($pending.Count -gt 1) { throw 'More than one unfinished fixture journal exists; refusing ambiguous recovery.' }
    if ($pending.Count -eq 0) { return $null }
    return $pending[0]
}

function Test-CapturedFileName([string]$Name, [string]$Sha256) {
    foreach ($record in @($metadata.capturedFiles)) {
        if ([string]$record.name -ceq $Name) {
            if ([string]$record.sha256 -cne $Sha256) { throw "Previously archived fixture evidence changed: $Name" }
            return $true
        }
    }
    return $false
}

function Archive-OwnedEspFile([string]$Path, [string]$Name, [string]$Extension) {
    $record = Get-SafeFileRecord $Path $Name
    if (-not (Test-CapturedFileName $Name $record.sha256)) { [void](Add-CapturedFile $Path $Name $Extension $record) }
    return $record
}

function Remove-CorrectionDirectoryForRecovery([string]$EspRoot, $Journal) {
    $path = Join-Path $EspRoot $correctionRelative
    if (-not (Assert-OwnedEspDirectory $path 'fixture correction')) { return }
    $allowed = @('smbios2-ep-prelude.efi','mode.txt','report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin')
    $items = @(Get-ChildItem -LiteralPath $path -Force)
    if ($items.Count -gt $allowed.Count) { throw 'Fixture recovery found too many correction-subtree entries.' }
    foreach ($item in $items) {
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $allowed -cnotcontains $item.Name) {
            throw "Fixture recovery found unowned correction-subtree content: $($item.FullName)"
        }
        switch -CaseSensitive ($item.Name) {
            'smbios2-ep-prelude.efi' { if ((Get-Sha256 $item.FullName) -cne [string]$Journal.preludeSha256) { throw 'Owned prelude hash differs from the journal.' } }
            'mode.txt' {
                $bytes = [IO.File]::ReadAllBytes($item.FullName)
                if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes([string]$Journal.modeFlag))) { throw 'Owned one-shot mode bytes differ from the journal.' }
            }
            'eps2-before.bin' { if ((Get-Item $item.FullName).Length -ne 31) { throw 'Owned SMBIOS2 before record has an unexpected size.' } }
            'eps2-after.bin' { if ((Get-Item $item.FullName).Length -ne 31) { throw 'Owned SMBIOS2 after record has an unexpected size.' } }
            'eps3-before.bin' { if ((Get-Item $item.FullName).Length -notin @(0,24)) { throw 'Owned SMBIOS3 before record has an unexpected size.' } }
            'eps3-after.bin' { if ((Get-Item $item.FullName).Length -notin @(0,24)) { throw 'Owned SMBIOS3 after record has an unexpected size.' } }
            'table-before.bin' { if ((Get-Item $item.FullName).Length -lt 1 -or (Get-Item $item.FullName).Length -gt 1048576) { throw 'Owned SMBIOS table before record has an unexpected size.' } }
            'table-after.bin' { if ((Get-Item $item.FullName).Length -lt 1 -or (Get-Item $item.FullName).Length -gt 1048576) { throw 'Owned SMBIOS table after record has an unexpected size.' } }
            'report.txt' { if ((Get-Item $item.FullName).Length -lt 32 -or (Get-Item $item.FullName).Length -gt 65536) { throw 'Owned prelude report has an unexpected size.' } }
        }
        $extension = if ($item.Name -in @('report.txt','mode.txt')) { 'txt' } elseif ($item.Name -ceq 'smbios2-ep-prelude.efi') { 'efi' } else { 'bin' }
        [void](Archive-OwnedEspFile $item.FullName $item.Name $extension)
    }
    foreach ($item in $items) {
        $operation = 'recovery_remove_correction_' + $item.Name
        Invoke-JournaledFixtureMutation $operation 'RECOVERING' { Remove-Item -LiteralPath $item.FullName -Force -ErrorAction Stop } | Out-Null
    }
    Invoke-JournaledFixtureMutation 'recovery_remove_correction_directory' 'RECOVERING' { Remove-Item -LiteralPath $path -Force -ErrorAction Stop } | Out-Null
    if (Test-Path -LiteralPath $path) { throw 'Fixture correction path remains after recovery removal.' }
}

function Remove-PathBStageForRecovery([string]$EspRoot, $Journal) {
    $stageRoot = Join-Path $EspRoot $efiStagePath
    if (-not (Assert-OwnedEspDirectory $stageRoot 'Path-B stage')) { return }
    $items = @(Get-ChildItem -LiteralPath $stageRoot -Force)
    if ($items.Count -gt 3) { throw 'Fixture recovery found extra Path-B stage entries.' }
    foreach ($item in $items) {
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $item.Name -notin @('loader.efi','inventory.efi','manifest.sha256')) {
            throw "Fixture recovery found unowned Path-B stage content: $($item.FullName)"
        }
        if ($item.Name -ceq 'loader.efi') {
            if ([long]$item.Length -ne [long]$Journal.stageExpected.loaderBytes) { throw 'Fixture recovery found a mismatched Path-B loader byte size.' }
            if ((Get-Sha256 $item.FullName) -cne [string]$Journal.stageExpected.loaderSha256) { throw 'Fixture recovery found a mismatched Path-B loader.' }
        }
        if ($item.Name -ceq 'inventory.efi') {
            if ([long]$item.Length -ne [long]$Journal.stageExpected.collectorBytes) { throw 'Fixture recovery found a mismatched Path-B collector byte size.' }
            if ((Get-Sha256 $item.FullName) -cne [string]$Journal.stageExpected.collectorSha256) { throw 'Fixture recovery found a mismatched Path-B collector.' }
        }
        if ($item.Name -ceq 'manifest.sha256' -and [IO.File]::ReadAllText($item.FullName, [Text.Encoding]::ASCII) -cne [string]$Journal.stageExpected.manifest) { throw 'Fixture recovery found a mismatched Path-B manifest.' }
    }
    foreach ($item in $items) {
        $operation = 'recovery_remove_stage_' + $item.Name
        Invoke-JournaledFixtureMutation $operation 'RECOVERING' { Remove-Item -LiteralPath $item.FullName -Force -ErrorAction Stop } | Out-Null
    }
    Invoke-JournaledFixtureMutation 'recovery_remove_stage_directory' 'RECOVERING' { Remove-Item -LiteralPath $stageRoot -Force -ErrorAction Stop } | Out-Null
    if (Test-Path -LiteralPath $stageRoot) { throw 'Path-B stage path remains after recovery removal.' }
}

function Remove-PathBStageAfterCapture($Journal) {
    if ($null -eq $Journal -or [string]$Journal.state -cne 'CLEANING') {
        throw 'Successful capture stage cleanup requires the durable CLEANING journal.'
    }
    if ([string]$Journal.espVolumeGuid -eq '' -or [string]$Journal.partition.partitionGuid -eq '' -or
        [string]$Journal.bootManagerSha256 -notmatch '\A[0-9A-F]{64}\z') {
        throw 'Successful capture stage cleanup is missing its journaled ESP identity or Boot Manager hash.'
    }
    if (@($Journal.espBefore.stageReportFiles).Count -ne 0 -or $Journal.espBefore.stageDirectoryExists -eq $true -or
        $Journal.espBefore.correctionDirectoryExists -eq $true) {
        throw 'Successful capture cleanup found a non-pristine journaled ESP baseline.'
    }

    $mount = Mount-Esp
    try {
        $espRoot = [string]$mount.root
        if ([string]$mount.volumeGuid -ine [string]$Journal.espVolumeGuid) {
            throw ('ESP volume changed before successful capture stage cleanup: expected=' + [string]$Journal.espVolumeGuid + ' actual=' + [string]$mount.volumeGuid)
        }
        $partitionFacts = Get-PartitionFacts $espRoot ([string]$mount.volumeGuid)
        if ([string]$partitionFacts.partitionGuid -ine [string]$Journal.partition.partitionGuid) {
            throw ('ESP partition changed before successful capture stage cleanup: expected=' + [string]$Journal.partition.partitionGuid + ' actual=' + [string]$partitionFacts.partitionGuid)
        }
        $bootManager = Join-Path $espRoot $bootManagerRelative
        if ((Get-Sha256 $bootManager) -cne [string]$Journal.bootManagerSha256) {
            throw 'Windows Boot Manager hash changed before successful capture stage cleanup.'
        }

        $before = Get-EspSnapshot $espRoot
        if (-not $before.stageDirectoryExists -or @($before.stageFiles).Count -ne 3 -or @($before.extraStageItems).Count -ne 0) {
            throw 'Path-B stage is not the exact three-file owned stage after firmware cleanup.'
        }
        $expectedNames = @('inventory.efi','loader.efi','manifest.sha256')
        $actualNames = @($before.stageFiles | ForEach-Object { [string]$_.name } | Sort-Object -CaseSensitive)
        if (($actualNames -join "`n") -cne (($expectedNames | Sort-Object -CaseSensitive) -join "`n")) {
            throw ('Path-B stage names differ after firmware cleanup: expected=' + ($expectedNames -join ',') + ' actual=' + ($actualNames -join ','))
        }
        if ($before.correctionDirectoryExists) { throw 'Fixture correction subtree remains after its evidence was captured.' }

        $inventoryReport = Join-Path $espRoot 'HypersinvEfiInventory.txt'
        if (Test-Path -LiteralPath $inventoryReport) {
            if (-not (Test-Path -LiteralPath $inventoryReport -PathType Leaf)) { throw 'Path-B inventory report path is not a regular file after Collect.' }
            throw 'Path-B Collect returned successfully but left HypersinvEfiInventory.txt on the ESP.'
        }
        $patchReport = Join-Path $espRoot 'HypersinvSmbiosPatch.txt'
        if (Test-Path -LiteralPath $patchReport) {
            if (-not (Test-Path -LiteralPath $patchReport -PathType Leaf)) { throw 'SMBIOS patch report path is not a regular file.' }
            if ([string]$Journal.mode -cne 'corrected-collector' -or $null -eq $metadata.collectorReport -or
                (Get-Sha256 $patchReport) -cne [string]$metadata.collectorReport.sha256) {
                throw 'SMBIOS patch report is not the exact active report already validated and archived during capture.'
            }
            [void](Archive-OwnedEspFile $patchReport 'HypersinvSmbiosPatch.txt' 'txt')
            Invoke-JournaledFixtureMutation 'capture_remove_HypersinvSmbiosPatch.txt' 'CLEANING' {
                Remove-Item -LiteralPath $patchReport -Force -ErrorAction Stop
            } | Out-Null
        } elseif ([string]$Journal.mode -ceq 'corrected-collector') {
            throw 'Validated active SMBIOS patch report disappeared before owned ESP cleanup.'
        }

        $beforeStageRemoval = Get-EspSnapshot $espRoot
        if ($beforeStageRemoval.bootManagerSha256 -cne [string]$Journal.bootManagerSha256 -or
            $beforeStageRemoval.correctionDirectoryExists -or @($beforeStageRemoval.stageReportFiles).Count -ne 0 -or
            @($beforeStageRemoval.stageFiles).Count -ne 3) {
            throw 'Tracked ESP contents changed unexpectedly before Path-B stage removal.'
        }
        Remove-PathBStageForRecovery $espRoot $Journal
        $after = Get-EspSnapshot $espRoot
        $stagePath = Join-Path $espRoot $efiStagePath
        $correctionPath = Join-Path $espRoot $correctionRelative
        if ((Test-Path -LiteralPath $stagePath) -or (Test-Path -LiteralPath $correctionPath) -or
            $after.stageDirectoryExists -or @($after.stageFiles).Count -ne 0 -or @($after.extraStageItems).Count -ne 0 -or
            $after.correctionDirectoryExists -or @($after.stageReportFiles).Count -ne 0 -or
            $after.bootManagerSha256 -cne [string]$Journal.bootManagerSha256) {
            throw 'Tracked ESP state differs from its clean pre-stage baseline after successful capture cleanup.'
        }
        $metadata.esp.afterCleanup = $after
        $metadata.cleanup.pathBStageRemoved = $true
        $metadata.cleanup.trackedEspBaselineRestored = $true
    } finally { Dismount-Esp $mount }
}

function Assert-RecoveryVmIdentity {
    $systemRows = @(Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop)
    $productRows = @(Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop)
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -Filter "Index=0" -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq '\\.\PHYSICALDRIVE0' })
    if ($systemRows.Count -ne 1 -or $productRows.Count -ne 1 -or $diskRows.Count -ne 1 -or [string]$diskRows[0].PNPDeviceID -cne [string]$config.diskPnp) {
        throw 'Recovery VM guard failed: system/product/disk cardinality or exact VMware disk PnP mismatch.'
    }
    $tuples = @(
        @((Get-ModeValue $config.smbios.system_vendor 'original' 'system_vendor'),(Get-ModeValue $config.smbios.system_product 'original' 'system_product'),(Get-ModeValue $config.smbios.system_uuid 'original' 'system_uuid')),
        @((Get-ModeValue $config.smbios.system_vendor 'replacement' 'system_vendor'),(Get-ModeValue $config.smbios.system_product 'replacement' 'system_product'),(Get-ModeValue $config.smbios.system_uuid 'replacement' 'system_uuid'))
    )
    $accepted = $false
    foreach ($tuple in $tuples) {
        if ([string]$systemRows[0].Manufacturer -ceq [string]$tuple[0] -and [string]$systemRows[0].Model -ceq [string]$tuple[1] -and
            [string]$productRows[0].Vendor -ceq [string]$tuple[0] -and [string]$productRows[0].Name -ceq [string]$tuple[1] -and [string]$productRows[0].UUID -ceq [string]$tuple[2]) {
            $accepted = $true
        }
    }
    if (-not $accepted) { throw 'Recovery VM guard failed: identity is neither the exact original VMware tuple nor the locked replacement tuple.' }
}

function Invoke-FixtureRecovery($Journal) {
    if ([string]$Journal.state -in @('CAPTURED_AND_CLEANED','RECOVERY_CLEANED')) { return }
    Assert-RecoveryVmIdentity
    $script:fixtureJournal = $Journal
    $script:fixtureJournalPath = Get-FixtureJournalPath ([string]$Journal.mode)
    $mount = Mount-Esp
    try {
        $espRoot = [string]$mount.root
        $partitionFacts = Get-PartitionFacts $espRoot ([string]$mount.volumeGuid)
        if ([string]$mount.volumeGuid -cne [string]$Journal.espVolumeGuid -or [string]$partitionFacts.partitionGuid -ine [string]$Journal.partition.partitionGuid) { throw 'Recovery ESP volume/partition differs from the durable intent journal.' }
        if ((Get-Sha256 (Join-Path $espRoot $bootManagerRelative)) -cne [string]$Journal.bootManagerSha256) { throw 'Windows Boot Manager differs from the journaled pre-stage hash.' }
        $bootOrder = Read-Variable 'BootOrder'
        $expectedOrder = [Convert]::FromBase64String([string]$Journal.bootOrderBefore)
        if (-not (Test-EfiVariableRecordEquals -Actual $bootOrder -ExpectedBytes $expectedOrder -ExpectedAttributes ([uint32]$Journal.bootOrderAttributes))) { throw 'Recovery refused because BootOrder differs from the durable baseline.' }

        $customAllocated = $null -ne $Journal.customBootNumber
        $customNumber = $null
        $customName = $null
        $customBytes = $null
        $customEntry = $null
        if ($customAllocated) {
            $customNumber = [int]$Journal.customBootNumber
            $customName = [string]$Journal.customBootName
            $customBytes = [Convert]::FromBase64String([string]$Journal.customBootBytes)
            $customEntry = Read-VariableOrAbsent $customName
            if ($customEntry.supported -eq $true) {
                if (-not (Test-EfiVariableRecordEquals -Actual $customEntry -ExpectedBytes $customBytes -ExpectedAttributes ([uint32]$Journal.customBootAttributes))) { throw 'Recovery refused a custom Boot#### whose bytes/attributes differ from the journal.' }
            } elseif ([int]$customEntry.win32Error -ne 203) { throw 'Recovery could not establish whether the custom Boot#### exists.' }
        }

        $bootNext = Read-VariableOrAbsent 'BootNext'
        $standardNumber = $null
        if ($bootNext.supported -eq $true) {
            $nextNumber = Get-BootOptionIndex 'BootNext' $bootNext
            if ($customAllocated -and $nextNumber -eq $customNumber) {
                $expectedNext = [BitConverter]::GetBytes([uint16]$customNumber)
                if (-not (Test-EfiVariableRecordEquals -Actual $bootNext -ExpectedBytes $expectedNext -ExpectedAttributes 7)) { throw 'Recovery refused a custom BootNext with unexpected bytes/attributes.' }
                Invoke-JournaledFixtureMutation 'recovery_remove_custom_boot_next' 'RECOVERING' { Remove-VariableExact 'BootNext' $expectedNext 7 } | Out-Null
            } elseif ($null -ne $Journal.standardBootNumber -and $nextNumber -eq [int]$Journal.standardBootNumber) {
                $standardNumber = $nextNumber
                $standardExpected = [Convert]::FromBase64String([string]$Journal.standardBootBytes)
                if (-not (Test-EfiVariableRecordEquals -Actual $bootNext -ExpectedBytes ([BitConverter]::GetBytes([uint16]$standardNumber)) -ExpectedAttributes 7)) { throw 'Recovery found a noncanonical standard BootNext.' }
                $actualStandard = Read-Variable (ConvertTo-EfiBootVariableName -Number $standardNumber)
                if (-not (Test-EfiVariableRecordEquals -Actual $actualStandard -ExpectedBytes $standardExpected -ExpectedAttributes ([uint32]$Journal.standardBootAttributes))) { throw 'Recovery refused a standard Boot#### that differs from the journal.' }
            } elseif ([bool]$Journal.pathBArmIntent) {
                $possibleName = ConvertTo-EfiBootVariableName -Number $nextNumber
                $possibleEntry = Read-Variable $possibleName
                $possibleBytes = [Convert]::FromBase64String([string]$possibleEntry.bytesBase64)
                $possible = Read-EfiBootLoadOptionBytes -Bytes $possibleBytes
                if ([string]$possible.filePath -cne $loaderRelative -or [string]$possible.hardDrive.partitionGuid -ine [string]$Journal.partition.partitionGuid -or [uint32]$possibleEntry.attributes -ne 7) {
                    throw 'Recovery found BootNext does not target the exact staged Path-B loader; it will not clear an unrelated option.'
                }
                $standardNumber = $nextNumber
            } else { throw 'Recovery found BootNext targets an unrelated Boot####; refusing mutation.' }
        } elseif ($bootNext.supported -ne $false -or [int]$bootNext.win32Error -ne 203) { throw 'Recovery could not establish BootNext absence or exact ownership.' }
        elseif ([bool]$Journal.pathBArmIntent -and $null -eq $Journal.standardBootNumber) {
            throw 'Recovery cannot identify whether Path-B Arm created an entry because its intent is durable but BootNext is absent; use the authoritative Clean4 snapshot restore.'
        }

        if ($customAllocated -and $customEntry.supported -eq $true) {
            Invoke-JournaledFixtureMutation 'recovery_remove_custom_boot_entry' 'RECOVERING' { Remove-VariableExact $customName $customBytes ([uint32]$Journal.customBootAttributes) } | Out-Null
        }
        $standardName = if ($null -ne $Journal.standardBootName) { [string]$Journal.standardBootName } elseif ($null -ne $standardNumber) { ConvertTo-EfiBootVariableName -Number $standardNumber } else { $null }
        $standardEntryPresent = $false
        if ($standardName) {
            $standardRecord = Read-VariableOrAbsent $standardName
            if ($standardRecord.supported -eq $true) {
                if ($null -ne $Journal.standardBootBytes) {
                    $standardExpected = [Convert]::FromBase64String([string]$Journal.standardBootBytes)
                    if (-not (Test-EfiVariableRecordEquals -Actual $standardRecord -ExpectedBytes $standardExpected -ExpectedAttributes ([uint32]$Journal.standardBootAttributes))) { throw 'Recovery refused to disarm a standard Boot#### whose bytes differ from the journal.' }
                } else {
                    $candidate = Read-EfiBootLoadOptionBytes -Bytes ([Convert]::FromBase64String([string]$standardRecord.bytesBase64))
                    if ([string]$candidate.filePath -cne $loaderRelative -or [string]$candidate.hardDrive.partitionGuid -ine [string]$Journal.partition.partitionGuid -or [uint32]$standardRecord.attributes -ne 7) { throw 'Recovery refused a standard Boot#### that does not target the exact Path-B loader.' }
                }
                $standardEntryPresent = $true
            } elseif ([int]$standardRecord.win32Error -ne 203) { throw 'Recovery could not establish standard Boot#### state.' }
        }
        if ($standardEntryPresent -and (Test-Path -LiteralPath ([string]$Journal.pathBReceiptPath) -PathType Leaf)) {
            if (-not [string]::IsNullOrWhiteSpace([string]$Journal.pathBReceiptSha256) -and (Get-Sha256 ([string]$Journal.pathBReceiptPath)) -cne [string]$Journal.pathBReceiptSha256) { throw 'Path-B receipt differs from the journal before exact Disarm.' }
            $stage = Get-StageSources
            $handoff = Invoke-JournaledFixtureMutation 'recovery_disarm_pathb' 'RECOVERING' {
                Invoke-PathBWithEspHandoff -ModeName 'Disarm' -Stage $stage -Mount $mount -RemountAfter `
                    -ExpectedVolumeGuid ([string]$Journal.espVolumeGuid) -ExpectedPartitionGuid ([string]$Journal.partition.partitionGuid) `
                    -ExpectedBootManagerSha256 ([string]$Journal.bootManagerSha256)
            }
            $mount = $handoff.mount
            $espRoot = [string]$mount.root
        }

        $customFinal = if ($customAllocated) { Read-VariableOrAbsent $customName } else { $null }
        $nextFinal = Read-VariableOrAbsent 'BootNext'
        if ($customAllocated -and ($customFinal.supported -ne $false -or [int]$customFinal.win32Error -ne 203)) { throw 'Owned custom Boot#### remains after recovery.' }
        if ($nextFinal.supported -ne $false -or [int]$nextFinal.win32Error -ne 203) { throw 'BootNext remains set after recovery.' }
        if ($standardName) {
            $standardFinal = Read-VariableOrAbsent $standardName
            if ($standardFinal.supported -ne $false -or [int]$standardFinal.win32Error -ne 203) { throw 'Owned standard Path-B Boot#### remains after recovery.' }
        }

        Remove-CorrectionDirectoryForRecovery $espRoot $Journal
        foreach ($rootFile in @(
            @{ name = 'HypersinvEfiInventory.txt'; ext = 'txt' },
            @{ name = 'HypersinvSmbiosPatch.txt'; ext = 'txt' }
        )) {
            $rootPath = Join-Path $espRoot $rootFile.name
            if (Test-Path -LiteralPath $rootPath -PathType Leaf) {
                if ($Journal.espBefore.stageReportFiles.Count -ne 0) { throw 'Recovery found a collector report despite a nonempty journal baseline.' }
                [void](Archive-OwnedEspFile $rootPath $rootFile.name $rootFile.ext)
                Invoke-JournaledFixtureMutation ('recovery_remove_' + $rootFile.name) 'RECOVERING' { Remove-Item -LiteralPath $rootPath -Force -ErrorAction Stop }
            } elseif (Test-Path -LiteralPath $rootPath) { throw "Recovery found an unsafe collector report path: $rootPath" }
        }
        Remove-PathBStageForRecovery $espRoot $Journal
        $after = Get-EspSnapshot $espRoot
        if ((Test-Path -LiteralPath (Join-Path $espRoot $efiStagePath)) -or
            (Test-Path -LiteralPath (Join-Path $espRoot $correctionRelative)) -or
            $after.stageDirectoryExists -or $after.correctionDirectoryExists -or $after.stageReportFiles.Count -ne 0 -or
            $after.bootManagerSha256 -cne [string]$Journal.bootManagerSha256) { throw 'Recovery ESP readback differs from the clean pre-stage baseline.' }
        $finalOrder = Read-Variable 'BootOrder'
        if (-not (Test-EfiVariableRecordEquals -Actual $finalOrder -ExpectedBytes $expectedOrder -ExpectedAttributes ([uint32]$Journal.bootOrderAttributes))) { throw 'BootOrder changed during fixture recovery.' }
        $script:fixtureJournal.pendingMutation = $null
        $script:fixtureJournal.state = 'RECOVERY_CLEANED'
        $script:fixtureJournal.cleanupCompletedUtc = [DateTime]::UtcNow.ToString('o')
        Save-FixtureJournal
        $metadata.cleanup.attempted = $true
        $metadata.cleanup.result = 'RECOVERY_CLEANED_EXACT_OWNED_STATE'
        $metadata.esp.afterCapture = $after
        $metadata.firmware.bootOrderAfter = $finalOrder
        $metadata.firmware.bootNextAfter = $nextFinal
        $metadata.journal = [ordered]@{ path = $script:fixtureJournalPath; sha256 = Get-Sha256 $script:fixtureJournalPath; state = 'RECOVERY_CLEANED' }
    } finally { Dismount-Esp $mount }
}

function Stage-And-Arm {
    $identity = Get-Identity 'original'
    Assert-PhaseIdentity $identity 'original'
    $secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop)
    $sbVariable = Read-Variable 'SecureBoot'
    if ($secureBoot -ne $false -or [string]$sbVariable.bytesHex -cne '00' -or [int]$sbVariable.attributes -ne 6) { throw 'Guest Secure Boot is not OFF with the expected readable variable; refusing to arm.' }
    $metadata.firmware.secureBoot = $secureBoot
    $metadata.firmware.secureBootVariable = $sbVariable
    $bootOrder = Read-Variable 'BootOrder'
    $bootNext = Read-VariableOrAbsent 'BootNext'
    if ([string]$bootOrder.bytesHex -cne [string]$config.expectedBootOrderHex) { throw "BootOrder baseline mismatch expected=$($config.expectedBootOrderHex) actual=$($bootOrder.bytesHex)" }
    if ($bootNext.supported -ne $false -or [int]$bootNext.win32Error -ne 203) { throw 'BootNext is not absent before staging.' }
    $metadata.firmware.bootOrderBefore = $bootOrder
    $metadata.firmware.bootNextBefore = $bootNext
    $stage = Get-StageSources
    $mount = Mount-Esp
    $espRoot = [string]$mount.root
    try {
        $partitionFacts = Get-PartitionFacts $espRoot ([string]$mount.volumeGuid)
        $before = Get-EspSnapshot $espRoot
        $metadata.esp.before = $before
        if ([string]$before.bootManagerSha256 -cne [string]$config.expectedBootManagerSha256) { throw 'Windows Boot Manager differs from its pinned pre-run hash.' }
        if ($before.stageDirectoryExists -or $before.correctionDirectoryExists -or @($before.stageReportFiles).Count -gt 0) { throw 'ESP contains an existing owned stage, correction subtree, or collector report.' }
    } finally { Dismount-Esp $mount }

    $journalPath = Get-FixtureJournalPath $mode
    if (Test-Path -LiteralPath $journalPath) { throw "Existing fixture journal would be overwritten: $journalPath" }
    $receiptPath = Join-Path ([string]$stage.inventoryBuild) 'path-b-stage.json'
    $script:fixtureJournalPath = $journalPath
    $script:fixtureJournal = [ordered]@{
        schema = 'combined-vm-fixture-correction-journal.v2'
        lifecycleId = [string]$config.lifecycleId
        vmUuid = [string]$config.vmUuid
        mode = $mode
        state = 'PREPARING'
        pendingMutation = $null
        completedMutations = @()
        startedUtc = [DateTime]::UtcNow.ToString('o')
        captureStartedUtc = $null
        stageExitCode = $null
        armExitCode = $null
        cleanupCompletedUtc = $null
        updatedUtc = $null
        partition = $partitionFacts
        espVolumeGuid = [string]$mount.volumeGuid
        bootManagerSha256 = [string]$before.bootManagerSha256
        espBefore = $before
        bootOrderBefore = [string]$bootOrder.bytesBase64
        bootOrderAttributes = [uint32]$bootOrder.attributes
        bootNextBefore = $null
        bootNextAttributes = $null
        standardBootNumber = $null
        standardBootName = $null
        standardBootBytes = $null
        standardBootAttributes = $null
        customBootNumber = $null
        customBootName = $null
        customBootBytes = $null
        customBootAttributes = $null
        customPreludeRelative = $preludeRelative
        preludeSha256 = [string]$config.preludeSha256
        modeFlag = $mode
        stageExpected = [ordered]@{
            loaderBytes = [long]$stage.loaderBytes
            loaderSha256 = [string]$config.assets.inventoryLoaderSha256
            collectorBytes = [long]$stage.collectorBytes
            collectorSha256 = [string]$config.assets.collectorSha256
            manifest = (Get-PathBManifestText $stage)
        }
        pathBReceiptPath = $receiptPath
        pathBReceiptSha256 = $null
        pathBArmIntent = $false
    }
    $script:fixtureJournal.bootNextBefore = [string]$bootNext.bytesBase64
    $script:fixtureJournal.bootNextAttributes = if ($bootNext.supported -eq $true) { [uint32]$bootNext.attributes } else { $null }
    Save-FixtureJournal
    $script:fixtureJournalOwnedThisInvocation = $true

    $stageResult = Invoke-JournaledFixtureMutation 'pathb_stage' 'PATHB_STAGED' {
        (Invoke-PathBWithEspHandoff -ModeName 'Stage' -Stage $stage).pathBResult
    }
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'Path-B Stage returned without its exact stage receipt.' }
    $script:fixtureJournal.pathBReceiptSha256 = Get-Sha256 $receiptPath
    Save-FixtureJournal
    $script:fixtureJournal.pathBArmIntent = $true
    Save-FixtureJournal
    $armResult = Invoke-JournaledFixtureMutation 'pathb_arm' 'PATHB_ARMED' {
        (Invoke-PathBWithEspHandoff -ModeName 'Arm' -Stage $stage).pathBResult
    }
    $standardBootNext = Read-Variable 'BootNext'
    $standardNumber = Get-BootOptionIndex 'BootNext' $standardBootNext
    $standardName = ConvertTo-EfiBootVariableName -Number $standardNumber
    $standardEntry = Read-Variable $standardName
    $standardBytes = [Convert]::FromBase64String([string]$standardEntry.bytesBase64)
    $standardParsed = Read-EfiBootLoadOptionBytes -Bytes $standardBytes
    if ([string]$standardParsed.filePath -cne $loaderRelative -or [string]$standardParsed.hardDrive.partitionGuid -ine [string]$partitionFacts.partitionGuid -or
        [uint32]$standardEntry.attributes -ne 7 -or [uint32]$bootOrder.attributes -ne [uint32](Read-Variable 'BootOrder').attributes) {
        throw 'Path-B armed entry is not the exact staged loader on the current ESP partition.'
    }
    $script:fixtureJournal.standardBootNumber = $standardNumber
    $script:fixtureJournal.standardBootName = $standardName
    $script:fixtureJournal.standardBootBytes = [string]$standardEntry.bytesBase64
    $script:fixtureJournal.standardBootAttributes = [uint32]$standardEntry.attributes
    Save-FixtureJournal
    $customNumber = Get-FreeBootNumber -ExcludeNumbers @($standardNumber)
    if ($customNumber -eq $standardNumber) { throw 'Custom Boot#### allocation collided with the armed Path-B entry.' }
    $customName = ConvertTo-EfiBootVariableName -Number $customNumber
    $customBytes = New-EfiBootLoadOptionBytes -Description ('Hypersinv SMBIOS fixture ' + $mode) -PartitionNumber $partitionFacts.partitionNumber -PartitionStartLba $partitionFacts.startLba -PartitionSizeLba $partitionFacts.sizeLba -PartitionGuid ([Guid]$partitionFacts.partitionGuid) -FilePath $preludeRelative
    $script:fixtureJournal.customBootNumber = $customNumber
    $script:fixtureJournal.customBootName = $customName
    $script:fixtureJournal.customBootBytes = [Convert]::ToBase64String($customBytes)
    $script:fixtureJournal.customBootAttributes = [uint32]7
    $script:fixtureJournal.state = 'CUSTOM_ENTRY_RESERVED'
    Save-FixtureJournal
    $afterStage = $null
    $mount = Mount-Esp
    $espRoot = [string]$mount.root
    try {
        $afterStage = Get-EspSnapshot $espRoot
        $metadata.esp.afterStageArm = $afterStage
        if ($afterStage.bootManagerSha256 -cne [string]$before.bootManagerSha256 -or $afterStage.stageFiles.Count -ne 3 -or $afterStage.correctionDirectoryExists) {
            throw 'Path-B Stage/Arm changed a protected file or did not leave exactly three stage files.'
        }
        $stageNameHashes = @{}
        foreach ($entry in $afterStage.stageFiles) { $stageNameHashes[[string]$entry.name] = [string]$entry.sha256 }
        if ($stageNameHashes['loader.efi'] -cne [string]$config.assets.inventoryLoaderSha256 -or
            $stageNameHashes['inventory.efi'] -cne [string]$config.assets.collectorSha256 -or
            -not $stageNameHashes.ContainsKey('manifest.sha256')) { throw 'Staged Path-B loader/collector hashes do not match pinned inputs.' }
        $correctionRoot = Join-Path $espRoot $correctionRelative
        [void](Invoke-JournaledFixtureMutation 'create_correction_directory' 'CORRECTION_DIRECTORY_CREATED' { New-Item -ItemType Directory -Path $correctionRoot -ErrorAction Stop | Out-Null })
        $preludePath = Join-Path $correctionRoot 'smbios2-ep-prelude.efi'
        $preludeBytes = [Convert]::FromBase64String([string]$config.preludeBase64)
        try { [void](Invoke-JournaledFixtureMutation 'write_prelude' 'PRELUDE_WRITTEN' { [IO.File]::WriteAllBytes($preludePath, $preludeBytes) }) } finally { [Array]::Clear($preludeBytes, 0, $preludeBytes.Length) }
        if ((Get-Sha256 $preludePath) -cne [string]$config.preludeSha256) { throw 'Staged prelude hash does not match the reviewed build.' }
        $modePath = Join-Path $correctionRoot 'mode.txt'
        $modeBytes = [Text.Encoding]::ASCII.GetBytes([string]$mode)
        $expectedModeLength = if ($mode -ceq 'correction-only') { 15 } else { 19 }
        if ($modeBytes.Length -ne $expectedModeLength) { throw 'One-shot fixture mode has an unexpected byte length.' }
        [void](Invoke-JournaledFixtureMutation 'write_mode_flag' 'MODE_FLAG_WRITTEN' { [IO.File]::WriteAllBytes($modePath, $modeBytes) })
        $modeReadback = [IO.File]::ReadAllBytes($modePath)
        if ([Convert]::ToBase64String($modeBytes) -cne [Convert]::ToBase64String($modeReadback)) { throw 'One-shot mode flag readback mismatch.' }
        [void](Invoke-JournaledFixtureMutation 'write_custom_boot_entry' 'CUSTOM_BOOT_ENTRY_WRITTEN' { Write-Variable $customName $customBytes 7 })
        $customEntry = Read-Variable $customName
        if (-not (Test-EfiVariableRecordEquals -Actual $customEntry -ExpectedBytes $customBytes -ExpectedAttributes 7)) { throw 'Custom Boot#### readback differs from exact requested bytes/attributes.' }
        $customParsed = Read-EfiBootLoadOptionBytes -Bytes $customBytes
        if ($customParsed.filePath -cne $preludeRelative -or $customParsed.hardDrive.partitionGuid -ine $partitionFacts.partitionGuid) { throw 'Custom Boot#### does not target the approved prelude on the current ESP.' }
        if (-not (Test-EfiVariableRecordEquals -Actual $standardEntry -ExpectedBytes $standardBytes -ExpectedAttributes 7)) { throw 'Standard Path-B Boot#### changed before custom BootNext.' }
        $bootNextBytes = [BitConverter]::GetBytes([uint16]$customNumber)
        [void](Invoke-JournaledFixtureMutation 'write_boot_next' 'CUSTOM_BOOT_NEXT_WRITTEN' { Write-Variable 'BootNext' $bootNextBytes 7 })
        $bootNextAfter = Read-Variable 'BootNext'
        if (-not (Test-EfiVariableRecordEquals -Actual $bootNextAfter -ExpectedBytes $bootNextBytes -ExpectedAttributes 7)) { throw 'BootNext did not read back as the exact custom fixture entry.' }
        $orderAfter = Read-Variable 'BootOrder'
        if (-not (Test-EfiVariableRecordEquals -Actual $orderAfter -ExpectedBytes ([Convert]::FromBase64String([string]$bootOrder.bytesBase64)) -ExpectedAttributes ([uint32]$bootOrder.attributes))) { throw 'BootOrder changed while setting custom BootNext.' }
        $metadata.firmware.bootNextAfter = $bootNextAfter
        $metadata.firmware.bootOrderAfter = $orderAfter
        $metadata.firmware.standardBootEntry = [ordered]@{ name = $standardName; bytes = $standardBytes.Length; sha256 = Get-Sha256FromBytes $standardBytes; attributes = [uint32]$standardEntry.attributes; filePath = $standardParsed.filePath }
        $metadata.firmware.customBootEntry = [ordered]@{ name = $customName; number = $customNumber; bytes = $customBytes.Length; sha256 = Get-Sha256FromBytes $customBytes; attributes = 7; filePath = $customParsed.filePath }
        $metadata.esp.afterStageArm = Get-EspSnapshot $espRoot
        if ($metadata.esp.afterStageArm.stageFiles.Count -ne 3 -or $metadata.esp.afterStageArm.correctionDirectoryExists -ne $true) { throw 'Fixture subtree was not staged alongside the unchanged three Path-B files.' }
    } finally { Dismount-Esp $mount }
    $script:fixtureJournal.stageExitCode = $stageResult.exitCode
    $script:fixtureJournal.armExitCode = $armResult.exitCode
    $script:fixtureJournal.pathBReceiptSha256 = Get-Sha256 $receiptPath
    $script:fixtureJournal.pendingMutation = $null
    $script:fixtureJournal.state = 'ARMED'
    Save-FixtureJournal
    $metadata.firmware.standardBootEntry = [ordered]@{ name = $standardName; number = $standardNumber; bytes = $standardBytes.Length; sha256 = Get-Sha256FromBytes $standardBytes; attributes = [uint32]$standardEntry.attributes; filePath = $standardParsed.filePath }
    $metadata.firmware.customBootEntry = [ordered]@{ name = $customName; number = $customNumber; bytes = $customBytes.Length; sha256 = Get-Sha256FromBytes $customBytes; attributes = 7; filePath = $customParsed.filePath }
    $metadata.firmware.bootNextAfter = Read-Variable 'BootNext'
    $metadata.firmware.bootOrderAfter = Read-Variable 'BootOrder'
    $metadata.journal = [ordered]@{ path = $journalPath; sha256 = Get-Sha256 $journalPath; state = 'ARMED' }
    $metadata.status = 'PASS'
    $result.Status = 'PASS'
    $result.Message = "Path-B was armed and BootNext was redirected to the isolated $mode prelude entry; BootOrder and Windows Boot Manager remained unchanged. No reboot was started."
}

function Get-ExpectedJournal {
    $journal = Read-FixtureJournal $mode
    if ($null -eq $journal) { throw "Exact $mode stage journal is missing." }
    if ([string]$journal.state -cne 'ARMED') { throw "Stage journal is not armed for capture: state=$($journal.state)" }
    if ((Get-Sha256 ([string]$journal.pathBReceiptPath)) -cne [string]$journal.pathBReceiptSha256) { throw 'Path-B receipt changed since the staged journal was written.' }
    $script:fixtureJournal = $journal
    $script:fixtureJournalPath = Get-FixtureJournalPath $mode
    $script:fixtureJournalOwnedThisInvocation = $true
    return $journal
}

function Capture-AfterBoot {
    $journal = Get-ExpectedJournal
    $script:fixtureJournal.state = 'CAPTURE_IN_PROGRESS'
    $script:fixtureJournal.captureStartedUtc = [DateTime]::UtcNow.ToString('o')
    Save-FixtureJournal
    $phaseMode = if ($mode -ceq 'correction-only') { 'original' } else { 'replacement' }
    $identity = Get-Identity $phaseMode
    Assert-PhaseIdentity $identity $phaseMode
    $metadata.bootIdentity = Get-BootIdentity
    $metadata.esp.before = $null
    $metadata.firmware.secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop)
    $metadata.firmware.secureBootVariable = Read-Variable 'SecureBoot'
    $metadata.firmware.bootOrderBefore = Read-Variable 'BootOrder'
    $metadata.firmware.bootNextBefore = Read-VariableOrAbsent 'BootNext'
    $metadata.firmware.bootCurrent = Read-Variable 'BootCurrent'
    $customNumber = [int]$journal.customBootNumber
    if ((Get-BootOptionIndex 'BootCurrent' $metadata.firmware.bootCurrent) -ne $customNumber) { throw 'Firmware BootCurrent does not identify the exact owned prelude entry.' }
    if ($metadata.firmware.bootNextBefore.supported -ne $false -or [int]$metadata.firmware.bootNextBefore.win32Error -ne 203) { throw 'BootNext is not absent after the one-shot boot.' }
    $expectedOrder = [Convert]::FromBase64String([string]$journal.bootOrderBefore)
    if (-not (Test-EfiVariableRecordEquals -Actual $metadata.firmware.bootOrderBefore -ExpectedBytes $expectedOrder -ExpectedAttributes ([uint32]$journal.bootOrderAttributes))) { throw 'BootOrder differs from its pre-arm bytes/attributes.' }
    $customRecord = Read-Variable ([string]$journal.customBootName)
    $customBytes = [Convert]::FromBase64String([string]$journal.customBootBytes)
    if (-not (Test-EfiVariableRecordEquals -Actual $customRecord -ExpectedBytes $customBytes -ExpectedAttributes ([uint32]$journal.customBootAttributes))) { throw 'Custom Boot#### differs from the exact owned pre-arm bytes/attributes.' }
    $standardRecord = Read-Variable ([string]$journal.standardBootName)
    $standardBytes = [Convert]::FromBase64String([string]$journal.standardBootBytes)
    if (-not (Test-EfiVariableRecordEquals -Actual $standardRecord -ExpectedBytes $standardBytes -ExpectedAttributes ([uint32]$journal.standardBootAttributes))) { throw 'Standard Path-B Boot#### changed during the prelude boot.' }
    $metadata.firmware.standardBootEntry = [ordered]@{ name = [string]$journal.standardBootName; bytes = $standardBytes.Length; sha256 = Get-Sha256FromBytes $standardBytes; attributes = [uint32]$journal.standardBootAttributes }
    $metadata.firmware.customBootEntry = [ordered]@{ name = [string]$journal.customBootName; number = $customNumber; bytes = $customBytes.Length; sha256 = Get-Sha256FromBytes $customBytes; attributes = [uint32]$journal.customBootAttributes }

    $mount = Mount-Esp
    $espRoot = [string]$mount.root
    $captured = [Collections.Generic.List[object]]::new()
    $rawEvidenceRecords = [Collections.Generic.List[object]]::new()
    $correctionRoot = Join-Path $espRoot $correctionRelative
    try {
        $esp = Get-EspSnapshot $espRoot
        if ($esp.bootManagerSha256 -cne [string]$journal.bootManagerSha256) { throw 'Windows Boot Manager hash changed during the one-shot boot.' }
        if ($esp.stageFiles.Count -ne 3 -or $esp.stageFiles.Where({$_.name -ceq 'loader.efi'}).Count -ne 1 -or $esp.stageFiles.Where({$_.name -ceq 'inventory.efi'}).Count -ne 1) { throw 'Path-B stage files differ from the recorded three-file stage.' }
        if (-not $esp.correctionDirectoryExists) { throw 'Prelude correction evidence directory is missing.' }
        $items = @(Get-ChildItem -LiteralPath $correctionRoot -Force)
        $expectedNames = @('smbios2-ep-prelude.efi','report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin')
        if (@($items | Where-Object { $_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $expectedNames -cnotcontains $_.Name }).Count -ne 0) { throw 'Prelude correction directory contains an unexpected file, directory, or reparse point.' }
        $modePath = Join-Path $correctionRoot 'mode.txt'
        if (Test-Path -LiteralPath $modePath) { throw 'Prelude mode flag was not consumed as a one-shot.' }
        foreach ($name in @('report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin')) {
            $sourcePath = Join-Path $correctionRoot $name
            $rec = Get-SafeFileRecord $sourcePath $name
            $metadata.esp.correctionEvidence += @([ordered]@{ name = $name; bytes = $rec.bytes; sha256 = $rec.sha256 })
            $rawEvidenceRecords.Add($rec)
            $ext = if ($name -ceq 'report.txt') { 'txt' } else { 'bin' }
            $captured.Add((Add-CapturedFile $sourcePath $name $ext $rec))
        }
        $preludePath = Join-Path $correctionRoot 'smbios2-ep-prelude.efi'
        $preludeRecord = Get-SafeFileRecord $preludePath 'smbios2-ep-prelude.efi'
        if ([string]$preludeRecord.sha256 -cne [string]$config.preludeSha256) { throw 'ESP prelude binary differs from the pinned build.' }
        $preludeCapture = Add-CapturedFile $preludePath 'smbios2-ep-prelude.efi' 'efi' $preludeRecord
        $reportRecord = @($captured | Where-Object { $_.name -ceq 'report.txt' })[0]
        $reportBytes = [IO.File]::ReadAllBytes([string]$reportRecord.path)
        $parsedReport = Parse-TextReport $reportBytes
        $reportValues = Assert-EfiReport $parsedReport $mode
        Assert-EpsAndTable $rawEvidenceRecords $reportValues
        $loadImageEvidence = Get-EfiLoadImageEvidence $reportValues $mode
        $childStartStatus = $null
        if ($null -ne $reportValues.PSObject.Properties['child_start_status']) {
            $childStartStatus = [string]$reportValues.child_start_status
        }
        $metadata.efi = [ordered]@{
            schema = [string]$reportValues.schema
            mode = [string]$reportValues.mode
            result = [string]$reportValues.result
            phase = [string]$reportValues.phase
            childLoadObserved = ($reportValues.child_load_observed -ceq 'true')
            childLoadStatus = [string]$reportValues.child_load_status
            childStartStatusObserved = ($reportValues.child_start_status_observed -ceq 'true')
            childStartStatus = $childStartStatus
            status = [string]$reportValues.child_load_status
            startStatusObserved = ($reportValues.child_start_status_observed -ceq 'true')
            statusSource = 'prelude.report.child_load_status'
            statusPhase = [string]$loadImageEvidence.statusPhase
            childTarget = [string]$loadImageEvidence.childTarget
            values = $reportValues
            preludePath = $preludeRelative
            preludeSha256 = [string]$config.preludeSha256
        }
        $metadata.windowsReadback = Get-Readback $phaseMode
        if ($mode -ceq 'corrected-collector') {
            $patch = Read-CollectorPatch $espRoot
            $metadata.collectorReport = $patch.metadata
            $metadata.collectorReport.efiStatusSource = 'prelude.report.child_load_status'
            $metadata.collectorReport.efiStatusPhase = 'LoadImage(loader.efi)'
            $metadata.collectorReport.efiStatus = [string]$metadata.efi.status
            $metadata.collectorReport.startStatusObserved = [bool]$metadata.efi.startStatusObserved
            $metadata.collectorReport.bootIdentity = $metadata.bootIdentity
            $metadata.collectorReport.bootIdentityProvenance = [ordered]@{
                source = 'Win32_OperatingSystem.LastBootUpTime via Capture-AfterBoot/Get-BootIdentity'
                association = 'Measured by this same authenticated CaptureActive invocation after the owned prelude boot; not emitted by the EFI collector.'
                emittedByEfiCollector = $false
            }
            $captured.Add((Add-CapturedFile (Join-Path $espRoot 'HypersinvSmbiosPatch.txt') 'HypersinvSmbiosPatch.txt' 'txt' $patch.record))
        }
        $metadata.esp.afterCapture = $esp
        # All firmware-written evidence is copied and hash-checked before the
        # exact owned subtree is removed. No recursive deletion is used.
        Remove-CorrectionDirectoryExact $espRoot $expectedNames
    } finally { Dismount-Esp $mount }

    $currentBootNext = Read-VariableOrAbsent 'BootNext'
    if ($currentBootNext.supported -eq $true) {
        $actualNext = Get-BootOptionIndex 'BootNext' $currentBootNext
        if ($actualNext -ne $customNumber) { throw 'BootNext points to an unrelated entry; refusing to clear it.' }
        $nextBytes = [Convert]::FromBase64String([string]$currentBootNext.bytesBase64)
        Invoke-JournaledFixtureMutation 'capture_remove_custom_boot_next' 'CLEANING' { Remove-VariableExact 'BootNext' $nextBytes ([uint32]$currentBootNext.attributes) } | Out-Null
    }
    Invoke-JournaledFixtureMutation 'capture_remove_custom_boot_entry' 'CLEANING' { Remove-VariableExact ([string]$journal.customBootName) $customBytes ([uint32]$journal.customBootAttributes) } | Out-Null
    $metadata.cleanup.attempted = $true
    $stage = Get-StageSources
    $metadata.pathBAction = Invoke-CapturePathBCleanup $mode $stage $mount
    $finalBootNext = Read-VariableOrAbsent 'BootNext'
    $finalBootOrder = Read-Variable 'BootOrder'
    $finalCustom = Read-VariableOrAbsent ([string]$journal.customBootName)
    $finalStandard = Read-VariableOrAbsent ([string]$journal.standardBootName)
    if ($finalBootNext.supported -ne $false -or [int]$finalBootNext.win32Error -ne 203 -or
        $finalCustom.supported -ne $false -or [int]$finalCustom.win32Error -ne 203 -or
        $finalStandard.supported -ne $false -or [int]$finalStandard.win32Error -ne 203) { throw 'Owned BootNext/Boot#### records remain after Path-B cleanup.' }
    $expectedOrder = [Convert]::FromBase64String([string]$journal.bootOrderBefore)
    if (-not (Test-EfiVariableRecordEquals -Actual $finalBootOrder -ExpectedBytes $expectedOrder -ExpectedAttributes ([uint32]$journal.bootOrderAttributes))) { throw 'BootOrder differs after fixture correction cleanup.' }
    $metadata.firmware.bootNextAfter = $finalBootNext
    $metadata.firmware.bootOrderAfter = $finalBootOrder
    Remove-PathBStageAfterCapture $journal
    $metadata.cleanup.result = 'OWNED_ENTRIES_STAGE_AND_TRACKED_ESP_RESTORED'
    $script:fixtureJournal.pendingMutation = $null
    $script:fixtureJournal.state = 'CAPTURED_AND_CLEANED'
    $script:fixtureJournal.cleanupCompletedUtc = [DateTime]::UtcNow.ToString('o')
    Save-FixtureJournal
    $metadata.journal = [ordered]@{ path = (Join-Path $stateRoot ($mode + '.journal.json')); sha256 = Get-Sha256 (Join-Path $stateRoot ($mode + '.journal.json')); state = 'CAPTURED_AND_CLEANED' }
    $metadata.status = 'PASS'
    $result.Status = 'PASS'
    $result.Message = "Observed and archived the $mode SMBIOS2 correction boot, verified fresh Windows RSMB/WMI values, removed only owned fixture evidence and boot entries, and preserved BootOrder."
}

try {
    Assert-RunRoot
    Assert-Admin
    if ($internalAction -notin @('StageArmCorrectionOnly','CaptureCorrectionOnly','StageArmActive','CaptureActive','RecoverPending')) { throw "Unknown fixture correction internal action: $internalAction" }
    if ($internalAction -ceq 'RecoverPending') {
        if ($mode -cne 'recovery') { throw "RecoverPending requires recovery mode; actual=$mode" }
        $expectedAction = 'RecoverPending'
    } else {
        if ($mode -notin @('correction-only','corrected-collector')) { throw "Unknown fixture correction mode: $mode" }
        $expectedAction = if ($internalAction -match '^StageArm') { 'StageArm' } else { 'Capture' }
        $expectedMode = if ($internalAction -match 'CorrectionOnly$') { 'correction-only' } else { 'corrected-collector' }
        if ($mode -cne $expectedMode) { throw "Internal action/mode mismatch: action=$internalAction mode=$mode" }
    }
    $firmwareModulePath = Import-EmbeddedModule ([string]$config.firmwareModuleBase64) 'firmware-variable.psm1'
    $bootOptionModulePath = Import-EmbeddedModule ([string]$config.bootOptionModuleBase64) 'boot-option.psm1'
    $pathBScript = Join-Path $runRootFull 'path-b-collector.ps1'
    if (Test-Path -LiteralPath $pathBScript) { throw 'Path-B script destination already exists.' }
    $pathBBytes = [Convert]::FromBase64String([string]$config.pathBScriptBase64)
    try { [IO.File]::WriteAllBytes($pathBScript, $pathBBytes) } finally { [Array]::Clear($pathBBytes, 0, $pathBBytes.Length) }
    if ((Get-Sha256 $pathBScript) -cne [string]$config.pathBScriptSha256) { throw 'Embedded Path-B script differs from the pinned source.' }
    [void][IO.Directory]::CreateDirectory($stateRoot)
    $firmwareAssetRoot = Join-Path $stateRoot 'assets'
    [void][IO.Directory]::CreateDirectory($firmwareAssetRoot)
    foreach ($asset in $config.assetFiles) {
        $localPath = Join-Path $stateRoot ([string]$asset.localName)
        Install-VerifiedReadOnlyAsset -SourcePath ([string]$asset.uncPath) -DestinationPath $localPath -ExpectedSha256 ([string]$asset.sha256)
    }
    $loaderBuild = Join-Path $stateRoot 'pathb\loader'
    $inventoryBuild = Join-Path $stateRoot 'pathb\inventory'
    [void][IO.Directory]::CreateDirectory($loaderBuild)
    [void][IO.Directory]::CreateDirectory((Join-Path $inventoryBuild 'efi'))
    foreach ($asset in $config.assetFiles) {
        $destination = switch ([string]$asset.localName) {
            'inventory_loader.efi' { Join-Path $loaderBuild 'inventory_loader.efi' }
            'ib-install.exe' { Join-Path $loaderBuild 'ib-install.exe' }
            'inventory.efi' { Join-Path (Join-Path $inventoryBuild 'efi') 'inventory.efi' }
            'efi_inventory.exe' { Join-Path $inventoryBuild 'efi_inventory.exe' }
            'smbios_windows_readback.exe' { Join-Path $stateRoot 'smbios_windows_readback.exe' }
            default { $null }
        }
        if ($null -ne $destination -and $destination -cne (Join-Path $stateRoot ([string]$asset.localName))) {
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { Copy-Item -LiteralPath (Join-Path $stateRoot ([string]$asset.localName)) -Destination $destination }
            Assert-Hash $destination ([string]$asset.sha256) ('Path-B staged ' + [string]$asset.localName)
        }
    }
    $stage = Get-StageSources
    if ($expectedAction -ceq 'StageArm') { Stage-And-Arm }
    elseif ($expectedAction -ceq 'Capture') { Capture-AfterBoot }
    else {
        $pendingJournal = Get-PendingFixtureJournal
        if ($null -eq $pendingJournal) {
            $metadata.cleanup.attempted = $false
            $metadata.cleanup.result = 'NO_PENDING_FIXTURE_JOURNAL'
            $metadata.status = 'PASS'
            $result.Status = 'PASS'
            $result.Message = 'No unfinished fixture journal exists for the exact VM/lifecycle identity.'
        } else {
            $script:fixtureJournal = $pendingJournal
            $script:fixtureJournalPath = Get-FixtureJournalPath ([string]$pendingJournal.mode)
            $script:fixtureJournalOwnedThisInvocation = $true
            $script:recoveryAttempted = $true
            Invoke-FixtureRecovery $pendingJournal
            if ([string]$script:fixtureJournal.state -cne 'RECOVERY_CLEANED') { throw "Recovery did not reach its durable terminal state: $($script:fixtureJournal.state)" }
            $metadata.status = 'PASS'
            $result.Status = 'PASS'
            $result.Message = 'Recovered only exact journal-owned fixture state and verified the recorded ESP/BootOrder baseline.'
        }
    }
    $result.Message | Write-Output
}
catch {
    $failure = $_.Exception.ToString()
    $metadata.errors += @($failure)
    $metadata.errorDetails += @(Get-FixtureErrorDetail -ErrorRecord $_ -Phase 'action')
    $result.Message = $failure
    $metadata.status = 'FAIL'
    if ($script:fixtureJournalOwnedThisInvocation -and -not $script:recoveryAttempted -and $null -ne $script:fixtureJournal -and [string]$script:fixtureJournal.state -notin @('CAPTURED_AND_CLEANED','RECOVERY_CLEANED')) {
        $script:recoveryAttempted = $true
        try {
            Invoke-FixtureRecovery $script:fixtureJournal
            $metadata.cleanup.automaticRecovery = 'RECOVERY_CLEANED'
        } catch {
            $metadata.cleanup.errors += @($_.Exception.ToString())
            $metadata.errorDetails += @(Get-FixtureErrorDetail -ErrorRecord $_ -Phase 'automaticRecovery')
            $metadata.cleanup.automaticRecovery = 'FAILED; snapshot recovery is required'
        }
    }
}
finally {
    $metadata.completedUtc = [DateTime]::UtcNow.ToString('o')
    $metadata.capturedFiles = @($metadata.capturedFiles)
    if (@($metadata.capturedFiles).Count -gt 0) {
        try { $metadata.rawEvidenceBundle = New-RawEvidenceBundle @($metadata.capturedFiles) }
        catch { $metadata.errors += @('rawEvidenceBundle: ' + $_.Exception.ToString()); $metadata.status = 'FAIL'; $result.Status = 'FAIL'; $result.Message = 'Evidence archive creation failed; raw files were retained.' }
    }
    try { [IO.File]::WriteAllText((Join-Path $runRootFull 'kernel-metadata.json'), ($metadata | ConvertTo-Json -Depth 32) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false)) }
    catch { $result.Status = 'FAIL'; $result.Message = 'Could not persist kernel-metadata.json: ' + $_.Exception.Message }
    [IO.File]::WriteAllText((Join-Path $runRootFull 'stdout.txt'), (($output.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runRootFull 'stderr.txt'), $(if ($result.Status -eq 'PASS') { '' } else { [string]$result.Message + [Environment]::NewLine }), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runRootFull 'result.json'), ($result | ConvertTo-Json -Depth 12) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
if ($result.Status -cne 'PASS') { exit 1 }
