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
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $fixtureParent ('gpu-stage-retirement-160027-tests-' + [guid]::NewGuid().ToString('N'))))
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

function Get-ProfileState {
    param([object]$Module)
    return & $Module {
        $values = @{}
        foreach ($variable in Get-Variable -Scope Script) {
            if ($variable.Name -like 'GpuRetirement160027*' -or $variable.Name -like 'GpuRetirementAuditTool*') { $values[$variable.Name] = $variable.Value }
        }
        return ,$values
    }
}

function Write-TestJson {
    param([string]$Path, [object]$Value)
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 40 -Compress) + "`n", [Text.UTF8Encoding]::new($false))
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
    param([string]$WorkspaceRoot, [object[]]$StagePins, [string]$Guid = ([guid]::NewGuid().ToString('N')))
    $directory = Join-Path $WorkspaceRoot ('.analysis\gpu-baremetal-readiness\' + $Guid)
    $path = Join-Path $directory 'readiness.json'
    $entries = @($StagePins | ForEach-Object {
        [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
    })
    $state = Get-ProfileState (Import-TestModule)
    $blocker = 'EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.'
    $report = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'; Succeeded=$false
        EvidencePath=$path; EvidenceDirectory=$directory
        Source=[pscustomobject]@{ Branch='feat/smbios-boot-serial-probe'; Commit=$state.GpuRetirement160027SourceCommit; Dirty=$false; DirtyEntryCount=0 }
        Preflight=[pscustomobject]@{ Complete=$true; InventoryComplete=$true; IntelPresent=$true; NvidiaPresent=$true; Error=$null }
        Blockers=@($blocker); Errors=@()
        Host=[pscustomobject]@{ LogicalCpus=20; SecureBoot=$true }
        Recovery=[pscustomobject]@{ Valid=$true; Disk=[pscustomobject]@{ IsVirtual=$false } }
        Selector=[pscustomobject]@{ Exists=$false; Valid=$true; Error=$null }
        Cleanup=[pscustomobject]@{ Succeeded=$true; Errors=@() }
        Esp=[pscustomobject]@{
            Inspected=$true; MountStateAfterCleanup='Unmounted'
            Identity=[pscustomobject]@{
                Volume=$state.GpuRetirement160027EspVolume; DiskNumber=$state.GpuRetirement160027EspDiskNumber
                PartitionGuid=$state.GpuRetirement160027EspPartitionGuid; DiskUniqueId='eui.0000000001000000E4D25CDF32275501'
            }
            BootManagerPin=[pscustomobject]@{ Sha256=$state.GpuRetirement160027BootManagerSha256; Bytes=3086848L }
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

function Select-TestProfile {
    param([object]$Module, [string]$WorkspaceRoot, [string]$ReadinessPath)
    return & $Module { param($root,$path) Select-GpuStageRetirement160027Profile -WorkspaceRoot $root -ReadinessPath $path } $WorkspaceRoot $ReadinessPath
}

function New-ProofFixture {
    param([string]$WorkspaceRoot, [hashtable]$State, [string]$Failure = '')
    $runId = [string]$State.GpuRetirement160027RunId
    $runDirectory = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\runs') $runId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\archive') ($runId + '.json')
    $sourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $finalAction = Join-Path $WorkspaceRoot $State.GpuRetirement160027FinalActionRelativePath
    $runIdentity = if ($Failure -ceq 'run') { 'gpu-unrelated-run' } else { $runId }
    $phase = if ($Failure -ceq 'phase') { 'Failed' } else { 'Complete' }
    $sourceCommit = if ($Failure -ceq 'source') { '0000000000000000000000000000000000000000' } else { $State.GpuRetirement160027SourceCommit }
    $status = if ($Failure -ceq 'status') { 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' } else { 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED' }
    $errors = if ($Failure -ceq 'errors') { @('injected fixture failure') } else { @() }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runIdentity; RunDirectory=$runDirectory; SourceRoot=$sourceRoot
        SourceCommit=$sourceCommit; Phase=$phase; InProgress=$null
        DisarmedBootId='2026-10-21T06:36:00.5000000Z'; LastAction=[pscustomobject]@{
            Action='FinalVerify'; Succeeded=$true; Status=$status; Directory=$finalAction
        }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        FinalVerification=[pscustomobject]@{
            BootId='2026-10-21T06:49:32.5000000Z'; ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
            ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=$errors
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20; CpuPassCount=0 }
            Esp=[pscustomobject]@{
                MatchesBaseline=$true; Volume=$State.GpuRetirement160027EspVolume; DiskNumber=$State.GpuRetirement160027EspDiskNumber
                PartitionGuid=$State.GpuRetirement160027EspPartitionGuid; BootManagerSha256=$State.GpuRetirement160027BootManagerSha256
            }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; ReviewedDifferenceAccepted=$true
                BaselineSha256=$State.GpuRetirement160027OriginalFirmwareSha256
                CurrentSha256=$State.GpuRetirement160027ReviewedFirmwareSha256
                CaptureContext='SystemEspMountedAtS'
                ReviewedDifference=[pscustomobject]@{
                    Accepted=$true; HistoricalRawBootOrderUnchangedProven=$false; FullBootVariableNamespaceAttested=$false
                }
            }
        }
    }
    $archive = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; JournalPath=$journalPath; SourceRoot=$sourceRoot
    }
    Write-TestJson -Path $journalPath -Value $journal
    Write-TestJson -Path $archivePath -Value $archive
    $journalPin = Get-TestPin $journalPath
    $archivePin = Get-TestPin $archivePath
    $module = Import-TestModule
    $readiness = New-ReadinessFixture -WorkspaceRoot $WorkspaceRoot -StagePins $State.GpuRetirement160027StagePins
    $null = Select-TestProfile -Module $module -WorkspaceRoot $WorkspaceRoot -ReadinessPath $readiness.Path
    & $module {
        param($journalHash,$journalBytes,$archiveHash,$archiveBytes)
        $script:GpuRetirement160027JournalSha256=$journalHash
        $script:GpuRetirement160027JournalBytes=$journalBytes
        $script:GpuRetirement160027ArchiveSha256=$archiveHash
        $script:GpuRetirement160027ArchiveBytes=$archiveBytes
    } $journalPin.Sha256 $journalPin.Bytes $archivePin.Sha256 $archivePin.Bytes
    return @{ Module=$module; Journal=$journal; Archive=$archive; JournalPath=$journalPath; ArchivePath=$archivePath; RunDirectory=$runDirectory }
}

function New-RawCaptureFixture {
    param(
        [object]$Module,
        [hashtable]$State,
        [string]$WorkspaceRoot,
        [object]$RawBootNext=$null,
        [object[]]$RawOrder=@(0,1,2,3,4,5),
        [int]$RawFindings=0
    )
    $directory = Join-Path $WorkspaceRoot ('.analysis\raw-capture-fixtures\' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($directory)
    $rawPath = Join-Path $directory 'before-boot-selection.json'
    $receiptLeaf = 'before-bootmgr-integrity-fixture.receipt.json'
    $receiptPath = Join-Path $directory $receiptLeaf
    $receiptStem = [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($receiptLeaf))
    $stdoutPath = Join-Path $directory ($receiptStem + '.stdout.txt')
    $stderrPath = Join-Path $directory ($receiptStem + '.stderr.txt')
    $raw = [pscustomobject]@{
        schema=1; kind='boot-selection'; findings=$RawFindings; boot_next=$RawBootNext; boot_order=[object[]]$RawOrder
        bootmgfw_sha256=$State.GpuRetirement160027BootManagerSha256
        log_sha256=$State.GpuRetirement160027ReferenceLogSha256
    }
    Write-TestJson -Path $rawPath -Value $raw
    $expectedOutput = 'BootmgfwSha256=' + $State.GpuRetirement160027BootManagerSha256 + ' Findings=0x00000000'
    [IO.File]::WriteAllText($stdoutPath, $expectedOutput + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderrPath, '', [Text.UTF8Encoding]::new($false))
    $auditToolPath = Join-Path $WorkspaceRoot $State.GpuRetirementAuditToolRelativePath
    $receipt = [pscustomobject]@{
        SchemaVersion=1; Succeeded=$true; ExitCode=0; AllowedExitCodes=@(0); LaunchError=$null
        Program=[IO.Path]::GetFullPath($auditToolPath)
        Arguments=@('bootmgr-integrity','--expected-hash',$State.GpuRetirement160027BootManagerSha256,'--esp-root','S:\')
        StdoutPath=$stdoutPath; StderrPath=$stderrPath
    }
    Write-TestJson -Path $receiptPath -Value $receipt
    $rawPin = Get-TestPin $rawPath
    & $Module {
        param($hash,$bytes)
        $script:GpuRetirement160027RawCaptureSha256=$hash
        $script:GpuRetirement160027RawCaptureBytes=$bytes
    } $rawPin.Sha256 $rawPin.Bytes
    $capture = [pscustomobject]@{
        Baseline=[pscustomobject]@{
            Schema=1; BootNext=$null; BootOrder=[object[]]@(0,1,2,3,4,5)
            BootManagerSha256=$State.GpuRetirement160027BootManagerSha256
            ReferenceLogSha256=$State.GpuRetirement160027ReferenceLogSha256; IntegrityFindings='00000000'
        }
        BaselinePin=$rawPin; BaselinePath=$rawPath
        AuditToolPin=[pscustomobject]@{
            Path=$auditToolPath; Sha256=$State.GpuRetirementAuditToolSha256; Bytes=$State.GpuRetirementAuditToolBytes
        }
        ReferenceLogPin=[pscustomobject]@{
            Path=(Join-Path $WorkspaceRoot $State.GpuRetirement160027ReferenceLogRelativePath)
            Sha256=$State.GpuRetirement160027ReferenceLogSha256; Bytes=$State.GpuRetirement160027ReferenceLogBytes
        }
        IntegrityOutput=$expectedOutput; IntegrityReceipt=$receiptPath
    }
    $workspacePrefix = [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    return @{ Capture=$capture; RelativePath=$rawPath.Substring($workspacePrefix.Length); Directory=$directory }
}

try {
    if (-not [string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot)), $fixtureParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test fixture escaped its dedicated worktree .codex-build parent.'
    }
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $workspaceRoot = Join-Path $fixtureRoot 'workspace'
    [void][IO.Directory]::CreateDirectory($workspaceRoot)

    Invoke-TestCase '160027 wrapper requires an explicit readiness path and previews without effects' {
        $wrapperToolRoot = Join-Path $workspaceRoot 'tools\gpu_diagnostic'
        $readinessPath = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\missing\readiness.json'
        $before = @(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        $preview = (& $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun160027 -ReadinessPath $readinessPath -WhatIf | Out-String) | ConvertFrom-Json
        $after = @(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        Assert-True ($preview.Preview -and -not $preview.FilesystemChangesPerformed -and -not $preview.NativeCommandsExecuted -and -not $preview.EspMounted -and $preview.CompletedRun160027 -and $before -eq $after) 'The 160027 WhatIf preview performed effects or omitted the selected profile.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun160027 -WhatIf } 'explicit EvidencePath' 'The wrapper accepted a missing fresh readiness path.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun160027 -CompletedRun042142 -ReadinessPath $readinessPath -WhatIf } 'mutually exclusive' 'Two completed-run profiles were accepted together.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -ReadinessPath $readinessPath -WhatIf } 'only with an explicit completed-run profile' 'ReadinessPath was accepted without a closed profile.'
    }

    Invoke-TestCase '160027 profile fixes the exact five names, hashes, sizes, and readiness identity' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $expected = @{
            'grubx64.efi'=@{ Sha256='FBE3015D1B8643647874582855C8E4514AD5991CB61F6A248CABC41251ED821A'; Bytes=77160L }
            'manifest.sha256'=@{ Sha256='647A3D5B79994437BB94C0E680A695B06340ABE030998CCD050F1EAA7F8EEB1E'; Bytes=394L }
            'payload.efi'=@{ Sha256='4C82E477A7015893644DBEA6B260ADA155A1A3A18900DF95AB58C7DB50BD65A2'; Bytes=191336L }
            'shimx64.efi'=@{ Sha256='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'; Bytes=966664L }
            'tcglog.ib'=@{ Sha256='22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'; Bytes=40838L }
        }
        $pins = @($state.GpuRetirement160027StagePins)
        Assert-True ($pins.Count -eq 5 -and @($pins.Name | Sort-Object -Unique).Count -eq 5) 'The closed profile does not contain exactly five unique stage files.'
        foreach ($pin in $pins) {
            Assert-True ($expected.ContainsKey($pin.Name) -and $pin.Sha256 -ceq $expected[$pin.Name].Sha256 -and [long]$pin.Bytes -eq [long]$expected[$pin.Name].Bytes) "The fixed 160027 pin differs for $($pin.Name)."
        }
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $pins
        $selected = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        Assert-True ($selected.Profile -ceq '160027' -and $selected.RunId -ceq $state.GpuRetirement160027RunId -and $selected.SourceCommit -ceq $state.GpuRetirement160027SourceCommit) 'Profile selection did not bind the fixed 160027 run identity.'
        Assert-True ($selected.ReadinessPath -ceq [IO.Path]::GetFullPath($readiness.Path) -and $selected.ReadinessPin.Bytes -gt 0 -and @($selected.StagePins).Count -eq 5) 'Selection did not bind the explicit readiness report and five fixed stage pins.'
        foreach ($older in @('201140','224018','023306','032117','042142','062106')) {
            $selector = "Select-GpuStageRetirement${older}Profile"
            Assert-Throws { & $module { param($name,$root,$path) & $name -WorkspaceRoot $root -ReadinessPath $path } $selector $workspaceRoot $readiness.Path } 'already bound' "The older $older profile was allowed to rebind after selecting 160027."
        }
    }

    Invoke-TestCase '160027 keeps original journal and review-inspector platform pins distinct' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $sourceRoot = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'
        $statePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
        $platformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
        $retirementPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
        $journal = [pscustomobject]@{ SourcePins=@(
            [pscustomobject]@{ Path=$statePath; Sha256=$state.GpuRetirement160027StateSha256; Bytes=$state.GpuRetirement160027StateBytes },
            [pscustomobject]@{ Path=$platformPath; Sha256=$state.GpuRetirement160027JournalPlatformSha256; Bytes=$state.GpuRetirement160027JournalPlatformBytes }
        ) }
        $modulePins = [pscustomobject]@{
            State=[pscustomobject]@{ Path=$statePath; Sha256=$state.GpuRetirement160027StateSha256; Bytes=$state.GpuRetirement160027StateBytes }
            Platform=[pscustomobject]@{ Path=$platformPath; Sha256=$state.GpuRetirement160027ReceiptPlatformSha256; Bytes=$state.GpuRetirement160027ReceiptPlatformBytes }
            Retirement=[pscustomobject]@{ Path=$retirementPath; Sha256=$state.GpuRetirement160027ReceiptRetirementSha256; Bytes=$state.GpuRetirement160027ReceiptRetirementBytes }
        }
        $receipt = [pscustomobject]@{ PinsBefore=[pscustomobject]@{ Modules=$modulePins }; PinsAfter=[pscustomobject]@{ Modules=$modulePins } }
        & $module { param($j,$r,$root) Assert-GpuStageRetirement160027HistoricalSourcePins -Journal $j -ReviewReceipt $r -SourceRoot $root } $journal $receipt $sourceRoot
        $badJournal = $journal | ConvertTo-Json -Depth 10 | ConvertFrom-Json
        $badJournal.SourcePins[1].Sha256 = $state.GpuRetirement160027ReceiptPlatformSha256
        $badJournal.SourcePins[1].Bytes = $state.GpuRetirement160027ReceiptPlatformBytes
        Assert-Throws { & $module { param($j,$r,$root) Assert-GpuStageRetirement160027HistoricalSourcePins -Journal $j -ReviewReceipt $r -SourceRoot $root } $badJournal $receipt $sourceRoot } 'closed 160027 evidence pin' 'The inspector platform pin was accepted as the journal source pin.'
        $badReceipt = $receipt | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $badReceipt.PinsBefore.Modules.Platform.Sha256 = $state.GpuRetirement160027JournalPlatformSha256
        $badReceipt.PinsBefore.Modules.Platform.Bytes = $state.GpuRetirement160027JournalPlatformBytes
        Assert-Throws { & $module { param($j,$r,$root) Assert-GpuStageRetirement160027HistoricalSourcePins -Journal $j -ReviewReceipt $r -SourceRoot $root } $journal $badReceipt $sourceRoot } 'closed 160027 evidence pin' 'The journal platform pin was accepted as the inspector module pin.'
    }

    Invoke-TestCase '160027 readiness requires a clean complete preflight and exactly the retained-stage blocker' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $base = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement160027StagePins
        & $module { param($value,$path) Assert-GpuStageRetirement160027Readiness -Readiness $value -ReadinessFullPath $path } $base.Report $base.Path
        $mutations = @(
            @{ Name='dirty source'; Edit={ param($r) $r.Source.Dirty=$true } },
            @{ Name='nonzero source dirty count'; Edit={ param($r) $r.Source.DirtyEntryCount=1 } },
            @{ Name='wrong source branch'; Edit={ param($r) $r.Source.Branch='main' } },
            @{ Name='invalid source commit'; Edit={ param($r) $r.Source.Commit='not-a-commit' } },
            @{ Name='source collection error'; Edit={ param($r) $r.Source | Add-Member -NotePropertyName Error -NotePropertyValue 'injected source error' } },
            @{ Name='incomplete preflight'; Edit={ param($r) $r.Preflight.Complete=$false } },
            @{ Name='incomplete source inventory'; Edit={ param($r) $r.Preflight.InventoryComplete=$false } },
            @{ Name='preflight adapter error'; Edit={ param($r) $r.Preflight.Error='injected preflight error' } },
            @{ Name='missing Intel adapter'; Edit={ param($r) $r.Preflight.IntelPresent=$false } },
            @{ Name='missing Nvidia adapter'; Edit={ param($r) $r.Preflight.NvidiaPresent=$false } },
            @{ Name='missing retained-stage blocker'; Edit={ param($r) $r.Blockers=@() } },
            @{ Name='additional unrelated blocker'; Edit={ param($r) $r.Blockers+= 'another blocker' } },
            @{ Name='different blocker text'; Edit={ param($r) $r.Blockers[0]='EFI\insecure-boot is ready to overwrite.' } }
        )
        foreach ($mutation in $mutations) {
            $fixture = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement160027StagePins
            & $mutation.Edit $fixture.Report
            Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement160027Readiness -Readiness $value -ReadinessFullPath $path } $fixture.Report $fixture.Path } 'clean, unmounted BLOCKED_READINESS|retained 160027 stage|exactly one blocker' "Readiness accepted $($mutation.Name)."
        }
    }

    Invoke-TestCase '160027 stage inventory rejects count, name, path, hash, size, and kind drift' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $entries = @($state.GpuRetirement160027StagePins | ForEach-Object {
            [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Hashed=$true; Bytes=$_.Bytes; Sha256=$_.Sha256 }
        })
        $accepted = & $module { param($items) Assert-GpuStageRetirement160027StageInventory -Entries $items -Description 'fixture' } $entries
        Assert-True (@($accepted).Count -eq 5) 'The exact fixed five-file inventory was not accepted.'
        $mutations = @(
            @{ Name='missing file'; Edit={ param($items) return @($items | Select-Object -Skip 1) } },
            @{ Name='extra file'; Edit={ param($items) $items += [pscustomobject]@{ RelativePath='EFI\insecure-boot\extra.txt'; Kind='File'; Hashed=$true; Bytes=1; Sha256=('A'*64) }; return $items } },
            @{ Name='duplicate name'; Edit={ param($items) $items += $items[0]; return $items } },
            @{ Name='unknown name'; Edit={ param($items) $items[0].RelativePath='EFI\insecure-boot\unexpected.efi'; return $items } },
            @{ Name='wrong hash'; Edit={ param($items) $items[0].Sha256='0'*64; return $items } },
            @{ Name='wrong size'; Edit={ param($items) $items[0].Bytes=[long]$items[0].Bytes+1; return $items } },
            @{ Name='non-file entry'; Edit={ param($items) $items[0].Kind='Directory'; return $items } },
            @{ Name='unhashed entry'; Edit={ param($items) $items[0].Hashed=$false; return $items } }
        )
        foreach ($mutation in $mutations) {
            $candidate = @($entries | ForEach-Object { [pscustomobject]@{ RelativePath=$_.RelativePath; Kind=$_.Kind; Hashed=$_.Hashed; Bytes=$_.Bytes; Sha256=$_.Sha256 } })
            $candidate = @(& $mutation.Edit $candidate)
            Assert-Throws { & $module { param($items) Assert-GpuStageRetirement160027StageInventory -Entries $items -Description 'fixture' } $candidate } 'exactly (the )?five|unknown|duplicate|pin differs|non-file|unhashed|outside' "Stage inventory accepted $($mutation.Name)."
        }
    }

    Invoke-TestCase '160027 boot-selection proof fixes null BootNext, order 0 through 5, and zero findings' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $makeCapture = {
            param([object[]]$Order=@(0,1,2,3,4,5), [object]$BootNext=$null, [string]$Findings='00000000')
            [pscustomobject]@{ Baseline=[pscustomobject]@{
                Schema=1; BootNext=$BootNext; BootOrder=[object[]]$Order
                BootManagerSha256=$state.GpuRetirement160027BootManagerSha256
                ReferenceLogSha256=$state.GpuRetirement160027ReferenceLogSha256
                IntegrityFindings=$Findings
            } }
        }
        $valid = & $makeCapture
        Assert-True ($null -ne $valid.Baseline.PSObject.Properties['BootNext']) 'The raw boot capture omitted BootNext.'
        $null = & $module { param($capture) Assert-GpuStageRetirement160027BootSelection -Capture $capture -Checkpoint 'fixture' } $valid
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement160027BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -Order @(0,1,2,3,5,4)) } 'BootOrder|sequence 0 through 5' 'A changed BootOrder was accepted.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement160027BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -BootNext 4) } 'BootNext|raw boot-selection' 'A set BootNext was accepted.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement160027BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -Findings '00000001') } 'findings|raw boot-selection' 'A nonzero integrity finding was accepted.'
    }

    Invoke-TestCase '160027 raw capture binds actual null BootNext, ordered boot entries, and zero findings' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement160027StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        $valid = New-RawCaptureFixture -Module $module -State $state -WorkspaceRoot $workspaceRoot
        $null = & $module {
            param($capture,$root,$relative,$leaf)
            Assert-GpuStageRetirement160027RawCapture -Capture $capture -WorkspaceRoot $root -ExpectedRelativePath $relative `
                -ExpectedIntegrityReceiptLeaf $leaf -Description 'fixture'
        } $valid.Capture $workspaceRoot $valid.RelativePath 'before-bootmgr-integrity-fixture.receipt.json'
        foreach ($case in @(
            @{ Name='raw BootNext'; BootNext=0; Order=@(0,1,2,3,4,5); Findings=0; Pattern='BootNext|raw capture lacks' },
            @{ Name='raw BootOrder'; BootNext=$null; Order=@(0,1,2,3,5,4); Findings=0; Pattern='BootOrder differs' },
            @{ Name='raw integrity findings'; BootNext=$null; Order=@(0,1,2,3,4,5); Findings=1; Pattern='findings|raw capture lacks' }
        )) {
            $bad = New-RawCaptureFixture -Module $module -State $state -WorkspaceRoot $workspaceRoot `
                -RawBootNext $case.BootNext -RawOrder $case.Order -RawFindings $case.Findings
            Assert-Throws {
                & $module {
                    param($capture,$root,$relative,$leaf)
                    Assert-GpuStageRetirement160027RawCapture -Capture $capture -WorkspaceRoot $root -ExpectedRelativePath $relative `
                        -ExpectedIntegrityReceiptLeaf $leaf -Description 'fixture'
                } $bad.Capture $workspaceRoot $bad.RelativePath 'before-bootmgr-integrity-fixture.receipt.json'
            } $case.Pattern "The raw capture accepted $($case.Name)."
        }
    }

    Invoke-TestCase '160027 collection requires five present pins and two explicit null absent rows' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement160027StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        $runDirectory = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\runs') $state.GpuRetirement160027RunId
        $journalPath = Join-Path $runDirectory 'journal.json'
        $sourceRoot = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'
        $stageEntries = @($state.GpuRetirement160027StagePins | ForEach-Object {
            [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
        })
        $recordedStage = @($state.GpuRetirement160027StagePins | ForEach-Object {
            [pscustomobject]@{ Name=$_.Name; RelativePath=('EFI\insecure-boot\' + $_.Name); Sha256=$_.Sha256; Bytes=$_.Bytes }
        })
        $collectionRows = @($state.GpuRetirement160027StagePins | ForEach-Object {
            $path = Join-Path (Join-Path $workspaceRoot $state.GpuRetirement160027CollectionActionRelativePath) ('esp-' + $_.Name)
            [pscustomobject]@{ Name=$_.Name; Present=$true; Path=$path; Pin=[pscustomobject]@{ Path=$path; Sha256=$_.Sha256; Bytes=$_.Bytes } }
        })
        $collectionRows += @(
            [pscustomobject]@{ Name='handoff-status.txt'; Present=$false; Path=$null; Pin=$null },
            [pscustomobject]@{ Name='payload-diagnostic.txt'; Present=$false; Path=$null; Pin=$null }
        )
        $audit = [pscustomobject]@{
            SchemaVersion=1; Status='BLOCKED_READINESS'
            Esp=[pscustomobject]@{
                Identity=[pscustomobject]@{ Volume=$state.GpuRetirement160027EspVolume; DiskNumber=$state.GpuRetirement160027EspDiskNumber; PartitionGuid=$state.GpuRetirement160027EspPartitionGuid }
                BootManagerPin=[pscustomobject]@{ Sha256=$state.GpuRetirement160027BootManagerSha256; Bytes=3086848L }
                MountStateAfterCleanup='Unmounted'
            }
            ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true; EntryCount=5; FileCount=5; Entries=$stageEntries; UnknownEntries=@(); UnsafeEntries=@(); Errors=@() }
            ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
        }
        $journal = [pscustomobject]@{
            SchemaVersion=1; RunId=$state.GpuRetirement160027RunId; SourceCommit=$state.GpuRetirement160027SourceCommit; SourceRoot=$sourceRoot
            RunDirectory=$runDirectory; Phase='Complete'; InProgress=$null; DisarmedBootId='2026-10-21T06:36:00.5000000Z'
            LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status='FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'; Directory=(Join-Path $workspaceRoot $state.GpuRetirement160027FinalActionRelativePath) }
            Host=[pscustomobject]@{ LogicalCpus=20 }
            FinalVerification=[pscustomobject]@{
                ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
                Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20; CpuPassCount=0 }
                Esp=[pscustomobject]@{ MatchesBaseline=$true }
                FirmwareEnumeration=[pscustomobject]@{
                    TextEqual=$false; ReviewedDifferenceAccepted=$true
                    BaselineSha256=$state.GpuRetirement160027OriginalFirmwareSha256
                    CurrentSha256=$state.GpuRetirement160027ReviewedFirmwareSha256
                }
                Errors=@()
            }
            Backup=[pscustomobject]@{
                EspVolume=$state.GpuRetirement160027EspVolume; EspDiskNumber=$state.GpuRetirement160027EspDiskNumber
                EspPartitionGuid=$state.GpuRetirement160027EspPartitionGuid; BootManagerSha256=$state.GpuRetirement160027BootManagerSha256
            }
            Stage=[pscustomobject]@{ Files=$recordedStage }
            Collection=[pscustomobject]@{ Esp=[pscustomobject]@{ StageFiles=$collectionRows } }
        }
        $archive = [pscustomobject]@{ SchemaVersion=1; RunId=$state.GpuRetirement160027RunId; RunDirectory=$runDirectory; JournalPath=$journalPath; SourceRoot=$sourceRoot }
        $arguments = @{
            Audit=$audit; Journal=$journal; ArchivedSelector=$archive; ExpectedAuditSha256=('A'*64)
            ExpectedRunId=$state.GpuRetirement160027RunId; ExpectedRunDirectory=$runDirectory
            ExpectedJournalPath=$journalPath; ExpectedSourceRoot=$sourceRoot; ExpectedWorkspaceRoot=$workspaceRoot
        }
        $invoke = {
            param($targetModule,$values)
            & $targetModule {
                param($arguments)
                function Assert-GpuRetirementProfilePin {
                    param($Pin,$ExpectedPath,$ExpectedSha256,$ExpectedBytes,$WorkspaceRoot,$Description)
                    if ([IO.Path]::GetFullPath([string]$Pin.Path) -cne [IO.Path]::GetFullPath($ExpectedPath) -or
                        [string]$Pin.Sha256 -cne $ExpectedSha256 -or [long]$Pin.Bytes -ne [long]$ExpectedBytes) {
                        throw "Fixture pin mismatch: $Description"
                    }
                    return $Pin
                }
                Assert-GpuStageRetirementEvidence @arguments
            } $values
        }
        $accepted = & $invoke $module $arguments
        Assert-True (@($accepted).Count -eq 5) 'The exact five present files and two absent diagnostics were not accepted.'

        foreach ($case in @(
            @{ Name='missing diagnostic row'; Edit={ param($j) $j.Collection.Esp.StageFiles=@($j.Collection.Esp.StageFiles | Where-Object Name -ne 'payload-diagnostic.txt') } },
            @{ Name='absent row marked present'; Edit={ param($j) $j.Collection.Esp.StageFiles[5].Present=$true } },
            @{ Name='absent row with path'; Edit={ param($j) $j.Collection.Esp.StageFiles[5].Path='fixture.txt' } },
            @{ Name='absent row with pin'; Edit={ param($j) $j.Collection.Esp.StageFiles[6].Pin=[pscustomobject]@{ Path='fixture.txt'; Sha256=('A'*64); Bytes=1L } } },
            @{ Name='missing present file'; Edit={ param($j) $j.Collection.Esp.StageFiles=@($j.Collection.Esp.StageFiles | Where-Object Name -ne 'payload.efi') } }
        )) {
            $bad = $journal | ConvertTo-Json -Depth 32 | ConvertFrom-Json
            & $case.Edit $bad
            $badArguments = @{} + $arguments; $badArguments.Journal=$bad
            Assert-Throws { & $invoke $module $badArguments } 'exactly seven|explicitly absent|explicit absent|diagnostic collection row|collected stage pin|missing' "Collection accepted $($case.Name)."
        }
    }

    Invoke-TestCase '160027 closed-run proof rejects unrelated and failed completion variants' {
        $different = [pscustomobject]@{ RunId='gpu-unrelated-run' }
        $stateModule = Import-TestModule
        Assert-Throws { & $stateModule { param($journal,$root) Assert-GpuStageRetirement160027Proof -Journal $journal -WorkspaceRoot $root } $different $workspaceRoot } 'fixed archived 160027|160027 run identity' 'The proof accepted a different run id.'
        foreach ($failure in @('phase','source','status','errors')) {
            $stateModule = Import-TestModule
            $state = Get-ProfileState $stateModule
            $fixture = New-ProofFixture -WorkspaceRoot $workspaceRoot -State $state -Failure $failure
            $pattern = if ($failure -ceq 'errors') { 'does not satisfy|errors|reset' } elseif ($failure -ceq 'phase') { 'Complete|identity|successful' } elseif ($failure -ceq 'source') { 'source|identity|successful' } else { 'status|identity|successful|FinalVerify' }
            Assert-Throws { & $fixture.Module { param($journal,$root) Assert-GpuStageRetirement160027Proof -Journal $journal -WorkspaceRoot $root } $fixture.Journal $workspaceRoot } $pattern "Proof accepted the $failure fixture."
        }
    }

    Invoke-TestCase '160027 Apply rejects malformed evidence before mount or file deletion' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement160027StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path

        # Keep the overall audit shape intact and corrupt one reviewed pin so
        # failure occurs inside the closed inventory gate before platform calls.
        $readiness.Report.ExistingStage.Entries[0].Sha256 = ('0' * 64)
        Write-TestJson -Path $readiness.Path -Value $readiness.Report
        $runId = $state.GpuRetirement160027RunId
        $runDirectory = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\runs') $runId
        $journalPath = Join-Path $runDirectory 'journal.json'
        $archivePath = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\archive') ($runId + '.json')
        $sourceRoot = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'
        $journal = [pscustomobject]@{
            SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; SourceRoot=$sourceRoot
            SourceCommit=$state.GpuRetirement160027SourceCommit; Phase='Complete'; InProgress=$null
            DisarmedBootId=$state.GpuRetirement160027DisarmedBootId
            LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status=$state.GpuRetirement160027FinalResultStatus }
            Host=[pscustomobject]@{ LogicalCpus=20 }
            FinalVerification=[pscustomobject]@{
                ResetBoundaryPassed=$true; SecureBoot=$true; ActivationAttempted=$true
                Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
                Esp=[pscustomobject]@{ MatchesBaseline=$true }
                FirmwareEnumeration=[pscustomobject]@{
                    TextEqual=$false; ReviewedDifferenceAccepted=$true
                    BaselineSha256=$state.GpuRetirement160027OriginalFirmwareSha256
                    CurrentSha256=$state.GpuRetirement160027ReviewedFirmwareSha256
                }
                Errors=@()
            }
        }
        $archive = [pscustomobject]@{
            SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; JournalPath=$journalPath; SourceRoot=$sourceRoot
        }
        Write-TestJson -Path $journalPath -Value $journal
        Write-TestJson -Path $archivePath -Value $archive
        $auditPin = Get-TestPin $readiness.Path
        $journalPin = Get-TestPin $journalPath
        $archivePin = Get-TestPin $archivePath
        $calls = @{ Mount=0; Delete=0 }
        $operations = @{
            VerifySource={ return $true }.GetNewClosure()
            AssertPathWithin={ return $true }.GetNewClosure()
            MountEsp={ $calls.Mount++; throw 'MountEsp must not be reached for rejected evidence.' }.GetNewClosure()
            DeleteFile={ $calls.Delete++; throw 'DeleteFile must not be reached for rejected evidence.' }.GetNewClosure()
        }
        $report = & $module {
            param($toolRoot,$workspace,$ops,$audit,$journalPin,$archivePin,$runId,$runDirectory,$journalPath,$sourceRoot,$state)
            Invoke-GpuStageRetirement -ToolRoot $toolRoot -WorkspaceRoot $workspace -Apply -Operations $ops `
                -ExpectedAuditSha256 $audit.Sha256 -ExpectedAuditBytes $audit.Bytes `
                -ExpectedJournalSha256 $journalPin.Sha256 -ExpectedJournalBytes $journalPin.Bytes `
                -ExpectedArchiveSha256 $archivePin.Sha256 -ExpectedArchiveBytes $archivePin.Bytes `
                -ExpectedRunId $runId -ExpectedProbeSha256 $state.GpuRetirement160027ProbeSha256 `
                -ExpectedProbeBytes $state.GpuRetirement160027ProbeBytes
        } $ToolRoot $workspaceRoot $operations $auditPin $journalPin $archivePin $runId $runDirectory $journalPath $sourceRoot $state
        Assert-True (-not $report.Succeeded -and @($report.Errors).Count -gt 0) 'Apply accepted a corrupted readiness inventory.'
        Assert-True ($calls.Mount -eq 0 -and $calls.Delete -eq 0 -and $null -eq $report.LocalReceiptDirectory) 'A mount, deletion, or receipt-directory creation happened before evidence validation.'
    }

    Invoke-TestCase '160027 live stage inventory accepts five private files and rejects extra entries and directories' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement160027StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        $stageDirectory = Join-Path $fixtureRoot 'inventory-valid'
        [void][IO.Directory]::CreateDirectory($stageDirectory)
        foreach ($pin in $state.GpuRetirement160027StagePins) { [IO.File]::WriteAllText((Join-Path $stageDirectory $pin.Name), "fixture $($pin.Name)", [Text.UTF8Encoding]::new($false)) }
        $inventory = & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory
        Assert-True ($inventory.Count -eq 5 -and @($inventory.Keys).Count -eq 5) 'The five-file private live inventory was not accepted.'

        [IO.File]::WriteAllText((Join-Path $stageDirectory 'unexpected.bin'), 'fixture', [Text.UTF8Encoding]::new($false))
        Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory } 'exactly five|unknown or duplicate' 'An extra live stage file was accepted.'
        Remove-Item -LiteralPath (Join-Path $stageDirectory 'unexpected.bin') -Force
        [void][IO.Directory]::CreateDirectory((Join-Path $stageDirectory 'nested'))
        Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory } 'exactly five|directory or reparse' 'A nested live-stage directory was accepted.'
    }

    $reparseRoot = Join-Path $fixtureRoot 'inventory-reparse'
    $reparseTarget = Join-Path $fixtureRoot 'inventory-reparse-target'
    [void][IO.Directory]::CreateDirectory($reparseTarget)
    $junctionAvailable = $false
    try { New-Item -ItemType Junction -Path $reparseRoot -Target $reparseTarget -ErrorAction Stop | Out-Null; $junctionAvailable = $true } catch { }
    if ($junctionAvailable) {
        Invoke-TestCase '160027 live inventory rejects a reparse-point root' {
            $module = Import-TestModule
            Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $reparseRoot } 'reparse point' 'A reparse-point stage root was accepted.'
        }
        Remove-Item -LiteralPath $reparseRoot -Force
    } else { Write-Output 'SKIP: Reparse-point fixture unavailable on this filesystem.' }

    Write-Output "Assertions: $script:Assertions"
    Write-Output "Passed cases: $script:PassedCases"
    if ($script:FailedCases.Count -gt 0) {
        Write-Output 'Failed cases:'
        foreach ($failure in $script:FailedCases) { Write-Output "- $failure" }
        exit 1
    }
    Write-Output 'All 160027 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))
    if ([string]::Equals($resolvedParent, $fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fixtureRoot) -and
        ([IO.File]::GetAttributes($fixtureRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
