[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuStageRetirement.psm1'
$scriptPath = Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $sourceRoot '.codex-build'))
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $fixtureParent ('gpu-stage-retirement-042142-tests-' + [guid]::NewGuid().ToString('N'))))
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-Throws {
    param([scriptblock]$Operation, [string]$MessagePattern, [string]$Message)
    $observed = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message }
    Assert-True ($null -ne $observed -and $observed -match $MessagePattern) "$Message (observed: $observed)"
}

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try { & $Body; $script:PassedCases++; Write-Output "PASS: $Name" }
    catch { $script:FailedCases.Add("$Name`: $($_.Exception.Message)"); Write-Output "FAIL: $Name`: $($_.Exception.Message)" }
}

function Import-TestModule {
    return Import-Module -Name $modulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1
}

function Write-TestJson {
    param([string]$Path, [object]$Value)
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 32 -Compress) + "`n", [Text.UTF8Encoding]::new($false))
}

function Get-TestPin {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '')
        $bytes = [long]$stream.Length
    } finally { $algorithm.Dispose(); $stream.Dispose() }
    return [pscustomobject]@{ Path=[IO.Path]::GetFullPath($Path); Sha256=$hash; Bytes=$bytes }
}

function Get-ProfileState {
    param([object]$Module)
    return & $Module {
        [pscustomobject]@{
            RunId=$script:GpuRetirement042142RunId
            SourceCommit=$script:GpuRetirement042142SourceCommit
            StagePins=@($script:GpuRetirement042142StagePins)
            EspVolume=$script:GpuRetirement042142EspVolume
            EspDiskNumber=$script:GpuRetirement042142EspDiskNumber
            EspPartitionGuid=$script:GpuRetirement042142EspPartitionGuid
            BootManagerSha256=$script:GpuRetirement042142BootManagerSha256
            FirmwareSha256=$script:GpuRetirement042142OriginalFirmwareSha256
            ReviewedFirmwareSha256=$script:GpuRetirement042142ReviewedFirmwareSha256
            CollectionActionRelativePath=$script:GpuRetirement042142CollectionActionRelativePath
            FinalActionRelativePath=$script:GpuRetirement042142FinalActionRelativePath
            DisarmedBootId='2026-10-21T04:30:43.5000000Z'
        }
    }
}

function New-ReadinessFixture {
    param([string]$WorkspaceRoot, [object[]]$StagePins, [string]$Guid = ([guid]::NewGuid().ToString('N')))
    $directory = Join-Path $WorkspaceRoot ('.analysis\gpu-baremetal-readiness\' + $Guid)
    $path = Join-Path $directory 'readiness.json'
    $entries = @($StagePins | ForEach-Object {
        [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
    })
    $stateModule = Import-TestModule
    $state = Get-ProfileState $stateModule
    $blocker = 'EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.'
    $report = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'; Succeeded=$false
        EvidencePath=$path; EvidenceDirectory=$directory
        Source=[pscustomobject]@{
            Branch='feat/smbios-boot-serial-probe'; Commit=$state.SourceCommit
            Dirty=$false; DirtyEntryCount=0
        }
        Preflight=[pscustomobject]@{
            Complete=$true; InventoryComplete=$true; IntelPresent=$true; NvidiaPresent=$true; Error=$null
        }
        Blockers=@($blocker); Errors=@()
        Host=[pscustomobject]@{ LogicalCpus=20; SecureBoot=$true }
        Recovery=[pscustomobject]@{ Valid=$true; Disk=[pscustomobject]@{ IsVirtual=$false } }
        Selector=[pscustomobject]@{ Exists=$false; Valid=$true; Error=$null }
        Cleanup=[pscustomobject]@{ Succeeded=$true; Errors=@() }
        Esp=[pscustomobject]@{
            Inspected=$true; MountStateAfterCleanup='Unmounted'
            Identity=[pscustomobject]@{
                Volume=$state.EspVolume; DiskNumber=$state.EspDiskNumber
                PartitionGuid=$state.EspPartitionGuid; DiskUniqueId='eui.0000000001000000E4D25CDF32275501'
            }
            BootManagerPin=[pscustomobject]@{ Sha256=$state.BootManagerSha256; Bytes=3086848L }
        }
        Effects=[pscustomobject]@{ EspFilesChanged=$false; FirmwareChanged=$false; BootStateChanged=$false; EspTemporarilyMounted=$true }
        ExistingStage=[pscustomobject]@{
            Exists=$true; Complete=$true; EntryCount=5; FileCount=5; Entries=$entries
            UnknownEntries=@(); UnsafeEntries=@(); Errors=@()
        }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
    }
    Write-TestJson -Path $path -Value $report
    return @{ Path=$path; Directory=$directory; Report=$report; Guid=$Guid }
}

function New-EvidenceFixture {
    param([string]$WorkspaceRoot, [object]$State)
    $runDirectory = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\runs') $State.RunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $finalAction = Join-Path $WorkspaceRoot $State.FinalActionRelativePath
    $sourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $stageEntries = @($State.StagePins | ForEach-Object {
        [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
    })
    $recordedStage = @($State.StagePins | ForEach-Object {
        [pscustomobject]@{ Name=$_.Name; RelativePath=('EFI\insecure-boot\' + $_.Name); Sha256=$_.Sha256; Bytes=$_.Bytes }
    })
    $collectionRows = @($State.StagePins | ForEach-Object {
        $path = Join-Path (Join-Path $WorkspaceRoot $State.CollectionActionRelativePath) ('esp-' + $_.Name)
        [pscustomobject]@{ Name=$_.Name; Present=$true; Path=$path; Pin=[pscustomobject]@{ Path=$path; Sha256=$_.Sha256; Bytes=$_.Bytes } }
    })
    $collectionRows += @(
        [pscustomobject]@{ Name='handoff-status.txt'; Present=$false; Path=$null; Pin=$null },
        [pscustomobject]@{ Name='payload-diagnostic.txt'; Present=$false; Path=$null; Pin=$null }
    )
    $audit = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'
        Esp=[pscustomobject]@{
            Identity=[pscustomobject]@{ Volume=$State.EspVolume; DiskNumber=$State.EspDiskNumber; PartitionGuid=$State.EspPartitionGuid }
            BootManagerPin=[pscustomobject]@{ Sha256=$State.BootManagerSha256; Bytes=3086848L }
            MountStateAfterCleanup='Unmounted'
        }
        ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true; EntryCount=5; FileCount=5; Entries=$stageEntries; UnknownEntries=@(); UnsafeEntries=@(); Errors=@() }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
    }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$State.RunId; SourceCommit=$State.SourceCommit; SourceRoot=$WorkspaceRoot
        RunDirectory=$runDirectory; Phase='Complete'; InProgress=$null; DisarmedBootId='fixture-disarm-boot'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status='FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'; Directory=$finalAction }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; ReviewedDifferenceAccepted=$true
                BaselineSha256=$State.FirmwareSha256; CurrentSha256=$State.ReviewedFirmwareSha256
            }
            Errors=@()
        }
        Backup=[pscustomobject]@{
            EspVolume=$State.EspVolume; EspDiskNumber=$State.EspDiskNumber
            EspPartitionGuid=$State.EspPartitionGuid; BootManagerSha256=$State.BootManagerSha256
        }
        Stage=[pscustomobject]@{ Files=$recordedStage }
        Collection=[pscustomobject]@{ Esp=[pscustomobject]@{ StageFiles=$collectionRows } }
    }
    $archive = [pscustomobject]@{
        SchemaVersion=1; RunId=$State.RunId; RunDirectory=$runDirectory
        JournalPath=$journalPath; SourceRoot=$WorkspaceRoot
    }
    return @{ Audit=$audit; Journal=$journal; Archive=$archive; RunDirectory=$runDirectory; JournalPath=$journalPath }
}

function New-BootSelectionFixture {
    param([object[]]$Order = @(0,1,2,3,4,5), [object]$BootNext = $null, [string]$Findings = '00000000')
    $module = Import-TestModule
    $state = Get-ProfileState $module
    return [pscustomobject]@{ Baseline=[pscustomobject]@{
        Schema=1; BootNext=$BootNext; BootOrder=[object[]]$Order
        BootManagerSha256=$state.BootManagerSha256
        ReferenceLogSha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        IntegrityFindings=$Findings
    } }
}

function New-ProofFixture {
    param([string]$WorkspaceRoot, [object]$State, [string]$Failure = '')
    $runDirectory = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\runs') $State.RunId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $finalAction = Join-Path $WorkspaceRoot $State.FinalActionRelativePath
    $sourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $verificationErrors = if ($Failure -ceq 'errors') { @('injected fixture failure') } else { @() }
    $status = if ($Failure -ceq 'status') { 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' } else { 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' }
    $action = if ($Failure -ceq 'action') { 'Collect' } else { 'FinalVerify' }
    $sourceCommit = if ($Failure -ceq 'source') { '0000000000000000000000000000000000000000' } else { $State.SourceCommit }
    $review = [pscustomobject]@{
        Accepted=$true; HistoricalRawBootOrderUnchangedProven=$false; FullBootVariableNamespaceAttested=$false
        OriginalBaselinePin=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\original-firmware.txt'); Sha256=$State.FirmwareSha256; Bytes=2890L }
        ReviewedFirmwarePin=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\reviewed-firmware.txt'); Sha256=$State.ReviewedFirmwareSha256; Bytes=4336L }
        FreshFirmwarePin=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\fresh-firmware.txt'); Sha256=$State.ReviewedFirmwareSha256; Bytes=4336L }
        ReceiptPin=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\review.json'); Sha256=('A'*64); Bytes=1L }
    }
    $journal = [pscustomobject]@{
        RunId=$State.RunId; RunDirectory=$runDirectory; SourceRoot=$sourceRoot; SourceCommit=$sourceCommit
        Phase='Complete'; InProgress=$null; DisarmedBootId='2026-10-21T04:30:43.5000000Z'
        LastAction=[pscustomobject]@{ Action=$action; Succeeded=$true; Status=$status; Directory=$finalAction }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        Backup=[pscustomobject]@{
            FirmwareEnumeration=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\original-firmware.txt'); Sha256=$State.FirmwareSha256; Bytes=2890L }
            ReferenceLog=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\reference.tcglog'); Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838L }
        }
        Tools=[pscustomobject]@{ Probe=[pscustomobject]@{ Path=(Join-Path $WorkspaceRoot 'fixture\probe.exe'); Sha256='86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36'; Bytes=172544L } }
        FinalVerification=[pscustomobject]@{
            BootId='2026-10-21T04:46:50.5000000Z'; ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
            ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=$verificationErrors
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20; CpuPassCount=0 }
            Esp=[pscustomobject]@{
                MatchesBaseline=$true; Volume=$State.EspVolume; DiskNumber=$State.EspDiskNumber
                PartitionGuid=$State.EspPartitionGuid; BootManagerSha256=$State.BootManagerSha256
            }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; ReviewedDifferenceAccepted=$true; BaselineSha256=$State.FirmwareSha256
                CurrentSha256=$State.ReviewedFirmwareSha256; CaptureContext='SystemEspMountedAtS'; ReviewedDifference=$review
            }
        }
    }
    $archive = [pscustomobject]@{
        SchemaVersion=1; RunId=$State.RunId; RunDirectory=$runDirectory; JournalPath=$journalPath
        SourceRoot=$sourceRoot
    }
    Write-TestJson -Path $journalPath -Value $journal
    $archivePath = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\archive') ($State.RunId + '.json')
    Write-TestJson -Path $archivePath -Value $archive
    return @{ Journal=$journal; JournalPath=$journalPath; ArchivePath=$archivePath; Archive=$archive; RunDirectory=$runDirectory }
}

try {
    if (-not [string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot)), $fixtureParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test fixture escaped its dedicated worktree .codex-build parent.'
    }
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $workspaceRoot = Join-Path $fixtureRoot 'workspace'
    [void][IO.Directory]::CreateDirectory($workspaceRoot)

    Invoke-TestCase '042142 wrapper preview is side-effect free and requires its explicit profile path' {
        $wrapperToolRoot = Join-Path $workspaceRoot 'tools\gpu_diagnostic'
        $before = @(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        $preview = (& $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun042142 -ReadinessPath (Join-Path $workspaceRoot 'readiness.json') -WhatIf | Out-String) | ConvertFrom-Json
        $after = @(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        Assert-True ($preview.Preview -and -not $preview.FilesystemChangesPerformed -and -not $preview.NativeCommandsExecuted -and -not $preview.EspMounted -and $preview.CompletedRun042142 -and $before -eq $after) 'The 042142 WhatIf preview performed effects or omitted the selected profile.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun042142 -WhatIf } 'explicit EvidencePath' 'The 042142 wrapper accepted a missing fresh readiness path.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun042142 -CompletedRun032117 -ReadinessPath 'fixture.json' -WhatIf } 'mutually exclusive' 'The wrapper accepted two completed-run profiles.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -ReadinessPath 'fixture.json' -WhatIf } 'only with an explicit completed-run profile' 'The wrapper accepted ReadinessPath without a fixed profile.'
    }

    Invoke-TestCase '042142 profile pins the fixed five stage files and selected readiness evidence' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $expected = @{
            'grubx64.efi'='B7B0176261592F97E3E78EA97C6AB1DEA411ECCA67AE19BF294E2A931183C455'
            'manifest.sha256'='9BBCFDE76392D55E0AE527C892F68BBA0B74D27CDC8A5A59D1D7B178C80B19B7'
            'payload.efi'='B5F277FC7F05ABE03AAE24DE8ADF4DE26B22E1FB3872743B4BB3A9A67305CF7F'
            'shimx64.efi'='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'
            'tcglog.ib'='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
        }
        Assert-True ($state.StagePins.Count -eq 5 -and @($state.StagePins.Name | Sort-Object -Unique).Count -eq 5) 'The profile does not contain exactly five unique stage names.'
        foreach ($pin in $state.StagePins) { Assert-True ($expected.ContainsKey($pin.Name) -and $pin.Sha256 -ceq $expected[$pin.Name]) "Unexpected fixed stage pin: $($pin.Name)" }
        $fixture = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.StagePins
        Assert-True ([IO.Directory]::Exists((Split-Path -Parent $fixture.Path))) 'The private readiness fixture directory was not created.'
        $module = Import-TestModule
        $selection = & $module { param($root,$path) Select-GpuStageRetirement042142Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path
        Assert-True ($selection.Profile -ceq '042142' -and $selection.RunId -ceq $state.RunId -and $selection.SourceCommit -ceq $state.SourceCommit) 'Selection did not bind the closed 042142 run identity.'
        Assert-True ($selection.ReadinessPath -ceq [IO.Path]::GetFullPath($fixture.Path) -and $selection.ReadinessPin.Bytes -gt 0 -and $selection.StagePins.Count -eq 5) 'Selection did not pin the exact readiness report and five stage files.'
    }

    Invoke-TestCase '042142 readiness and stage inventory reject missing, extra, malformed, and changed entries' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $base = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.StagePins
        & $module { param($value,$path) Assert-GpuStageRetirement042142Readiness -Readiness $value -ReadinessFullPath $path } $base.Report $base.Path
        $mutations = @(
            @{ Name='extra retained-stage blocker'; Edit={ param($r) $r.Blockers += 'another readiness blocker' }; Pattern='clean, unmounted BLOCKED_READINESS' },
            @{ Name='dirty source tree'; Edit={ param($r) $r.Source.Dirty=$true }; Pattern='clean, unmounted BLOCKED_READINESS' },
            @{ Name='incomplete host inventory'; Edit={ param($r) $r.Preflight.InventoryComplete=$false }; Pattern='clean, unmounted BLOCKED_READINESS' },
            @{ Name='missing staged file'; Edit={ param($r) $r.ExistingStage.Entries=@($r.ExistingStage.Entries | Select-Object -Skip 1) }; Pattern='five reviewed|exactly the five|missing' },
            @{ Name='extra diagnostic file'; Edit={ param($r) $r.ExistingStage.Entries += [pscustomobject]@{ RelativePath='EFI\insecure-boot\payload-diagnostic.txt'; Kind='File'; Bytes=10; Sha256=('A'*64); Hashed=$true } }; Pattern='five reviewed|exactly the five' },
            @{ Name='wrong file hash'; Edit={ param($r) $r.ExistingStage.Entries[0].Sha256='0'*64 }; Pattern='pin differs' },
            @{ Name='wrong stage path'; Edit={ param($r) $r.ExistingStage.Entries[0].RelativePath='EFI\other\grubx64.efi' }; Pattern='outside the reviewed|pin differs' },
            @{ Name='non-file inventory entry'; Edit={ param($r) $r.ExistingStage.Entries[0].Kind='Directory' }; Pattern='non-file' },
            @{ Name='unhashed inventory entry'; Edit={ param($r) $r.ExistingStage.Entries[0].Hashed=$false }; Pattern='unhashed' }
        )
        foreach ($mutation in $mutations) {
            $fixture = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.StagePins
            & $mutation.Edit $fixture.Report
            Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement042142Readiness -Readiness $value -ReadinessFullPath $path } $fixture.Report $fixture.Path } $mutation.Pattern "Readiness accepted $($mutation.Name)."
        }
        $validInventory = @($state.StagePins | ForEach-Object { [pscustomobject]@{ RelativePath=('EFI\insecure-boot\'+$_.Name); Kind='File'; Hashed=$true; Bytes=$_.Bytes; Sha256=$_.Sha256 } })
        $accepted = & $module { param($entries) Assert-GpuStageRetirement042142StageInventory -Entries $entries -Description 'fixture' } $validInventory
        Assert-True (@($accepted).Count -eq 5) 'Valid fixed inventory did not return all five pins.'
    }

    Invoke-TestCase '042142 generic retirement proof accepts five staged pins and two explicit absent diagnostics' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.StagePins
        $null = & $module { param($root,$path) Select-GpuStageRetirement042142Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $readiness.Path
        $fixture = New-EvidenceFixture -WorkspaceRoot $workspaceRoot -State $state
        $arguments = @{
            Audit=$fixture.Audit; Journal=$fixture.Journal; ArchivedSelector=$fixture.Archive
            ExpectedAuditSha256=('A' * 64); ExpectedRunId=$state.RunId
            ExpectedRunDirectory=$fixture.RunDirectory; ExpectedJournalPath=(Join-Path $fixture.RunDirectory 'journal.json')
            ExpectedSourceRoot=$workspaceRoot; ExpectedWorkspaceRoot=$workspaceRoot
        }
        $stage = & $module {
            param($value)
            function Assert-GpuRetirementProfilePin {
                param($Pin,$ExpectedPath,$ExpectedSha256,$ExpectedBytes,$WorkspaceRoot,$Description)
                if ([IO.Path]::GetFullPath([string]$Pin.Path) -cne [IO.Path]::GetFullPath($ExpectedPath) -or [string]$Pin.Sha256 -cne $ExpectedSha256 -or [long]$Pin.Bytes -ne [long]$ExpectedBytes) { throw "Fixture pin mismatch: $Description" }
                return $Pin
            }
            Assert-GpuStageRetirementEvidence @value
        } $arguments
        Assert-True (@($stage).Count -eq 5 -and @($stage | Where-Object Name -eq 'payload.efi').Count -eq 1) 'The fixed five stage files were not accepted.'

        $bad = New-EvidenceFixture -WorkspaceRoot $workspaceRoot -State $state
        $badArguments = @{} + $arguments; $badArguments.Audit=$bad.Audit; $badArguments.Journal=$bad.Journal; $badArguments.ArchivedSelector=$bad.Archive
        $bad.Journal.Collection.Esp.StageFiles = @($bad.Journal.Collection.Esp.StageFiles | Where-Object Name -ne 'payload-diagnostic.txt')
        Assert-Throws { & $module {
            param($value)
            function Assert-GpuRetirementProfilePin { param($Pin,$ExpectedPath,$ExpectedSha256,$ExpectedBytes,$WorkspaceRoot,$Description) return $Pin }
            Assert-GpuStageRetirementEvidence @value
        } $badArguments } 'exactly seven|exactly seven|two explicit absent' 'Collection passed without the explicit absent diagnostic row.'

        $bad = New-EvidenceFixture -WorkspaceRoot $workspaceRoot -State $state
        $badArguments = @{} + $arguments; $badArguments.Audit=$bad.Audit; $badArguments.Journal=$bad.Journal; $badArguments.ArchivedSelector=$bad.Archive
        $bad.Journal.Collection.Esp.StageFiles[0].Pin.Sha256 = '0' * 64
        Assert-Throws { & $module {
            param($value)
            function Assert-GpuRetirementProfilePin { param($Pin,$ExpectedPath,$ExpectedSha256,$ExpectedBytes,$WorkspaceRoot,$Description) return $Pin }
            Assert-GpuStageRetirementEvidence @value
        } $badArguments } 'differs from the reviewed audit' 'Collection passed with a changed staged-file hash.'
    }

    Invoke-TestCase '042142 live inventory accepts five private fixture files and rejects extras or directories' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.StagePins
        $null = & $module { param($root,$path) Select-GpuStageRetirement042142Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $readiness.Path
        $stageDirectory = Join-Path $fixtureRoot 'inventory-valid'
        [void][IO.Directory]::CreateDirectory($stageDirectory)
        foreach ($pin in $state.StagePins) { [IO.File]::WriteAllText((Join-Path $stageDirectory $pin.Name), "fixture $($pin.Name)", [Text.UTF8Encoding]::new($false)) }
        $inventory = & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory
        Assert-True ($inventory.Count -eq 5 -and @($inventory.Keys).Count -eq 5) 'The five-file private stage inventory was not accepted.'

        [IO.File]::WriteAllText((Join-Path $stageDirectory 'unexpected.bin'), 'fixture', [Text.UTF8Encoding]::new($false))
        Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory } 'exactly five|unknown or duplicate' 'A sixth unknown file was accepted.'
        Remove-Item -LiteralPath (Join-Path $stageDirectory 'unexpected.bin') -Force
        [void][IO.Directory]::CreateDirectory((Join-Path $stageDirectory 'nested'))
        Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory } 'exactly five|directory or reparse' 'A nested directory was accepted.'
    }

    Invoke-TestCase '042142 raw boot-selection requires absent BootNext, exact order, hashes, and zero findings' {
        $module = Import-TestModule
        $good = New-BootSelectionFixture
        Assert-True ($null -ne $good.Baseline.PSObject.Properties['BootNext']) 'The raw capture fixture omitted BootNext.'
        $null = & $module { param($capture) Assert-GpuStageRetirement042142BootSelection -Capture $capture -Checkpoint 'fixture' } $good
        $differentOrder = New-BootSelectionFixture -Order @(0,1,2,3,5,4)
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement042142BootSelection -Capture $capture -Checkpoint 'fixture' } $differentOrder } 'BootOrder differs' 'A changed BootOrder was accepted.'
        $withBootNext = New-BootSelectionFixture -BootNext 4
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement042142BootSelection -Capture $capture -Checkpoint 'fixture' } $withBootNext } 'BootNext|differs from the closed' 'A set BootNext was accepted.'
        $withFindings = New-BootSelectionFixture -Findings '00000001'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement042142BootSelection -Capture $capture -Checkpoint 'fixture' } $withFindings } 'differs|findings' 'A nonzero integrity finding was accepted.'
    }

    Invoke-TestCase '042142 proof rejects a different run and unsuccessful FinalVerify identity' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $different = [pscustomobject]@{ RunId='gpu-foreign-run' }
        Assert-Throws { & $module { param($journal,$root) Assert-GpuStageRetirement042142Proof -Journal $journal -WorkspaceRoot $root } $different $workspaceRoot } 'fixed archived 042142 run' 'Proof accepted a different run id.'
        foreach ($failure in @('action','status','source','errors')) {
            $module = Import-TestModule
            $state = Get-ProfileState $module
            $fixture = New-ProofFixture -WorkspaceRoot $workspaceRoot -State $state -Failure $failure
            $journalPin = Get-TestPin $fixture.JournalPath
            $archivePin = Get-TestPin $fixture.ArchivePath
            & $module {
                param($journalHash,$journalBytes,$archiveHash,$archiveBytes)
                $script:GpuRetirement042142JournalSha256=$journalHash
                $script:GpuRetirement042142JournalBytes=$journalBytes
                $script:GpuRetirement042142ArchiveSha256=$archiveHash
                $script:GpuRetirement042142ArchiveBytes=$archiveBytes
            } $journalPin.Sha256 $journalPin.Bytes $archivePin.Sha256 $archivePin.Bytes
            $pattern = if ($failure -ceq 'errors') { 'does not satisfy the reset' } else { 'lacks its exact archived run identity or successful reviewed FinalVerify completion' }
            Assert-Throws { & $module { param($journal,$root) Assert-GpuStageRetirement042142Proof -Journal $journal -WorkspaceRoot $root } $fixture.Journal $workspaceRoot } $pattern "Proof accepted the $failure variant."
        }
    }

    Invoke-TestCase '042142 recorded review and result pins reject path, hash, and size drift' {
        $module = Import-TestModule
        $pin = [pscustomobject]@{ Path=(Join-Path $workspaceRoot 'fixture\review.json'); Sha256=('A'*64); Bytes=17L }
        $expectedPath = [string]$pin.Path
        $accepted = & $module { param($value,$path) Assert-GpuStageRetirement042142RecordedPin -Pin $value -ExpectedPath $path -ExpectedSha256 ('A'*64) -ExpectedBytes 17L -Description 'fixture receipt' } $pin $expectedPath
        Assert-True ($null -eq $accepted) 'A valid pinned receipt metadata unexpectedly returned a value.'
        $wrongPath = [pscustomobject]@{ Path=(Join-Path $workspaceRoot 'fixture\other.json'); Sha256=('A'*64); Bytes=17L }
        Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement042142RecordedPin -Pin $value -ExpectedPath $path -ExpectedSha256 ('A'*64) -ExpectedBytes 17L -Description 'fixture receipt' } $wrongPath $expectedPath } 'differs from the closed 042142 evidence pin' 'A changed receipt/result path was accepted.'
        $wrongHash = [pscustomobject]@{ Path=$expectedPath; Sha256=('B'*64); Bytes=17L }
        Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement042142RecordedPin -Pin $value -ExpectedPath $path -ExpectedSha256 ('A'*64) -ExpectedBytes 17L -Description 'fixture receipt' } $wrongHash $expectedPath } 'differs from the closed 042142 evidence pin' 'A changed receipt/result hash was accepted.'
        $wrongBytes = [pscustomobject]@{ Path=$expectedPath; Sha256=('A'*64); Bytes=18L }
        Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement042142RecordedPin -Pin $value -ExpectedPath $path -ExpectedSha256 ('A'*64) -ExpectedBytes 17L -Description 'fixture receipt' } $wrongBytes $expectedPath } 'differs from the closed 042142 evidence pin' 'A changed receipt/result size was accepted.'
    }

    $inventory = Join-Path $fixtureRoot 'inventory-reparse'
    $target = Join-Path $fixtureRoot 'inventory-reparse-target'
    [void][IO.Directory]::CreateDirectory($target)
    $junctionAvailable = $false
    try { New-Item -ItemType Junction -Path $inventory -Target $target -ErrorAction Stop | Out-Null; $junctionAvailable = $true } catch { }
    if ($junctionAvailable) {
        Invoke-TestCase '042142 live inventory rejects a reparse-point root' {
            $module = Import-TestModule
            Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $inventory } 'reparse point' 'A reparse-point stage root was accepted.'
        }
        Remove-Item -LiteralPath $inventory -Force
    } else { Write-Output 'SKIP: Reparse-point fixture unavailable on this filesystem.' }

    Write-Output "Assertions: $script:Assertions"
    Write-Output "Passed cases: $script:PassedCases"
    if ($script:FailedCases.Count -gt 0) {
        Write-Output 'Failed cases:'
        foreach ($failure in $script:FailedCases) { Write-Output "- $failure" }
        exit 1
    }
    Write-Output 'All 042142 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))
    if ([string]::Equals($resolvedParent, $fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fixtureRoot) -and
        ([IO.File]::GetAttributes($fixtureRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
