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
    $runId = 'gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7'
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
    $names = @('grubx64.efi','handoff-status.txt','manifest.sha256','payload.efi','shimx64.efi','tcglog.ib')
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

    $auditEntries = @($stagePins | ForEach-Object { [pscustomobject]@{ RelativePath=$_.RelativePath; Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true } })
    $audit = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'
        Esp=[pscustomobject]@{ Identity=[pscustomobject]@{ Volume='fixture-volume'; DiskNumber=7; DiskUniqueId='fixture-disk-id'; PartitionGuid='FIXTURE-PARTITION' }; BootManagerPin=[pscustomobject]@{ Sha256=$bootPin.Sha256; Bytes=$bootPin.Bytes } ; MountStateAfterCleanup='Unmounted' }
        ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true; EntryCount=6; FileCount=6; Entries=$auditEntries; UnknownEntries=@(); UnsafeEntries=@(); Errors=@() }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
    }

    $stageFiles = @($stagePins | Where-Object Name -ne 'handoff-status.txt' | ForEach-Object { [pscustomobject]@{ RelativePath=$_.RelativePath; Sha256=$_.Sha256; Bytes=$_.Bytes } })
    $collectedStage = @($stagePins | ForEach-Object { [pscustomobject]@{ Name=$_.Name; Pin=$_.Pin } })
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runId; SourceCommit='c6e65a2b265d1f3c13ac83bfe61c69281907c003'; SourceRoot=$root
        RunDirectory=$runDirectory; JournalPath=(Join-Path $runDirectory 'journal.json'); Phase='Complete'; InProgress=$null; DisarmedBootId='old-boot-id'; RecoveryRunDirectory=$recoveryDirectory
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
    $auditPath = Join-Path $root '.analysis\gpu-baremetal-readiness\334426c80d1d47b2bb454d2c25e65d41\readiness.json'
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
    $ops.CaptureFirmware = { param($Actions) $Behavior.FirmwareCaptureCount++; if ($Behavior.FirmwareChanges -or ($Behavior.FirmwareChangesAt -gt 0 -and $Behavior.FirmwareCaptureCount -ge $Behavior.FirmwareChangesAt)) { return [pscustomobject]@{ Text='foreign firmware entry' } }; return [pscustomobject]@{ Text=$Fixture.FirmwareText; Pin=$null; ReceiptPath='mock-bcdedit.receipt.json' } }.GetNewClosure()
    $ops.CaptureHealth = { param($State,$Actions) return [pscustomobject]@{ CommandSucceeded=$true; Verdict=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=2; ExpectedCpus=2 }; ReceiptPath='mock-health.receipt.json' } }
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
        ExpectedReferenceLogSha256=$Fixture.ReferenceLogPin.Sha256; ExpectedReferenceLogBytes=$Fixture.ReferenceLogPin.Bytes
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

    Invoke-TestCase 'complete reviewed evidence yields six immutable file pins' {
        Assert-True ($stageSpec.Count -eq 6 -and @($stageSpec | Where-Object Name -eq 'handoff-status.txt').Count -eq 1) 'Expected exactly six reviewed files including handoff status.'
    }
    Invoke-TestCase 'audit rejects an added unknown entry' {
        $bad = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
        $bad.ExistingStage.Entries += [pscustomobject]@{ RelativePath='EFI\insecure-boot\foreign.bin'; Kind='File'; Bytes=1; Sha256=('A'*64); Hashed=$true }
        $bad.ExistingStage.EntryCount++ ; $bad.ExistingStage.FileCount++
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $bad -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'complete|unknown|exactly six' 'Unknown audit entry was accepted.'
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
    Invoke-TestCase 'evidence rejects incomplete run, failed reset, and wrong archived selector' {
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badJournal.Phase='Staged'
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'complete|action' 'Incomplete run was accepted.'
        $badJournal = ($fixture.Journal | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badJournal.FinalVerification.ResetBoundaryPassed=$false
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $badJournal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'Final verification' 'Failed reset boundary was accepted.'
        $badArchive = ($fixture.Archive | ConvertTo-Json -Depth 10 | ConvertFrom-Json); $badArchive.RunId='other-run'
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $fixture.Audit -Journal $fixture.Journal -ArchivedSelector $badArchive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'archived selector' 'Wrong archived run was accepted.'
    }
    Invoke-TestCase 'ESP and boot-manager evidence must match the archived backup' {
        $badAudit = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badAudit.Esp.Identity.DiskNumber++
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $badAudit -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'ESP disk|boot-manager' 'Wrong ESP disk was accepted.'
        $badAudit = ($fixture.Audit | ConvertTo-Json -Depth 30 | ConvertFrom-Json); $badAudit.Esp.BootManagerPin.Sha256=('B'*64)
        Assert-Throws { Assert-GpuStageRetirementEvidence -Audit $badAudit -Journal $fixture.Journal -ArchivedSelector $fixture.Archive -ExpectedAuditSha256 $auditPin.Sha256 -ExpectedRunId $fixture.RunId -ExpectedRunDirectory $fixture.RunDirectory -ExpectedJournalPath $fixture.JournalPath -ExpectedSourceRoot $fixture.Root } 'boot-manager' 'Wrong boot-manager pin was accepted.'
    }
    Invoke-TestCase 'live inventory refuses missing, extra, directory, and modified files' {
        $valid = Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory
        Assert-True ($valid.Count -eq 6) 'Expected six live stage files.'
        [IO.File]::Delete((Join-Path $fixture.StageDirectory 'shimx64.efi'))
        Assert-Throws { Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory } 'exactly six|missing' 'Missing live file was accepted.'
        [IO.File]::WriteAllText((Join-Path $fixture.StageDirectory 'shimx64.efi'),'restored')
        [void][IO.Directory]::CreateDirectory((Join-Path $fixture.StageDirectory 'foreign'))
        Assert-Throws { Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory } 'directory|exactly six' 'Directory entry was accepted.'
        [IO.Directory]::Delete((Join-Path $fixture.StageDirectory 'foreign'))
        $live=Get-GpuStageRetirementLiveInventory -StageDirectory $fixture.StageDirectory
        Assert-Throws { Assert-GpuStageRetirementLivePins -LivePins $live -ExpectedPins $stageSpec } 'differs' 'Modified live file was accepted.'
    }
    Invoke-TestCase 'raw boot selection rejects BootNext, empty order, changed order, bad integrity, and command failure' {
        $baseline=[pscustomobject]@{schema=1;boot_next=$null;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
        $integrity="BootmgfwSha256=$($fixture.BootManagerSha256) Findings=0x00000000"
        $valid=& $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $baseline $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256
        Assert-True ($valid.BootOrder.Count -eq 2) 'Valid raw boot selection was not parsed.'
        foreach ($bad in @(
            [pscustomobject]@{schema=1;boot_next=42;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256},
            [pscustomobject]@{schema=1;boot_next=$null;boot_order=@();bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
        )) { Assert-Throws { & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $bad $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 } '.' 'Bad BootNext/BootOrder was accepted.' }
        $changed=[pscustomobject]@{ BootNext=$null;BootOrder=[uint16[]]@(1,3);BootManagerSha256=$valid.BootManagerSha256;ReferenceLogSha256=$valid.ReferenceLogSha256;IntegrityFindings='00000000' }
        Assert-Throws { & $retirementModule { param($b,$c) Assert-GpuStageRetirementBootSelectionStable -Before $b -Current $c -Checkpoint 'test' } $valid $changed } 'BootOrder changed' 'Changed BootOrder was accepted.'
        Assert-Throws { & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $true } $baseline $integrity.Replace('00000000','00000001') $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 } 'integrity|findings' 'Nonzero integrity finding was accepted.'
        Assert-Throws { & $retirementModule { param($b,$i,$bm,$log) Assert-GpuStageRetirementBootSelection -Baseline $b -IntegrityOutput $i -ExpectedBootManagerSha256 $bm -ExpectedReferenceLogSha256 $log -CommandSucceeded $false } $baseline $integrity $fixture.BootManagerSha256 $fixture.ReferenceLogPin.Sha256 } 'failed to read' 'Failed raw read was accepted.'
    }
    Invoke-TestCase 'production raw-capture callback resolves its own helpers and invokes only the injected native runner' {
        $nativeRoot=Join-Path $fixtureRoot 'native-callback'; [void][IO.Directory]::CreateDirectory($nativeRoot)
        $state=[pscustomobject]@{ Paths=[pscustomobject]@{LoaderBuild=$fixture.BuildDirectory}; Backup=[pscustomobject]@{BootManagerSha256=$fixture.BootManagerSha256;ReferenceLog=$fixture.ReferenceLogPin} }
        $native={ param($Program,[string[]]$Arguments,$ActionDirectory,$Label)
            if ($Arguments[0] -eq 'baseline') {
                $out=$Arguments[4]; $payload=[pscustomobject]@{schema=1;boot_next=$null;boot_order=[uint16[]]@(1,2);bootmgfw_sha256=$fixture.BootManagerSha256;log_sha256=$fixture.ReferenceLogPin.Sha256}
                Write-TestJson $out $payload
                $stdout=Join-Path $ActionDirectory ($Label+'.stdout'); $stderr=Join-Path $ActionDirectory ($Label+'.stderr'); [IO.File]::WriteAllText($stdout,'');[IO.File]::WriteAllText($stderr,'')
            } else {
                $stdout=Join-Path $ActionDirectory ($Label+'.stdout'); $stderr=Join-Path $ActionDirectory ($Label+'.stderr')
                [IO.File]::WriteAllText($stdout,"BootmgfwSha256=$($fixture.BootManagerSha256) Findings=0x00000000`n");[IO.File]::WriteAllText($stderr,'')
            }
            return [pscustomobject]@{Succeeded=$true;StdoutPath=$stdout;StderrPath=$stderr;ReceiptPath=($Label+'.receipt')}
        }.GetNewClosure()
        $operationArgs=@{ToolRoot=$ToolRoot;NativeCommand=$native;ExpectedAuditToolSha256=(Get-TestPin (Join-Path $fixture.BuildDirectory 'ibaudit.exe')).Sha256;ExpectedAuditToolBytes=(Get-TestPin (Join-Path $fixture.BuildDirectory 'ibaudit.exe')).Bytes;ExpectedReferenceLogSha256=$fixture.ReferenceLogPin.Sha256;ExpectedReferenceLogBytes=$fixture.ReferenceLogPin.Bytes}
        $productionOps=& $retirementModule { param($a) New-GpuStageRetirementPlatformOperations @a } $operationArgs
        $capture=& $productionOps.CaptureBootSelection $state $nativeRoot 'before'
        Assert-True ($capture.Baseline.BootOrder.Count -eq 2 -and $capture.Baseline.IntegrityFindings -ceq '00000000') 'Production capture callback failed to parse raw baseline or integrity output.'
    }
    Invoke-TestCase 'default review creates verified local and external copies without deleting ESP files' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $ops=New-MockOperations $fixture $behavior
        $result=Invoke-FixtureRetirement $fixture $ops
        Assert-True ($result.Status -ceq 'REVIEWED_STAGE_READY_FOR_RETIREMENT' -and $result.Succeeded -and $result.Copies.Count -eq 6 -and $behavior.DeleteCount -eq 0 -and $result.EspUnmounted) "Review did not preserve stage, create six copies, and unmount. status=$($result.Status) succeeded=$($result.Succeeded) copies=$(@($result.Copies).Count) deleted=$($behavior.DeleteCount) unmounted=$($result.EspUnmounted) errors=$($result.Errors -join '; ') cleanup=$($result.CleanupErrors -join '; ')"
        foreach ($copy in $result.Copies) { Assert-True ((Get-TestPin $copy.Local.Path).Sha256 -ceq $copy.Source.Sha256 -and (Get-TestPin $copy.External.Path).Sha256 -ceq $copy.Source.Sha256) "Review copy mismatch: $($copy.Name)" }
    }
    Invoke-TestCase 'Apply removes only the six pinned files after evidence copies and all gates pass' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIRED' -and $result.Succeeded -and $result.RemovedFiles.Count -eq 6 -and $result.EspFilesChanged -and -not (Test-Path -LiteralPath $fixture.StageDirectory) -and (Test-Path -LiteralPath $fixture.BootManagerPath) -and $result.EspUnmounted) 'Apply did not retire exactly the stage or preserve the boot manager.'
    }
    Invoke-TestCase 'external evidence-copy failure blocks before any ESP deletion' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=2;FailDeleteAt=0;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and $behavior.DeleteCount -eq 0 -and (Test-Path -LiteralPath $fixture.StageDirectory)) 'Copy failure did not fail closed before deletion.'
    }
    Invoke-TestCase 'partial delete remains failed, keeps verified copies, and cannot resume on a later apply' {
        $fixture=New-TestFixture
        $behavior=@{CopyCount=0;DeleteCount=0;BootCaptureCount=0;UnmountCount=0;FirmwareCaptureCount=0;FirmwareChangesAt=0;FailCopyAt=0;FailDeleteAt=2;DismountFails=$false;FirmwareChanges=$false;BootNextPresent=$false;BootReadFails=$false;BootOrderChanges=$false;WrongEsp=$false}
        $result=Invoke-FixtureRetirement $fixture (New-MockOperations $fixture $behavior) -Apply
        Assert-True ($result.Status -ceq 'STAGE_RETIREMENT_FAILED' -and $result.RemovedFiles.Count -eq 2 -and $result.Copies.Count -eq 6 -and (Test-Path -LiteralPath $fixture.StageDirectory)) 'Partial deletion was hidden or copies were lost.'
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
