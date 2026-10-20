[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$ModulePath = Join-Path $ToolRoot 'GpuFinalFirmwareReview.psm1'
$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}
function Assert-Throws {
    param([scriptblock]$Operation, [string]$Pattern, [string]$Message)
    $observed = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message }
    Assert-True ($null -ne $observed -and $observed -match $Pattern) "$Message (observed: $observed)"
}
function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try { & $Body; $script:PassedCases++; Write-Output "PASS: $Name" }
    catch { $script:FailedCases.Add("$Name`: $($_.Exception.Message)"); Write-Output "FAIL: $Name`: $($_.Exception.Message)" }
}
function New-Pin {
    param([string]$Path, [string]$Sha256, [long]$Bytes)
    return [pscustomobject]@{ Path=[IO.Path]::GetFullPath($Path); Sha256=$Sha256; Bytes=$Bytes }
}
function New-ValidFinalReviewFixture {
    param([string]$WorkspaceRoot)
    $sourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $runId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $runDirectory = Join-Path $WorkspaceRoot ('.analysis\gpu-baremetal\runs\' + $runId)
    $actionDirectory = Join-Path (Join-Path $runDirectory 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    $backupAction = Join-Path (Join-Path $runDirectory 'actions') '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'
    $oldFinalAction = Join-Path (Join-Path $runDirectory 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    $reviewDirectory = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-final-firmware-review') '8abb7b84ca644a7c84019a1f29b35bb4'
    $observations = Join-Path $reviewDirectory 'observations'
    $statePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
    $platformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
    $retirementPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
    $selectorPath = Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\host-prepared.json'
    $journalPath = Join-Path $runDirectory 'journal.json'
    $baselinePath = Join-Path $backupAction 'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'
    $originalCurrentPath = Join-Path $oldFinalAction 'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'
    $auditToolPath = Join-Path $WorkspaceRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
    $referencePath = Join-Path $backupAction 'tcglog.ib'
    $probePath = Join-Path $WorkspaceRoot '.codex-build\gb\8be3274dfe56cec7\l\ib-tcg-dump.exe'
    $inspectorPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1'
    $state = [pscustomobject]@{
        WorkspaceRoot=$WorkspaceRoot; SourceRoot=$sourceRoot; SourceCommit='933b4c676318f26396cdf2c66de01c3ccbf1a950'
        RunId=$runId; RunDirectory=$runDirectory; Phase='Disarmed'; EverArmIntent=$true
        DisarmedBootId='2026-10-20T20:19:07.5000000Z'; InProgress=[pscustomobject]@{ Action='FinalVerify'; Directory=$actionDirectory }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        Backup=[pscustomobject]@{
            EspVolume='\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'; EspDiskNumber=0
            EspPartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD'
            BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'
            FirmwareEnumeration=(New-Pin $baselinePath 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' 4336)
            ReferenceLog=(New-Pin $referencePath '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0' 40838)
        }
        Tools=[pscustomobject]@{ Probe=(New-Pin $probePath 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47' 172544) }
        FinalVerification=[pscustomobject]@{ FirmwareEnumeration=[pscustomobject]@{
            TextEqual=$false; BaselineSha256='ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
            CurrentSha256='4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'; CaptureContext='SystemEspMountedAtS'
        } }
    }
    $currentHost = [pscustomobject]@{ BootId='2026-10-20T20:48:51.5000000Z'; LogicalCpus=20; SecureBoot=$true }
    $currentFirmwarePin = New-Pin (Join-Path $actionDirectory 'firmware-enumeration-final-fresh.stdout.txt') '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336
    $firmwareText = 'fixture mounted firmware text'
    $baseline = [pscustomobject]@{ Schema=1; BootNext=$null; BootOrder=[uint16[]]@(0,1,2,3,4,5); BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; ReferenceLogSha256='8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'; IntegrityFindings='00000000' }
    $beforeRawPath = Join-Path $observations 'before-boot-selection.json'
    $afterRawPath = Join-Path $observations 'after-boot-selection.json'
    $rawPin = { param($path) New-Pin $path 'B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0' 259 }
    $rawCapture = {
        param($path)
        [pscustomobject]@{
            Baseline=$baseline; BaselinePath=$path; BaselinePin=(& $rawPin $path)
            IntegrityOutput='BootmgfwSha256=3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511 Findings=0x00000000'
            IntegrityReceipt=(Join-Path $observations (([IO.Path]::GetFileNameWithoutExtension($path)) + '-integrity.receipt.json'))
            AuditToolPin=(New-Pin $auditToolPath 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' 246784)
            ReferenceLogPin=(New-Pin $referencePath '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0' 40838)
        }
    }
    $receipt = [pscustomobject]@{
        SchemaVersion=1; Status='READONLY_FIRMWARE_REVIEW_CAPTURED'; Succeeded=$true; RunId=$runId
        RecoveryCompleted=$false; HistoricalRawBootOrderUnchangedProven=$false; SelectorAndJournalUnchanged=$true
        EspUnmounted=$true; CleanupSucceeded=$true; EspFilesChanged=$false; FirmwareWritten=$false; Errors=@(); CleanupErrors=@()
        EvidenceDirectory=$reviewDirectory
        PinsBefore=[pscustomobject]@{
            Selector=(New-Pin $selectorPath 'A7416148825A6ABC3603681C45F6A8277AFEE432D4B503BDEC89D34F58D34FFB' 533)
            Journal=(New-Pin $journalPath '631E2811091F4085445170A500985A30A296762EA15EEC0FD70C70422C55101D' 45269)
            FirmwareBaseline=(New-Pin $baselinePath 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE' 4336)
            FirmwareCurrent=(New-Pin $originalCurrentPath '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336)
            Ibaudit=(New-Pin $auditToolPath 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' 246784)
            ReferenceLog=(New-Pin $referencePath '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0' 40838)
            Probe=(New-Pin $probePath 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47' 172544)
            InspectorScript=(New-Pin $inspectorPath '9EFE53FCC2CEA79BF0F2DF7E8E550AE122D381259AB7C176D7FA64A76A7A6275' 27895)
            Modules=[pscustomobject]@{
                State=(New-Pin $statePath '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' 19843)
                Platform=(New-Pin $platformPath '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899' 107733)
                Retirement=(New-Pin $retirementPath 'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553' 61875)
            }
        }
        PinsAfter=$null
        CurrentHost=$currentHost
        Health=[pscustomobject]@{
            CommandSucceeded=$true; Verdict=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; ExpectedCpus=20; CpuCount=20; Aggregate='ABSENT' }
            ReceiptPath=(Join-Path $observations 'health.receipt.json')
        }
        Esp=[pscustomobject]@{ Volume=$state.Backup.EspVolume; DiskNumber=0; PartitionGuid=$state.Backup.EspPartitionGuid; BootManagerSha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' }
        FirmwareTextStable=$true; RawBootSelectionStable=$true
        FirmwareObservations=@(
            [pscustomobject]@{ Checkpoint='before'; Succeeded=$true; Text=$firmwareText; Pin=(New-Pin (Join-Path $observations 'stage-retirement-firmware-3ba00749b6d94f82a0ef932ab7fb214d.stdout.txt') '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336); ReceiptPath=(Join-Path $observations 'firmware-before.receipt.json') }
            [pscustomobject]@{ Checkpoint='after'; Succeeded=$true; Text=$firmwareText; Pin=(New-Pin (Join-Path $observations 'stage-retirement-firmware-1f63298289ce49ed81012994adc954ba.stdout.txt') '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE' 4336); ReceiptPath=(Join-Path $observations 'firmware-after.receipt.json') }
        )
        BootSelectionCaptures=@((& $rawCapture $beforeRawPath), (& $rawCapture $afterRawPath))
    }
    $receipt.PinsAfter = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $receipt.PinsBefore -Depth 8 -Compress)
    return @{ State=$state; CurrentHost=$currentHost; CurrentFirmwarePin=$currentFirmwarePin; ActionDirectory=$actionDirectory; Receipt=$receipt }
}

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) { throw "Review module not found: $ModulePath" }
$module = Import-Module -Name $ModulePath -Force -PassThru -DisableNameChecking -ErrorAction Stop | Select-Object -First 1
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..\..\..\..'))
$invokeShape = { param($f) & $module { param($receipt,$state,$host,$firmware,$action) Assert-GpuFinalFirmwareReviewReceiptShape -Receipt $receipt -State $state -CurrentHost $host -CurrentFirmwarePin $firmware -ActionDirectory $action } $f.Receipt $f.State $f.CurrentHost $f.CurrentFirmwarePin $f.ActionDirectory }

Invoke-TestCase 'fixed current-run review receipt passes strict evidence-shape checks' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $null = & $invokeShape $fixture
    Assert-True ($module.ExportedFunctions.ContainsKey('Invoke-GpuReviewedFinalFirmware')) 'Public review helper is not exported.'
    Assert-True ($module.ExportedFunctions.Count -eq 1) 'Internal review overrides leaked into the public module surface.'
}

Invoke-TestCase 'wrong run, source commit, host boot, or action directory is rejected' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.RunId='gpu-elsewhere'
    Assert-Throws { $null = & $invokeShape $fixture } 'run ID' 'Wrong receipt run was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.State.SourceCommit='different-commit'
    Assert-Throws { $null = & $invokeShape $fixture } 'source commit' 'Wrong source commit was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.CurrentHost.BootId='different-boot'
    Assert-Throws { $null = & $invokeShape $fixture } 'Current host boot identity' 'Wrong boot ID was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.ActionDirectory=Join-Path $workspaceRoot 'outside-action'
    Assert-Throws { $null = & $invokeShape $fixture } 'FinalVerify intent|action directory|outside' 'Outside action directory was accepted'
}

Invoke-TestCase 'recovery, cleanup, selector, or journal changes prevent acceptance' {
    foreach ($mutation in @(
        { param($f) $f.Receipt.Status='FAILED' },
        { param($f) $f.Receipt.RecoveryCompleted=$true },
        { param($f) $f.Receipt.CleanupSucceeded=$false },
        { param($f) $f.Receipt.SelectorAndJournalUnchanged=$false },
        { param($f) $f.Receipt.PinsAfter.Journal.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA' }
    )) {
        $fixture = New-ValidFinalReviewFixture $workspaceRoot
        & $mutation $fixture
        Assert-Throws { $null = & $invokeShape $fixture } 'receipt|Journal pin|clean|changed' 'Mutated receipt evidence was accepted'
    }
}

Invoke-TestCase 'firmware text, health result, and saved pin changes prevent acceptance' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.CurrentFirmwarePin.Sha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    Assert-Throws { $null = & $invokeShape $fixture } 'Current firmware text pin' 'Wrong current firmware pin was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.State.FinalVerification.FirmwareEnumeration.BaselineSha256='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
    Assert-Throws { $null = & $invokeShape $fixture } 'mounted-ESP firmware-text mismatch' 'Wrong baseline was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.Health.Verdict.ExpectedResult='PASS'
    Assert-Throws { $null = & $invokeShape $fixture } '20 ABSENT CPUs' 'Wrong saved health result was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.PinsAfter.FirmwareBaseline.Bytes=4337
    Assert-Throws { $null = & $invokeShape $fixture } 'After FirmwareBaseline pin' 'Changed immutable pin was accepted'
}

Invoke-TestCase 'raw BootNext, BootOrder, integrity findings, and evidence path are rejected' {
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[0].Baseline.BootNext=4
    Assert-Throws { $null = & $invokeShape $fixture } 'BootSelection|BootNext|raw' 'Set BootNext was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[1].Baseline.BootOrder[4]=7
    Assert-Throws { $null = & $invokeShape $fixture } 'BootOrder|raw' 'Changed BootOrder was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[0].Baseline.IntegrityFindings='00000001'
    Assert-Throws { $null = & $invokeShape $fixture } 'findings' 'Audit finding was accepted'
    $fixture = New-ValidFinalReviewFixture $workspaceRoot
    $fixture.Receipt.BootSelectionCaptures[0].BaselinePath=Join-Path $workspaceRoot 'outside.json'
    Assert-Throws { $null = & $invokeShape $fixture } 'outside its pinned inspection evidence|pin differs' 'Raw JSON outside evidence directory was accepted'
}

$summary = [pscustomobject]@{ PassedCases=$script:PassedCases; FailedCases=$script:FailedCases.ToArray(); Assertions=$script:Assertions }
$summary | ConvertTo-Json -Depth 5
if ($script:FailedCases.Count -gt 0) { exit 1 }
