$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
$implementation = Join-Path $PSScriptRoot 'run.ps1'
$mapPath = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
$script:canonicalMapPath = $mapPath
$reportRoot = Join-Path $repositoryRoot 'spoof-handoff\reports'
$fixtureRoot = Join-Path (Join-Path $reportRoot 'combined-host-20261018') ('.orchestration-tests\' + [Guid]::NewGuid().ToString('N'))
. $implementation -Action Status -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -LibraryOnly
. (Join-Path $PSScriptRoot 'nic-transaction.ps1')

$script:passed = 0
function Assert-Test {
    param([bool]$Condition,[string]$Name)
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:passed++
}
function Assert-Throws {
    param([scriptblock]$Action,[string]$Name)
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Assert-Test $threw $Name
}
function New-TestReadback {
    param([object]$BindingMap,[ValidateSet('Original','Replacement')][string]$Mode,[string]$MapPath,[string]$MapSha)
    if ([string]::IsNullOrWhiteSpace($MapPath)) { $MapPath = $script:canonicalMapPath }
    if ([string]::IsNullOrWhiteSpace($MapSha)) { $MapSha = Get-CombinedHostSha256 $MapPath }
    $polls = [Collections.Generic.List[object]]::new()
    for ($pollNumber=1; $pollNumber -le 2; $pollNumber++) {
        $pollBindings = [Collections.Generic.List[object]]::new()
        foreach ($binding in $BindingMap.bindings) {
            $expected = if ($Mode -ceq 'Replacement') { [string]$binding.replacement } else { [string]$binding.original }
            $readers = [Collections.Generic.List[object]]::new()
            foreach ($readerName in $binding.readers) {
                $readers.Add([pscustomobject]@{reader=[string]$readerName;actual=$expected;expected=$expected;match=$true;cardinality=1})
            }
            $pollBindings.Add([pscustomobject]@{id=[string]$binding.id;readers=@($readers.ToArray())})
        }
        $polls.Add([pscustomobject]@{bindings=@($pollBindings.ToArray())})
    }
    return [pscustomobject]@{
        schema='combined-host-readback.v1'
        expectedMode=$Mode
        status='PASS'
        simultaneousReadbackPass=$true
        stableAcrossPolls=$true
        lockedMap=[pscustomobject]@{path=$MapPath;sha256=$MapSha;bindingCount=$BindingMap.bindings.Count}
        polls=@($polls.ToArray())
    }
}
function New-TestVmFixtureRows {
    param([object[]]$CanonicalRows)
    $nicIndexByOriginal = @{
        '005056C00008'='7'
        '005056C00001'='6'
        'C4BDE53206D1'='2'
        'D8BBC1D85720'='5'
    }
    foreach ($row in $CanonicalRows) {
        $copy = [pscustomobject][ordered]@{member=[string]$row.member;device=[string]$row.device;original=[string]$row.original;replacement=[string]$row.replacement}
        switch -CaseSensitive ($copy.member) {
            'FakeSMBIOSSerial' { $copy.original = 'VM-SERIAL-0001' }
            'FakeSMBIOSManufacturer' { $copy.original = 'VMware, Inc.' }
            'FakeSMBIOSProduct' { $copy.original = 'VMware7,1' }
            'FakeMotherboardSerial' { $copy.original = 'VM-BOARD-0001' }
            'FakeMotherboardManufacturer' { $copy.original = 'VMware, Inc.' }
            'FakeMotherboardProduct' { $copy.original = 'Virtual Platform' }
            'FakeSMBIOSUUID' { $copy.original = '564D0000-0000-4000-8000-000000000001' }
            'FakeDiskSerial' { $copy.device = 'disk:\\.\PHYSICALDRIVE0'; $copy.original = 'VMWARE-VIRTUAL-DISK-0001' }
            'FakeMacAddress' {
                $key = ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $copy.original
                $ifIndex = [string]$nicIndexByOriginal[[string]$key]
                $pnp = switch ([string]$key) {
                    '005056C00008' { 'ROOT\NET\0001' }
                    '005056C00001' { 'ROOT\NET\0000' }
                    'C4BDE53206D1' { 'PCI\VEN_15AD&DEV_07B0\C4BDE5FFFF3206D100' }
                    'D8BBC1D85720' { 'PCI\VEN_15AD&DEV_07B0\D8BBC1FFFFD8572000' }
                    default { throw "Unexpected canonical NIC original in test fixture: $($copy.original)" }
                }
                $copy.device = 'adapter:' + $ifIndex + '|' + $pnp
            }
        }
        $copy
    }
}
function Write-TestVmMap {
    param([object[]]$Rows,[string]$Path)
    $csv = @($Rows | ConvertTo-Csv -Delimiter "`t" -NoTypeInformation)
    [IO.File]::WriteAllText($Path,($csv -join "`r`n") + "`r`n",[Text.UTF8Encoding]::new($false))
}
function Write-TestEfiApplication([string]$Path) {
    $bytes = [byte[]]::new(512)
    $bytes[0] = 0x4D; $bytes[1] = 0x5A
    [Array]::Copy([BitConverter]::GetBytes([uint32]0x80),0,$bytes,0x3C,4)
    $bytes[0x80] = 0x50; $bytes[0x81] = 0x45
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x8664),0,$bytes,0x84,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0xF0),0,$bytes,0x94,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x20B),0,$bytes,0x98,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]10),0,$bytes,0xDC,2)
    [IO.File]::WriteAllBytes($Path,$bytes)
    return ,$bytes
}

$rows = Get-CombinedMapRows $mapPath
$bindingMap = New-CombinedBindingMap $rows
Assert-Test ($bindingMap.bindings.Count -eq 12) 'locked scope resolves to seven SMBIOS, one disk, and four NIC bindings'
Assert-Test (@($bindingMap.bindings | Where-Object { $_.category -ceq 'NIC' }).Count -eq 4) 'all four locked NIC bindings are present'
$diskBindings = @($bindingMap.bindings | Where-Object { $_.category -ceq 'DISK' })
Assert-Test ($diskBindings.Count -eq 1 -and $diskBindings[0].device -match 'PHYSICALDRIVE0$') 'only the locked system disk is in scope; other disks including USB are excluded'

$validTransitions = @(
    @('EnableTestMode','Prepared'),@('InstallTransparent','PolicyPendingReboot'),@('VerifyTransparent','TransparentInstallPendingReboot'),
    @('Remove','TransparentVerified'),@('VerifyOriginal','TransparentRemovalPendingReboot'),@('InstallReplacement','TransparentRemoved'),
    @('VerifyReplacement','ReplacementInstallPendingReboot'),@('ArmCombined','ReplacementVerified'),@('CollectCombined','CombinedArmPendingReboot'),
    @('Remove','CombinedReadbackComplete'),@('VerifyOriginal','ReplacementRemovalPendingReboot'),@('RestorePolicy','ReplacementRemoved'),@('FinalVerify','PolicyRestorePendingReboot')
)
foreach ($transition in $validTransitions) { Assert-Test (Assert-CombinedLifecyclePhase -Action $transition[0] -Phase $transition[1]) ("lifecycle allows " + $transition[0] + ' from ' + $transition[1]) }
Assert-Throws { Assert-CombinedLifecyclePhase -Action 'ArmCombined' -Phase 'Prepared' } 'lifecycle rejects arming before the replacement storage phase is verified'
Assert-Throws { Assert-CombinedLifecyclePhase -Action 'FinalVerify' -Phase 'CombinedReadbackComplete' } 'lifecycle rejects final verification before package and policy restoration'

$original = New-TestReadback $bindingMap 'Original'
$active = New-TestReadback $bindingMap 'Replacement'
Assert-Test (Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $original -Phase original) 'original phase validates all raw reader values'
Assert-Test (Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $active -Phase active) 'active phase validates all raw reader values'
Assert-Test (Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $original -Phase restored) 'restored phase validates all raw reader values'

$forged = New-TestReadback $bindingMap 'Replacement'
$forged.polls[0].bindings[0].readers[0].actual = [string]$bindingMap.bindings[0].original
$forged.polls[0].bindings[0].readers[0].match = $true
Assert-Throws { Assert-CombinedReadbackPhase -BindingMap $bindingMap -Readback $forged -Phase active } 'actual value mismatch is rejected even when status and match booleans claim success'

$sourcePin = Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path 'tools/combined_host/model.psm1'
Assert-Test ($sourcePin.relative -ceq 'tools/combined_host/model.psm1' -and (Test-Path -LiteralPath $sourcePin.path -PathType Leaf)) 'worktree source pins resolve using the gate path convention'
$pathBScriptPath = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'tools\efi_inventory\path-b-collector.ps1'))
$pathBManifestPath = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'spoof-handoff\reports\combined-9field-vm-20261018\pathb-source-tree-manifest.json'))
$pathBSourcePin = Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path $pathBScriptPath
Assert-Test ($pathBSourcePin.root -ceq 'repository' -and $pathBSourcePin.relative -ceq 'tools/efi_inventory/path-b-collector.ps1' -and [IO.Path]::GetFullPath($pathBSourcePin.path) -ceq $pathBScriptPath) 'absolute Path-B source pins resolve to the mutating child in the main repository'
Assert-Test ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..\tools\efi_inventory\path-b-collector.ps1')) -ceq $pathBScriptPath) 'Path-B mutating child is resolved from the main repository rather than the worktree'
Assert-Test (Test-Path -LiteralPath $pathBManifestPath -PathType Leaf) 'hash-pinned Path-B native source manifest exists under reports'
$completeRequiredPins = @(foreach ($relative in $script:CombinedRequiredSourcePins) { $sourcePath = if ($relative -ceq 'tools/efi_inventory/path-b-collector.ps1') { Join-Path $repositoryRoot 'tools\efi_inventory\path-b-collector.ps1' } else { $relative }; $resolved = Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path $sourcePath; if (-not (Test-Path -LiteralPath $resolved.path -PathType Leaf)) { throw "Required VM source fixture is missing: $relative" }; [pscustomobject]@{relative=$resolved.relative;path=$resolved.path} })
Assert-Test (Assert-CombinedRequiredVmSourcePins -NormalizedPins $completeRequiredPins) 'required VM gate pins include the SMBIOS collector headers, table source, CMake configuration, and fixture builder'
$missingSmbiosHeaderPins = @($completeRequiredPins | Where-Object { $_.relative -cne 'tools/smbios_boot/boot_report.h' })
Assert-Throws { Assert-CombinedRequiredVmSourcePins -NormalizedPins $missingSmbiosHeaderPins } 'VM gate with the boot report compile header omitted is rejected'
$executionPins = @(
    [pscustomobject]@{name='combined-host-runner';path=(Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\run.ps1');sha256=(Get-CombinedHostSha256 (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\run.ps1'))},
    [pscustomobject]@{name='combined-nic-transaction';path=(Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\nic-transaction.ps1');sha256=(Get-CombinedHostSha256 (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\nic-transaction.ps1'))},
    [pscustomobject]@{name='combined-host-payload-builder';path=(Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\build-host-payload.ps1');sha256=(Get-CombinedHostSha256 (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\build-host-payload.ps1'))},
    [pscustomobject]@{name='path-b-child';path=$pathBScriptPath;sha256=(Get-CombinedHostSha256 $pathBScriptPath)},
    [pscustomobject]@{name='path-b-source-manifest';path=$pathBManifestPath;sha256=(Get-CombinedHostSha256 $pathBManifestPath)}
)
$executionPinJournal = [pscustomobject]@{executionSourcePins=$executionPins;activationAvailable=$false;vmGate=[pscustomobject]@{pathBSourcePin=[pscustomobject]@{path=$pathBScriptPath;sha256=$executionPins[3].sha256};pathBSourceManifest=[pscustomobject]@{path=$pathBManifestPath;sha256=$executionPins[4].sha256}}}
Assert-Test (Assert-CombinedExecutionSourcePins -Journal $executionPinJournal -Repository $repositoryRoot -Worktree $worktreeRoot) 'prepared execution source pins validate runner, NIC transaction, host payload builder, Path-B child, and source manifest hashes'
$staleExecutionPins = @($executionPins | ForEach-Object { [pscustomobject]@{name=$_.name;path=$_.path;sha256=$_.sha256} })
$staleExecutionPins[3].sha256 = '0' * 64
$staleExecutionPinJournal = [pscustomobject]@{executionSourcePins=$staleExecutionPins;activationAvailable=$false;vmGate=[pscustomobject]@{pathBSourcePin=[pscustomobject]@{path=$pathBScriptPath;sha256=$staleExecutionPins[3].sha256};pathBSourceManifest=[pscustomobject]@{path=$pathBManifestPath;sha256=$staleExecutionPins[4].sha256}}}
Assert-Throws { Assert-CombinedExecutionSourcePins -Journal $staleExecutionPinJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'Path-B source drift after Prepare is rejected before execution'
$staleManifestPins = @($executionPins | ForEach-Object { [pscustomobject]@{name=$_.name;path=$_.path;sha256=$_.sha256} })
$staleManifestPins[4].sha256 = '0' * 64
$staleManifestJournal = [pscustomobject]@{executionSourcePins=$staleManifestPins;activationAvailable=$false;vmGate=[pscustomobject]@{pathBSourcePin=[pscustomobject]@{path=$pathBScriptPath;sha256=$staleManifestPins[3].sha256};pathBSourceManifest=[pscustomobject]@{path=$pathBManifestPath;sha256=$staleManifestPins[4].sha256}}}
Assert-Throws { Assert-CombinedExecutionSourcePins -Journal $staleManifestJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'Path-B native source manifest file drift is rejected before execution'
    $hostPayloadFixtureRoot = Join-Path $repositoryRoot ('spoof-handoff\artifacts\chp\.orchestration-test.' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($hostPayloadFixtureRoot)
    $hostPayloadImagePath = Join-Path $hostPayloadFixtureRoot 'inventory.efi'
    $hostPayloadImageBytes = Write-TestEfiApplication $hostPayloadImagePath
    $hostPayloadCtestPath = Join-Path $hostPayloadFixtureRoot 'ctest.log'
    [IO.File]::WriteAllText($hostPayloadCtestPath,"100% tests passed, 0 tests failed out of 2`r`n",[Text.UTF8Encoding]::new($false))
    $hostPayloadPins = @(Get-CombinedHostPayloadSourcePins -Repository $repositoryRoot -Worktree $worktreeRoot)
    $hostPayloadSourceSetSha = Get-CombinedHostPayloadSourceSetHash -Pins $hostPayloadPins
    $hostPayloadMapSha = Get-CombinedHostSha256 $mapPath
    $hostPayloadManifestSha = Get-CombinedHostSha256 $pathBManifestPath
    $hostPayloadImageSha = Get-CombinedHostSha256 $hostPayloadImagePath
    $hostPayloadCtestSha = Get-CombinedHostSha256 $hostPayloadCtestPath
    $hostPayloadBuildId = '0123456789ABCDEF0123456789ABCDEF'
    $hostPayloadVerdictPath = Join-Path $hostPayloadFixtureRoot 'build-verdict.json'
    $hostPayloadVerdict = [ordered]@{
        schema='combined-host-smbios-payload.v1';result='PASS';hostValuesProfile='checked-in-host-values';vmOnlyProfileEnabled=$false;activeBootMask=127;buildId=$hostPayloadBuildId
        payload=[ordered]@{path=$hostPayloadImagePath;bytes=[long]$hostPayloadImageBytes.Length;sha256=$hostPayloadImageSha;efiMachine='x64';efiSubsystem='EFI_APPLICATION'}
        lockedMap=[ordered]@{path=$mapPath;sha256=$hostPayloadMapSha;bindingCount=12;activeSmbiosFields=7;usbDiskTargetsExcluded=$true}
        sourceManifest=[ordered]@{path=$pathBManifestPath;sha256=$hostPayloadManifestSha}
        sourceSet=[ordered]@{sha256=$hostPayloadSourceSetSha;fileCount=$hostPayloadPins.Count;pinnedFiles=$hostPayloadPins}
        configuration=[ordered]@{vmOnlyProfile='OFF';vmValuesHeader='';buildType='Release';buildTesting=$true;activeBootMask=127;diskSerialEnabled=$false;adapterMacEnabled=$false;cpuSerialEnabled=$false;gpuSerialEnabled=$false}
        ctest=[ordered]@{count=2;passed=2;failed=0;exitCode=0;logPath=$hostPayloadCtestPath;logSha256=$hostPayloadCtestSha}
        liveBootProven=$false;signingPerformed=$false;stageOrArmPerformed=$false
    }
    [IO.File]::WriteAllText($hostPayloadVerdictPath,($hostPayloadVerdict | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    $hostPayloadReference = [pscustomobject][ordered]@{verdictPath=$hostPayloadVerdictPath;verdictSha256=(Get-CombinedHostSha256 $hostPayloadVerdictPath);payloadPath=$hostPayloadImagePath;payloadSha256=$hostPayloadImageSha;buildId=$hostPayloadBuildId;sourceSetSha256=$hostPayloadSourceSetSha}
    Assert-Test ((Assert-CombinedHostPayload -Reference $hostPayloadReference -Repository $repositoryRoot -Worktree $worktreeRoot).activeBootMask -eq 127) 'fresh host payload validator accepts the locked host profile, two passing tests, current source set, and x64 EFI application'
    $staleHostPayloadReference = [pscustomobject][ordered]@{verdictPath=$hostPayloadVerdictPath;verdictSha256=$hostPayloadReference.verdictSha256;payloadPath=$hostPayloadImagePath;payloadSha256=('0' * 64);buildId=$hostPayloadBuildId;sourceSetSha256=$hostPayloadSourceSetSha}
    Assert-Throws { Assert-CombinedHostPayload -Reference $staleHostPayloadReference -Repository $repositoryRoot -Worktree $worktreeRoot } 'host payload with a mismatched prepared binary SHA is rejected before stage or arm'
    $hostPayloadVerdict.configuration.vmOnlyProfile='ON'
    [IO.File]::WriteAllText($hostPayloadVerdictPath,($hostPayloadVerdict | ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
    $vmProfileOnReference = [pscustomobject][ordered]@{verdictPath=$hostPayloadVerdictPath;verdictSha256=(Get-CombinedHostSha256 $hostPayloadVerdictPath);payloadPath=$hostPayloadImagePath;payloadSha256=$hostPayloadImageSha;buildId=$hostPayloadBuildId;sourceSetSha256=$hostPayloadSourceSetSha}
    Assert-Throws { Assert-CombinedHostPayload -Reference $vmProfileOnReference -Repository $repositoryRoot -Worktree $worktreeRoot } 'VM-only SMBIOS profile is rejected for the host-specific payload'
Assert-Throws { Resolve-CombinedPinnedSource -Repository $repositoryRoot -Worktree $worktreeRoot -Path '../outside.ps1' } 'source pin path traversal is rejected'
$nicBundleEvidence = Import-CombinedNicHelperBundle -RepositoryRoot $repositoryRoot
$missingNicHelpers = @($script:CombinedNicHelperNames | Where-Object { $null -eq (Get-Command $_ -ErrorAction SilentlyContinue) })
Assert-Test ($nicBundleEvidence.functionCount -eq 11 -and $missingNicHelpers.Count -eq 0) 'tested NIC helper bundle is source-parity pinned and its functions remain callable after module import returns'

[void][IO.Directory]::CreateDirectory($fixtureRoot)
$vehicleBuildRoot = $null
$hostPayloadFixtureRoot = $null
try {
    $sourceFixture = Join-Path $fixtureRoot 'source-manifest-inputs'
    $sourceRootOne = Join-Path $sourceFixture 'components\uefi-loader'
    $sourceRootTwo = Join-Path $sourceFixture 'tools\efi_inventory'
    foreach ($directory in @($sourceRootOne,$sourceRootTwo,(Join-Path $sourceRootOne 'build'),(Join-Path $sourceRootTwo 'private'))) { [void][IO.Directory]::CreateDirectory($directory) }
    [IO.File]::WriteAllText((Join-Path $sourceRootOne 'boot.c'),'int boot(void) { return 0; }',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootOne 'CMakeLists.txt'),'add_library(boot boot.c)',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'reader.cpp'),'int reader(void) { return 1; }',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootOne 'build\generated.c'),'generated',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'private\signing-key.c'),'secret',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'notes.md'),'not a native build input',[Text.UTF8Encoding]::new($false))
    $fixtureRootRelative = (ConvertTo-CombinedRelativePath $sourceFixture.Substring(([IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\') + '\').Length))
    $sourceRoots = @($fixtureRootRelative)
    $sourceManifestPath = Join-Path $fixtureRoot 'source-tree-manifest.json'
    $sourceManifest = Write-CombinedPathBSourceManifest -Repository $repositoryRoot -OutputPath $sourceManifestPath -SourceRoots $sourceRoots
    Assert-Test ($sourceManifest.fileCount -eq 3) 'Path-B source manifest includes native source and CMake inputs but excludes generated, private, and unrelated files'
    $sourceManifestCheck = Assert-CombinedPathBSourceManifest -Repository $repositoryRoot -Path $sourceManifestPath -ExpectedFileSha256 $sourceManifest.sha256 -SourceRoots $sourceRoots
    Assert-Test ($sourceManifestCheck.filesSha256 -ceq $sourceManifest.filesSha256) 'Path-B source manifest recomputes the exact sorted source set and member hashes'
    [IO.File]::WriteAllText((Join-Path $sourceRootTwo 'added.hpp'),'struct Added {};',[Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-CombinedPathBSourceManifest -Repository $repositoryRoot -Path $sourceManifestPath -ExpectedFileSha256 $sourceManifest.sha256 -SourceRoots $sourceRoots } 'Path-B source additions after manifest creation are rejected'

    $vehicleRunId='orchestration-test.'+[Guid]::NewGuid().ToString('N')
    $vehicleBuildRoot=Join-Path $repositoryRoot ('spoof-handoff\artifacts\combined-host\'+$vehicleRunId)
    $vehicleLoaderBuild=Join-Path $vehicleBuildRoot 'loader'
    $vehicleSourceRoot=Join-Path $vehicleBuildRoot 'vm-pinned-source'
    foreach ($directory in @($vehicleLoaderBuild,$vehicleSourceRoot)) { [void][IO.Directory]::CreateDirectory($directory) }
    $vehicleRecords=[Collections.Generic.List[object]]::new()
    foreach ($name in @('inventory_loader.efi','ib-install.exe')) {
        $source=Join-Path $vehicleSourceRoot $name
        $destination=Join-Path $vehicleLoaderBuild $name
        [IO.File]::WriteAllText($source,('vm-tested-'+$name),[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($destination,('native-rebuild-'+$name),[Text.UTF8Encoding]::new($false))
        $vehicleRecords.Add([pscustomobject]@{name=$name;path=$source;sha256=(Get-CombinedHostSha256 $source)})
    }
    $vehicleJournal=[pscustomobject]@{runDirectory=(Join-Path $reportRoot $vehicleRunId);vmGate=[pscustomobject]@{pathBArtifacts=@($vehicleRecords.ToArray())}}
    $vehicleInstall=Install-CombinedVmPinnedPathBArtifacts -Journal $vehicleJournal -Repository $repositoryRoot -BuildRoot $vehicleBuildRoot
    $vehicleInstallPassed=$vehicleInstall.Count -eq 2
    foreach ($record in $vehicleInstall) {
        $sourceRecord=@($vehicleRecords.ToArray() | Where-Object { $_.name -ceq $record.name })
        if ($sourceRecord.Count -ne 1 -or $record.installedSha256 -cne $sourceRecord[0].sha256 -or (Get-CombinedHostSha256 $record.destination) -cne $sourceRecord[0].sha256 -or -not $record.nativeBuildBackup -or (Get-CombinedHostSha256 $record.nativeBuildBackup) -cne $record.nativeBuildSha256) { $vehicleInstallPassed=$false }
    }
    Assert-Test $vehicleInstallPassed 'host build selects hash-pinned VM-tested loader and installer bytes while preserving the native rebuild outputs'
    $tamperedVehicleJournal=[pscustomobject]@{runDirectory=$vehicleJournal.runDirectory;vmGate=[pscustomobject]@{pathBArtifacts=@([pscustomobject]@{name='inventory_loader.efi';path=$vehicleRecords[0].path;sha256=('0'*64)},$vehicleRecords[1])}}
    Assert-Throws { Install-CombinedVmPinnedPathBArtifacts -Journal $tamperedVehicleJournal -Repository $repositoryRoot -BuildRoot $vehicleBuildRoot } 'VM-tested vehicle artifact drift is rejected before copying'

    $journalFixtureDirectory = Join-Path $fixtureRoot 'journal-roundtrip'
    [void][IO.Directory]::CreateDirectory($journalFixtureDirectory)
    $journalFixturePath = Join-Path $journalFixtureDirectory 'journal.json'
    $journalFixture = [ordered]@{
        schema='combined-host-preparation.v1'
        runDirectory=$journalFixtureDirectory
        journalPath=$journalFixturePath
        updatedUtc=[DateTime]::UtcNow.ToString('o')
        phase='Prepared'
        operationInProgress=$false
        hostMutationPerformed=$false
        activationAvailable=$false
        nicTransaction=$null
    }
    Write-CombinedHostAtomicJson -Path $journalFixturePath -Value $journalFixture
    $loadedJournal = Get-CombinedHostJournal -Repository $repositoryRoot -RunDirectory $journalFixtureDirectory
    Assert-Test ($loadedJournal.journal -is [Collections.IDictionary] -and [IO.Path]::GetFullPath($loadedJournal.journal.journalPath) -ceq [IO.Path]::GetFullPath($journalFixturePath)) 'prepared JSON journal reloads as a mutable dictionary with its authoritative path'
    $persistedLifecycleProperties = @('lastStorageAction','secureBootOffObservation','policyMutationBootIdentity','replacementVerifiedBootIdentity','armBootIdentity','pathBBuild','pathBBackupPath','pathBStageEvidence','pathBArmEvidence','armedUtc','collectBootIdentity','pathBSmbiosReport','smbiosBootProof','bootIdentityAfterArm','storageReplacementVerification','pathBCollectEvidence','pathBStageRestore','nicReadback','finalStorageVerification','finalReadback','finalBcdSnapshot','finalEspSnapshot','finalFirmwareUnchanged','finalEspComparison','storagePhase','lastRecoveryError','recoveryUpdatedUtc','pinnedVehicleArtifacts')
    foreach ($propertyName in $persistedLifecycleProperties) { $loadedJournal.journal.$propertyName = ('persisted:' + $propertyName) }
    $loadedJournal.journal.nicTransaction = [ordered]@{path=(Join-Path $journalFixtureDirectory 'nic-transaction.json');mutationIntentRecorded=$true;restorationComplete=$false}
    $loadedJournal.journal.nicTransaction.restorationComplete = $true
    $loadedJournal.journal.nicTransaction.sha256 = ('A' * 64)
    $loadedJournal.journal.phase = 'NicMutationIntent'
    $loadedJournal.journal.operationInProgress = $true
    $loadedJournal.journal.hostMutationPerformed = $true
    $null = Save-CombinedHostJournal $loadedJournal.journal
    $reloadedJournal = Get-CombinedHostJournal -Repository $repositoryRoot -RunDirectory $journalFixtureDirectory
    $lifecyclePropertiesPersisted = $true
    foreach ($propertyName in $persistedLifecycleProperties) {
        if ([string](Get-CombinedValue $reloadedJournal.journal $propertyName) -cne ('persisted:' + $propertyName)) { $lifecyclePropertiesPersisted=$false; break }
    }
    Assert-Test ($lifecyclePropertiesPersisted -and [bool]$reloadedJournal.journal.operationInProgress -and [string]$reloadedJournal.journal.phase -ceq 'NicMutationIntent' -and [bool]$reloadedJournal.journal.nicTransaction.restorationComplete -and [string]$reloadedJournal.journal.nicTransaction.sha256 -ceq ('A' * 64)) 'dynamic lifecycle and nested NIC recovery writes persist across JSON reload before mutation resumes'
    $mockMutation = { param([bool]$IntentPersisted) [pscustomobject]@{called=$true;intentPersisted=$IntentPersisted} }
    $mockMutationResult = & $mockMutation ([bool]$reloadedJournal.journal.operationInProgress -and [string]$reloadedJournal.journal.phase -ceq 'NicMutationIntent')
    Assert-Test ($mockMutationResult.called -and $mockMutationResult.intentPersisted) 'mock lifecycle mutation dispatch occurs only after the persisted intent is reloaded'

    $vmMapPath = Join-Path $fixtureRoot 'vm-fixture-map.tsv'
    $vmRows = @(New-TestVmFixtureRows $rows)
    Write-TestVmMap -Rows $vmRows -Path $vmMapPath
    $vmMapReference = [pscustomobject]@{path=$vmMapPath;sha256=(Get-CombinedHostSha256 $vmMapPath)}
    $vmFixture = Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Reference $vmMapReference -HostBindingMap $bindingMap
    Assert-Test ($vmFixture.bindingMap.bindings.Count -eq 12) 'VM fixture map with distinct originals and PnP identities resolves to 12 bindings'
    $vmSerial = @($vmFixture.bindingMap.bindings | Where-Object { $_.id -ceq 'SMBIOS:system_serial' })[0]
    $hostSerial = @($bindingMap.bindings | Where-Object { $_.id -ceq 'SMBIOS:system_serial' })[0]
    $vmDisk = @($vmFixture.bindingMap.bindings | Where-Object { $_.category -ceq 'DISK' })[0]
    $hostDisk = @($bindingMap.bindings | Where-Object { $_.category -ceq 'DISK' })[0]
    $hostPnps = @($bindingMap.bindings | Where-Object { $_.category -ceq 'NIC' } | ForEach-Object { $_.pnpDeviceId } | Sort-Object)
    $vmPnps = @($vmFixture.bindingMap.bindings | Where-Object { $_.category -ceq 'NIC' } | ForEach-Object { $_.pnpDeviceId } | Sort-Object)
    Assert-Test ($vmSerial.original -cne $hostSerial.original -and $vmDisk.original -cne $hostDisk.original -and ($vmPnps -join ',') -cne ($hostPnps -join ',')) 'VM originals and PnP identities are distinct from the physical-host map'
    $vmOriginalReadback = New-TestReadback $vmFixture.bindingMap 'Original' $vmFixture.path $vmFixture.sha256
    $vmActiveReadback = New-TestReadback $vmFixture.bindingMap 'Replacement' $vmFixture.path $vmFixture.sha256
    Assert-Test (Assert-CombinedReadbackMapReference -Readback $vmOriginalReadback -MapReference $vmFixture) 'VM-phase readback pins the exact VM fixture map hash'
    Assert-Test (Assert-CombinedReadbackPhase -BindingMap $vmFixture.bindingMap -Readback $vmOriginalReadback -Phase original) 'gate original phase accepts VM-captured originals and PnP identifiers'
    Assert-Test (Assert-CombinedReadbackPhase -BindingMap $vmFixture.bindingMap -Readback $vmActiveReadback -Phase active) 'gate active phase verifies exact locked replacements against VM NIC identities'
    Assert-Throws { Assert-CombinedReadbackMapReference -Readback $vmOriginalReadback -MapReference ([pscustomobject]@{path=$mapPath;sha256=(Get-CombinedHostSha256 $mapPath)}) } 'VM phase is rejected when its readback names a different map hash'
    $alteredVmRows = @(New-TestVmFixtureRows $rows)
    ($alteredVmRows | Where-Object { $_.member -ceq 'FakeSMBIOSSerial' })[0].replacement = 'UNLOCKED-REPLACEMENT'
    $alteredVmMapPath = Join-Path $fixtureRoot 'vm-fixture-map-altered.tsv'
    Write-TestVmMap -Rows $alteredVmRows -Path $alteredVmMapPath
    $alteredVmReference = [pscustomobject]@{path=$alteredVmMapPath;sha256=(Get-CombinedHostSha256 $alteredVmMapPath)}
    Assert-Throws { Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Reference $alteredVmReference -HostBindingMap $bindingMap } 'VM fixture map with an altered locked replacement is rejected'

    $swappedVmRows = @(New-TestVmFixtureRows $rows)
    $firstVirtual = @($swappedVmRows | Where-Object { $_.member -ceq 'FakeMacAddress' -and (ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $_.original) -ceq '005056C00008' })
    $secondVirtual = @($swappedVmRows | Where-Object { $_.member -ceq 'FakeMacAddress' -and (ConvertTo-CombinedComparableValue -Category 'NIC' -Field 'mac' -Value $_.original) -ceq '005056C00001' })
    if ($firstVirtual.Count -ne 1 -or $secondVirtual.Count -ne 1) { throw 'Could not locate the two canonical VMnet adapter rows in the unit fixture.' }
    $temporaryReplacement = [string]$firstVirtual[0].replacement
    $firstVirtual[0].replacement = [string]$secondVirtual[0].replacement
    $secondVirtual[0].replacement = $temporaryReplacement
    $swappedVmMapPath = Join-Path $fixtureRoot 'vm-fixture-map-swapped-vmnet-values.tsv'
    Write-TestVmMap -Rows $swappedVmRows -Path $swappedVmMapPath
    $swappedVmReference = [pscustomobject]@{path=$swappedVmMapPath;sha256=(Get-CombinedHostSha256 $swappedVmMapPath)}
    Assert-Throws { Get-CombinedVmFixtureBindingMap -Repository $repositoryRoot -Reference $swappedVmReference -HostBindingMap $bindingMap } 'VM fixture map with the two virtual adapter replacement values swapped is rejected'

    $gatePath = Join-Path $fixtureRoot 'pass-boolean-only.json'
    [IO.File]::WriteAllText($gatePath,('{"schema":"combined-vm-gate.v1","status":"PASS"}'),[Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-CombinedVmGateEvidence -Repository $repositoryRoot -Worktree $worktreeRoot -GatePath $gatePath } 'bare PASS status without pinned source/evidence is rejected before any host preparation'

    $preparedRun = Join-Path $fixtureRoot 'prepared-run'
    [void][IO.Directory]::CreateDirectory($preparedRun)
    $preparedJournal = [ordered]@{schema='combined-host-preparation.v1';runDirectory=$preparedRun;status='PREPARED_READONLY_VM_GATE_REQUIRED';phase='Prepared';hostMutationPerformed=$false;activationAvailable=$false;blockedBoundary='VM gate required';operationInProgress=$false}
    [IO.File]::WriteAllText((Join-Path $preparedRun 'journal.json'),($preparedJournal | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
    $simulatedMain = Join-Path $fixtureRoot 'wrapper-root'
    $simulatedWorktree = Join-Path $simulatedMain '.codex-build\worktrees\smbios-boot-serial-probe'
    $mainCommands = Join-Path $simulatedMain 'spoof-handoff\commands'
    $worktreeCommands = Join-Path $simulatedWorktree 'spoof-handoff\commands'
    $mainImplementation = Join-Path $simulatedWorktree 'spoof-handoff\tools\combined_host'
    [void][IO.Directory]::CreateDirectory($mainCommands)
    [void][IO.Directory]::CreateDirectory($worktreeCommands)
    [void][IO.Directory]::CreateDirectory($mainImplementation)
    $testWrapperSource = Join-Path $PSScriptRoot '..\..\commands\combined-host-test.ps1'
    $rollbackWrapperSource = Join-Path $PSScriptRoot '..\..\commands\combined-host-rollback.ps1'
    Copy-Item -LiteralPath $testWrapperSource -Destination (Join-Path $mainCommands 'combined-host-test.ps1')
    Copy-Item -LiteralPath $testWrapperSource -Destination (Join-Path $worktreeCommands 'combined-host-test.ps1')
    Copy-Item -LiteralPath $rollbackWrapperSource -Destination (Join-Path $mainCommands 'combined-host-rollback.ps1')
    Copy-Item -LiteralPath $rollbackWrapperSource -Destination (Join-Path $worktreeCommands 'combined-host-rollback.ps1')
    $dummyRun = "param([string]`$Action,[string]`$RepositoryRoot,[string]`$WorktreeRoot,[string]`$RunDirectory) [pscustomobject]@{status='ROOTS';Action=`$Action;RepositoryRoot=`$RepositoryRoot;WorktreeRoot=`$WorktreeRoot;RunDirectory=`$RunDirectory} | ConvertTo-Json -Compress"
    $dummyRecovery = "param([string]`$RepositoryRoot,[string]`$WorktreeRoot,[string]`$RunDirectory) [pscustomobject]@{status='ROOTS';RepositoryRoot=`$RepositoryRoot;WorktreeRoot=`$WorktreeRoot;RunDirectory=`$RunDirectory} | ConvertTo-Json -Compress"
    [IO.File]::WriteAllText((Join-Path $mainImplementation 'run.ps1'),$dummyRun,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $mainImplementation 'recovery.ps1'),$dummyRecovery,[Text.UTF8Encoding]::new($false))
    $mainStatus = ((@(& (Join-Path $mainCommands 'combined-host-test.ps1') -Action Status -RunDirectory $preparedRun) -join "`n") | ConvertFrom-Json -ErrorAction Stop)
    $worktreeStatus = ((@(& (Join-Path $worktreeCommands 'combined-host-test.ps1') -Action Status -RunDirectory $preparedRun) -join "`n") | ConvertFrom-Json -ErrorAction Stop)
    Assert-Test ([IO.Path]::GetFullPath($mainStatus.RepositoryRoot) -ceq [IO.Path]::GetFullPath($simulatedMain) -and [IO.Path]::GetFullPath($mainStatus.WorktreeRoot) -ceq [IO.Path]::GetFullPath($simulatedWorktree)) 'main-checkout command wrapper resolves repository and worktree roots'
    Assert-Test ([IO.Path]::GetFullPath($worktreeStatus.RepositoryRoot) -ceq [IO.Path]::GetFullPath($simulatedMain) -and [IO.Path]::GetFullPath($worktreeStatus.WorktreeRoot) -ceq [IO.Path]::GetFullPath($simulatedWorktree)) 'worktree command wrapper resolves path segments without a backslash regex'
    $mainRollback = ((@(& (Join-Path $mainCommands 'combined-host-rollback.ps1') -RunDirectory $preparedRun) -join "`n") | ConvertFrom-Json -ErrorAction Stop)
    $worktreeRollback = ((@(& (Join-Path $worktreeCommands 'combined-host-rollback.ps1') -RunDirectory $preparedRun) -join "`n") | ConvertFrom-Json -ErrorAction Stop)
    Assert-Test ([IO.Path]::GetFullPath($mainRollback.RepositoryRoot) -ceq [IO.Path]::GetFullPath($simulatedMain) -and [IO.Path]::GetFullPath($mainRollback.WorktreeRoot) -ceq [IO.Path]::GetFullPath($simulatedWorktree)) 'main-checkout rollback wrapper resolves recovery roots'
    Assert-Test ([IO.Path]::GetFullPath($worktreeRollback.RepositoryRoot) -ceq [IO.Path]::GetFullPath($simulatedMain) -and [IO.Path]::GetFullPath($worktreeRollback.WorktreeRoot) -ceq [IO.Path]::GetFullPath($simulatedWorktree)) 'worktree rollback wrapper resolves recovery roots'
    $recoveryScript = Join-Path $PSScriptRoot 'recovery.ps1'
    $recoveryOutput = @(& $recoveryScript -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -RunDirectory $preparedRun)
    $recoveryResult = ($recoveryOutput -join "`n") | ConvertFrom-Json -ErrorAction Stop
    Assert-Test ($recoveryResult.status -ceq 'NO_MUTATION_TO_ROLL_BACK') 'rollback explicitly confirms a read-only preparation has no system changes to restore'

    $mutatedRun = Join-Path $fixtureRoot 'mutated-run'
    [void][IO.Directory]::CreateDirectory($mutatedRun)
    $mutatedJournal = [ordered]@{schema='combined-host-preparation.v1';runDirectory=$mutatedRun;journalPath=(Join-Path $mutatedRun 'journal.json');phase='NicMutationIntent';operationInProgress=$true;hostMutationPerformed=$true;activationAvailable=$true;storageSnapshot=$null}
    [IO.File]::WriteAllText((Join-Path $mutatedRun 'journal.json'),($mutatedJournal | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
    $mutationFailure = $null
    try { & $recoveryScript -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -RunDirectory $mutatedRun | Out-Null } catch { $mutationFailure = $_.Exception.Message }
    Assert-Test ($mutationFailure -match 'cannot roll back an armed or mutated combined run without its verified external storage recovery snapshot') 'rollback fails closed for any journal that reports host mutation without its recovery snapshot'
}
finally {
    if (Test-Path -LiteralPath $fixtureRoot -PathType Container) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
    if ($vehicleBuildRoot -and (Test-Path -LiteralPath $vehicleBuildRoot -PathType Container)) { Remove-Item -LiteralPath $vehicleBuildRoot -Recurse -Force }
    if ($hostPayloadFixtureRoot -and (Test-Path -LiteralPath $hostPayloadFixtureRoot -PathType Container)) { Remove-Item -LiteralPath $hostPayloadFixtureRoot -Recurse -Force }
}

[pscustomobject]@{schema='combined-host-orchestration-tests.v1';status='PASS';assertions=$script:passed;readbackBindings=$bindingMap.bindings.Count;vmGateAccepted=$false;hostMutationPerformed=$false;fixtureRootRemoved=(-not (Test-Path -LiteralPath $fixtureRoot))} | ConvertTo-Json -Depth 8
