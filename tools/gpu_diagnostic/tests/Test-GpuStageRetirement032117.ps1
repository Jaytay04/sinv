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
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $fixtureParent ('gpu-stage-retirement-032117-tests-' + [guid]::NewGuid().ToString('N'))))
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()
$script:StageNames = @('grubx64.efi','handoff-status.txt','manifest.sha256','payload-diagnostic.txt','payload.efi','shimx64.efi','tcglog.ib')
$script:StagePins = @(
    [pscustomobject]@{ Name='grubx64.efi'; RelativePath='EFI\insecure-boot\grubx64.efi'; Sha256='68CCCF30E90E95D4A2FB96E0EB78D5AF0631289D74516C0904E345F1C0D35825'; Bytes=62824L },
    [pscustomobject]@{ Name='handoff-status.txt'; RelativePath='EFI\insecure-boot\handoff-status.txt'; Sha256='168EBF88856C9A5CCD1A48613D781A66B19421A4507E3B6A8F6A2C520B18D21D'; Bytes=104L },
    [pscustomobject]@{ Name='manifest.sha256'; RelativePath='EFI\insecure-boot\manifest.sha256'; Sha256='D05C07DA5132230340F70A7BAE6A9749E3C56ACC2042267048590E04452ED6BE'; Bytes=394L },
    [pscustomobject]@{ Name='payload-diagnostic.txt'; RelativePath='EFI\insecure-boot\payload-diagnostic.txt'; Sha256='1E162092649E707EB6D185816BB33C4B0F09B0B70CE548EF02432ABFB960FB8D'; Bytes=577L },
    [pscustomobject]@{ Name='payload.efi'; RelativePath='EFI\insecure-boot\payload.efi'; Sha256='15704297FAD2A2F884875BE407E9E9CDCD93684F7AE91C7356D7FCABC0F223CC'; Bytes=190312L },
    [pscustomobject]@{ Name='shimx64.efi'; RelativePath='EFI\insecure-boot\shimx64.efi'; Sha256='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'; Bytes=966664L },
    [pscustomobject]@{ Name='tcglog.ib'; RelativePath='EFI\insecure-boot\tcglog.ib'; Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838L }
)

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
    return Import-Module $modulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1
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

function New-ReadinessFixture {
    param([string]$Root, [hashtable]$Changes = @{})
    $guid = '032117e70cc55505f948aab50c6c830b'
    $directory = Join-Path $Root ('.analysis\gpu-baremetal-readiness\' + $guid)
    $path = Join-Path $directory 'readiness.json'
    $entries = @($script:StagePins | ForEach-Object {
        [pscustomobject]@{ RelativePath=$_.RelativePath; Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
    })
    $report = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'; Succeeded=$false
        EvidencePath=$path; EvidenceDirectory=$directory
        Selector=[pscustomobject]@{ Exists=$false; Valid=$true; Error=$null }
        Cleanup=[pscustomobject]@{ Succeeded=$true; Errors=@() }
        Esp=[pscustomobject]@{ Inspected=$true; MountStateAfterCleanup='Unmounted' }
        Effects=[pscustomobject]@{ EspFilesChanged=$false; FirmwareChanged=$false; BootStateChanged=$false; EspTemporarilyMounted=$true }
        ExistingStage=[pscustomobject]@{
            Exists=$true; Complete=$true; EntryCount=7; FileCount=7; Entries=$entries
            UnknownEntries=@(); UnsafeEntries=@(); Errors=@()
        }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
    }
    foreach ($key in $Changes.Keys) { $report.$key = $Changes[$key] }
    Write-TestJson $path $report
    return @{ Path=$path; Directory=$directory; Report=$report; Guid=$guid }
}

function New-EvidenceFixture {
    param([string]$Root, [object]$ReadinessFixture)
    $runId = 'gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1'
    $runDirectory = Join-Path $Root ('.analysis\gpu-baremetal\runs\' + $runId)
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path $Root ('.analysis\gpu-baremetal\archive\' + $runId + '.json')
    $journal = [pscustomobject]@{ RunId=$runId; RunDirectory=$runDirectory; SourceCommit='c6a36ffcda3e7d599ceff57439b8d7d6049f40da'; Phase='Complete'; InProgress=$null }
    $archive = [pscustomobject]@{ SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; JournalPath=$journalPath; SourceRoot=$Root }
    Write-TestJson $journalPath $journal
    Write-TestJson $archivePath $archive
    return @{ JournalPath=$journalPath; ArchivePath=$archivePath; Journal=$journal; Archive=$archive; RunDirectory=$runDirectory }
}

function New-ProofJournalFixture {
    param([string]$Root, [hashtable]$Changes = @{})
    $runId = 'gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1'
    $runDirectory = Join-Path (Join-Path $Root '.analysis\gpu-baremetal\runs') $runId
    $finalAction = Join-Path $runDirectory 'actions\20261021T033825524Z-FinalVerify-75e8910ae96948b692c1a9c1135b52ec'
    $backupDirectory = Join-Path $runDirectory 'actions\20261021T032438876Z-Backup-f6bae3de7c634bc6a9086b6e9686f8a6'
    $journal = [pscustomobject]@{
        RunId=$runId; RunDirectory=$runDirectory; SourceCommit='c6a36ffcda3e7d599ceff57439b8d7d6049f40da'
        Phase='Complete'; InProgress=$null; DisarmedBootId='fixture-disarm-boot'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status='FINAL_RESET_VERIFIED_GPU_UNATTESTED'; Directory=$finalAction }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        Backup=[pscustomobject]@{
            FirmwareEnumeration=[pscustomobject]@{
                Path=(Join-Path $backupDirectory 'firmware-enumeration-backup-f0f0d2e114d3489a9af255d15032ddbd.stdout.txt')
                Sha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'; Bytes=2890
            }
            ReferenceLog=[pscustomobject]@{
                Path=(Join-Path $backupDirectory 'tcglog.ib')
                Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838
            }
        }
        Tools=[pscustomobject]@{ Probe=[pscustomobject]@{
            Path=(Join-Path $Root '.codex-build\gb\b50c6c830b9048b1\l\ib-tcg-dump.exe')
            Sha256='80BD76951F16AE34F10CB3981C6D6CE71CA7E51422CF3A56615DF637D6FBF98D'; Bytes=172544
        } }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
            ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=@()
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20; CpuPassCount=0 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true; BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; PartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD'; DiskNumber=0 }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$true
                BaselineSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
                CurrentSha256='66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
                CaptureContext='SystemEspMountedAtS'
            }
        }
    }
    foreach ($key in $Changes.Keys) { $journal.$key = $Changes[$key] }
    return $journal
}

function New-GenericEvidenceFixture {
    param([string]$Root)
    $runId = 'gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1'
    $runDirectory = Join-Path $Root ('.analysis\gpu-baremetal\runs\' + $runId)
    $entries = @($script:StagePins | ForEach-Object {
        [pscustomobject]@{ RelativePath=$_.RelativePath; Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
    })
    $recordedStage = @($script:StagePins | Where-Object Name -notin @('handoff-status.txt','payload-diagnostic.txt') | ForEach-Object {
        [pscustomobject]@{ RelativePath=$_.RelativePath; Sha256=$_.Sha256; Bytes=$_.Bytes }
    })
    $collected = @($script:StagePins | ForEach-Object {
        [pscustomobject]@{ Name=$_.Name; Pin=[pscustomobject]@{ Sha256=$_.Sha256; Bytes=$_.Bytes } }
    })
    $firmware = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
    $audit = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'
        Esp=[pscustomobject]@{
            Identity=[pscustomobject]@{ Volume='fixture-volume'; DiskNumber=0; DiskUniqueId='fixture-disk'; PartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD' }
            BootManagerPin=[pscustomobject]@{ Sha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; Bytes=3086848 }
            MountStateAfterCleanup='Unmounted'
        }
        ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true; EntryCount=7; FileCount=7; Entries=$entries; UnknownEntries=@(); UnsafeEntries=@(); Errors=@() }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
    }
    $verification = [pscustomobject]@{
        ResetBoundaryPassed=$true; SecureBoot=$true; ActivationAttempted=$true
        Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
        Esp=[pscustomobject]@{ MatchesBaseline=$true }
        FirmwareEnumeration=[pscustomobject]@{ TextEqual=$true; BaselineSha256=$firmware; CurrentSha256=$firmware; CaptureContext='SystemEspMountedAtS' }
        Errors=@()
    }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; SourceCommit='c6a36ffcda3e7d599ceff57439b8d7d6049f40da'; SourceRoot=$Root
        RunDirectory=$runDirectory; Phase='Complete'; InProgress=$null; DisarmedBootId='fixture-boot'
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        FinalVerification=$verification
        Backup=[pscustomobject]@{
            EspVolume='fixture-volume'; EspDiskNumber=0; EspPartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD'
            BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
        }
        Stage=[pscustomobject]@{ Files=$recordedStage }
        Collection=[pscustomobject]@{ Esp=[pscustomobject]@{ StageFiles=$collected } }
    }
    $archive = [pscustomobject]@{ SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; JournalPath=(Join-Path $runDirectory 'journal.json'); SourceRoot=$Root }
    return @{ Audit=$audit; Journal=$journal; Archive=$archive; RunDirectory=$runDirectory }
}

try {
    if (-not [string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot)), $fixtureParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test fixture escaped its dedicated worktree .codex-build parent.'
    }
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $workspaceRoot = Join-Path $fixtureRoot 'workspace'
    [void][IO.Directory]::CreateDirectory($workspaceRoot)

    Invoke-TestCase '032117 preview is filesystem-free and reports the fixed profile' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $before = @(Get-ChildItem -LiteralPath $workspaceRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        $output = & $scriptPath -ToolRoot (Join-Path $workspaceRoot 'tools\gpu_diagnostic') -CompletedRun032117 -ReadinessPath $fixture.Path -WhatIf | Out-String
        $preview = $output | ConvertFrom-Json
        $after = @(Get-ChildItem -LiteralPath $workspaceRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        Assert-True ($preview.Preview -and -not $preview.FilesystemChangesPerformed -and -not $preview.NativeCommandsExecuted -and -not $preview.EspMounted -and $preview.CompletedRun032117 -and $before -eq $after) 'WhatIf did not remain a side-effect-free preview.'
    }

    Invoke-TestCase '032117 is mutually exclusive and requires an explicit fresh readiness path' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        Assert-Throws { & $scriptPath -CompletedRun201140 -CompletedRun032117 -ReadinessPath $fixture.Path -WhatIf } 'mutually exclusive' '201140 and 032117 were allowed together.'
        Assert-Throws { & $scriptPath -CompletedRun224018 -CompletedRun032117 -ReadinessPath $fixture.Path -WhatIf } 'mutually exclusive' '224018 and 032117 were allowed together.'
        Assert-Throws { & $scriptPath -CompletedRun023306 -CompletedRun032117 -ReadinessPath $fixture.Path -WhatIf } 'mutually exclusive' '023306 and 032117 were allowed together.'
        Assert-Throws { & $scriptPath -CompletedRun032117 -WhatIf } 'explicit EvidencePath' '032117 proceeded without a fresh explicit readiness path.'
        Assert-Throws { & $scriptPath -ReadinessPath $fixture.Path -WhatIf } 'only with an explicit completed-run profile' 'ReadinessPath proceeded without a fixed profile.'
    }

    Invoke-TestCase '032117 selection binds exact immutable run evidence and blocks profile rebinding' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $module = Import-TestModule
        $selected = & $module { param($root,$path) Select-GpuStageRetirement032117Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path
        $state = & $module { [pscustomobject]@{
            Profile=$script:GpuRetirementProfile; RunId=$script:GpuRetirementRunId; SourceCommit=$script:GpuRetirementSourceCommit
            JournalSha256=$script:GpuRetirementJournalSha256; JournalBytes=$script:GpuRetirementJournalBytes
            ArchiveSha256=$script:GpuRetirementArchiveSha256; ArchiveBytes=$script:GpuRetirementArchiveBytes
            ProbeSha256=$script:GpuRetirementProbeSha256; ProbeBytes=$script:GpuRetirementProbeBytes
            ReferenceSha256=$script:GpuRetirementReferenceLogSha256; ReferenceBytes=$script:GpuRetirementReferenceLogBytes
        } }
        Assert-True ($selected.Profile -ceq '032117' -and $selected.RunId -ceq 'gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1') 'Selection chose a different fixed run.'
        Assert-True ($selected.ReadinessPath -ceq [IO.Path]::GetFullPath($fixture.Path) -and $selected.ReadinessPin.Bytes -gt 0) 'Selection did not pin the explicit readiness report.'
        Assert-True ($state.SourceCommit -ceq 'c6a36ffcda3e7d599ceff57439b8d7d6049f40da' -and $state.JournalSha256 -ceq '152259205B75D5E2CAB6F0D036041707E0AAA9DDD3A2431BFF239C421F38699F' -and $state.JournalBytes -eq 44878 -and $state.ArchiveSha256 -ceq 'F72F7BA7A21AB3DF2D075541B6F2103F57133A8199CD3DB14EC735ACB91C82A7' -and $state.ArchiveBytes -eq 533) 'Journal or archive pins differ from the closed evidence profile.'
        Assert-True ($state.ProbeSha256 -ceq '80BD76951F16AE34F10CB3981C6D6CE71CA7E51422CF3A56615DF637D6FBF98D' -and $state.ProbeBytes -eq 172544 -and $state.ReferenceSha256 -ceq '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447' -and $state.ReferenceBytes -eq 40838) 'Probe or reference-log pin differs from the completed run.'
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement023306Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path } 'already bound' 'The selected profile was rebound to 023306.'
    }

    Invoke-TestCase '032117 selection rejects malformed or unsafe readiness evidence' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $fixture.Report.Selector.Exists = $true
        Write-TestJson $fixture.Path $fixture.Report
        $module = Import-TestModule
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement032117Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $fixture.Path } 'complete, cleanly unmounted' 'An active selector was accepted.'
        Assert-Throws { & $module { param($root,$path) Select-GpuStageRetirement032117Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot (Join-Path $workspaceRoot 'outside.json') } 'Reviewed evidence path is outside the workspace|missing|malformed' 'An unsafe readiness path was accepted.'
    }

    Invoke-TestCase '032117 proof rejects cross-run, non-normal FinalVerify, firmware drift, and reviewed override' {
        $module = Import-TestModule
        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.RunId = 'gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact successful normal FinalVerify' 'A different completed run passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.LastAction.Status = 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact successful normal FinalVerify' 'A reviewed firmware-difference completion passed the normal profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration.CurrentSha256 = '0' * 64
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'pinned original firmware baseline' 'A changed firmware hash passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration.TextEqual = $false
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'pinned original firmware baseline' 'A firmware text difference passed the profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.FinalVerification.FirmwareEnumeration | Add-Member -NotePropertyName ReviewedDifferenceAccepted -NotePropertyValue $true
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'pinned original firmware baseline' 'A reviewed-difference override passed the normal profile proof.'

        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.LastAction.Directory = Join-Path $journal.RunDirectory 'actions\future-final-verify'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'exact successful normal FinalVerify' 'A future or unpinned FinalVerify path passed the profile proof.'
    }

    Invoke-TestCase '032117 proof binds exact original firmware baseline path and pin' {
        $module = Import-TestModule
        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.Backup.FirmwareEnumeration.Bytes = 2891
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'path or pin differs' 'A modified original firmware pin passed the profile proof.'
        $journal = New-ProofJournalFixture -Root $workspaceRoot
        $journal.Backup.FirmwareEnumeration.Path = Join-Path $journal.RunDirectory 'foreign-firmware.txt'
        Assert-Throws { & $module { param($value,$root) Assert-GpuStageRetirement032117Proof -Journal $value -WorkspaceRoot $root } $journal $workspaceRoot } 'path or pin differs' 'A modified original firmware path passed the profile proof.'
    }

    Invoke-TestCase '032117 seven-file inventory is matched across readiness, journal, archive, and collection' {
        $module = Import-TestModule
        $readiness = New-ReadinessFixture -Root $workspaceRoot
        $null = & $module { param($root,$path) Select-GpuStageRetirement032117Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $readiness.Path
        $fixture = New-GenericEvidenceFixture -Root $workspaceRoot
        $arguments = @{
            Audit=$fixture.Audit; Journal=$fixture.Journal; ArchivedSelector=$fixture.Archive
            ExpectedAuditSha256=('A' * 64); ExpectedRunId=$fixture.Journal.RunId
            ExpectedRunDirectory=$fixture.RunDirectory; ExpectedJournalPath=(Join-Path $fixture.RunDirectory 'journal.json')
            ExpectedSourceRoot=$workspaceRoot
        }
        $stage = & $module { param($value) Assert-GpuStageRetirementEvidence @value } $arguments
        Assert-True (@($stage).Count -eq 7 -and (@($stage | Where-Object Name -eq 'payload-diagnostic.txt').Count -eq 1)) 'The retained seven-file inventory was not fully pinned.'
        $fixture.Journal.Collection.Esp.StageFiles[0].Pin.Sha256 = '0' * 64
        Assert-Throws { & $module { param($value) Assert-GpuStageRetirementEvidence @value } $arguments } 'differs from the reviewed audit' 'A modified collection file pin passed the inventory proof.'
    }

    Invoke-TestCase '032117 profile proof failure prevents ESP mounting and all file removal' {
        $fixture = New-ReadinessFixture -Root $workspaceRoot
        $evidence = New-EvidenceFixture -Root $workspaceRoot -ReadinessFixture $fixture
        $toolFixture = Join-Path $workspaceRoot 'tools\gpu_diagnostic'
        foreach ($name in @('Retire-GpuCompletedStage.ps1','GpuStageRetirement.psm1','GpuHostState.psm1','GpuHostPlatform.psm1','Get-GpuBaremetalReadiness.ps1')) {
            $path = Join-Path $toolFixture $name
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent $path))
            [IO.File]::WriteAllText($path, "fixture source $name`n", [Text.UTF8Encoding]::new($false))
        }
        $readinessPin = Get-TestPin $fixture.Path
        $journalPin = Get-TestPin $evidence.JournalPath
        $archivePin = Get-TestPin $evidence.ArchivePath
        $calls = @{ Mount=0; Delete=0 }
        $operations = @{
            VerifySource = { param($root,$commit) }
            AssertPathWithin = { param($path,$root,$mustExist)
                $full = [IO.Path]::GetFullPath($path); $base = [IO.Path]::GetFullPath($root)
                $prefix = $base.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
                if (-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Mock path escaped root.' }
                if ($mustExist -and -not (Test-Path -LiteralPath $full)) { throw "Mock path missing: $full" }
                return $full
            }
            MountEsp = { $calls.Mount++ }.GetNewClosure()
            DeleteFile = { $calls.Delete++ }.GetNewClosure()
        }
        $parameters = @{
            ToolRoot=$toolFixture; WorkspaceRoot=$workspaceRoot; Operations=$operations
            ExpectedRunId='gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1'
            ExpectedAuditSha256=$readinessPin.Sha256; ExpectedAuditBytes=$readinessPin.Bytes
            ExpectedJournalSha256=$journalPin.Sha256; ExpectedJournalBytes=$journalPin.Bytes
            ExpectedArchiveSha256=$archivePin.Sha256; ExpectedArchiveBytes=$archivePin.Bytes
            ExpectedProbeSha256='80BD76951F16AE34F10CB3981C6D6CE71CA7E51422CF3A56615DF637D6FBF98D'; ExpectedProbeBytes=172544
            ExpectedAuditToolSha256=('A' * 64); ExpectedAuditToolBytes=1; ExpectedAuditToolRelativePath='fixture\ibaudit.exe'
            ExpectedReferenceLogSha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; ExpectedReferenceLogBytes=40838
        }
        $module = Import-TestModule
        $result = & $module {
            param($p,$root,$readiness)
            $null = Select-GpuStageRetirement032117Profile -WorkspaceRoot $root -ReadinessPath $readiness
            function Assert-GpuStageRetirementEvidence { param($Audit,$Journal,$ArchivedSelector,$ExpectedAuditSha256,$ExpectedRunId,$ExpectedRunDirectory,$ExpectedJournalPath,$ExpectedSourceRoot) return @() }
            function Assert-GpuStageRetirement032117Proof { param($Journal,$WorkspaceRoot) throw 'mock 032117 profile proof failure' }
            Invoke-GpuStageRetirement @p
        } $parameters $workspaceRoot $fixture.Path
        Assert-True (-not $result.Succeeded -and @($result.Errors | Where-Object { $_ -match 'mock 032117 profile proof failure' }).Count -eq 1) 'The failed proof did not fail retirement closed.'
        Assert-True ($calls.Mount -eq 0 -and $calls.Delete -eq 0 -and $null -eq $result.LocalReceiptDirectory) 'ESP mounting, file removal, or receipt creation occurred before the profile proof passed.'
        Assert-True ([IO.Directory]::Exists($fixture.Directory) -and [IO.File]::Exists($evidence.JournalPath) -and [IO.File]::Exists($evidence.ArchivePath)) 'The failed proof removed readiness or archived evidence.'
    }

    Write-Output "Assertions: $script:Assertions"
    Write-Output "Passed cases: $script:PassedCases"
    if ($script:FailedCases.Count -gt 0) {
        Write-Output 'Failed cases:'
        foreach ($failure in $script:FailedCases) { Write-Output "- $failure" }
        exit 1
    }
    Write-Output 'All 032117 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))
    if ([string]::Equals($resolvedParent, $fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fixtureRoot) -and
        ([IO.File]::GetAttributes($fixtureRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
