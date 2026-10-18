[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$templatePath = Join-Path $PSScriptRoot 'guest-fixture-correction.template.ps1'
$templateText = [IO.File]::ReadAllText($templatePath)
$tokens = $null
$parseErrors = $null
$templateAst = [Management.Automation.Language.Parser]::ParseInput($templateText, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -ne 0) { throw 'Fixture recovery template does not parse.' }

$requiredFunctions = @(
    'Ensure-FixtureJournalProperties','Save-FixtureJournal','Invoke-JournaledFixtureMutation','Assert-OwnedEspDirectory',
    'Get-Sha256FromBytes','Get-FixtureErrorDetail','Get-EspSnapshot','Get-SafeFileRecord','Add-CapturedFile','Read-FixtureJournal','Get-ExpectedJournal','Capture-AfterBoot','Remove-CorrectionDirectoryExact','Remove-CorrectionDirectoryForRecovery',
    'Get-StageSources','Get-PathBManifestText','Remove-PathBStageForRecovery','Invoke-FixtureRecovery',
    'Invoke-PathBWithEspHandoff','Invoke-CapturePathBCleanup','Remove-PathBStageAfterCapture',
    'Parse-TextReport','Assert-EfiReport','Get-EfiLoadImageEvidence','Test-Checksum','Measure-Smbios2Table','Assert-EpsAndTable',
    'Test-CapturedFileName','Archive-OwnedEspFile','Get-ModeValue','Assert-PhaseIdentity','Get-BootIdentity','Get-CanonicalBindingMap','Read-CollectorPatch'
)
$functionAsts = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -in $requiredFunctions }, $false))
foreach ($name in $requiredFunctions) {
    if (@($functionAsts | Where-Object { $_.Name -ceq $name }).Count -ne 1) { throw "Could not isolate production recovery function $name." }
}
foreach ($functionAst in $functionAsts) { Invoke-Expression $functionAst.Extent.Text }

$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$reportRoot = Join-Path $worktreeRoot 'spoof-handoff\reports\f6\runtime'
[void][IO.Directory]::CreateDirectory($reportRoot)
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $reportRoot ('recovery-mocks.' + $testId)
[void][IO.Directory]::CreateDirectory($testRoot)
$checks = [Collections.Generic.List[object]]::new()

function Assert-True([bool]$Condition, [string]$Name, $Actual = $null) {
    if (-not $Condition) { throw "$Name failed: actual=$Actual" }
    $checks.Add([ordered]@{ name = $Name; pass = $true; actual = $Actual })
}

function Get-Sha256([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant() }
function Assert-Hash([string]$Path, [string]$Expected, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Name mock source is missing: $Path" }
    $actual = Get-Sha256 $Path
    if ($actual -cne $Expected) { throw "$Name mock source hash mismatch: expected=$Expected actual=$actual" }
}
function ConvertTo-EfiBootVariableName([int]$Number) { return ('Boot{0:X4}' -f $Number) }
function Get-FixtureJournalPath([string]$ModeName) { return $script:currentJournalPath }
function Assert-RecoveryVmIdentity { return }
function Dismount-Esp($Mount) {
    if ($null -eq $Mount) { throw 'Mock dismount requires the owned mount handle.' }
    if ($Mount.mounted -ne $true) { return }
    if (-not $script:mockEspMounted -or [string]$Mount.root -cne [string]$script:espRoot -or [string]$Mount.volumeGuid -cne $script:volumeGuid) {
        throw 'Mock dismount refused an unowned or mismatched ESP mount.'
    }
    $script:mockEspMounted = $false
    $Mount.mounted = $false
    $script:mockDismountCount++
}
function Test-EspMountLetterInUse { return ($script:mockEspMounted -or $script:mockPathBOwnsMount) }

function New-MockRecord([byte[]]$Bytes, [uint32]$Attributes = 7) {
    return [pscustomobject]@{
        supported = $true
        win32Error = 0
        attributes = $Attributes
        bytesBase64 = [Convert]::ToBase64String($Bytes)
        bytesHex = ([BitConverter]::ToString($Bytes)).Replace('-', '')
    }
}

function Read-Variable([string]$Name) {
    if (-not $script:mockVariables.ContainsKey($Name)) { throw "Mock variable is absent: $Name" }
    return $script:mockVariables[$Name]
}

function Read-VariableOrAbsent([string]$Name) {
    if ($script:mockVariables.ContainsKey($Name)) { return $script:mockVariables[$Name] }
    return [pscustomobject]@{ supported = $false; win32Error = 203; attributes = 0; bytesBase64 = ''; bytesHex = '' }
}

function Get-BootOptionIndex([string]$VariableName, $Record) {
    $bytes = [Convert]::FromBase64String([string]$Record.bytesBase64)
    if ($bytes.Length -ne 2) { throw "Invalid mock $VariableName length." }
    return [int][BitConverter]::ToUInt16($bytes, 0)
}

function Read-EfiBootLoadOptionBytes([byte[]]$Bytes) {
    $text = [Text.Encoding]::ASCII.GetString($Bytes)
    $parts = $text.Split('|')
    if ($parts.Count -ne 2) { throw 'Mock load option is malformed.' }
    return [pscustomobject]@{ filePath = $parts[0]; hardDrive = [pscustomobject]@{ partitionGuid = $parts[1] } }
}

function Test-EfiVariableRecordEquals($Actual, [byte[]]$ExpectedBytes, [uint32]$ExpectedAttributes) {
    if ($null -eq $Actual -or $Actual.supported -ne $true -or [uint32]$Actual.attributes -ne $ExpectedAttributes) { return $false }
    return [Convert]::ToBase64String([Convert]::FromBase64String([string]$Actual.bytesBase64)) -ceq [Convert]::ToBase64String($ExpectedBytes)
}

function Remove-VariableExact([string]$Name, [byte[]]$ExpectedBytes, [uint32]$ExpectedAttributes) {
    $current = Read-Variable $Name
    if (-not (Test-EfiVariableRecordEquals $current $ExpectedBytes $ExpectedAttributes)) { throw "Mock exact removal mismatch: $Name" }
    [void]$script:mockVariables.Remove($Name)
}

function Invoke-PathB([string]$ModeName, $Stage) {
    if ($script:mockEspMounted) { throw 'Path-B stub refused because Z: is mounted by the caller.' }
    $script:pathBObservedUnmounted++
    $script:pathBCalls++
    if ($ModeName -notin @('Disarm','Collect')) { throw "Unexpected mock Path-B action: $ModeName" }
    $script:mockPathBOwnsMount = $true
    try {
        if ($script:mockVariables.ContainsKey([string]$script:mockStandardName)) {
            [void]$script:mockVariables.Remove([string]$script:mockStandardName)
        }
        if ($script:mockVariables.ContainsKey('BootNext')) {
            $n = Get-BootOptionIndex 'BootNext' $script:mockVariables['BootNext']
            if ($n -eq $script:mockStandardNumber) { [void]$script:mockVariables.Remove('BootNext') }
        }
    } finally {
        $script:mockPathBOwnsMount = $false
    }
    if ($script:mockPathBLeakMountAfterSuccess) { $script:mockEspMounted = $true }
    return [pscustomobject]@{ exitCode = 0 }
}

function Get-PartitionFacts([string]$EspRoot, [string]$VolumeGuid) {
    if ($VolumeGuid -cne $script:volumeGuid) { throw "Mock partition lookup received wrong volume: $VolumeGuid" }
    return [pscustomobject]@{ partitionGuid = $script:partitionGuid; partitionNumber = 1; startLba = 2048; sizeLba = 262144 }
}

function Mount-Esp {
    if ($script:mockEspMounted -or $script:mockPathBOwnsMount) { throw 'Mock mount refused because Z: is already owned.' }
    $script:mockEspMounted = $true
    $script:mockMountCount++
    return [pscustomobject]@{ root = $script:espRoot; volumeGuid = $script:volumeGuid; mounted = $true }
}

function Read-EfiBootOptionRecord([string]$Path) {
    return [pscustomobject]@{ name = [IO.Path]::GetFileName($Path); bytes = (Get-Item -LiteralPath $Path).Length; sha256 = Get-Sha256 $Path }
}

function Get-Identity([string]$ExpectedMode = 'original') {
    $script:mockIdentityModes += $ExpectedMode
    $vendor = Get-ModeValue $script:config.smbios.system_vendor $ExpectedMode 'system_vendor'
    $product = Get-ModeValue $script:config.smbios.system_product $ExpectedMode 'system_product'
    $uuid = Get-ModeValue $script:config.smbios.system_uuid $ExpectedMode 'system_uuid'
    return [pscustomobject]@{
        manufacturer = $vendor
        model = $product
        uuid = $uuid
        productVendor = $vendor
        productName = $product
        productIdentifyingNumber = $uuid
        diskPnp = 'SCSI\\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\\5&25A13950&0&000000'
        diskSerial = '59F5_4E30_1778_CC3A_000C_2967_3817_6676.'
    }
}

function Get-CimInstance {
    [CmdletBinding()]
    param([string]$ClassName)
    if ($ClassName -cne 'Win32_OperatingSystem' -or $null -eq $script:mockBootUpTime) { throw "Unexpected or unconfigured CIM mock class: $ClassName" }
    return [pscustomobject]@{ LastBootUpTime = [DateTime]$script:mockBootUpTime }
}

function Get-Readback([string]$ExpectedMode) {
    $script:mockReadbackModes += $ExpectedMode
    return [ordered]@{ schema = 'offline-mocked-smbios-readback.v1'; expectedMode = $ExpectedMode; status = 'MOCKED' }
}

function Confirm-SecureBootUEFI {
    $script:mockSecureBootQueryCount++
    return $false
}

$script:bootManagerRelative = 'EFI\Microsoft\Boot\bootmgfw.efi'
$script:efiStagePath = 'EFI\HypersinvInventory'
$script:correctionRelative = 'EFI\HypersinvInventory\fixture-correction'
$script:loaderRelative = 'EFI\HypersinvInventory\loader.efi'
$script:preludeRelative = 'EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi'
$script:partitionGuid = '11223344-5566-7788-99AA-BBCCDDEEFF00'
$script:volumeGuid = 'volume-guid-test'
$script:espLetter = 'Z:'
$script:stateRoot = Join-Path $testRoot 'stage-sources'
$mainRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$lockedOriginalRows = @(Import-Csv -LiteralPath (Join-Path $mainRepositoryRoot 'spoof-handoff\reports\client-originals.tsv') -Delimiter "`t")
$lockedSmbiosMembers = [ordered]@{
    system_serial = 'FakeSMBIOSSerial'
    system_vendor = 'FakeSMBIOSManufacturer'
    system_product = 'FakeSMBIOSProduct'
    board_serial = 'FakeMotherboardSerial'
    board_vendor = 'FakeMotherboardManufacturer'
    board_product = 'FakeMotherboardProduct'
    system_uuid = 'FakeSMBIOSUUID'
}
$lockedSmbiosValues = [ordered]@{}
foreach ($fieldName in $lockedSmbiosMembers.Keys) {
    $lockedFieldRows = @($lockedOriginalRows | Where-Object { [string]$_.member -ceq [string]$lockedSmbiosMembers[$fieldName] })
    if ($lockedFieldRows.Count -ne 1) { throw "Expected one canonical locked SMBIOS row for $fieldName; actual=$($lockedFieldRows.Count)" }
    $lockedSmbiosValues[$fieldName] = [pscustomobject]@{ original = [string]$lockedFieldRows[0].original; replacement = [string]$lockedFieldRows[0].replacement }
}
$script:mockBootUpTime = [DateTime]::ParseExact('2026-10-18T00:00:00.0000000Z','o',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)
$loaderBuild = Join-Path $script:stateRoot 'pathb\loader'
$inventoryBuild = Join-Path $script:stateRoot 'pathb\inventory'
[void][IO.Directory]::CreateDirectory($loaderBuild)
[void][IO.Directory]::CreateDirectory((Join-Path $inventoryBuild 'efi'))
[IO.File]::WriteAllBytes((Join-Path $loaderBuild 'inventory_loader.efi'), [byte[]](0x4D,0x5A,0x10,0x20,0x30,0x40,0x50))
[IO.File]::WriteAllBytes((Join-Path $inventoryBuild 'efi\inventory.efi'), [byte[]](0x4D,0x5A,0x11,0x21,0x31,0x41,0x51,0x61,0x71))
[IO.File]::WriteAllBytes((Join-Path $loaderBuild 'ib-install.exe'), [byte[]](1,3,5,7))
[IO.File]::WriteAllBytes((Join-Path $inventoryBuild 'efi_inventory.exe'), [byte[]](2,4,6,8,10))
$script:config = [pscustomobject]@{
    lifecycleId = 'recovery-test'
    vmUuid = 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1'
    collectorBuildId = 'OFFLINE-ACTIVE-CAPTURE-TEST'
    smbios = [pscustomobject]$lockedSmbiosValues
    preludeSha256 = ('A' * 64)
    assets = [pscustomobject]@{
        inventoryLoaderSha256 = Get-Sha256 (Join-Path $loaderBuild 'inventory_loader.efi')
        collectorSha256 = Get-Sha256 (Join-Path $inventoryBuild 'efi\inventory.efi')
        ibInstallSha256 = Get-Sha256 (Join-Path $loaderBuild 'ib-install.exe')
        efiInventorySha256 = Get-Sha256 (Join-Path $inventoryBuild 'efi_inventory.exe')
    }
}

$stageSources = Get-StageSources
Assert-True ([long]$stageSources.loaderBytes -eq [IO.File]::ReadAllBytes([string]$stageSources.loader).Length) 'Path-B loader size is measured in bytes' $stageSources.loaderBytes
Assert-True ([long]$stageSources.collectorBytes -eq [IO.File]::ReadAllBytes([string]$stageSources.collector).Length) 'Path-B collector size is measured in bytes' $stageSources.collectorBytes
Assert-True ([long]$stageSources.loaderBytes -ne ([string]$stageSources.loader).Length) 'Path-B loader byte size is not confused with its path character count' ([ordered]@{ bytes = $stageSources.loaderBytes; pathCharacters = ([string]$stageSources.loader).Length })
Assert-True ([long]$stageSources.collectorBytes -ne ([string]$stageSources.collector).Length) 'Path-B collector byte size is not confused with its path character count' ([ordered]@{ bytes = $stageSources.collectorBytes; pathCharacters = ([string]$stageSources.collector).Length })
$pinnedPathB = Join-Path ([IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))) 'tools\efi_inventory\path-b-collector.ps1'
$pathBTokens = $null
$pathBParseErrors = $null
$pathBAst = [Management.Automation.Language.Parser]::ParseFile($pinnedPathB, [ref]$pathBTokens, [ref]$pathBParseErrors)
if (@($pathBParseErrors).Count -ne 0) { throw 'Pinned Path-B collector does not parse for the manifest comparison.' }
$pathBManifestAst = @($pathBAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'New-Manifest' }, $false))
if ($pathBManifestAst.Count -ne 1) { throw 'Could not isolate the single pinned Path-B New-Manifest writer.' }
Invoke-Expression $pathBManifestAst[0].Extent.Text
$pinnedPathBSha256 = Get-Sha256 $pinnedPathB
$pinnedManifest = New-Manifest @{
    LoaderSize = [long]$stageSources.loaderBytes
    LoaderHash = [string]$stageSources.loaderSha256
    CollectorSize = [long]$stageSources.collectorBytes
    CollectorHash = [string]$stageSources.collectorSha256
}
$fixtureManifest = Get-PathBManifestText $stageSources
Assert-True ($fixtureManifest -ceq $pinnedManifest) 'fixture recovery manifest exactly matches the pinned Path-B writer' ([ordered]@{ actual = $fixtureManifest; pinned = $pinnedManifest })

function Reset-RecoveryCase([string]$Name, [string]$BootNextTarget, [bool]$AllocateCustom, [bool]$MismatchedCustom = $false) {
    $caseRoot = Join-Path $testRoot $Name
    $script:espRoot = Join-Path $caseRoot 'esp'
    $script:stageRoot = Join-Path $caseRoot 'state'
    [void][IO.Directory]::CreateDirectory((Join-Path $script:espRoot 'EFI\Microsoft\Boot'))
    [IO.File]::WriteAllBytes((Join-Path $script:espRoot $script:bootManagerRelative), [byte[]](1,2,3,4))
    $script:currentJournalPath = Join-Path $script:stageRoot 'fixture.journal.json'
    [void][IO.Directory]::CreateDirectory($script:stageRoot)
    $receiptPath = Join-Path $script:stageRoot 'path-b-stage.json'
    [IO.File]::WriteAllText($receiptPath, 'receipt')
    $script:mockVariables = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    $script:mockStandardNumber = 6
    $script:mockStandardName = 'Boot0006'
    $script:pathBCalls = 0
    $script:pathBObservedUnmounted = 0
    $script:mockEspMounted = $false
    $script:mockPathBOwnsMount = $false
    $script:mockPathBLeakMountAfterSuccess = $false
    $script:mockMountCount = 0
    $script:mockDismountCount = 0
    $script:bootOrderBytes = [byte[]](5,0,0,0,1,0,2,0,3,0,4,0)
    $script:mockVariables['BootOrder'] = New-MockRecord $script:bootOrderBytes 7
    $standardBytes = [Text.Encoding]::ASCII.GetBytes($script:loaderRelative + '|' + $script:partitionGuid)
    $customBytes = [Text.Encoding]::ASCII.GetBytes($script:preludeRelative + '|' + $script:partitionGuid)
    $script:mockVariables['Boot0006'] = New-MockRecord $standardBytes 7
    if ($AllocateCustom) { $script:mockVariables['Boot0007'] = New-MockRecord $(if($MismatchedCustom){[Text.Encoding]::ASCII.GetBytes('foreign|'+$script:partitionGuid)}else{$customBytes}) 7 }
    switch ($BootNextTarget) {
        'custom' { $script:mockVariables['BootNext'] = New-MockRecord ([BitConverter]::GetBytes([uint16]7)) 7 }
        'standard' { $script:mockVariables['BootNext'] = New-MockRecord ([BitConverter]::GetBytes([uint16]6)) 7 }
        'unowned' {
            $script:mockVariables['Boot0099'] = New-MockRecord ([Text.Encoding]::ASCII.GetBytes('EFI\Other\other.efi|' + $script:partitionGuid)) 7
            $script:mockVariables['BootNext'] = New-MockRecord ([BitConverter]::GetBytes([uint16]99)) 7
        }
    }
    $script:fixtureJournal = [ordered]@{
        schema = 'combined-vm-fixture-correction-journal.v2'; lifecycleId = 'recovery-test'; vmUuid = $script:config.vmUuid
        mode = 'correction-only'; state = if($BootNextTarget -ceq 'custom'){'CUSTOM_BOOT_NEXT_WRITTEN'}else{'PATHB_ARMED'}
        pendingMutation = $null; completedMutations = @(); startedUtc = [DateTime]::UtcNow.ToString('o'); cleanupCompletedUtc = $null
        partition = [ordered]@{ partitionGuid = $script:partitionGuid; partitionNumber = 1; startLba = 2048; sizeLba = 262144 }
        espVolumeGuid = $script:volumeGuid; bootManagerSha256 = Get-Sha256 (Join-Path $script:espRoot $script:bootManagerRelative)
        espBefore = [ordered]@{ bootManagerSha256 = Get-Sha256 (Join-Path $script:espRoot $script:bootManagerRelative); stageDirectoryExists = $false; stageFiles = @(); correctionDirectoryExists = $false; stageReportFiles = @() }; bootOrderBefore = [Convert]::ToBase64String($script:bootOrderBytes); bootOrderAttributes = [uint32]7
        bootNextBefore = $null; bootNextAttributes = $null
        standardBootNumber = if($BootNextTarget -ceq 'standard') { 6 } elseif($BootNextTarget -ceq 'custom' -or $BootNextTarget -ceq 'unowned') { 6 } else { $null }
        standardBootName = if($BootNextTarget -ceq 'standard' -or $BootNextTarget -ceq 'custom' -or $BootNextTarget -ceq 'unowned') { 'Boot0006' } else { $null }
        standardBootBytes = if($BootNextTarget -ceq 'standard' -or $BootNextTarget -ceq 'custom' -or $BootNextTarget -ceq 'unowned') { [Convert]::ToBase64String($standardBytes) } else { $null }
        standardBootAttributes = if($BootNextTarget -ceq 'standard' -or $BootNextTarget -ceq 'custom' -or $BootNextTarget -ceq 'unowned') { [uint32]7 } else { $null }
        customBootNumber = if($AllocateCustom) { 7 } else { $null }; customBootName = if($AllocateCustom) { 'Boot0007' } else { $null }
        customBootBytes = if($AllocateCustom) { [Convert]::ToBase64String($customBytes) } else { $null }; customBootAttributes = if($AllocateCustom) { [uint32]7 } else { $null }
        customPreludeRelative = $script:preludeRelative; preludeSha256 = $script:config.preludeSha256; modeFlag = 'correction-only'
        stageExpected = [ordered]@{ loaderSha256 = 'B'; collectorSha256 = 'C'; manifest = 'manifest' }
        pathBReceiptPath = $receiptPath; pathBReceiptSha256 = Get-Sha256 $receiptPath; pathBArmIntent = $true
    }
    $script:fixtureJournalPath = $script:currentJournalPath
    $script:metadata = [ordered]@{
        cleanup = [ordered]@{}
        esp = [ordered]@{}
        firmware = [ordered]@{}
        capturedFiles = @()
        collectorReport = $null
        journal = $null
    }
    Save-FixtureJournal
}

function Install-MockPathBStage {
    $stageRoot = Join-Path $script:espRoot $script:efiStagePath
    [void][IO.Directory]::CreateDirectory($stageRoot)
    $stage = Get-StageSources
    [IO.File]::WriteAllBytes((Join-Path $stageRoot 'loader.efi'), [IO.File]::ReadAllBytes([string]$stage.loader))
    [IO.File]::WriteAllBytes((Join-Path $stageRoot 'inventory.efi'), [IO.File]::ReadAllBytes([string]$stage.collector))
    [IO.File]::WriteAllText((Join-Path $stageRoot 'manifest.sha256'), (Get-PathBManifestText $stage), [Text.Encoding]::ASCII)
    $script:fixtureJournal.stageExpected = [ordered]@{
        loaderBytes = [long]$stage.loaderBytes
        loaderSha256 = [string]$stage.loaderSha256
        collectorBytes = [long]$stage.collectorBytes
        collectorSha256 = [string]$stage.collectorSha256
        manifest = Get-PathBManifestText $stage
    }
    Save-FixtureJournal
    return $stageRoot
}

$preludeSourcePath = Join-Path $PSScriptRoot 'smbios_fixture\efi_prelude.c'
$preludeSource = [IO.File]::ReadAllText($preludeSourcePath)
$rawR6Root = Join-Path $PSScriptRoot 'smbios_fixture\testdata\r6'
$rawR6ProvenancePath = Join-Path $rawR6Root 'provenance.json'
$rawR6Provenance = Get-Content -LiteralPath $rawR6ProvenancePath -Raw | ConvertFrom-Json
$rawR6Files = @(
    @{ name = 'report.txt'; archiveEntry = 'f.0001.txt'; bytes = 4202; sha256 = '4B055AF6F6A8C8A1B1AC52FDA195EA464CA850915B0C2326D9E9CCA16027864C' },
    @{ name = 'eps2-before.bin'; archiveEntry = 'f.0002.bin'; bytes = 31; sha256 = '9DFFD0DBF88306081200969321ECB5EA8C1AC50CDB072EE65AEF9D368568558D' },
    @{ name = 'eps2-after.bin'; archiveEntry = 'f.0003.bin'; bytes = 31; sha256 = '47A73BF9D5B34F33A411991463A7A76FA21F9AF56103A5D23FB46924AAE40EA1' },
    @{ name = 'eps3-before.bin'; archiveEntry = 'f.0004.bin'; bytes = 0; sha256 = 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855' },
    @{ name = 'eps3-after.bin'; archiveEntry = 'f.0005.bin'; bytes = 0; sha256 = 'E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855' },
    @{ name = 'table-before.bin'; archiveEntry = 'f.0006.bin'; bytes = 12842; sha256 = '1E021344CA3E9460E4820EC8BBF1D6DF109BF400E0CA3933559C6461F50949D6' },
    @{ name = 'table-after.bin'; archiveEntry = 'f.0007.bin'; bytes = 12842; sha256 = '1E021344CA3E9460E4820EC8BBF1D6DF109BF400E0CA3933559C6461F50949D6' }
)
Assert-True ([string]$rawR6Provenance.schema -ceq 'combined-vm-smbios2-ep-test-fixture-provenance.v1' -and [string]$rawR6Provenance.sourceRunId -ceq 'uefi-windows-probe-20261018-065342-f7f63d6fad' -and [string]$rawR6Provenance.sourceRunRelativePath -ceq 'spoof-handoff/reports/f6/runtime/mailbox/uefi-windows-probe-20261018-065342-f7f63d6fad' -and [string]$rawR6Provenance.sourceEvidenceStatus -ceq 'FAIL' -and [string]$rawR6Provenance.sourceRunFailure -ceq 'Capture child_target relative/absolute representation mismatch; actual BootCurrentFFFF' -and [string]$rawR6Provenance.sourceRecoverySha256 -ceq 'D8BBE0F21A03E13BE58E16CAE096FC66167648D17ECB0B14842378BEE1D50D03' -and $rawR6Provenance.acceptanceClaim -eq $false) 'R6 fixture preserves exact failed source-run provenance without claiming VM-gate acceptance' ([ordered]@{ runId = $rawR6Provenance.sourceRunId; status = $rawR6Provenance.sourceEvidenceStatus; failure = $rawR6Provenance.sourceRunFailure; acceptanceClaim = $rawR6Provenance.acceptanceClaim })
Assert-True ([string]$rawR6Provenance.sourceEvidenceSha256 -ceq 'B6742750D2F1766CD0F98DD171C7E7D273D93701E972CE49221DB3F8C8381E54' -and [string]$rawR6Provenance.sourceNestedArchiveSha256 -ceq 'CCB02ECE85E538BA0BE35291CD64E01C7676B9844A1A939B231EFEA6B3D9DBA0') 'R6 fixture binds the source mailbox metadata and nested evidence archive' ([ordered]@{ metadataSha256 = $rawR6Provenance.sourceEvidenceSha256; nestedArchiveSha256 = $rawR6Provenance.sourceNestedArchiveSha256 })
Assert-True (@($rawR6Files).Count -eq 7 -and @($rawR6Provenance.capturedInputs).Count -eq 7) 'R6 offline fixture includes exactly seven parser inputs' @($rawR6Provenance.capturedInputs).Count
foreach ($pin in $rawR6Files) {
    $fixturePath = Join-Path $rawR6Root $pin.name
    Assert-Hash $fixturePath $pin.sha256 ('R6 raw ' + $pin.name)
    Assert-True ([long](Get-Item -LiteralPath $fixturePath).Length -eq [long]$pin.bytes) ('R6 raw byte length ' + $pin.name) (Get-Item -LiteralPath $fixturePath).Length
    $provenanceMatch = @($rawR6Provenance.capturedInputs | Where-Object { [string]$_.name -ceq [string]$pin.name })
    Assert-True ($provenanceMatch.Count -eq 1 -and [string]$provenanceMatch[0].archiveEntry -ceq [string]$pin.archiveEntry -and [long]$provenanceMatch[0].bytes -eq [long]$pin.bytes -and [string]$provenanceMatch[0].sha256 -ceq [string]$pin.sha256) ('R6 provenance pin ' + $pin.name) $provenanceMatch
}

$rawReportBytes = [IO.File]::ReadAllBytes((Join-Path $rawR6Root 'report.txt'))
$rawReport = Parse-TextReport $rawReportBytes
$rawValues = Assert-EfiReport $rawReport 'correction-only'
Assert-True ([string]$rawValues.eps2_changed_offsets -ceq '4,8') 'R6 report records only the byte-derived EPS2 changes' $rawValues.eps2_changed_offsets
$rawBinaryRecords = @(
    'eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin' |
        ForEach-Object { [pscustomobject]@{ name = $_; content = [IO.File]::ReadAllBytes((Join-Path $rawR6Root $_)) } }
)
Assert-EpsAndTable $rawBinaryRecords $rawValues
Assert-True ([int]$rawValues.smbios2_structure_count_before -eq 165 -and [int]$rawValues.smbios2_max_structure_before -eq 111 -and [int]$rawValues.smbios2_max_structure_after -eq 112) 'R6 full table/EPS walk validates the measured max-structure correction' ([ordered]@{ count = $rawValues.smbios2_structure_count_before; before = $rawValues.smbios2_max_structure_before; after = $rawValues.smbios2_max_structure_after })

$activeMetadataPath = Join-Path $reportRoot 'mailbox\uefi-windows-probe-20261018-074033-5c8c896732\evidence\kernel-metadata.json'
if (-not (Test-Path -LiteralPath $activeMetadataPath -PathType Leaf)) { throw ('Authenticated CaptureActive source metadata is missing: ' + $activeMetadataPath) }
$activeMetadataSha = Get-Sha256 $activeMetadataPath
if ($activeMetadataSha -cne '4D221503727B7DE382E45B5948087C61A49C1C51C8B1C5CA93DCA304A2A2B4BD') { throw ('CaptureActive source metadata hash mismatch: expected=4D221503727B7DE382E45B5948087C61A49C1C51C8B1C5CA93DCA304A2A2B4BD actual=' + $activeMetadataSha) }
$activeSourceMetadata = Get-Content -LiteralPath $activeMetadataPath -Raw | ConvertFrom-Json
if ([string]$activeSourceMetadata.schema -cne 'combined-vm-fixture-correction-action.v1' -or [string]$activeSourceMetadata.status -cne 'PASS' -or [string]$activeSourceMetadata.mode -cne 'corrected-collector' -or [string]$activeSourceMetadata.internalAction -cne 'CaptureActive') { throw 'Pinned CaptureActive source metadata is not a passing corrected-collector action.' }
$activeArchiveBytes = [Convert]::FromBase64String([string]$activeSourceMetadata.rawEvidenceBundle.base64)
if ([string]$activeSourceMetadata.rawEvidenceBundle.format -cne 'zip' -or $activeArchiveBytes.Length -ne [long]$activeSourceMetadata.rawEvidenceBundle.bytes -or (Get-Sha256FromBytes $activeArchiveBytes) -cne [string]$activeSourceMetadata.rawEvidenceBundle.sha256) { throw 'CaptureActive nested evidence archive does not match its embedded byte/sha receipt.' }
Add-Type -AssemblyName System.IO.Compression
$activeArchiveStream = [IO.MemoryStream]::new($activeArchiveBytes)
$activeArchive = [IO.Compression.ZipArchive]::new($activeArchiveStream, [IO.Compression.ZipArchiveMode]::Read, $true)
$activeEvidenceRoot = Join-Path $testRoot 'actual-capture-active-evidence'
[void][IO.Directory]::CreateDirectory($activeEvidenceRoot)
$activeEvidenceByName = @{}
$activeArchiveFailure = $null
try {
    if ($activeArchive.Entries.Count -ne 9 -or @($activeSourceMetadata.capturedFiles).Count -ne 9) { throw 'CaptureActive nested evidence must contain exactly nine archived files.' }
    foreach ($capturedFile in @($activeSourceMetadata.capturedFiles)) {
        $name = [string]$capturedFile.name
        if ($name -notin @('report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin','smbios2-ep-prelude.efi','HypersinvSmbiosPatch.txt') -or $activeEvidenceByName.ContainsKey($name)) { throw "CaptureActive evidence has an unexpected or duplicate filename: $name" }
        $leaf = [string]$capturedFile.archiveLeaf
        if ($leaf -notmatch '\Af\.[0-9]{4}\.[A-Za-z0-9]{1,10}\z') { throw "CaptureActive evidence archive member is unsafe: $leaf" }
        $entry = $activeArchive.GetEntry($leaf)
        if ($null -eq $entry -or [long]$entry.Length -ne [long]$capturedFile.bytes) { throw "CaptureActive archive member size/membership mismatch: $leaf" }
        $inputStream = $entry.Open()
        $outputStream = [IO.MemoryStream]::new()
        try { $inputStream.CopyTo($outputStream); $fileBytes = $outputStream.ToArray() } finally { $inputStream.Dispose(); $outputStream.Dispose() }
        if ($fileBytes.Length -ne [long]$capturedFile.bytes -or (Get-Sha256FromBytes $fileBytes) -cne [string]$capturedFile.sha256) { throw "CaptureActive archive member hash mismatch: $name" }
        $destination = Join-Path $activeEvidenceRoot $name
        [IO.File]::WriteAllBytes($destination, $fileBytes)
        $activeEvidenceByName[$name] = $destination
    }
    if ($activeEvidenceByName.Count -ne 9) { throw ('CaptureActive evidence file count mismatch: actual=' + $activeEvidenceByName.Count) }
} catch { $activeArchiveFailure = $_.Exception.Message }
finally { $activeArchive.Dispose(); $activeArchiveStream.Dispose(); [Array]::Clear($activeArchiveBytes, 0, $activeArchiveBytes.Length) }
if ($null -ne $activeArchiveFailure) { throw ('Could not verify the pinned CaptureActive archive: ' + $activeArchiveFailure) }
Assert-True ([string]$activeSourceMetadata.collectorReport.buildId -ceq '41A645260038585605897C5FFA0B5544' -and [string]$activeSourceMetadata.bootIdentity.windowsLastBootUpTimeUtc -ceq '2026-10-18T07:40:14.5000000Z') 'CaptureActive regression fixture is tied to the recorded active collector and Windows boot' ([ordered]@{ buildId = $activeSourceMetadata.collectorReport.buildId; bootIdentity = $activeSourceMetadata.bootIdentity })

$badOffsetText = $rawReport.text.Replace('eps2_changed_offsets=4,8', 'eps2_changed_offsets=4,8,9')
$badOffsetValues = Assert-EfiReport (Parse-TextReport ([Text.Encoding]::ASCII.GetBytes($badOffsetText))) 'correction-only'
$badOffsetError = $null
try { Assert-EpsAndTable $rawBinaryRecords $badOffsetValues } catch { $badOffsetError = $_.Exception.Message }
Assert-True ($null -ne $badOffsetError -and $badOffsetError -like '*expected=4,8 actual=4,8,9*') 'R6 EPS bytes reject a report claiming an unchanged byte as changed' $badOffsetError

$sourceTargets = @(
    @{ mode = 'correction-only'; target = 'EFI\Microsoft\Boot\bootmgfw.efi'; sourceLiteral = '"EFI\\Microsoft\\Boot\\bootmgfw.efi"' },
    @{ mode = 'corrected-collector'; target = 'EFI\HypersinvInventory\loader.efi'; sourceLiteral = '"EFI\\HypersinvInventory\\loader.efi"' }
)
foreach ($targetCase in $sourceTargets) {
    Assert-True ($preludeSource.Contains([string]$targetCase.sourceLiteral)) "F10 prelude emits the exact $($targetCase.mode) report target" $targetCase.sourceLiteral
    $modeReportText = $rawReport.text
    if ($targetCase.mode -ceq 'corrected-collector') {
        $modeReportText = $modeReportText.Replace('mode=correction-only', 'mode=corrected-collector').Replace('child_target=EFI\Microsoft\Boot\bootmgfw.efi', ('child_target=' + $targetCase.target))
    }
    $modeValues = Assert-EfiReport (Parse-TextReport ([Text.Encoding]::ASCII.GetBytes($modeReportText))) ([string]$targetCase.mode)
    Assert-True ([string]$modeValues.child_target -ceq [string]$targetCase.target) "$($targetCase.mode) target parser accepts its exact F10 path form" $modeValues.child_target
    $loadImageEvidence = Get-EfiLoadImageEvidence $modeValues ([string]$targetCase.mode)
    $expectedStatusPhase = if ($targetCase.mode -ceq 'correction-only') { 'LoadImage(bootmgfw.efi)' } else { 'LoadImage(loader.efi)' }
    Assert-True ([string]$loadImageEvidence.childTarget -ceq [string]$targetCase.target -and [string]$loadImageEvidence.statusPhase -ceq $expectedStatusPhase) "$($targetCase.mode) maps its validated child target to the exact LoadImage phase" $loadImageEvidence
    $swappedTarget = if ($targetCase.mode -ceq 'correction-only') { 'EFI\HypersinvInventory\loader.efi' } else { 'EFI\Microsoft\Boot\bootmgfw.efi' }
    $swappedReportText = $modeReportText.Replace(('child_target=' + [string]$targetCase.target), ('child_target=' + $swappedTarget))
    $swappedValues = (Parse-TextReport ([Text.Encoding]::ASCII.GetBytes($swappedReportText))).values
    $swappedTargetError = $null
    try { Get-EfiLoadImageEvidence $swappedValues ([string]$targetCase.mode) | Out-Null } catch { $swappedTargetError = $_.Exception.Message }
    Assert-True ($null -ne $swappedTargetError -and $swappedTargetError -like '*Prelude child target/mode mismatch*') "$($targetCase.mode) rejects the other mode's child target" $swappedTargetError
    if ($targetCase.mode -ceq 'corrected-collector') {
        $activeBadTargetText = $modeReportText.Replace(('child_target=' + [string]$targetCase.target), ('child_target=\' + [string]$targetCase.target))
        $activeBadTargetError = $null
        try { Assert-EfiReport (Parse-TextReport ([Text.Encoding]::ASCII.GetBytes($activeBadTargetText))) 'corrected-collector' | Out-Null } catch { $activeBadTargetError = $_.Exception.Message }
        Assert-True ($null -ne $activeBadTargetError -and $activeBadTargetError -like '*Prelude child target mismatch*') 'corrected-collector parser rejects a leading-slash variation' $activeBadTargetError
    }
}

$caseSpec = @(
    @{ name = 'custom-bootnext'; next = 'custom'; allocate = $true; mismatch = $false; expectPass = $true },
    @{ name = 'standard-arm-before-custom'; next = 'standard'; allocate = $false; mismatch = $false; expectPass = $true },
    @{ name = 'arm-intent-no-next-unresolved'; next = 'absent'; allocate = $false; mismatch = $false; expectPass = $false },
    @{ name = 'unowned-bootnext'; next = 'unowned'; allocate = $false; mismatch = $false; expectPass = $false },
    @{ name = 'mismatched-custom-entry'; next = 'custom'; allocate = $true; mismatch = $true; expectPass = $false }
)

foreach ($spec in $caseSpec) {
    Reset-RecoveryCase $spec.name $spec.next $spec.allocate $spec.mismatch
    $rehydrated = Read-FixtureJournal 'correction-only'
    Assert-True ($null -ne $rehydrated -and [string]$rehydrated.lifecycleId -ceq 'recovery-test') "$($spec.name) journal rehydrates with identity guard" $rehydrated
    $beforeVariableNames = @($script:mockVariables.Keys | Sort-Object)
    $beforePathBCalls = $script:pathBCalls
    $errorText = $null
    try { Invoke-FixtureRecovery $rehydrated } catch { $errorText = $_.Exception.Message }
    if ($spec.expectPass) {
        Assert-True ($null -eq $errorText) "$($spec.name) recovery passes" $errorText
        Assert-True ([string]$script:fixtureJournal.state -ceq 'RECOVERY_CLEANED') "$($spec.name) journal terminal state" $script:fixtureJournal.state
        Assert-True (-not $script:mockVariables.ContainsKey('BootNext') -and -not $script:mockVariables.ContainsKey('Boot0006')) "$($spec.name) removes only owned boot state" (@($script:mockVariables.Keys) -join ',')
        Assert-True ($script:pathBCalls -eq ($beforePathBCalls + 1)) "$($spec.name) invokes exact Path-B disarm once" $script:pathBCalls
        Assert-True (-not $script:mockEspMounted -and -not $script:mockPathBOwnsMount) "$($spec.name) returns with no ESP mount owned" $script:mockEspMounted
        Assert-True ($script:pathBObservedUnmounted -eq $script:pathBCalls) "$($spec.name) Path-B stub observed caller mount released" $script:pathBObservedUnmounted
        Assert-True ($script:mockMountCount -eq 2 -and $script:mockDismountCount -eq 2) "$($spec.name) remounts/revalidates exact ESP after Path-B before cleanup" ("mounts=$script:mockMountCount dismounts=$script:mockDismountCount")
        $stateAfter = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json
        Assert-True ([string]$stateAfter.state -ceq 'RECOVERY_CLEANED') "$($spec.name) terminal journal survives rehydration" $stateAfter.state
        $calls = $script:pathBCalls
        Invoke-FixtureRecovery $stateAfter
        Assert-True ($script:pathBCalls -eq $calls) "$($spec.name) recovery is idempotent after terminal checkpoint" $script:pathBCalls
    } else {
        Assert-True ($null -ne $errorText) "$($spec.name) recovery fails closed" $errorText
        Assert-True ($script:pathBCalls -eq $beforePathBCalls) "$($spec.name) does not disarm on unverified firmware data" $script:pathBCalls
        if ($spec.name -in @('unowned-bootnext','arm-intent-no-next-unresolved')) {
            Assert-True ((@($script:mockVariables.Keys | Sort-Object) -join ',') -ceq ($beforeVariableNames -join ',')) 'unowned BootNext refusal makes no variable changes' (@($script:mockVariables.Keys | Sort-Object) -join ',')
        } else {
            Assert-True ($script:mockVariables.ContainsKey('Boot0007') -and $script:mockVariables.ContainsKey('BootNext')) 'mismatched custom entry refusal preserves both values' $errorText
        }
    }
}

Reset-RecoveryCase 'pathb-manifest-exact' 'standard' $false
$exactStageRoot = Join-Path $script:espRoot $script:efiStagePath
[void][IO.Directory]::CreateDirectory($exactStageRoot)
[IO.File]::WriteAllBytes((Join-Path $exactStageRoot 'loader.efi'), [IO.File]::ReadAllBytes([string]$stageSources.loader))
[IO.File]::WriteAllBytes((Join-Path $exactStageRoot 'inventory.efi'), [IO.File]::ReadAllBytes([string]$stageSources.collector))
[IO.File]::WriteAllText((Join-Path $exactStageRoot 'manifest.sha256'), $pinnedManifest, [Text.Encoding]::ASCII)
$script:fixtureJournal.stageExpected = [ordered]@{
    loaderBytes = [long]$stageSources.loaderBytes
    loaderSha256 = [string]$stageSources.loaderSha256
    collectorBytes = [long]$stageSources.collectorBytes
    collectorSha256 = [string]$stageSources.collectorSha256
    manifest = $fixtureManifest
}
Save-FixtureJournal
$exactManifestJournal = Read-FixtureJournal 'correction-only'
$exactManifestError = $null
try { Invoke-FixtureRecovery $exactManifestJournal } catch { $exactManifestError = $_.Exception.Message }
Assert-True ($null -eq $exactManifestError) 'recovery accepts the exact pinned Path-B manifest and byte hashes' $exactManifestError
Assert-True ([string]$script:fixtureJournal.state -ceq 'RECOVERY_CLEANED' -and -not (Test-Path -LiteralPath $exactStageRoot)) 'exact Path-B stage recovery removes only the validated owned directory' $script:fixtureJournal.state

Reset-RecoveryCase 'pathb-manifest-path-character-size-refusal' 'standard' $false
$wrongSizeStageRoot = Join-Path $script:espRoot $script:efiStagePath
[void][IO.Directory]::CreateDirectory($wrongSizeStageRoot)
[IO.File]::WriteAllBytes((Join-Path $wrongSizeStageRoot 'loader.efi'), [IO.File]::ReadAllBytes([string]$stageSources.loader))
[IO.File]::WriteAllBytes((Join-Path $wrongSizeStageRoot 'inventory.efi'), [IO.File]::ReadAllBytes([string]$stageSources.collector))
$wrongSizeStage = [ordered]@{
    loaderBytes = [long]([string]$stageSources.loader).Length
    loaderSha256 = [string]$stageSources.loaderSha256
    collectorBytes = [long]([string]$stageSources.collector).Length
    collectorSha256 = [string]$stageSources.collectorSha256
}
$wrongSizeManifest = Get-PathBManifestText $wrongSizeStage
[IO.File]::WriteAllText((Join-Path $wrongSizeStageRoot 'manifest.sha256'), $wrongSizeManifest, [Text.Encoding]::ASCII)
$script:fixtureJournal.stageExpected = [ordered]@{
    loaderBytes = $wrongSizeStage.loaderBytes
    loaderSha256 = $wrongSizeStage.loaderSha256
    collectorBytes = $wrongSizeStage.collectorBytes
    collectorSha256 = $wrongSizeStage.collectorSha256
    manifest = $wrongSizeManifest
}
Save-FixtureJournal
$wrongSizeError = $null
try { Remove-PathBStageForRecovery $script:espRoot (Read-FixtureJournal 'correction-only') } catch { $wrongSizeError = $_.Exception.Message }
Assert-True ($null -ne $wrongSizeError -and $wrongSizeError -like '*byte size*') 'recovery rejects a manifest that substitutes path character counts for file byte lengths' $wrongSizeError
Assert-True ((Test-Path -LiteralPath $wrongSizeStageRoot -PathType Container) -and (Test-Path -LiteralPath (Join-Path $wrongSizeStageRoot 'loader.efi') -PathType Leaf)) 'wrong-size stage remains untouched after exact recovery refusal' $wrongSizeStageRoot

Reset-RecoveryCase 'capture-pathb-after-archive' 'standard' $false
$captureStageRoot = Install-MockPathBStage
$script:fixtureJournal = Read-FixtureJournal 'correction-only'
$captureMount = Mount-Esp
Dismount-Esp $captureMount
$captureCallsBefore = $script:pathBCalls
$captureAction = Invoke-CapturePathBCleanup 'correction-only' (Get-StageSources) $captureMount
Assert-True ($captureAction -ceq 'Disarm') 'capture chooses the exact mode-specific Path-B action' $captureAction
Assert-True ($script:pathBCalls -eq ($captureCallsBefore + 1) -and $script:pathBObservedUnmounted -eq $script:pathBCalls) 'capture Path-B dispatch executes after its ESP handle was released' $script:pathBObservedUnmounted
Remove-PathBStageAfterCapture $script:fixtureJournal
Assert-True (-not (Test-Path -LiteralPath $captureStageRoot) -and $script:metadata.cleanup.pathBStageRemoved -eq $true) 'successful correction-only capture removes the exact Path-B stage' $script:metadata.cleanup
Assert-True ($script:metadata.cleanup.trackedEspBaselineRestored -eq $true -and -not $script:metadata.esp.afterCleanup.stageDirectoryExists -and -not $script:metadata.esp.afterCleanup.correctionDirectoryExists) 'successful correction-only capture records the clean tracked ESP delta' $script:metadata.esp.afterCleanup
Assert-True (-not $script:mockEspMounted -and -not $script:mockPathBOwnsMount) 'successful capture stage cleanup releases its owned ESP mount' $script:mockEspMounted
Assert-True ($script:mockMountCount -eq 2 -and $script:mockDismountCount -eq 2) 'successful capture revalidates and dismounts the ESP after Path-B cleanup' ("mounts=$script:mockMountCount dismounts=$script:mockDismountCount")

Reset-RecoveryCase 'capture-active-patch-report-cleanup' 'standard' $false
$activeStageRoot = Install-MockPathBStage
$script:fixtureJournal.mode = 'corrected-collector'
$script:fixtureJournal.state = 'CLEANING'
Save-FixtureJournal
$patchPath = Join-Path $script:espRoot 'HypersinvSmbiosPatch.txt'
$patchBytes = [Text.Encoding]::ASCII.GetBytes("schema=combined-vm-smbios-patch-report.v1`nresult=applied`n")
[IO.File]::WriteAllBytes($patchPath, $patchBytes)
$patchHash = Get-Sha256 $patchPath
$script:metadata.collectorReport = [ordered]@{ sha256 = $patchHash }
$script:metadata.capturedFiles = @([ordered]@{ name = 'HypersinvSmbiosPatch.txt'; sha256 = $patchHash })
$script:fixtureJournal.state = 'CLEANING'
Remove-PathBStageAfterCapture $script:fixtureJournal
Assert-True (-not (Test-Path -LiteralPath $activeStageRoot) -and -not (Test-Path -LiteralPath $patchPath)) 'successful active capture removes its validated patch report and Path-B stage' ([ordered]@{ stageExists = (Test-Path -LiteralPath $activeStageRoot); patchExists = (Test-Path -LiteralPath $patchPath) })
Assert-True ($script:metadata.cleanup.trackedEspBaselineRestored -eq $true -and @($script:metadata.esp.afterCleanup.stageReportFiles).Count -eq 0) 'active capture final ESP snapshot has no tracked stage/report delta' $script:metadata.esp.afterCleanup

Reset-RecoveryCase 'capture-stage-mismatch-refusal' 'standard' $false
$mismatchedCaptureStage = Install-MockPathBStage
$script:fixtureJournal.state = 'CLEANING'
Save-FixtureJournal
[IO.File]::WriteAllText((Join-Path $mismatchedCaptureStage 'loader.efi'), 'changed')
$mismatchMountError = $null
try { Remove-PathBStageAfterCapture $script:fixtureJournal } catch { $mismatchMountError = $_.Exception.Message }
Assert-True ($null -ne $mismatchMountError -and $mismatchMountError -like '*Fixture recovery found a mismatched Path-B loader.*') 'successful capture refuses a modified stage file' $mismatchMountError
Assert-True ((Test-Path -LiteralPath $mismatchedCaptureStage -PathType Container) -and -not $script:mockEspMounted) 'stage mismatch remains for recovery and releases only the owned mount' ([ordered]@{ stageExists = (Test-Path -LiteralPath $mismatchedCaptureStage); mounted = $script:mockEspMounted })

Reset-RecoveryCase 'unowned-mounted-letter' 'standard' $false
$script:mockEspMounted = $true
$unownedMountError = $null
try { Invoke-PathBWithEspHandoff -ModeName 'Disarm' -Stage (Get-StageSources) | Out-Null } catch { $unownedMountError = $_.Exception.Message }
Assert-True ($null -ne $unownedMountError -and $script:pathBCalls -eq 0 -and $script:mockEspMounted) 'Path-B refuses a mount without this action owning its handle' $unownedMountError
$script:mockEspMounted = $false

Reset-RecoveryCase 'pathb-zero-exit-no-remount-leak' 'standard' $false
$script:mockPathBLeakMountAfterSuccess = $true
$noRemountLeakError = $null
try { Invoke-PathBWithEspHandoff -ModeName 'Disarm' -Stage (Get-StageSources) | Out-Null } catch { $noRemountLeakError = $_.Exception.Message }
Assert-True ($null -ne $noRemountLeakError -and $noRemountLeakError -like '*returned while ESP mount letter Z: remains in use*exit=0*') 'zero-exit Path-B no-remount call fails closed when Z remains mounted' $noRemountLeakError
Assert-True ($script:mockEspMounted -and $script:mockDismountCount -eq 0) 'no-remount postcondition does not dismount a mount without an owned handle' ("mounted=$script:mockEspMounted dismounts=$script:mockDismountCount")

Reset-RecoveryCase 'pathb-zero-exit-recovery-leak' 'standard' $false
$script:mockPathBLeakMountAfterSuccess = $true
$recoveryLeakError = $null
try { Invoke-FixtureRecovery (Read-FixtureJournal 'correction-only') } catch { $recoveryLeakError = $_.Exception.Message }
$leakedMountJournal = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json
Assert-True ($null -ne $recoveryLeakError -and $recoveryLeakError -like '*returned while ESP mount letter Z: remains in use*') 'recovery rejects a zero-exit Path-B call with leaked Z' $recoveryLeakError
Assert-True ([string]$leakedMountJournal.state -cne 'RECOVERY_CLEANED' -and [string]$leakedMountJournal.pendingMutation -ceq 'recovery_disarm_pathb') 'leaked Path-B mount leaves a durable pending journal instead of terminal recovery proof' $leakedMountJournal
Assert-True ($script:mockEspMounted -and $script:pathBCalls -eq 1) 'recovery does not dismount the unowned leaked mount' ("mounted=$script:mockEspMounted pathBCalls=$script:pathBCalls")

$directoryGuardRoot = Join-Path $testRoot 'directory-guard'
[void][IO.Directory]::CreateDirectory((Join-Path $directoryGuardRoot 'EFI\Microsoft\Boot'))
[IO.File]::WriteAllBytes((Join-Path $directoryGuardRoot $script:bootManagerRelative), [byte[]](1,2,3,4))
$stageAsFile = Join-Path $directoryGuardRoot $script:efiStagePath
[IO.File]::WriteAllText($stageAsFile, 'wrong-type')
$stageError = $null
try { Get-EspSnapshot $directoryGuardRoot | Out-Null } catch { $stageError = $_.Exception.Message }
Assert-True ($null -ne $stageError -and (Test-Path -LiteralPath $stageAsFile -PathType Leaf)) 'stage directory file collision fails closed and preserves file' $stageError
Remove-Item -LiteralPath $stageAsFile -Force
[void][IO.Directory]::CreateDirectory($stageAsFile)
$correctionAsFile = Join-Path $directoryGuardRoot $script:correctionRelative
[IO.File]::WriteAllText($correctionAsFile, 'wrong-type')
$correctionError = $null
try { Get-EspSnapshot $directoryGuardRoot | Out-Null } catch { $correctionError = $_.Exception.Message }
Assert-True ($null -ne $correctionError -and (Test-Path -LiteralPath $correctionAsFile -PathType Leaf)) 'correction directory file collision fails closed and preserves file' $correctionError

Reset-RecoveryCase 'recovery-stage-file-collision' 'standard' $false
$stagePathDuringRecovery = Join-Path $script:espRoot $script:efiStagePath
[IO.File]::WriteAllText($stagePathDuringRecovery, 'wrong-type')
$recoveryStageError = $null
try { Invoke-FixtureRecovery (Read-FixtureJournal 'correction-only') } catch { $recoveryStageError = $_.Exception.Message }
$stageJournalAfterFailure = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json
Assert-True ($null -ne $recoveryStageError -and (Test-Path -LiteralPath $stagePathDuringRecovery -PathType Leaf)) 'recovery refuses a regular file at the Path-B stage directory' $recoveryStageError
Assert-True ([string]$stageJournalAfterFailure.state -cne 'RECOVERY_CLEANED') 'stage-path type mismatch cannot produce a clean journal' $stageJournalAfterFailure.state

Reset-RecoveryCase 'recovery-correction-file-collision' 'standard' $false
$stagePathForCorrection = Join-Path $script:espRoot $script:efiStagePath
[void][IO.Directory]::CreateDirectory($stagePathForCorrection)
$correctionPathDuringRecovery = Join-Path $script:espRoot $script:correctionRelative
[IO.File]::WriteAllText($correctionPathDuringRecovery, 'wrong-type')
$recoveryCorrectionError = $null
try { Invoke-FixtureRecovery (Read-FixtureJournal 'correction-only') } catch { $recoveryCorrectionError = $_.Exception.Message }
$correctionJournalAfterFailure = Get-Content -LiteralPath $script:fixtureJournalPath -Raw | ConvertFrom-Json
Assert-True ($null -ne $recoveryCorrectionError -and (Test-Path -LiteralPath $correctionPathDuringRecovery -PathType Leaf)) 'recovery refuses a regular file at the correction directory' $recoveryCorrectionError
Assert-True ([string]$correctionJournalAfterFailure.state -cne 'RECOVERY_CLEANED') 'correction-path type mismatch cannot produce a clean journal' $correctionJournalAfterFailure.state

function Invoke-FixtureStackTraceProbe { throw 'expected offline stack-trace probe failure' }
$stackTraceDetail = $null
try { Invoke-FixtureStackTraceProbe } catch { $stackTraceDetail = Get-FixtureErrorDetail -ErrorRecord $_ -Phase 'test' }
Assert-True ([string]$stackTraceDetail.message -ceq 'expected offline stack-trace probe failure' -and [string]$stackTraceDetail.scriptStackTrace -match 'Invoke-FixtureStackTraceProbe' -and [string]$stackTraceDetail.positionMessage -match 'tests-fixture-correction-recovery\.ps1') 'failure detail preserves PowerShell ScriptStackTrace and source position' $stackTraceDetail

Reset-RecoveryCase 'capture-after-boot-add-captured-file-end-to-end' 'custom' $true
$script:mode = 'correction-only'
$script:stateRoot = $script:stageRoot
$script:currentJournalPath = Join-Path $script:stateRoot 'correction-only.journal.json'
$script:fixtureJournalPath = $script:currentJournalPath
$script:runRootFull = Join-Path $testRoot 'capture-after-boot-run'
[void][IO.Directory]::CreateDirectory($script:runRootFull)
$script:result = [ordered]@{ Status = 'FAIL'; Message = 'Offline capture contract test did not finish.' }
$script:mockReadbackModes = @()
$script:mockIdentityModes = @()
$script:mockSecureBootQueryCount = 0
$script:metadata = [ordered]@{
    schema = 'combined-vm-fixture-correction-action.v1'
    internalAction = 'CaptureCorrectionOnly'
    mode = 'correction-only'
    status = 'FAIL'
    vm = [ordered]@{ manufacturer = $null; model = $null; uuid = $null; diskPnp = $null; diskSerial = $null }
    bootIdentity = $null
    firmware = [ordered]@{ secureBoot = $null; secureBootVariable = $null; bootOrderBefore = $null; bootOrderAfter = $null; bootNextBefore = $null; bootNextAfter = $null; bootCurrent = $null; standardBootEntry = $null; customBootEntry = $null }
    esp = [ordered]@{ before = $null; afterStageArm = $null; afterCapture = $null; correctionEvidence = @() }
    windowsReadback = $null
    collectorReport = $null
    pathBAction = $null
    journal = $null
    sourcePins = @()
    capturedFiles = @()
    rawEvidenceBundle = $null
    errors = @()
    errorDetails = @()
    cleanup = [ordered]@{ attempted = $false; errors = @() }
}
$e2eLoaderBuild = Join-Path $script:stateRoot 'pathb\loader'
$e2eInventoryBuild = Join-Path $script:stateRoot 'pathb\inventory'
[void][IO.Directory]::CreateDirectory($e2eLoaderBuild)
[void][IO.Directory]::CreateDirectory((Join-Path $e2eInventoryBuild 'efi'))
[IO.File]::WriteAllBytes((Join-Path $e2eLoaderBuild 'inventory_loader.efi'), [IO.File]::ReadAllBytes([string]$stageSources.loader))
[IO.File]::WriteAllBytes((Join-Path $e2eLoaderBuild 'ib-install.exe'), [IO.File]::ReadAllBytes([string]$stageSources.installer))
[IO.File]::WriteAllBytes((Join-Path $e2eInventoryBuild 'efi\inventory.efi'), [IO.File]::ReadAllBytes([string]$stageSources.collector))
[IO.File]::WriteAllBytes((Join-Path $e2eInventoryBuild 'efi_inventory.exe'), [IO.File]::ReadAllBytes([string]$stageSources.scanner))
$script:mockVariables.Remove('BootNext') | Out-Null
$script:mockVariables['BootCurrent'] = New-MockRecord ([BitConverter]::GetBytes([uint16]7)) 7
$script:mockVariables['SecureBoot'] = New-MockRecord ([byte[]](0)) 6
$e2eStageRoot = Install-MockPathBStage
$script:fixtureJournal.state = 'ARMED'
Save-FixtureJournal
$e2eCorrectionRoot = Join-Path $script:espRoot $script:correctionRelative
[void][IO.Directory]::CreateDirectory($e2eCorrectionRoot)
foreach ($pin in $rawR6Files) {
    Copy-Item -LiteralPath (Join-Path $rawR6Root $pin.name) -Destination (Join-Path $e2eCorrectionRoot $pin.name) -ErrorAction Stop
}
$testPreludeBytes = [byte[]](0x46,0x49,0x58,0x54,0x55,0x52,0x45)
$e2ePreludePath = Join-Path $e2eCorrectionRoot 'smbios2-ep-prelude.efi'
[IO.File]::WriteAllBytes($e2ePreludePath, $testPreludeBytes)
$script:config.preludeSha256 = Get-Sha256 $e2ePreludePath
$script:fixtureJournal.preludeSha256 = [string]$script:config.preludeSha256
Save-FixtureJournal
$toctouTarget = Join-Path $e2eCorrectionRoot 'eps2-before.bin'
$toctouExpectedBytes = [IO.File]::ReadAllBytes($toctouTarget)
$script:toctouMutationBytes = [byte[]](0x54,0x4F,0x43,0x54,0x4F,0x55)
$script:toctouMutationTarget = [IO.Path]::GetFullPath($toctouTarget)
$script:toctouMutationApplied = $false
$script:productionAddCapturedFile = (Get-Item Function:\Add-CapturedFile).ScriptBlock
$addCapturedFileWithMutation = {
    param([string]$Source, [string]$Name, [string]$Extension, $ValidatedRecord)
    if (-not $script:toctouMutationApplied -and [string]::Equals([IO.Path]::GetFullPath($Source), $script:toctouMutationTarget, [StringComparison]::OrdinalIgnoreCase)) {
        [IO.File]::WriteAllBytes($Source, [byte[]]$script:toctouMutationBytes)
        $script:toctouMutationApplied = $true
    }
    & $script:productionAddCapturedFile $Source $Name $Extension $ValidatedRecord
}
Set-Item Function:\Add-CapturedFile -Value $addCapturedFileWithMutation -Force
$captureContractError = $null
try { Capture-AfterBoot } catch { $captureContractError = $_.Exception.ToString() }
finally { Set-Item Function:\Add-CapturedFile -Value $script:productionAddCapturedFile -Force }
Assert-True ($null -eq $captureContractError) 'end-to-end Capture-AfterBoot accepts production Get-SafeFileRecord output while archiving Add-CapturedFile metadata' $captureContractError
Assert-True ($script:toctouMutationApplied -eq $true) 'end-to-end capture injects a source mutation after safe-read and before archive write' $script:toctouMutationApplied
$toctouArchiveRecord = @($script:metadata.capturedFiles | Where-Object { [string]$_.name -ceq 'eps2-before.bin' })
$toctouArchivedBytes = [IO.File]::ReadAllBytes([string]$toctouArchiveRecord[0].path)
Assert-True ($toctouArchiveRecord.Count -eq 1 -and [long]$toctouArchiveRecord[0].bytes -eq $toctouExpectedBytes.Length -and [string]$toctouArchiveRecord[0].sha256 -ceq (Get-Sha256FromBytes $toctouExpectedBytes) -and [Convert]::ToBase64String($toctouArchivedBytes) -ceq [Convert]::ToBase64String($toctouExpectedBytes) -and [Convert]::ToBase64String($script:toctouMutationBytes) -cne [Convert]::ToBase64String($toctouArchivedBytes)) 'TOCTOU injection archives the exact previously validated bytes and metadata, not the mutated source bytes' ([ordered]@{ validatedBytes = $toctouExpectedBytes.Length; archivedBytes = $toctouArchivedBytes.Length; archivedSha256 = $toctouArchiveRecord[0].sha256; injectedBytesSha256 = Get-Sha256FromBytes $script:toctouMutationBytes })
Assert-True ([string]$script:result.Status -ceq 'PASS' -and [string]$script:metadata.status -ceq 'PASS') 'end-to-end capture reaches its real success terminal state' ([ordered]@{ result = $script:result.Status; metadata = $script:metadata.status })
Assert-True ([string]$script:metadata.efi.statusPhase -ceq 'LoadImage(bootmgfw.efi)' -and [string]$script:metadata.efi.childTarget -ceq 'EFI\Microsoft\Boot\bootmgfw.efi') 'correction-only Capture metadata reports its validated Windows Boot Manager LoadImage target' ([ordered]@{ statusPhase = $script:metadata.efi.statusPhase; childTarget = $script:metadata.efi.childTarget })
Assert-True ($script:metadata.efi.childStartStatusObserved -eq $false -and $null -eq $script:metadata.efi.childStartStatus) 'unobserved EFI child-start status remains null instead of inventing a value' ([ordered]@{ observed = $script:metadata.efi.childStartStatusObserved; status = $script:metadata.efi.childStartStatus })
Assert-True ($script:mockReadbackModes.Count -eq 1 -and $script:mockReadbackModes[0] -ceq 'original' -and $script:mockSecureBootQueryCount -eq 1) 'capture calls the mocked Windows readback and Secure Boot APIs exactly once in original mode' ([ordered]@{ readbackModes = $script:mockReadbackModes; secureBootQueries = $script:mockSecureBootQueryCount })
Assert-True (@($script:metadata.capturedFiles).Count -eq 8 -and @($script:metadata.capturedFiles | Where-Object { $null -ne $_.PSObject.Properties['content'] }).Count -eq 0) 'archive metadata contains all eight files without embedding raw content fields' ([ordered]@{ count = @($script:metadata.capturedFiles).Count; contentFields = @($script:metadata.capturedFiles | Where-Object { $null -ne $_.PSObject.Properties['content'] }).Count })
Assert-True ([string]$script:metadata.pathBAction -ceq 'Disarm' -and [string]$script:fixtureJournal.state -ceq 'CAPTURED_AND_CLEANED' -and $script:metadata.cleanup.pathBStageRemoved -eq $true -and $script:metadata.cleanup.trackedEspBaselineRestored -eq $true) 'end-to-end cleanup removes only the fixture subtree, owned boot state, and exact Path-B stage' ([ordered]@{ pathBAction = $script:metadata.pathBAction; journal = $script:fixtureJournal.state; cleanup = $script:metadata.cleanup })
Assert-True (-not (Test-Path -LiteralPath $e2eCorrectionRoot) -and -not (Test-Path -LiteralPath $e2eStageRoot) -and -not $script:mockEspMounted -and -not $script:mockVariables.ContainsKey('BootNext') -and -not $script:mockVariables.ContainsKey('Boot0006') -and -not $script:mockVariables.ContainsKey('Boot0007')) 'end-to-end capture leaves owned filesystem, mount, and Boot#### state clean' ([ordered]@{ correctionExists = (Test-Path -LiteralPath $e2eCorrectionRoot); stageExists = (Test-Path -LiteralPath $e2eStageRoot); mounted = $script:mockEspMounted; variables = @($script:mockVariables.Keys | Sort-Object) })

Reset-RecoveryCase 'capture-active-boot-identity-association' 'custom' $true
$script:mode = 'corrected-collector'
$script:fixtureJournal.mode = $script:mode
$script:fixtureJournal.modeFlag = $script:mode
$script:fixtureJournal.state = 'ARMED'
$script:currentJournalPath = Join-Path $script:stateRoot 'corrected-collector.journal.json'
$script:fixtureJournalPath = $script:currentJournalPath
$script:metadata = [ordered]@{
    schema = 'combined-vm-fixture-correction-action.v1'; internalAction = 'CaptureActive'; mode = 'corrected-collector'; status = 'FAIL'
    vm = [ordered]@{ manufacturer = $null; model = $null; uuid = $null; diskPnp = $null; diskSerial = $null }
    bootIdentity = $null
    firmware = [ordered]@{ secureBoot = $null; secureBootVariable = $null; bootOrderBefore = $null; bootOrderAfter = $null; bootNextBefore = $null; bootNextAfter = $null; bootCurrent = $null; standardBootEntry = $null; customBootEntry = $null }
    esp = [ordered]@{ before = $null; afterStageArm = $null; afterCapture = $null; correctionEvidence = @() }
    windowsReadback = $null; collectorReport = $null; pathBAction = $null; journal = $null; sourcePins = @(); capturedFiles = @(); rawEvidenceBundle = $null; errors = @(); errorDetails = @()
    cleanup = [ordered]@{ attempted = $false; errors = @() }
}
$script:result = [ordered]@{ Status = 'FAIL'; Message = 'Offline CaptureActive boot identity association test did not finish.' }
$script:runRootFull = Join-Path $testRoot 'capture-active-run'
[void][IO.Directory]::CreateDirectory($script:runRootFull)
$script:mockReadbackModes = @()
$script:mockIdentityModes = @()
$script:mockSecureBootQueryCount = 0
$activeBootText = [string]$activeSourceMetadata.bootIdentity.windowsLastBootUpTimeUtc
$script:mockBootUpTime = [DateTime]::ParseExact($activeBootText, 'o', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
$script:config.collectorBuildId = [string]$activeSourceMetadata.collectorReport.buildId
$script:config.preludeSha256 = Get-Sha256 ([string]$activeEvidenceByName['smbios2-ep-prelude.efi'])
$script:fixtureJournal.preludeSha256 = [string]$script:config.preludeSha256
$activeStageRoot = Install-MockPathBStage
$script:fixtureJournal.state = 'ARMED'
Save-FixtureJournal
$script:mockVariables.Remove('BootNext') | Out-Null
$script:mockVariables['BootCurrent'] = New-MockRecord ([BitConverter]::GetBytes([uint16]7)) 7
$script:mockVariables['SecureBoot'] = New-MockRecord ([byte[]](0)) 6
$activeCorrectionRoot = Join-Path $script:espRoot $script:correctionRelative
[void][IO.Directory]::CreateDirectory($activeCorrectionRoot)
foreach ($name in @('report.txt','eps2-before.bin','eps2-after.bin','eps3-before.bin','eps3-after.bin','table-before.bin','table-after.bin','smbios2-ep-prelude.efi')) {
    Copy-Item -LiteralPath ([string]$activeEvidenceByName[$name]) -Destination (Join-Path $activeCorrectionRoot $name) -ErrorAction Stop
}
Copy-Item -LiteralPath ([string]$activeEvidenceByName['HypersinvSmbiosPatch.txt']) -Destination (Join-Path $script:espRoot 'HypersinvSmbiosPatch.txt') -ErrorAction Stop
$script:fixtureJournalPath = $script:currentJournalPath
Save-FixtureJournal
$activeCaptureError = $null
try { Capture-AfterBoot } catch { $activeCaptureError = $_.Exception.ToString() }
Assert-True ($null -eq $activeCaptureError) 'offline CaptureActive follows production report parse and boot identity association path' $activeCaptureError
$collectorBootIdentity = $script:metadata.collectorReport.bootIdentity
$collectorBootProvenance = $script:metadata.collectorReport.bootIdentityProvenance
Assert-True ([string]$script:metadata.status -ceq 'PASS' -and [string]$script:metadata.bootIdentity.bootId -ceq [string]$activeSourceMetadata.bootIdentity.bootId) 'CaptureActive measured boot identity matches the archived same-boot original active capture' ([ordered]@{ status = $script:metadata.status; expectedBootId = $activeSourceMetadata.bootIdentity.bootId; actualBootId = $script:metadata.bootIdentity.bootId })
Assert-True ([string]$collectorBootIdentity.bootId -ceq [string]$script:metadata.bootIdentity.bootId -and [string]$collectorBootIdentity.windowsLastBootUpTimeUtc -ceq [string]$script:metadata.bootIdentity.windowsLastBootUpTimeUtc -and [string]$collectorBootIdentity.lastBootUpTimeUtc -ceq [string]$script:metadata.bootIdentity.lastBootUpTimeUtc) 'CaptureActive collectorReport carries the exact measured bootId and both observed timestamp fields' $collectorBootIdentity
Assert-True ([string]$collectorBootProvenance.source -ceq 'Win32_OperatingSystem.LastBootUpTime via Capture-AfterBoot/Get-BootIdentity' -and [string]$collectorBootProvenance.association -like '*same authenticated CaptureActive invocation*' -and $collectorBootProvenance.emittedByEfiCollector -eq $false) 'CaptureActive boot identity provenance explicitly identifies same-action Windows measurement and says EFI did not emit it' $collectorBootProvenance
Assert-True ([string]$script:metadata.collectorReport.status -ceq 'PASS' -and [string]$script:metadata.collectorReport.result -ceq 'applied' -and [int]$script:metadata.collectorReport.mask -eq 127 -and [string]$script:metadata.efi.status -ceq '0x0000000000000000' -and [string]$script:metadata.efi.statusPhase -ceq 'LoadImage(loader.efi)') 'CaptureActive source collector and EFI status remain tied to the validated corrected-collector mode' ([ordered]@{ status = $script:metadata.collectorReport.status; result = $script:metadata.collectorReport.result; mask = $script:metadata.collectorReport.mask; efi = $script:metadata.efi.status; phase = $script:metadata.efi.statusPhase })
Assert-True (@($script:metadata.capturedFiles).Count -eq 9 -and [string]$script:metadata.pathBAction -ceq 'Collect' -and [string]$script:fixtureJournal.state -ceq 'CAPTURED_AND_CLEANED') 'CaptureActive archives all nine actual fixture inputs and reaches its normal clean terminal state' ([ordered]@{ capturedFileCount = @($script:metadata.capturedFiles).Count; pathBAction = $script:metadata.pathBAction; journalState = $script:fixtureJournal.state })

$reportPath = Join-Path $reportRoot ('fixture-correction-recovery-tests.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + $testId + '.json')
$report = [ordered]@{
    schema = 'combined-vm-fixture-correction-recovery-tests.v1'
    status = 'PASS'
    checkCount = $checks.Count
    cases = @($caseSpec | ForEach-Object { $_.name })
    pinnedPathBManifestWriter = [ordered]@{ path = $pinnedPathB; sha256 = $pinnedPathBSha256; function = 'New-Manifest' }
    exactPathBManifest = $pinnedManifest
    vmBooted = $false
    firmwareChanged = $false
    hostStateChanged = $false
    checks = @($checks.ToArray())
}
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 20) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction Stop
[pscustomobject]@{ status = $report.status; checkCount = $report.checkCount; reportPath = $reportPath; vmBooted = $false; firmwareChanged = $false; hostStateChanged = $false } | ConvertTo-Json -Depth 5
