[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuStageRetirement.psm1'
$scriptPath = Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'
$retirementModule = Import-Module $modulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()
$fixtureRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $ToolRoot)) ('.codex-build\gpu-stage-retirement-contract-' + [guid]::NewGuid().ToString('N'))
$fixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
$fixtureParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))

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
function Get-TestPin {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force
    return [pscustomobject]@{
        Path = [IO.Path]::GetFullPath($Path)
        Sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
        Bytes = [long]$item.Length
    }
}
function Write-TestJson {
    param([string]$Path, [object]$Value)
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 32 -Compress) + "`n", [Text.UTF8Encoding]::new($false))
}
function New-TestFixture {
    $root = Join-Path $fixtureRoot 'workspace'
    $tools = Join-Path $root 'tools\gpu_diagnostic'
    $stateRoot = Join-Path $root '.analysis\gpu-baremetal'
    $runId = 'gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1'
    $runDirectory = Join-Path $stateRoot ('runs\' + $runId)
    $archiveDirectory = Join-Path $stateRoot 'archive'
    $recoveryDirectory = Join-Path $root ('recovery\' + $runId)
    $buildDirectory = Join-Path $root 'build\loader'
    $espRoot = Join-Path $root 'esp'
    $stageDirectory = Join-Path $espRoot 'EFI\insecure-boot'
    $bootManagerPath = Join-Path $espRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
    foreach ($directory in @($tools,$runDirectory,$archiveDirectory,$recoveryDirectory,$buildDirectory,$stageDirectory,(Split-Path -Parent $bootManagerPath))) {
        [void][IO.Directory]::CreateDirectory($directory)
    }
    foreach ($file in @('Retire-GpuCompletedStage.ps1','GpuStageRetirement.psm1','GpuHostState.psm1','GpuHostPlatform.psm1','Get-GpuBaremetalReadiness.ps1')) {
        [IO.File]::WriteAllText((Join-Path $tools $file), "fixture source $file`n", [Text.UTF8Encoding]::new($false))
    }
    $names = @('grubx64.efi','handoff-status.txt','manifest.sha256','payload.efi','payload-diagnostic.txt','shimx64.efi','tcglog.ib')
    $stagePins = @()
    foreach ($name in $names) {
        $path = Join-Path $stageDirectory $name
        [IO.File]::WriteAllText($path, "reviewed stage file $name`n", [Text.UTF8Encoding]::new($false))
        $pin = Get-TestPin $path
        $stagePins += [pscustomobject]@{ Name=$name; RelativePath=('EFI\insecure-boot\' + $name); Sha256=$pin.Sha256; Bytes=$pin.Bytes; Pin=$pin; Kind='File'; Hashed=$true }
    }
    [IO.File]::WriteAllText($bootManagerPath, "windows boot manager`n", [Text.UTF8Encoding]::new($false))
    $bootPin = Get-TestPin $bootManagerPath
    [IO.File]::WriteAllText((Join-Path $buildDirectory 'ibaudit.exe'), "audit tool fixture`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $buildDirectory 'ib-tcg-dump.exe'), "probe tool fixture`n", [Text.UTF8Encoding]::new($false))
    $probePin = Get-TestPin (Join-Path $buildDirectory 'ib-tcg-dump.exe')
    [IO.File]::WriteAllText((Join-Path $buildDirectory 'tcglog.ib'), "reference log fixture`n", [Text.UTF8Encoding]::new($false))
    $logPin = Get-TestPin (Join-Path $buildDirectory 'tcglog.ib')
    $firmwarePath = Join-Path $recoveryDirectory 'firmware-enumeration-before.txt'
    [IO.File]::WriteAllText($firmwarePath, "Firmware baseline context=S:`nBootOrder: {fixture}`n", [Text.UTF8Encoding]::new($false))
    $firmwarePin = Get-TestPin $firmwarePath
    $reviewedFirmwareRelativePath = '.analysis\gpu-stage-retirement\0851536237dd4f6a9ff5df72b0c36848\observations\stage-retirement-firmware-214f7cdb11a647beb09fd271978cca83.stdout.txt'
    $reviewedFirmwarePath = Join-Path $root $reviewedFirmwareRelativePath
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $reviewedFirmwarePath))
    $reviewedFirmwareText = "Firmware Boot Manager`n---------------------`nidentifier {fixture-reviewed-later-capture}`n"
    [IO.File]::WriteAllText($reviewedFirmwarePath, $reviewedFirmwareText, [Text.UTF8Encoding]::new($false))
    $reviewedFirmwarePin = Get-TestPin $reviewedFirmwarePath

    $auditEntries = @($stagePins | ForEach-Object { [pscustomobject]@{ RelativePath=$_.RelativePath; Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true } })
    $audit = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'
        Esp=[pscustomobject]@{ Identity=[pscustomobject]@{ Volume='fixture-volume'; DiskNumber=7; DiskUniqueId='fixture-disk-id'; PartitionGuid='FIXTURE-PARTITION' }; BootManagerPin=[pscustomobject]@{ Sha256=$bootPin.Sha256; Bytes=$bootPin.Bytes } ; MountStateAfterCleanup='Unmounted' }
        ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true; EntryCount=7; FileCount=7; Entries=$auditEntries; UnknownEntries=@(); UnsafeEntries=@(); Errors=@() }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
    }

    $stageFiles = @($stagePins | Where-Object { $_.Name -notin @('handoff-status.txt','payload-diagnostic.txt') } | ForEach-Object { [pscustomobject]@{ RelativePath=$_.RelativePath; Sha256=$_.Sha256; Bytes=$_.Bytes } })
    $collectedStage = @($stagePins | ForEach-Object { [pscustomobject]@{ Name=$_.Name; Pin=$_.Pin } })
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; SourceCommit='d79806d51187963cd81578b7287076e17558c2e2'; SourceRoot=$root
        RunDirectory=$runDirectory; Phase='Complete'; InProgress=$null; DisarmedBootId='old-boot-id'; RecoveryRunDirectory=$recoveryDirectory
        LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true }
        FinalVerification=[pscustomobject]@{
            ResetBoundaryPassed=$true; SecureBoot=$true; ActivationAttempted=$true
            Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=2; ExpectedCpus=2 }
            Esp=[pscustomobject]@{ MatchesBaseline=$true }
            FirmwareEnumeration=[pscustomobject]@{ TextEqual=$true }; Errors=@()
        }
        Host=[pscustomobject]@{ LogicalCpus=2; MachineId='fixture-machine' }
        Paths=[pscustomobject]@{ LoaderBuild=$buildDirectory }
        Tools=[pscustomobject]@{ Probe=$probePin }
        Backup=[pscustomobject]@{
            EspVolume='fixture-volume'; EspDiskNumber=7; EspPartitionGuid='FIXTURE-PARTITION'; BootManagerSha256=$bootPin.Sha256
            ReferenceLog=$logPin; FirmwareEnumeration=$firmwarePin
        }
        Stage=[pscustomobject]@{ Files=$stageFiles }
        Collection=[pscustomobject]@{ Esp=[pscustomobject]@{ StageFiles=$collectedStage } }
    }
    $selector = [pscustomobject]@{ SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; JournalPath=(Join-Path $runDirectory 'journal.json'); SourceRoot=$root }
    $auditPath = Join-Path $root '.analysis\gpu-baremetal-readiness\7216b61c2c11467f851d6b3192ed5dc6\readiness.json'
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $auditPath))
    Write-TestJson $auditPath $audit
    $journalPath = Join-Path $runDirectory 'journal.json'
    Write-TestJson $journalPath $journal
    $archivePath = Join-Path $archiveDirectory ($runId + '.json')
    Write-TestJson $archivePath $selector
    return @{
        Root=$root; ToolRoot=$tools; RunId=$runId; RunDirectory=$runDirectory; StateRoot=$stateRoot; RecoveryDirectory=$recoveryDirectory
        BuildDirectory=$buildDirectory; EspRoot=$espRoot; StageDirectory=$stageDirectory; BootManagerPath=$bootManagerPath; BootManagerSha256=$bootPin.Sha256
        AuditPath=$auditPath; Audit=$audit; JournalPath=$journalPath; Journal=$journal; ArchivePath=$archivePath; Archive=$selector
        StagePins=$stagePins; ProbePin=$probePin; ReferenceLogPin=$logPin; FirmwareText=[IO.File]::ReadAllText($firmwarePath)
        ReviewedFirmwareText=$reviewedFirmwareText; ReviewedFirmwarePath=$reviewedFirmwarePath
        ReviewedFirmwareRelativePath=$reviewedFirmwareRelativePath; ReviewedFirmwarePin=$reviewedFirmwarePin
    }
}
function New-MockOperations {
    param([hashtable]$Fixture, [hashtable]$Behavior)
    $ops = @{}
    $ops.VerifySource = { param($SourceRoot,$Commit) }
    $ops.AssertPathWithin = { param($Path,$Root,$MustExist)
        $full = [IO.Path]::GetFullPath($Path); $base = [IO.Path]::GetFullPath($Root)
        $prefix = $base.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw "Mock path escaped root: $full" }
        if ($MustExist -and -not (Test-Path -LiteralPath $full)) { throw "Mock path missing: $full" }
        return $full
    }
    $ops.AssertAdministrator = { }
    $ops.GetCurrentSnapshot = { return [pscustomobject]@{ MachineId='fixture-machine'; BootId='new-boot-id'; SecureBoot=$true; LogicalCpus=2 } }
    $ops.AssertSameMachine = { param($State,$Current) }
    $ops.AssertSecureBoot = { param($Current) }
    $ops.AssertBackup = { param($State) return [pscustomobject]@{ RunDirectory=$Fixture.RecoveryDirectory } }.GetNewClosure()
    $ops.MountEsp = { param($Actions) return [pscustomobject]@{ Root=$Fixture.EspRoot; Acquired=$true; ActionDirectory=$Actions; Identity=[pscustomobject]@{ Volume='fixture-volume'; DiskNumber=7; DiskUniqueId='fixture-disk-id'; PartitionGuid='FIXTURE-PARTITION' } } }.GetNewClosure()
    $ops.DismountEsp = { param($Mount) if ($Behavior.DismountFails) { throw 'mock unmount failed' }; $Behavior.UnmountCount++ }.GetNewClosure()
    $ops.AssertMountedEsp = { param($Mount,$Backup) if ($Behavior.WrongEsp) { throw 'mock wrong ESP' } }.GetNewClosure()
    $ops.CaptureFirmware = { param($Actions)
        $Behavior.FirmwareCaptureCount++
        $captureIndex = [int]$Behavior.FirmwareCaptureCount
        if ($Behavior.ContainsKey('FirmwareSequence') -and $Behavior.FirmwareSequence.Count -ge $captureIndex) {
            $text = [string]$Behavior.FirmwareSequence[$captureIndex - 1]
        } elseif ($Behavior.FirmwareChanges -or ($Behavior.FirmwareChangesAt -gt 0 -and $captureIndex -ge $Behavior.FirmwareChangesAt)) {
            $text = 'foreign firmware entry'
        } elseif ($Behavior.ContainsKey('UseReviewedFirmware') -and $Behavior.UseReviewedFirmware) {
            $text = $Fixture.ReviewedFirmwareText
        } else {
            $text = $Fixture.FirmwareText
        }
        return [pscustomobject]@{ Text=$text; Pin=$null; ReceiptPath='mock-bcdedit.receipt.json' }
    }.GetNewClosure()
    $ops.CaptureHealth = { param($State,$Actions)
        $passed = -not ($Behavior.ContainsKey('HealthFails') -and $Behavior.HealthFails)
        return [pscustomobject]@{ CommandSucceeded=$true; Verdict=[pscustomobject]@{ Passed=$passed; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=2; ExpectedCpus=2 }; ReceiptPath='mock-health.receipt.json' }
    }.GetNewClosure()
    $ops.CaptureBootSelection = { param($State,$Actions,$Checkpoint)
        $Behavior.BootCaptureCount++
        if ($Behavior.BootNextPresent) { throw 'BootNext is set; refusing stage retirement.' }
        if ($Behavior.BootReadFails) { throw 'mock raw firmware read failed' }
        $order = if ($Behavior.BootOrderChanges -and $Behavior.BootCaptureCount -gt 1) { [uint16[]]@(0x1234,0x2222) } else { [uint16[]]@(0x1234,0x5678) }
        return [pscustomobject]@{ Baseline=[pscustomobject]@{ BootNext=$null; BootOrder=$order; BootManagerSha256=$Fixture.BootManagerSha256.ToUpperInvariant(); ReferenceLogSha256=$Fixture.ReferenceLogPin.Sha256.ToUpperInvariant(); IntegrityFindings='00000000' }; ReceiptPath='mock-ibaudit.receipt.json' }
    }.GetNewClosure()
    $ops.CopyFile = { param($Source,$Destination,$Pin)
        $Behavior.CopyCount++
        if ($Behavior.FailCopyAt -gt 0 -and $Behavior.CopyCount -eq $Behavior.FailCopyAt) { throw 'mock external-copy failure' }
        [IO.File]::Copy($Source,$Destination,$false)
        $copy = Get-TestPin $Destination; $sourcePin = Get-TestPin $Source
        if ($copy.Sha256 -cne $Pin.Sha256 -or $sourcePin.Sha256 -cne $Pin.Sha256) { throw 'mock copy verification failed' }
        return [pscustomobject]@{ Name=[IO.Path]::GetFileName($Source); Source=$sourcePin; Copy=$copy }
    }.GetNewClosure()
    $ops.DeleteFile = { param($Path)
        $Behavior.DeleteCount++
        [IO.File]::Delete($Path)
        if ($Behavior.FailDeleteAt -gt 0 -and $Behavior.DeleteCount -eq $Behavior.FailDeleteAt) { throw 'mock partial deletion failure' }
    }.GetNewClosure()
    return $ops
}
function Invoke-FixtureRetirement {
    param([hashtable]$Fixture, [hashtable]$Operations, [switch]$Apply, [hashtable]$Pins)
    $parameters = @{
        ToolRoot=$Fixture.ToolRoot; WorkspaceRoot=$Fixture.Root; Operations=$Operations; ExpectedRunId=$Fixture.RunId
        ExpectedAuditSha256=(Get-TestPin $Fixture.AuditPath).Sha256; ExpectedAuditBytes=(Get-TestPin $Fixture.AuditPath).Bytes
        ExpectedJournalSha256=(Get-TestPin $Fixture.JournalPath).Sha256; ExpectedJournalBytes=(Get-TestPin $Fixture.JournalPath).Bytes
        ExpectedArchiveSha256=(Get-TestPin $Fixture.ArchivePath).Sha256; ExpectedArchiveBytes=(Get-TestPin $Fixture.ArchivePath).Bytes
        ExpectedProbeSha256=$Fixture.ProbePin.Sha256; ExpectedProbeBytes=$Fixture.ProbePin.Bytes
        ExpectedAuditToolSha256=(Get-TestPin (Join-Path $Fixture.BuildDirectory 'ibaudit.exe')).Sha256
        ExpectedAuditToolBytes=(Get-TestPin (Join-Path $Fixture.BuildDirectory 'ibaudit.exe')).Bytes
        ExpectedAuditToolRelativePath='build\loader\ibaudit.exe'
        ExpectedReferenceLogSha256=$Fixture.ReferenceLogPin.Sha256; ExpectedReferenceLogBytes=$Fixture.ReferenceLogPin.Bytes
        ExpectedReviewedFirmwareRelativePath=$Fixture.ReviewedFirmwareRelativePath
        ExpectedReviewedFirmwareSha256=$Fixture.ReviewedFirmwarePin.Sha256
        ExpectedReviewedFirmwareBytes=$Fixture.ReviewedFirmwarePin.Bytes
    }
    if ($Apply) { $parameters.Apply = $true }
    if ($null -ne $Pins) { foreach ($key in $Pins.Keys) { $parameters[$key]=$Pins[$key] } }
    return & $retirementModule { param($p) Invoke-GpuStageRetirement @p } $parameters
}

try {
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $fixture = New-TestFixture
    $auditPin = Get-TestPin $fixture.AuditPath
    $journalPin = Get-TestPin $fixture.JournalPath
    $archivePin = Get-TestPin $fixture.ArchivePath
    $stageSpec = @(Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $fixture.Journal -ArchivedSelector $fixture.Archive `
        -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory `
        -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root)

    Invoke-TestCase 'immutable production profile pins the reviewed seven-file run' {
        $profile = & $retirementModule {
            [pscustomobject]@{
                RunId=$script:GpuRetirementRunId; SourceCommit=$script:GpuRetirementSourceCommit
                AuditRelativePath=$script:GpuRetirementAuditRelativePath; AuditSha256=$script:GpuRetirementAuditSha256; AuditBytes=$script:GpuRetirementAuditBytes
                JournalSha256=$script:GpuRetirementJournalSha256; JournalBytes=$script:GpuRetirementJournalBytes
                ArchiveSha256=$script:GpuRetirementArchiveSha256; ArchiveBytes=$script:GpuRetirementArchiveBytes
                ProbeSha256=$script:GpuRetirementProbeSha256; ProbeBytes=$script:GpuRetirementProbeBytes
                ReferenceLogSha256=$script:GpuRetirementReferenceLogSha256; ReferenceLogBytes=$script:GpuRetirementReferenceLogBytes
                ReviewedFirmwareRelativePath=$script:GpuRetirementReviewedFirmwareRelativePath
                ReviewedFirmwareSha256=$script:GpuRetirementReviewedFirmwareSha256
                ReviewedFirmwareBytes=$script:GpuRetirementReviewedFirmwareBytes
                AuditToolSha256=$script:GpuRetirementAuditToolSha256; AuditToolBytes=$script:GpuRetirementAuditToolBytes
                Names=@($script:GpuRetirementNames)
            }
        }
        Assert-True ($profile.RunId -ceq 'gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1') 'Immutable profile points at the wrong run.'
        Assert-True ($profile.SourceCommit -ceq 'd79806d51187963cd81578b7287076e17558c2e2') 'Immutable profile points at the wrong source commit.'
        Assert-True ($profile.AuditRelativePath -ceq '.analysis\gpu-baremetal-readiness\7216b61c2c11467f851d6b3192ed5dc6\readiness.json' -and $profile.AuditSha256 -ceq '05D4B6FCB5831CAC81A3E1C77EC7B4E852F0AA11C3FCA83C837676577C2F86C0' -and $profile.AuditBytes -eq 12050) 'Immutable profile audit pin differs from reviewed readiness evidence.'
        Assert-True ($profile.JournalSha256 -ceq 'F53925C27953EA695BE0DCA94105611571D08F7877AABFB95F00513AAF6F46C9' -and $profile.JournalBytes -eq 44878) 'Immutable profile journal pin differs from reviewed run evidence.'
        Assert-True ($profile.ArchiveSha256 -ceq 'ABC00DBDFFC570B1EC78255426F3145CC12438DE023CFD5877D9B6248F363CA7' -and $profile.ArchiveBytes -eq 533) 'Immutable profile archive pin differs from the reviewed selector.'
        Assert-True ($profile.ProbeSha256 -ceq '49CCB02124E6536F4F107B83C4E79D31C5DA71993585DFD48139726F11C79D5E' -and $profile.ProbeBytes -eq 172544) 'Immutable profile probe pin differs from the reviewed build.'
        Assert-True ($profile.ReferenceLogSha256 -ceq '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447' -and $profile.ReferenceLogBytes -eq 40838) 'Immutable profile reference-log pin differs from the reviewed backup.'
        Assert-True ($profile.ReviewedFirmwareRelativePath -ceq '.analysis\gpu-stage-retirement\0851536237dd4f6a9ff5df72b0c36848\observations\stage-retirement-firmware-214f7cdb11a647beb09fd271978cca83.stdout.txt' -and $profile.ReviewedFirmwareSha256 -ceq 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' -and $profile.ReviewedFirmwareBytes -eq 4336) 'Immutable profile later firmware capture pin differs from reviewed evidence.'
        Assert-True ($profile.AuditToolSha256 -ceq 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' -and $profile.AuditToolBytes -eq 246784) 'Immutable profile audit-tool pin changed unexpectedly.'
        Assert-True ($profile.Names.Count -eq 7 -and $profile.Names -contains 'payload-diagnostic.txt') 'Immutable profile must include the retained payload diagnostic.'
    }
    Invoke-TestCase 'complete reviewed evidence yields seven immutable file pins' {
        Assert-True ($stageSpec.Count -eq 7 -and @($stageSpec | Where-Object Name -eq 'handoff-status.txt').Count -eq 1 -and @($stageSpec | Where-Object Name -eq 'payload-diagnostic.txt').Count -eq 1) 'Expected exactly seven reviewed files including handoff status and payload diagnostic.'
    }
    Invoke-TestCase 'schema 1 journal uses RunDirectory and selector JournalPath remains mandatory' {
        $savedJournal = Get-Content -LiteralPath $fixture.JournalPath -Raw | ConvertFrom-Json
        $savedSelector = Get-Content -LiteralPath $fixture.ArchivePath -Raw | ConvertFrom-Json
        Assert-True ($null -eq $savedJournal.PSObject.Properties['JournalPath']) 'Fixture invented a JournalPath field absent from schema 1 journals.'
        Assert-True ($savedSelector.JournalPath -ceq $fixture.JournalPath) 'Selector must record the journal path.'
        $validation = @{
            Audit=$fixture.Audit; Journal=$savedJournal; ArchivedSelector=$savedSelector
            ExpectedAuditSha256=$auditPin.Sha256; ExpectedRunId=$fixture.RunId
            ExpectedRunDirectory=$fixture.RunDirectory; ExpectedJournalPath=$fixture.JournalPath; ExpectedSourceRoot=$fixture.Root
        }
        $accepted = @(Assert-GpuStageRetirementEvidence @validation)
        Assert-True ($accepted.Count -eq 7) 'Valid serialized schema 1 evidence was rejected.'
        $validation.ExpectedJournalPath = Join-Path $fixture.RunDirectory 'wrong-journal.json'
        Assert-Throws { Assert-GpuStageRetirementEvidence @validation } 'Journal path' 'A journal path outside its recorded run directory was accepted.'
        $validation.ExpectedJournalPath = $fixture.JournalPath
        $savedSelector.JournalPath = Join-Path $fixture.RunDirectory 'wrong-journal.json'
        Assert-Throws { Assert-GpuStageRetirementEvidence @validation } 'Archived selector journal path' 'A selector pointing at another journal was accepted.'
        $savedSelector.PSObject.Properties.Remove('JournalPath')
        Assert-Throws { Assert-GpuStageRetirementEvidence @validation } 'JournalPath' 'A selector without JournalPath was accepted.'
    }
    Invoke-TestCase 'audit rejects an added unknown entry' {
        $bad = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
        $bad.ExistingStage.Entries += [pscustomobject]@{ RelativePath='EFI\insecure-boot\foreign.bin'; Kind='File'; Bytes=1; Sha256=('A'*64); Hashed=$true }
        $bad.ExistingStage.EntryCount++ ; $bad.ExistingStage.FileCount++
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $bad -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'complete|unknown|exactly seven' 'Unknown audit entry was accepted.'
    }
    Invoke-TestCase 'audit rejects missing, directory, and un-hashed entries' {
        foreach ($mutation in @('missing','directory','unhashed')) {
            $bad = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
            if ($mutation -eq 'missing') { $bad.ExistingStage.Entries=@($bad.ExistingStage.Entries | Select-Object -First 5); $bad.ExistingStage.EntryCount=5; $bad.ExistingStage.FileCount=5 }
            if ($mutation -eq 'directory') { $bad.ExistingStage.Entries[0].Kind='Directory' }
            if ($mutation -eq 'unhashed') { $bad.ExistingStage.Entries[0].Hashed=$false }
            Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $bad -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } '.' "Audit $mutation mutation was accepted."
        }
    }
    Invoke-TestCase 'payload diagnostic is mandatory in the seven-file audit and collection' {
        $badAudit = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
        $badAudit.ExistingStage.Entries = @($badAudit.ExistingStage.Entries | Where-Object RelativePath -ne 'EFI\insecure-boot\payload-diagnostic.txt')
        $badAudit.ExistingStage.EntryCount = 6
        $badAudit.ExistingStage.FileCount = 6
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $badAudit -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'seven-file|exactly seven|missing payload-diagnostic' 'Audit without payload diagnostic was accepted.'

        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
        $badJournal.Collection.Esp.StageFiles = @($badJournal.Collection.Esp.StageFiles | Where-Object Name -ne 'payload-diagnostic.txt')
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'exactly seven|missing payload-diagnostic' 'Collection without payload diagnostic was accepted.'
    }
    Invoke-TestCase 'tampered collected payload diagnostic pin is rejected' {
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
        $diagnostic = @($badJournal.Collection.Esp.StageFiles | Where-Object Name -ceq 'payload-diagnostic.txt')[0]
        $diagnostic.Pin.Sha256 = 'A' * 64
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'Collected stage pin differs' 'Tampered payload diagnostic pin was accepted.'
    }
    Invoke-TestCase 'payload diagnostic cannot masquerade as an installer-staged artifact' {
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
        $auditDiagnostic = @($fixture.Audit.ExistingStage.Entries | Where-Object RelativePath -ceq 'EFI\insecure-boot\payload-diagnostic.txt')[0]
        $badJournal.Stage.Files = @($badJournal.Stage.Files | Where-Object RelativePath -ne 'EFI\insecure-boot\tcglog.ib')
        $badJournal.Stage.Files += [pscustomobject]@{ RelativePath=$auditDiagnostic.RelativePath; Sha256=$auditDiagnostic.Sha256; Bytes=$auditDiagnostic.Bytes }
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'unexpected staged file' 'Collected diagnostic was accepted as a staged installer artifact.'
        Assert-True (@($fixture.Journal.Stage.Files | Where-Object RelativePath -eq 'EFI\insecure-boot\payload-diagnostic.txt').Count -eq 0) 'Valid fixture incorrectly records payload diagnostic in installer Stage.Files.'
    }
    Invoke-TestCase 'evidence rejects incomplete run, failed reset, and wrong archived selector' {
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badJournal.Phase='Staged'
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'complete|action' 'Incomplete run was accepted.'
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badJournal.FinalVerification.ResetBoundaryPassed=$false
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'Final verification' 'Failed reset boundary was accepted.'
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badJournal.RunId='gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7'
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'fixed reviewed retirement target' 'Journal from the prior six-file run was accepted.'
        $badArchive = ($fixture.Archive | ConvertTo-Json -Depth 10 | ConvertFrom-Json); $badArchive.RunId='other-run'
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $fixture.Journal -ArchivedSelector $badArchive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'archived selector' 'Wrong archived run was accepted.'
        $badArchive = ($fixture.Archive | ConvertTo-Json -Depth 10 | ConvertFrom-Json); $badArchive.RunId='gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7'
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $fixture.Journal -ArchivedSelector $badArchive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'archived selector' 'Archived selector from the prior six-file run was accepted.'
        $oldJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $oldJournal.RunId='gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7'
        $oldArchive = ($fixture.Archive | ConvertTo-Json -Depth 10 | ConvertFrom-Json); $oldArchive.RunId=$oldJournal.RunId
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $oldJournal -ArchivedSelector $oldArchive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $oldJournal.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'fixed reviewed retirement target' 'The prior run was accepted when its journal and selector agreed.'
    }
    Invoke-TestCase 'ESP and boot-manager evidence must match the archived backup' {
        $badAudit = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badAudit.Esp.Identity.DiskNumber++
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $badAudit -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'ESP disk|boot-manager' 'Wrong ESP disk was accepted.'
        $badAudit = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badAudit.Esp.BootManagerPin.Sha256=('B'*64)
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $badAudit -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'boot-manager' 'Wrong boot-manager pin was accepted.'
    }
    Invoke-TestCase 'live inventory refuses missing, extra, directory, and modified files' {
        $valid = Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory
        Assert-True ($valid.Count -eq 7 -and $valid.ContainsKey('payload-diagnostic.txt')) 'Expected seven live stage files including payload diagnostic.'
        [IO.File]::Delete((Join-Path $fixture.StageDirectory 'shimx64.efi'))
        Assert-Throws { Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory } 'exactly seven|missing' 'Missing live file was accepted.'
        [IO.File]::WriteAllText((Join-Path $fixture.StageDirectory 'shimx64.efi'),'restored')
        [IO.File]::Delete((Join-Path $fixture.StageDirectory 'payload-diagnostic.txt'))
        Assert-Throws { Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory } 'exactly seven|missing payload-diagnostic' 'Missing live payload diagnostic was accepted.'
        [IO.File]::WriteAllText((Join-Path $fixture.StageDirectory 'payload-diagnostic.txt'), "reviewed stage file payload-diagnostic.txt`n", [Text.UTF8Encoding]::new($false))
        [void][IO.Directory]::CreateDirectory((Join-Path $fixture.StageDirectory 'foreign'))
        Assert-Throws { Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory } 'directory|exactly seven' 'Directory entry was accepted.'
        [IO.Directory]::Delete((Join-Path $fixture.StageDirectory 'foreign'))
        $live=Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory
        Assert-Throws { Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $stageSpec } 'differs' 'Modified live file was accepted.'
        [IO.File]::WriteAllText((Join-Path $fixture.StageDirectory 'shimx64.efi'), "reviewed stage file shimx64.efi`n", [Text.UTF8Encoding]::new($false))
        $live=Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory
        Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $stageSpec
        [IO.File]::WriteAllText((Join-Path $fixture.StageDirectory 'payload-diagnostic.txt'),'tampered diagnostic')
        $live=Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory
        Assert-Throws { Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $stageSpec } 'differs' 'Tampered live payload diagnostic was accepted.'
    }
    Invoke-TestCase 'raw boot selection rejects BootNext, empty order, changed order, bad integrity, and command failure' {
        $baseline=[pscustomobject]@{schema=1;kind='boot-selection';findings=0;boot_next=$null;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
        $integrity="BootmgfwSha256=$($fixture.BootManagerSha256) Findings=0x00000000"
        $valid=& $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $baseline $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256
        Assert-True ($valid.BootOrder.Count -eq 2) 'Valid raw boot selection was not parsed.'
        foreach ($bad in @(
            [pscustomobject]@{schema=1;kind='boot-selection';findings=0;boot_next=42;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256},
            [pscustomobject]@{schema=1;kind='boot-selection';findings=0;boot_next=$null;boot_order=@();bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256},
            [pscustomobject]@{schema=1;kind='baseline';findings=0;boot_next=$null;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256},
            [pscustomobject]@{schema=1;kind='boot-selection';findings=64;boot_next=$null;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
        )) { Assert-Throws { & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $bad $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 } '.' 'Bad BootNext/BootOrder was accepted.' }
        $changed=[pscustomobject]@{ BootNext=$null;BootOrder=[uint16[]]@(1,3);BootManagerSha256=$valid.BootManagerSha256;ReferenceLogSha256=$valid.ReferenceLogSha256;IntegrityFindings='00000000' }
        Assert-Throws { & $retirementModule { param($b,$c) Assert-GpuStageRetirementBootSelectionStable -Before $b -Current $c -Checkpoint 'test' } $valid $changed } 'BootOrder changed' 'Changed BootOrder was accepted.'
        Assert-Throws { & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $baseline $integrity.Replace('00000000','00000001') $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 } 'integrity|findings' 'Nonzero integrity finding was accepted.'
        Assert-Throws { & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $false } $baseline $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 } 'failed to read' 'Failed raw read was accepted.'
    }
    Invoke-TestCase 'production raw-capture callback resolves its own helpers and invokes only the injected native runner' {
        $nativeRoot=Join-Path $fixtureRoot 'native-callback'; [void][IO.Directory]::CreateDirectory($nativeRoot)
        $state=[pscustomobject]@{ Paths=[pscustomobject]@{LoaderBuild=$fixture.BuildDirectory}; Backup=[pscustomobject]@{BootManagerSha256=$fixture.BootManagerSha256;ReferenceLog=$fixture.ReferenceLogPin} }
        $nativeCalls=[Collections.Generic.List[string]]::new()
        $native={ param($Program,[string[]]$Arguments,$ActionDirectory,$Label)
            $nativeCalls.Add($Arguments[0])
            if ($Arguments[0] -eq 'boot-selection') {
                if ($Arguments.Count -ne 9 -or $Arguments[7] -cne '--expected-hash' -or $Arguments[8] -cne $fixture.BootManagerSha256) { throw 'Focused command did not receive the expected boot-manager hash.' }
                $out=$Arguments[4]; $payload=[pscustomobject]@{schema=1;kind='boot-selection';findings=0;boot_next=$null;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
                Write-TestJson $out $payload
                $stdout=Join-Path $ActionDirectory ($Label+'.stdout'); $stderr=Join-Path $ActionDirectory ($Label+'.stderr'); [IO.File]::WriteAllText($stdout,'');[IO.File]::WriteAllText($stderr,'')
            } elseif ($Arguments[0] -eq 'bootmgr-integrity') {
                $stdout=Join-Path $ActionDirectory ($Label+'.stdout'); $stderr=Join-Path $ActionDirectory ($Label+'.stderr')
                [IO.File]::WriteAllText($stdout,"BootmgfwSha256=$($fixture.BootManagerSha256) Findings=0x00000000`n");[IO.File]::WriteAllText($stderr,'')
            } else { throw "Unexpected audit command: $($Arguments[0])" }
            return [pscustomobject]@{Succeeded=$true;StdoutPath=$stdout;StderrPath=$stderr;ReceiptPath=($Label+'.receipt')}
        }.GetNewClosure()
        $operationArgs=@{ToolRoot=$ToolRoot;AuditToolPath=(Join-Path $fixture.BuildDirectory 'ibaudit.exe');NativeCommand=$native;ExpectedAuditToolSha256=(Get-TestPin (Join-Path $fixture.BuildDirectory 'ibaudit.exe')).Sha256;ExpectedAuditToolBytes=(Get-TestPin (Join-Path $fixture.BuildDirectory 'ibaudit.exe')).Bytes;ExpectedReferenceLogSha256=$fixture.ReferenceLogPin.Sha256;ExpectedReferenceLogBytes=$fixture.ReferenceLogPin.Bytes}
        $productionOps=& $retirementModule { param($a) New-GpuStageRetirementPlatformOperations @a } $operationArgs
        $capture=& $productionOps.CaptureBootSelection $state $nativeRoot 'before'
        Assert-True ($capture.Baseline.BootOrder.Count -eq 2 -and $capture.Baseline.IntegrityFindings -ceq '00000000') 'Production capture callback failed to parse raw baseline or integrity output.'
        Assert-True (($nativeCalls -join ',') -ceq 'boot-selection,bootmgr-integrity') 'Retirement invoked a full TPM/MOK baseline or skipped integrity validation.'
        $workingNative=$operationArgs.NativeCommand
        $operationArgs.NativeCommand={ param($Program,$Arguments,$ActionDirectory,$Label)
            if ($Arguments[0] -eq 'boot-selection') { return & $workingNative $Program $Arguments $ActionDirectory $Label }
            return [pscustomobject]@{Succeeded=$false;ReceiptPath='integrity-denied.receipt.json';StderrPath='integrity-denied.stderr.txt'}
        }.GetNewClosure()
        $integrityDeniedOps=& $retirementModule { param($a) New-GpuStageRetirementPlatformOperations @a } $operationArgs
        Assert-Throws { & $integrityDeniedOps.CaptureBootSelection $state $nativeRoot 'integrity-denied' } 'integrity failed.*integrity-denied.receipt.json.*integrity-denied.stderr.txt' 'Integrity failure did not expose its evidence paths.'
        $operationArgs.NativeCommand={ param($Program,$Arguments,$ActionDirectory,$Label)
            $result=& $workingNative $Program $Arguments $ActionDirectory $Label
            if ($Arguments[0] -eq 'boot-selection') { $payload=Get-Content -LiteralPath $Arguments[4] -Raw | ConvertFrom-Json; $payload.boot_next=42; Write-TestJson $Arguments[4] $payload }
            return $result
        }.GetNewClosure()
        $setNextOps=& $retirementModule { param($a) New-GpuStageRetirementPlatformOperations @a } $operationArgs
        Assert-Throws { & $setNextOps.CaptureBootSelection $state $nativeRoot 'set-next' } 'BootNext is set.*Capture:.*set-next-boot-selection.json.*Integrity evidence:' 'Rejected capture did not preserve discoverable evidence paths.'
        $operationArgs.NativeCommand={ param($Program,$Arguments,$ActionDirectory,$Label) [pscustomobject]@{Succeeded=$false;ReceiptPath='denied.receipt.json';StderrPath='denied.stderr.txt'} }
        $deniedOps=& $retirementModule { param($a) New-GpuStageRetirementPlatformOperations @a } $operationArgs
        Assert-Throws { & $deniedOps.CaptureBootSelection $state $nativeRoot 'denied' } 'boot-selection failed.*denied.receipt.json.*denied.stderr.txt' 'Native read failure did not expose its evidence paths.'
    }
    Invoke-TestCase 'focused capture requires every field and an integer zero finding count' {
        $valid=[pscustomobject]@{schema=1;kind='boot-selection';findings=0;boot_next=$null;boot_order=@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
        $integrity="BootmgfwSha256=$($fixture.BootManagerSha256) Findings=0x00000000"
        $check={ param($b) & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $b $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 }
        foreach ($name in @('schema','kind','findings','boot_next','boot_order','bootmgfw_sha256','log_sha256')) {
            $bad=$valid | ConvertTo-Json | ConvertFrom-Json
            $bad.PSObject.Properties.Remove($name)
            Assert-Throws { & $check $bad } ('missing.*'+$name) "Missing $name was accepted as an observation."
        }
        foreach ($findings in @('0',$false,$null)) {
            $bad=$valid | ConvertTo-Json | ConvertFrom-Json; $bad.findings=$findings
            Assert-Throws { & $check $bad } 'unsupported|failed boot-selection' 'Malformed findings were coerced to zero.'
        }
    }
    Invoke-TestCase 'production copy callback verifies ordinary fixture files and refuses overwrites or changed sources' {
        $copyRoot=Join-Path $fixtureRoot 'copy-callback'; [void][IO.Directory]::CreateDirectory($copyRoot)
        $source=Join-Path $copyRoot 'source.bin'; $destination=Join-Path $copyRoot 'copy.bin'
        [IO.File]::WriteAllBytes($source, [byte[]]@(0,1,2,127,128,255))
        $pin=Get-TestPin $source
        $productionOps=& $retirementModule { param($root) New-GpuStageRetirementPlatformOperations -ToolRoot $root } $ToolRoot
        $copy=& $productionOps.CopyFile $source $destination $pin
        Assert-True ($copy.Copy.Sha256 -ceq $pin.Sha256 -and $copy.Copy.Bytes -eq $pin.Bytes -and $copy.Source.Sha256 -ceq $pin.Sha256) 'Production copy did not preserve and verify fixture bytes.'
        Assert-Throws { & $productionOps.CopyFile $source $destination $pin } 'already exists' 'Production copy overwrote an existing destination.'
        [IO.File]::WriteAllBytes($source, [byte[]]@(9,8,7))
        $blocked=Join-Path $copyRoot 'blocked.bin'
        Assert-Throws { & $productionOps.CopyFile $source $blocked $pin } 'Source changed' 'Production copy accepted a changed source.'
        Assert-True (-not [IO.File]::Exists($blocked)) 'Changed source created a destination.'
    }
    Invoke-TestCase 'default review creates verified local and external copies without deleting ESP files' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $ops=New-MockOperations $fixture $behavior
        $result=Invoke-FixtureRetirement $fixture $ops
        Assert-True ($result.Status -ceq 'REVIEWED_STAGE_READY_FOR_RETIREMENT' -and $result.Succeeded -and $result.Copies.Count -eq 7 -and $behavior.DeleteCount -eq 0 -and $result.EspUnmounted) "Review did not preserve stage, create seven copies, and unmount. status=$($result.Status) succeeded=$($result.Succeeded) copies=$(@($result.Copies).Count) deleted=$($behavior.DeleteCount) unmounted=$($result.EspUnmounted) errors=$($result.Errors -join '; ') cleanup=$($result.CleanupErrors -join '; ')"
        Assert-True ($result.FirmwareBaselineSource -ceq 'ARCHIVED_BACKUP' -and $result.OriginalFirmwareBaselineEqual -and $result.FirmwareBaselineEqual) 'Exact archived firmware text did not remain the chosen baseline.'
        Assert-True ($result.OriginalFirmwareBaselinePin.Sha256 -ceq $fixture.Journal.Backup.FirmwareEnumeration.Sha256 -and $null -eq $result.ReviewedFirmwareBaselinePin) 'Archived baseline provenance is incorrect.'
        foreach ($copy in $result.Copies) { Assert-True ((Get-TestPin $copy.Local.Path).Sha256 -ceq $copy.Source.Sha256 -and (Get-TestPin $copy.External.Path).Sha256 -ceq $copy.Source.Sha256) "Review copy mismatch: $($copy.Name)" }
    }
    Invoke-TestCase 'exact pinned later firmware capture is accepted for review and Apply' {
        $fixture=New-TestFixture
        $reviewBehavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false;UseReviewedFirmware=$true}
        $review=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $reviewBehavior)
        Assert-True ($review.Succeeded -and $review.Status -ceq 'REVIEWED_STAGE_READY_FOR_RETIREMENT' -and $review.FirmwareBaselineSource -ceq 'REVIEWED_LATER_CAPTURE' -and -not $review.OriginalFirmwareBaselineEqual -and $review.FirmwareBaselineEqual) 'Exact pinned later capture was not accepted as a stable review baseline.'
        Assert-True ($review.ReviewedFirmwareBaselinePin.Sha256 -ceq $fixture.ReviewedFirmwarePin.Sha256 -and $review.ReviewedFirmwareBaselinePin.Bytes -eq $fixture.ReviewedFirmwarePin.Bytes -and [IO.Path]::GetFullPath($review.ReviewedFirmwareBaselinePin.Path) -ceq [IO.Path]::GetFullPath($fixture.ReviewedFirmwarePath)) 'Review did not record the exact approved later capture pin.'
        Assert-True ($review.Copies.Count -eq 7 -and $reviewBehavior.DeleteCount -eq 0) 'Later-capture review did not preserve the seven-file stage.'

        $applyBehavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false;UseReviewedFirmware=$true}
        $applied=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $applyBehavior) -Apply
        Assert-True ($applied.Succeeded -and $applied.Status -ceq 'STAGE_RETIRED' -and $applied.FirmwareBaselineSource -ceq 'REVIEWED_LATER_CAPTURE' -and -not $applied.OriginalFirmwareBaselineEqual -and $applied.FirmwareBaselineEqual) 'Apply did not use the same exact pinned later-capture baseline.'
        Assert-True ($applied.RemovedFiles.Count -eq 7 -and $applied.Copies.Count -eq 7 -and $applyBehavior.DeleteCount -eq 7 -and -not (Test-Path -LiteralPath $fixture.StageDirectory)) 'Apply did not copy then remove exactly the seven reviewed files.'
    }
    Invoke-TestCase 'invalid later firmware captures and baseline drift fail before deletion' {
        foreach ($failure in @('tampered-capture','missing-capture','different-text','baseline-drift','health-failure','bootnext','boot-read-failure','boot-order-change')) {
            $fixture=New-TestFixture
            $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false;UseReviewedFirmware=$true}
            if ($failure -eq 'tampered-capture') { [IO.File]::AppendAllText($fixture.ReviewedFirmwarePath,'tampered') }
            if ($failure -eq 'missing-capture') { [IO.File]::Delete($fixture.ReviewedFirmwarePath) }
            if ($failure -eq 'different-text') { $behavior.FirmwareSequence=@('different current firmware text') }
            if ($failure -eq 'baseline-drift') { $behavior.FirmwareSequence=@($fixture.ReviewedFirmwareText,$fixture.FirmwareText) }
            if ($failure -eq 'health-failure') { $behavior.HealthFails=$true }
            if ($failure -eq 'bootnext') { $behavior.BootNextPresent=$true }
            if ($failure -eq 'boot-read-failure') { $behavior.BootReadFails=$true }
            if ($failure -eq 'boot-order-change') { $behavior.BootOrderChanges=$true }

            $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
            Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and -not $result.Succeeded -and $behavior.DeleteCount -eq 0 -and (Test-Path -LiteralPath $fixture.StageDirectory)) "$failure did not fail closed before ESP deletion. errors=$($result.Errors -join '; ')"
            if ($failure -eq 'baseline-drift') { Assert-True ($behavior.FirmwareCaptureCount -ge 2 -and $result.FirmwareBaselineSource -ceq 'REVIEWED_LATER_CAPTURE' -and ($result.Errors -join '; ') -match 'immediately before stage removal') 'Checkpoint drift switched to another baseline or was not rejected.' }
            if ($failure -eq 'health-failure') { Assert-True ($behavior.BootCaptureCount -eq 0) 'A later firmware baseline bypassed the ABSENT health gate.' }
            if ($failure -eq 'bootnext') { Assert-True ($behavior.BootCaptureCount -eq 1) 'A later firmware baseline bypassed raw BootNext ownership checks.' }
            if ($failure -eq 'boot-read-failure') { Assert-True ($behavior.BootCaptureCount -eq 1) 'A later firmware baseline bypassed the raw boot-selection read gate.' }
            if ($failure -eq 'boot-order-change') { Assert-True ($behavior.BootCaptureCount -eq 2) 'A later firmware baseline bypassed the raw BootOrder stability checkpoint.' }
        }
    }
    Invoke-TestCase 'Apply removes only the seven pinned files after evidence copies and all gates pass' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIRED' -and $result.Succeeded -and $result.RemovedFiles.Count -eq 7 -and $result.EspFilesChanged -and -not (Test-Path -LiteralPath $fixture.StageDirectory) -and (Test-Path -LiteralPath $fixture.BootManagerPath) -and $result.EspUnmounted) 'Apply did not retire exactly the stage or preserve the boot manager.'
    }
    Invoke-TestCase 'external evidence-copy failure blocks before any ESP deletion' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=2;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and $behavior.DeleteCount -eq 0 -and (Test-Path -LiteralPath $fixture.StageDirectory)) 'Copy failure did not fail closed before deletion.'
    }
    Invoke-TestCase 'replacement audit image pin mismatch blocks before host inspection or ESP mount' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $ops=New-MockOperations $fixture $behavior
        $ops.AssertAdministrator={ throw 'Host inspection must not be reached for a bad audit image.' }
        $ops.MountEsp={ throw 'ESP mount must not be reached for a bad audit image.' }
        $path=Join-Path $fixture.BuildDirectory 'ibaudit.exe'; $pin=Get-TestPin $path
        [IO.File]::AppendAllText($path, 'changed')
        $result=Invoke-FixtureRetirement $fixture $ops -Pins @{ExpectedAuditToolSha256=$pin.Sha256;ExpectedAuditToolBytes=$pin.Bytes}
        Assert-True (-not $result.Succeeded -and ($result.Errors -join '; ') -match 'read-only retirement ibaudit.exe differs' -and $behavior.DeleteCount -eq 0 -and $null -eq $result.LocalReceiptDirectory) 'Changed audit image reached host inspection or a receipt/mutation step.'
    }
    Invoke-TestCase 'partial delete remains failed, keeps verified copies, and cannot resume on a later apply' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=2;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and $result.RemovedFiles.Count -eq 2 -and $result.Copies.Count -eq 7 -and (Test-Path -LiteralPath $fixture.StageDirectory)) 'Partial deletion was hidden or copies were lost.'
        $before=$behavior.DeleteCount
        $retry=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($retry.Status -ceq 'STAGE_RETIREMENT_FAILED' -and $behavior.DeleteCount -eq $before) 'A later Apply resumed against a missing stage file.'
    }
    Invoke-TestCase 'firmware drift and raw ownership failures block before deletion' {
        foreach ($failure in @('FirmwareChanges','PreDeleteFirmwareChanges','BootNextPresent','BootReadFails','BootOrderChanges','WrongEsp')) {
            $fixture=New-TestFixture
            $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
            if ($failure -eq 'PreDeleteFirmwareChanges') { $behavior.FirmwareChangesAt=2 } else { $behavior[$failure]=$true }
            $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
            Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and $behavior.DeleteCount -eq 0 -and (Test-Path -LiteralPath $fixture.StageDirectory)) "$failure did not block before deletion."
        }
    }
    Invoke-TestCase 'ESP unmount failure changes a successful removal result to failure' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$true;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and -not $result.Succeeded -and $result.CleanupErrors.Count -eq 1) 'Unmount failure was not surfaced as a failed retirement.'
    }
    Invoke-TestCase 'production evidence-pin overrides require explicit mock operations' {
        Assert-Throws { & $retirementModule { param($root) Invoke-GpuStageRetirement -ToolRoot (Join-Path $root 'tools\gpu_diagnostic') -WorkspaceRoot $root -ExpectedAuditSha256 ('0'*64) } $fixture.Root } 'immutable|mock operations' 'Production pin override was accepted.'
        Assert-Throws { & $retirementModule { param($root) Invoke-GpuStageRetirement -ToolRoot (Join-Path $root 'tools\gpu_diagnostic') -WorkspaceRoot $root -ExpectedAuditToolRelativePath 'build\loader\ibaudit.exe' } $fixture.Root } 'immutable|mock operations' 'Production audit tool path override was accepted.'
        Assert-Throws { & $retirementModule { param($root) Invoke-GpuStageRetirement -ToolRoot (Join-Path $root 'tools\gpu_diagnostic') -WorkspaceRoot $root -ExpectedReviewedFirmwareRelativePath 'reviewed-firmware.txt' } $fixture.Root } 'immutable|mock operations' 'Production later firmware capture override was accepted.'
    }
    Invoke-TestCase 'WhatIf exits before importing helpers or touching filesystem/host state' {
        $before=(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue | Measure-Object).Count
        $output=& $scriptPath -WhatIf -ToolRoot $ToolRoot | ConvertFrom-Json
        $after=(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue | Measure-Object).Count
        Assert-True ($output.Preview -and -not $output.FilesystemChangesPerformed -and -not $output.NativeCommandsExecuted -and -not $output.EspMounted -and $before -eq $after) 'WhatIf performed work or reported an incorrect effect.'
    }

    Write-Output "RESULT: $script:PassedCases cases, $script:Assertions assertions, $($script:FailedCases.Count) failed."
    if ($script:FailedCases.Count -gt 0) { throw ($script:FailedCases -join "`n") }
}
finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        $actual=[IO.Path]::GetFullPath($fixtureRoot)
        $name=[IO.Path]::GetFileName($actual)
        if (-not $actual.Equals([IO.Path]::GetFullPath((Join-Path $fixtureParent $name)),[StringComparison]::OrdinalIgnoreCase) -or
            -not [IO.Path]::GetFullPath((Split-Path -Parent $actual)).Equals($fixtureParent,[StringComparison]::OrdinalIgnoreCase) -or
            $name -notmatch '^gpu-stage-retirement-contract-[0-9a-f]{32}$') { throw 'Fixture cleanup refused a path outside its exact owned test directory.' }
        $reparse=@(Get-ChildItem -LiteralPath $actual -Force -Recurse | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 })
        if ($reparse.Count -ne 0) { throw "Fixture cleanup refused reparse point $($reparse[0].FullName)." }
        Remove-Item -LiteralPath $actual -Recurse -Force
    }
}
