[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuStageRetirement.psm1'
$scriptPath = Join-Path $ToolRoot 'Retire-GpuCompletedStage.ps1'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$readinessGuid = 'b6037d0adbbc4299af077f2afcf0691b'
$readinessRelativePath = '.analysis\gpu-baremetal-readiness\' + $readinessGuid + '\readiness.json'
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $sourceRoot '.codex-build'))
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $fixtureParent ('gpu-stage-retirement-193833-tests-' + [guid]::NewGuid().ToString('N'))))
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
    $observedStack = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message; $observedStack = [string]$_.ScriptStackTrace }
    Assert-True ($null -ne $observed -and $observed -match $MessagePattern) "$Message (observed: $observed; stack: $observedStack)"
}

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try { & $Body; $script:PassedCases++; Write-Output "PASS: $Name" }
    catch {
        $script:FailedCases.Add("$Name`: $($_.Exception.Message)")
        Write-Output "FAIL: $Name`: $($_.Exception.Message)"
        if (-not [string]::IsNullOrWhiteSpace([string]$_.ScriptStackTrace)) { Write-Output $_.ScriptStackTrace }
    }
}

function Import-TestModule {
    return Import-Module -Name $modulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1
}

function Get-ProfileState {
    param([object]$Module)
    return & $Module {
        $values = @{}
        foreach ($variable in Get-Variable -Scope Script) {
            if ($variable.Name -like 'GpuRetirement193833*' -or $variable.Name -like 'GpuRetirementAuditTool*') { $values[$variable.Name] = $variable.Value }
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
    param([string]$WorkspaceRoot, [object[]]$StagePins)
    $directory = Join-Path $WorkspaceRoot (Split-Path -Parent $script:readinessRelativePath)
    $path = Join-Path $directory 'readiness.json'
    $fixtureSourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $sourcePins = @(
        [pscustomobject]@{ RelativePath='tools/gpu_diagnostic/Get-GpuBaremetalReadiness.ps1'; Sha256='57A42B2294882AD8F0B2E5C239BC2EF2FEB82CA667E4361E819B1E436F6A01A5'; Bytes=33730L },
        [pscustomobject]@{ RelativePath='tools/gpu_diagnostic/GpuHostState.psm1'; Sha256='067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'; Bytes=19843L },
        [pscustomobject]@{ RelativePath='tools/gpu_diagnostic/GpuHostPlatform.psm1'; Sha256='0881A2EBE0C292107DDD7D83E165830FEBE617A31B0CA1A8FF731DB723EDFC4C'; Bytes=133298L },
        [pscustomobject]@{ RelativePath='components/hypervisor/setup/Get-GpuDiagnosticPreflight.ps1'; Sha256='37FF3F987EEF7D3879CB943A1662D2558A0EC215D4FC28888C1A3C9B7687CF77'; Bytes=10992L }
    )
    foreach ($sourcePin in $sourcePins) {
        $relative = ([string]$sourcePin.RelativePath).Replace('/', '\')
        $from = Join-Path $script:sourceRoot $relative
        $to = Join-Path $fixtureSourceRoot $relative
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
        Copy-Item -LiteralPath $from -Destination $to -Force
        $copied = Get-TestPin $to
        if ($copied.Sha256 -cne [string]$sourcePin.Sha256 -or $copied.Bytes -ne [long]$sourcePin.Bytes) {
            throw "A copied readiness source pin differs from the audited file: $relative"
        }
    }

    $preflightPath = Join-Path $directory 'gpu-preflight-readiness.json'
    [void][IO.Directory]::CreateDirectory($directory)
    Write-TestJson -Path $preflightPath -Value ([pscustomobject]@{ SchemaVersion=1; Complete=$true; FixtureOnly=$true })
    $preflightCopy = Get-TestPin $preflightPath
    $entries = @($StagePins | ForEach-Object {
        [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
    })
    $report = [pscustomobject]@{
        SchemaVersion=1; Status='BLOCKED_READINESS'; Succeeded=$false
        EvidencePath=$path; EvidenceDirectory=$directory
        Source=[pscustomobject]@{
            Branch='feat/smbios-boot-serial-probe'; Commit='799cff99549b11ae2a65b8c7050d043813c77141'
            Dirty=$false; DirtyEntryCount=0; Pins=$sourcePins
        }
        Preflight=[pscustomobject]@{
            Complete=$true; Path=$preflightPath
            Pin=[pscustomobject]@{ Path=$preflightPath; Sha256=$preflightCopy.Sha256; Bytes=$preflightCopy.Bytes }
            InventoryComplete=$true; IntelPresent=$true; NvidiaPresent=$true; ConnectedMonitorCount=2; Error=$null
        }
        Blockers=@('EFI\\insecure-boot is retained; ownership-reviewed retirement is required before a new stage. Do not delete it generically or retry Stage.')
        Errors=@()
        Host=[pscustomobject]@{ LogicalCpus=20; SecureBoot=$true }
        Recovery=[pscustomobject]@{
            Valid=$true; Directory='E:\CodexBackup\Recovery\hsinv-gpu-test'
            Disk=[pscustomobject]@{ Number=1; UniqueId='USBSTOR\DISK&VEN_VENDORCO&PROD_PRODUCTCODE&REV_2.00\3495191025225542250&0:DESKTOP-5852FUT'; IsVirtual=$false }
        }
        Selector=[pscustomobject]@{ Exists=$false; Valid=$true; Error=$null }
        Cleanup=[pscustomobject]@{ Succeeded=$true; Errors=@() }
        Esp=[pscustomobject]@{
            Inspected=$true; MountedDuringInspection=$true; MountStateAfterCleanup='Unmounted'
            Error='EFI\insecure-boot already exists; preserve and review it before another stage.'
            Identity=[pscustomobject]@{
                Volume='\\?\Volume{187bc913-7576-4e6e-8d54-18eeb5c38fcd}\'; DiskNumber=0
                PartitionGuid='187BC913-7576-4E6E-8D54-18EEB5C38FCD'; DiskUniqueId='eui.0000000001000000E4D25CDF32275501'
            }
            BootManagerPin=[pscustomobject]@{ Sha256='3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511'; Bytes=3086848L }
        }
        Effects=[pscustomobject]@{
            LocalEvidenceDirectoryCreated=$true; SharedStateDirectoryCreated=$false; SharedLockFileCreated=$false
            EspFilesChanged=$false; FirmwareChanged=$false; BootStateChanged=$false; EspTemporarilyMounted=$true
        }
        ExistingStage=[pscustomobject]@{
            Exists=$true; Complete=$true; EntryCount=7; FileCount=7; BytesHashed=1289362L; Entries=$entries
            UnknownEntries=@(); UnsafeEntries=@(); Errors=@()
        }
        ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@(); ValidationError='EFI\insecure-boot already exists; preserve and review it before another stage.' }
    }
    if (@($StagePins).Count -ne 7 -or @($report.ExistingStage.Entries).Count -ne 7) {
        throw 'The readiness fixture must preserve the exact seven-file stage inventory.'
    }
    Write-TestJson -Path $path -Value $report
    $readinessPin = Get-TestPin $path
    return @{ Path=$path; Directory=$directory; Report=$report; Guid=$script:readinessGuid; Pin=$readinessPin }
}

function Set-TestReadinessProfilePins {
    param([object]$Module, [hashtable]$Fixture)
    $readinessPin = Get-TestPin $Fixture.Path
    $preflightPath = Join-Path $Fixture.Directory 'gpu-preflight-readiness.json'
    $preflightPin = Get-TestPin $preflightPath
    & $Module {
        param($readinessHash,$readinessBytes,$preflightHash,$preflightBytes)
        $script:GpuRetirement193833ReadinessSha256 = $readinessHash
        $script:GpuRetirement193833ReadinessBytes = $readinessBytes
        $script:GpuRetirement193833PreflightSha256 = $preflightHash
        $script:GpuRetirement193833PreflightBytes = $preflightBytes
    } $readinessPin.Sha256 $readinessPin.Bytes $preflightPin.Sha256 $preflightPin.Bytes
}

function Select-TestProfile {
    param([object]$Module, [string]$WorkspaceRoot, [string]$ReadinessPath)
    $expectedPath = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $script:readinessRelativePath))
    if ([IO.File]::Exists($ReadinessPath) -and
        [string]::Equals([IO.Path]::GetFullPath($ReadinessPath), $expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
        $directory = Split-Path -Parent $ReadinessPath
        Set-TestReadinessProfilePins -Module $Module -Fixture @{ Path=$ReadinessPath; Directory=$directory }
    }
    return & $Module { param($root,$path) Select-GpuStageRetirement193833Profile -WorkspaceRoot $root -ReadinessPath $path } $WorkspaceRoot $ReadinessPath
}

function New-ProofFixture {
    param([string]$WorkspaceRoot, [hashtable]$State, [string]$Failure = '')
    $runId = [string]$State.GpuRetirement193833RunId
    $runDirectory = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\runs') $runId
    $journalPath = Join-Path $runDirectory 'journal.json'
    $archivePath = Join-Path (Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\archive') ($runId + '.json')
    $sourceRoot = Join-Path $WorkspaceRoot '.codex-build\worktrees\gpu-windows-validation'
    $finalAction = if ($Failure -ceq 'action-path') { Join-Path $WorkspaceRoot '.analysis\gpu-baremetal\wrong-final-action' } else { Join-Path $WorkspaceRoot $State.GpuRetirement193833FinalActionRelativePath }
    $disarmedBootId = [string]$State.GpuRetirement193833DisarmedBootId
    $currentBootId = if ($Failure -ceq 'boot') { $disarmedBootId } else { [string]$State.GpuRetirement193833CurrentBootId }
    $runIdentity = if ($Failure -ceq 'run') { 'gpu-unrelated-run' } else { $runId }
    $phase = if ($Failure -ceq 'phase') { 'Failed' } else { 'Complete' }
    $sourceCommit = if ($Failure -ceq 'source') { '0000000000000000000000000000000000000000' } else { $State.GpuRetirement193833SourceCommit }
    $status = if ($Failure -ceq 'status') { 'FINAL_VERIFY_FAILED_GPU_UNATTESTED' } else { $State.GpuRetirement193833FinalResultStatus }
    $errors = if ($Failure -ceq 'errors') { @('injected fixture failure') } else { @() }
    $journal = [pscustomobject]@{
        SchemaVersion=1; RunId=$runIdentity; RunDirectory=$runDirectory; SourceRoot=$sourceRoot
        SourceCommit=$sourceCommit; Phase=$phase; InProgress=$null
        DisarmedBootId=$disarmedBootId; LastAction=[pscustomobject]@{
            Action='FinalVerify'; Succeeded=($Failure -cne 'action'); Status=$status; Directory=$finalAction
        }
        Host=[pscustomobject]@{ LogicalCpus=20 }
        FinalVerification=[pscustomobject]@{
            BootId=$currentBootId; ResetBoundaryPassed=($Failure -cne 'reset'); ActivationAttempted=$true; SecureBoot=$true
            ActiveGpuProof=$false; GpuAttestation='UNATTESTED'; Errors=$errors
            Health=[pscustomobject]@{
                Passed=($Failure -cne 'health'); ExpectedResult='ABSENT'; Aggregate='ABSENT'
                CpuCount=$(if ($Failure -ceq 'health') { 19 } else { 20 }); ExpectedCpus=20; CpuPassCount=0
            }
            Esp=[pscustomobject]@{
                MatchesBaseline=($Failure -cne 'esp'); Volume=$State.GpuRetirement193833EspVolume; DiskNumber=$State.GpuRetirement193833EspDiskNumber
                PartitionGuid=$State.GpuRetirement193833EspPartitionGuid; BootManagerSha256=$State.GpuRetirement193833BootManagerSha256
            }
            FirmwareEnumeration=[pscustomobject]@{
                TextEqual=$false; ReviewedDifferenceAccepted=$true
                BaselineSha256=$State.GpuRetirement193833OriginalFirmwareSha256
                CurrentSha256=$(if ($Failure -ceq 'firmware') { '0' * 64 } else { $State.GpuRetirement193833ReviewedFirmwareSha256 })
                CaptureContext='SystemEspMountedAtS'
                ReviewedDifference=[pscustomobject]@{
                    Accepted=($Failure -cne 'review'); HistoricalRawBootOrderUnchangedProven=$false; FullBootVariableNamespaceAttested=$false
                    ReceiptPin=[pscustomobject]@{
                        Path=(Join-Path $WorkspaceRoot $State.GpuRetirement193833ReviewReceiptRelativePath)
                        Sha256=$State.GpuRetirement193833ReviewReceiptSha256; Bytes=$State.GpuRetirement193833ReviewReceiptBytes
                    }
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
    $readiness = New-ReadinessFixture -WorkspaceRoot $WorkspaceRoot -StagePins $State.GpuRetirement193833StagePins
    $null = Select-TestProfile -Module $module -WorkspaceRoot $WorkspaceRoot -ReadinessPath $readiness.Path
    & $module {
        param($journalHash,$journalBytes,$archiveHash,$archiveBytes)
        $script:GpuRetirement193833JournalSha256=$journalHash
        $script:GpuRetirement193833JournalBytes=$journalBytes
        $script:GpuRetirement193833ArchiveSha256=$archiveHash
        $script:GpuRetirement193833ArchiveBytes=$archiveBytes
    } $journalPin.Sha256 $journalPin.Bytes $archivePin.Sha256 $archivePin.Bytes
    return @{ Module=$module; Journal=$journal; Archive=$archive; JournalPath=$journalPath; ArchivePath=$archivePath; RunDirectory=$runDirectory }
}

function New-RawCaptureFixture {
    param(
        [object]$Module,
        [hashtable]$State,
        [string]$WorkspaceRoot,
        [object]$RawBootNext=$null,
        [object[]]$RawOrder=@(0,1,2),
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
        bootmgfw_sha256=$State.GpuRetirement193833BootManagerSha256
        log_sha256=$State.GpuRetirement193833ReferenceLogSha256
    }
    Write-TestJson -Path $rawPath -Value $raw
    $expectedOutput = 'BootmgfwSha256=' + $State.GpuRetirement193833BootManagerSha256 + ' Findings=0x00000000'
    [IO.File]::WriteAllText($stdoutPath, $expectedOutput + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderrPath, '', [Text.UTF8Encoding]::new($false))
    $auditToolPath = Join-Path $WorkspaceRoot $State.GpuRetirementAuditToolRelativePath
    $receipt = [pscustomobject]@{
        SchemaVersion=1; Succeeded=$true; ExitCode=0; AllowedExitCodes=@(0); LaunchError=$null
        Program=[IO.Path]::GetFullPath($auditToolPath)
        Arguments=@('bootmgr-integrity','--expected-hash',$State.GpuRetirement193833BootManagerSha256,'--esp-root','S:\')
        StdoutPath=$stdoutPath; StderrPath=$stderrPath
    }
    Write-TestJson -Path $receiptPath -Value $receipt
    $rawPin = Get-TestPin $rawPath
    & $Module {
        param($hash,$bytes)
        $script:GpuRetirement193833RawCaptureSha256=$hash
        $script:GpuRetirement193833RawCaptureBytes=$bytes
    } $rawPin.Sha256 $rawPin.Bytes
    $capture = [pscustomobject]@{
        Baseline=[pscustomobject]@{
            Schema=1; BootNext=$null; BootOrder=[object[]]@(0,1,2)
            BootManagerSha256=$State.GpuRetirement193833BootManagerSha256
            ReferenceLogSha256=$State.GpuRetirement193833ReferenceLogSha256; IntegrityFindings='00000000'
        }
        BaselinePin=$rawPin; BaselinePath=$rawPath
        AuditToolPin=[pscustomobject]@{
            Path=$auditToolPath; Sha256=$State.GpuRetirementAuditToolSha256; Bytes=$State.GpuRetirementAuditToolBytes
        }
        ReferenceLogPin=[pscustomobject]@{
            Path=(Join-Path $WorkspaceRoot $State.GpuRetirement193833ReferenceLogRelativePath)
            Sha256=$State.GpuRetirement193833ReferenceLogSha256; Bytes=$State.GpuRetirement193833ReferenceLogBytes
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

    Invoke-TestCase '193833 wrapper requires an explicit readiness path and previews without effects' {
        $wrapperToolRoot = Join-Path $workspaceRoot 'tools\gpu_diagnostic'
        $state = Get-ProfileState (Import-TestModule)
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        $readinessPath = $readiness.Path
        $before = @(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        $preview = (& $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun193833 -ReadinessPath $readinessPath -WhatIf | Out-String) | ConvertFrom-Json
        $after = @(Get-ChildItem -LiteralPath $fixtureRoot -Force -Recurse -ErrorAction SilentlyContinue).Count
        Assert-True ($preview.Preview -and -not $preview.FilesystemChangesPerformed -and -not $preview.NativeCommandsExecuted -and -not $preview.EspMounted -and $preview.CompletedRun193833 -and $before -eq $after) 'The 193833 WhatIf preview performed effects or omitted the selected profile.'
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -CompletedRun193833 -WhatIf } 'explicit EvidencePath' 'The wrapper accepted a missing fresh readiness path.'
        foreach ($older in @('201140','224018','023306','032117','042142','062106','160027')) {
        $arguments = @{ ToolRoot=$wrapperToolRoot; ReadinessPath=$readinessPath; WhatIf=$true; CompletedRun193833=$true }
            $arguments['CompletedRun' + $older] = $true
            Assert-Throws { & $scriptPath @arguments } 'mutually exclusive' "The wrapper accepted CompletedRun$older with 193833."
        }
        Assert-Throws { & $scriptPath -ToolRoot $wrapperToolRoot -ReadinessPath $readinessPath -WhatIf } 'only with an explicit completed-run profile' 'ReadinessPath was accepted without a closed profile.'
    }

    Invoke-TestCase '193833 profile fixes the exact seven names, hashes, sizes, and readiness identity' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $expected = @{
            'grubx64.efi'=@{ Sha256='72A1C17ADFCBCA27EA7A64DEABE1E951B9E16BF10198E5D141DAD697ACFD9D33'; Bytes=88936L }
            'manifest.sha256'=@{ Sha256='73962C878CAAA6C970AAD2B1931E3858EFD265838E977A0F9B971D556800A60B'; Bytes=394L }
            'payload.efi'=@{ Sha256='5E4DE74F9973DF583CEFBD60B00F6BF2F670BA889DD27E9C6AFC18559DBB98A1'; Bytes=191336L }
            'shimx64.efi'=@{ Sha256='6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B'; Bytes=966664L }
            'tcglog.ib'=@{ Sha256='F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'; Bytes=40838L }
            'handoff-status.txt'=@{ Sha256='D371E06861AE50638D36023BA06A6A233E69208ED3BA119E9565236FD9C29C55'; Bytes=617L }
            'payload-diagnostic.txt'=@{ Sha256='E7679BC5BE861DC410088D2C8104EF1DEE3B2EB1A1CF963E02B45B09F7D42226'; Bytes=577L }
        }
        $pins = @($state.GpuRetirement193833StagePins)
        Assert-True ($pins.Count -eq 7 -and @($pins.Name | Sort-Object -Unique).Count -eq 7) 'The closed profile does not contain exactly seven unique stage files.'
        foreach ($pin in $pins) {
            Assert-True ($expected.ContainsKey($pin.Name) -and $pin.Sha256 -ceq $expected[$pin.Name].Sha256 -and [long]$pin.Bytes -eq [long]$expected[$pin.Name].Bytes) "The fixed 193833 pin differs for $($pin.Name)."
        }
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $pins
        $selected = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        Assert-True ($selected.Profile -ceq '193833' -and $selected.RunId -ceq 'gpu-20261021T193833Z-c48461667089493988074edfaf008d56' -and $selected.SourceCommit -ceq '15842e7d789eddaea401754734ce9540a10b69e7') 'Profile selection did not bind the fixed 193833 run identity.'
        Assert-True ($state.GpuRetirement193833JournalSha256 -ceq '1841DB8A726043FE6D3D9A22A6BBA70FF0E6AF3008D2437103F8461E0193BE8B' -and $state.GpuRetirement193833JournalBytes -eq 52111 -and
            $state.GpuRetirement193833ArchiveSha256 -ceq 'E0611ECB3A3F1DC94A064D40783E68A495C5FFED761BE01626C4BC95E63916A3' -and $state.GpuRetirement193833ArchiveBytes -eq 533) 'The 193833 completed journal and archived selector pins differ from the reviewed evidence.'
        Assert-True ($state.GpuRetirement193833ReadinessSha256 -ceq '3C8D0FB6E4A4B52F18AAD800DE6151C99026C9537C47ED9906E2F79C0DEE4779' -and $state.GpuRetirement193833ReadinessBytes -eq 12050 -and
            $state.GpuRetirement193833ReviewReceiptSha256 -ceq '82B73A584E7DF37F223D0E5F9B8E5E69B4E68A854F1905E17F9E9AA66B1346EF' -and $state.GpuRetirement193833ReviewReceiptBytes -eq 16103 -and
            $state.GpuRetirement193833FinalResultSha256 -ceq 'B4A9A74DF11382ACDBACDA6256E2F9B9EEB5071D9D9FD02F0DC81B0DB3CC7039' -and $state.GpuRetirement193833FinalResultBytes -eq 9184) 'The 193833 readiness, read-only review, or accepted FinalVerify result pin differs from the reviewed evidence.'
        Assert-True ($state.GpuRetirement193833ReadinessRelativePath -ceq $readinessRelativePath -and
            $state.GpuRetirement193833PreflightSha256 -ceq '79536B4C213967D18AA54D96C05130CA2F4E55FA6B9C656777003A194FBE43C6' -and
            $state.GpuRetirement193833PreflightBytes -eq 8861) 'The fixed readiness path or supporting preflight pin differs from the captured evidence.'
        Assert-True ($state.GpuRetirement193833RawCaptureSha256 -ceq '450434A4447DC11F57FBC9859C5369960DBAA7D42B1700CAA8BC74B849099B9F' -and $state.GpuRetirement193833RawCaptureBytes -eq 253 -and
            $state.GpuRetirement193833ReferenceLogSha256 -ceq 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8' -and $state.GpuRetirement193833ReferenceLogBytes -eq 40838 -and
            $state.GpuRetirement193833ProbeSha256 -ceq 'F2CACE0FA3BF07140593B64E48529C6CA5AF0EA036E4B500F2F98C3046212333' -and $state.GpuRetirement193833ProbeBytes -eq 172544 -and
            $state.GpuRetirement193833OriginalFirmwareSha256 -ceq '98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80' -and
            $state.GpuRetirement193833ReviewedFirmwareSha256 -ceq '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007') 'The raw boot capture, reference log, health probe, or firmware baseline pins differ from the reviewed evidence.'
        Assert-True ($state.GpuRetirementAuditToolSha256 -ceq 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' -and $state.GpuRetirementAuditToolBytes -eq 246784) 'The fixed read-only integrity audit tool pin differs from the reviewed evidence.'
        Assert-True ($selected.ReadinessPath -ceq [IO.Path]::GetFullPath($readiness.Path) -and $selected.ReadinessPin.Bytes -gt 0 -and @($selected.StagePins).Count -eq 7) 'Selection did not bind the explicit readiness report and seven fixed stage pins.'
        foreach ($older in @('201140','224018','023306','032117','042142','062106','160027')) {
            $selector = "Select-GpuStageRetirement${older}Profile"
            Assert-Throws { & $module { param($name,$root,$path) & $name -WorkspaceRoot $root -ReadinessPath $path } $selector $workspaceRoot $readiness.Path } 'already bound' "The older $older profile was allowed to rebind after selecting 193833."
        }
    }

    Invoke-TestCase '193833 readiness selection rejects missing, outside, non-GUID, and reparse paths' {
        $state = Get-ProfileState (Import-TestModule)
        $valid = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        $missing = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\ffffffffffffffffffffffffffffffff\readiness.json'
        Assert-Throws { Select-TestProfile -Module (Import-TestModule) -WorkspaceRoot $workspaceRoot -ReadinessPath $missing } 'missing|not found' 'A missing explicit readiness file was accepted.'

        $outsideDirectory = Join-Path $fixtureRoot 'outside-readiness'
        [void][IO.Directory]::CreateDirectory($outsideDirectory)
        $outside = Join-Path $outsideDirectory 'readiness.json'
        Copy-Item -LiteralPath $valid.Path -Destination $outside
        Assert-Throws { Select-TestProfile -Module (Import-TestModule) -WorkspaceRoot $workspaceRoot -ReadinessPath $outside } 'workspace|within|outside|path' 'A readiness report outside the workspace was accepted.'

        $nonGuid = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\readiness.json'
        Copy-Item -LiteralPath $valid.Path -Destination $nonGuid
        Assert-Throws { Select-TestProfile -Module (Import-TestModule) -WorkspaceRoot $workspaceRoot -ReadinessPath $nonGuid } 'GUID' 'A readiness report outside its GUID directory was accepted.'

        $otherGuidDirectory = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab'
        [void][IO.Directory]::CreateDirectory($otherGuidDirectory)
        $otherGuidPath = Join-Path $otherGuidDirectory 'readiness.json'
        Copy-Item -LiteralPath $valid.Path -Destination $otherGuidPath
        Assert-Throws { Select-TestProfile -Module (Import-TestModule) -WorkspaceRoot $workspaceRoot -ReadinessPath $otherGuidPath } 'fixed 193833 ownership-review report' 'A different valid GUID readiness path was accepted.'

        $linkParent = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness'
        [void][IO.Directory]::CreateDirectory($linkParent)
        $linkedDirectory = Join-Path $linkParent 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        $linkedPath = Join-Path $linkedDirectory 'readiness.json'
        $junctionAvailable = $false
        try {
            New-Item -ItemType Junction -Path $linkedDirectory -Target $valid.Directory -ErrorAction Stop | Out-Null
            $junctionAvailable = $true
        } catch { }
        if ($junctionAvailable) {
            $valid.Report.EvidencePath = $linkedPath
            $valid.Report.EvidenceDirectory = $linkedDirectory
            Write-TestJson -Path $valid.Path -Value $valid.Report
            try {
                Assert-Throws { Select-TestProfile -Module (Import-TestModule) -WorkspaceRoot $workspaceRoot -ReadinessPath $linkedPath } 'reparse|junction|symbolic link' 'A reparse-point readiness path was accepted.'
            } finally { [IO.Directory]::Delete($linkedDirectory, $false) }
        } else { Write-Output 'SKIP: Reparse-point readiness fixture unavailable on this filesystem.' }
    }

    Invoke-TestCase '193833 selector rejects every previously selected closed profile' {
        foreach ($older in @('201140','224018','023306','032117','042142','062106','160027')) {
            $module = Import-TestModule
            & $module {
                param($profile)
                Set-Variable -Scope Script -Name ('GpuRetirement' + $profile + 'ProfileSelected') -Value $true -Force
            } $older
            $unreadReadiness = Join-Path $workspaceRoot '.analysis\gpu-baremetal-readiness\ffffffffffffffffffffffffffffffff\readiness.json'
            Assert-Throws {
                & $module { param($root,$path) Select-GpuStageRetirement193833Profile -WorkspaceRoot $root -ReadinessPath $path } $workspaceRoot $unreadReadiness
            } 'already bound' "The 193833 selector accepted a previously selected $older profile."
        }
    }

    Invoke-TestCase '193833 keeps original journal and review-inspector platform pins distinct' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        Assert-True ($state.GpuRetirement193833StateSha256 -ceq '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC' -and $state.GpuRetirement193833StateBytes -eq 19843 -and
            $state.GpuRetirement193833PreparePlatformSha256 -ceq '43AA4F580F6D896068D8D4DE4139A509CC19254647D400DA5803115E5957D7FE' -and $state.GpuRetirement193833PreparePlatformBytes -eq 111899 -and
            $state.GpuRetirement193833ReceiptPlatformSha256 -ceq '66A3B826C01A3F3A3D962DB5798C81FA0FE758325BC0734D93778691409125B2' -and $state.GpuRetirement193833ReceiptPlatformBytes -eq 133220 -and
            $state.GpuRetirement193833HistoricalRetirementSha256 -ceq '24CDEFAE98EFFCFF664CB269E3642C35E4920EB7560F1492010C766486143E59' -and $state.GpuRetirement193833HistoricalRetirementBytes -eq 372336) 'The historical journal and review-receipt module pins differ from the audited source facts.'
        Assert-True ($state.GpuRetirement193833CurrentPlatformSha256 -ceq '0881A2EBE0C292107DDD7D83E165830FEBE617A31B0CA1A8FF731DB723EDFC4C' -and $state.GpuRetirement193833CurrentPlatformBytes -eq 133298 -and
            $state.GpuRetirement193833ReviewHelperSha256 -ceq '37B8C016B9BB4A47BA41423BF38DD4B59E305D51DC2F9E6B2D7F5C2FAD49BFCB' -and $state.GpuRetirement193833ReviewHelperBytes -eq 81754 -and
            $state.GpuRetirement193833InspectorSha256 -ceq '0F127D49BC45C8F597CD0AE0B7D1CF2016E5FAE41509EBB276779483C87A0E75' -and $state.GpuRetirement193833InspectorBytes -eq 46304) 'The later platform, review helper, or inspector pins differ from the audited source facts.'
        $sourceRoot = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'
        $statePath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostState.psm1'
        $platformPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
        $retirementPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuStageRetirement.psm1'
        $journal = [pscustomobject]@{ SourcePins=@(
            [pscustomobject]@{ Path=$statePath; Sha256=$state.GpuRetirement193833StateSha256; Bytes=$state.GpuRetirement193833StateBytes },
            [pscustomobject]@{ Path=$platformPath; Sha256=$state.GpuRetirement193833PreparePlatformSha256; Bytes=$state.GpuRetirement193833PreparePlatformBytes }
        ) }
        $modulePins = [pscustomobject]@{
            State=[pscustomobject]@{ Path=$statePath; Sha256=$state.GpuRetirement193833StateSha256; Bytes=$state.GpuRetirement193833StateBytes }
            Platform=[pscustomobject]@{ Path=$platformPath; Sha256=$state.GpuRetirement193833ReceiptPlatformSha256; Bytes=$state.GpuRetirement193833ReceiptPlatformBytes }
            Retirement=[pscustomobject]@{ Path=$retirementPath; Sha256=$state.GpuRetirement193833HistoricalRetirementSha256; Bytes=$state.GpuRetirement193833HistoricalRetirementBytes }
        }
        $receipt = [pscustomobject]@{ PinsBefore=[pscustomobject]@{ Modules=$modulePins }; PinsAfter=[pscustomobject]@{ Modules=$modulePins } }
        & $module { param($j,$r,$root) Assert-GpuStageRetirement193833HistoricalSourcePins -Journal $j -ReviewReceipt $r -SourceRoot $root } $journal $receipt $sourceRoot
        $badJournal = $journal | ConvertTo-Json -Depth 10 | ConvertFrom-Json
        $badJournal.SourcePins[1].Sha256 = $state.GpuRetirement193833ReceiptPlatformSha256
        $badJournal.SourcePins[1].Bytes = $state.GpuRetirement193833ReceiptPlatformBytes
        Assert-Throws { & $module { param($j,$r,$root) Assert-GpuStageRetirement193833HistoricalSourcePins -Journal $j -ReviewReceipt $r -SourceRoot $root } $badJournal $receipt $sourceRoot } 'closed 193833 evidence pin' 'The inspector platform pin was accepted as the journal source pin.'
        $badReceipt = $receipt | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $badReceipt.PinsBefore.Modules.Platform.Sha256 = $state.GpuRetirement193833PreparePlatformSha256
        $badReceipt.PinsBefore.Modules.Platform.Bytes = $state.GpuRetirement193833PreparePlatformBytes
        Assert-Throws { & $module { param($j,$r,$root) Assert-GpuStageRetirement193833HistoricalSourcePins -Journal $j -ReviewReceipt $r -SourceRoot $root } $journal $badReceipt $sourceRoot } 'closed 193833 evidence pin' 'The journal platform pin was accepted as the inspector module pin.'
    }

    Invoke-TestCase '193833 readiness requires a clean complete preflight and exactly the retained-stage blocker' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $base = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        Set-TestReadinessProfilePins -Module $module -Fixture $base
        & $module { param($value,$path) Assert-GpuStageRetirement193833Readiness -Readiness $value -ReadinessFullPath $path } $base.Report $base.Path
        $mutations = @(
            @{ Name='dirty source'; Edit={ param($r) $r.Source.Dirty=$true } },
            @{ Name='nonzero source dirty count'; Edit={ param($r) $r.Source.DirtyEntryCount=1 } },
            @{ Name='wrong source branch'; Edit={ param($r) $r.Source.Branch='main' } },
            @{ Name='invalid source commit'; Edit={ param($r) $r.Source.Commit='not-a-commit' } },
            @{ Name='source pin metadata changed'; Edit={ param($r) $r.Source.Pins[0].Sha256='0'*64 } },
            @{ Name='source collection error'; Edit={ param($r) $r.Source | Add-Member -NotePropertyName Error -NotePropertyValue 'injected source error' } },
            @{ Name='incomplete preflight'; Edit={ param($r) $r.Preflight.Complete=$false } },
            @{ Name='preflight pin metadata changed'; Edit={ param($r) $r.Preflight.Pin.Sha256='0'*64 } },
            @{ Name='incomplete source inventory'; Edit={ param($r) $r.Preflight.InventoryComplete=$false } },
            @{ Name='preflight adapter error'; Edit={ param($r) $r.Preflight.Error='injected preflight error' } },
            @{ Name='missing Intel adapter'; Edit={ param($r) $r.Preflight.IntelPresent=$false } },
            @{ Name='missing Nvidia adapter'; Edit={ param($r) $r.Preflight.NvidiaPresent=$false } },
            @{ Name='selector appeared'; Edit={ param($r) $r.Selector.Exists=$true } },
            @{ Name='ESP remains mounted after cleanup'; Edit={ param($r) $r.Esp.MountStateAfterCleanup='Mounted' } },
            @{ Name='stage inventory incomplete'; Edit={ param($r) $r.ExistingStage.Complete=$false } },
            @{ Name='stage count differs'; Edit={ param($r) $r.ExistingStage.EntryCount=6 } },
            @{ Name='missing readiness pin'; Edit={ param($r) $r.ExistingStage.Entries=@($r.ExistingStage.Entries | Select-Object -Skip 1); $r.ExistingStage.EntryCount=6; $r.ExistingStage.FileCount=6 } },
            @{ Name='extra readiness pin'; Edit={ param($r) $r.ExistingStage.Entries+= [pscustomobject]@{ RelativePath='EFI\insecure-boot\extra.bin'; Kind='File'; Bytes=1; Sha256=('A'*64); Hashed=$true }; $r.ExistingStage.EntryCount=8; $r.ExistingStage.FileCount=8 } },
            @{ Name='mismatched readiness pin'; Edit={ param($r) $r.ExistingStage.Entries[0].Sha256='0'*64 } },
            @{ Name='unknown stage entry'; Edit={ param($r) $r.ExistingStage.UnknownEntries=@('EFI\insecure-boot\unknown.bin') } },
            @{ Name='ESP files changed during readiness'; Edit={ param($r) $r.Effects.EspFilesChanged=$true } },
            @{ Name='firmware changed during readiness'; Edit={ param($r) $r.Effects.FirmwareChanged=$true } },
            @{ Name='boot state changed during readiness'; Edit={ param($r) $r.Effects.BootStateChanged=$true } },
            @{ Name='cleanup failed'; Edit={ param($r) $r.Cleanup.Succeeded=$false } },
            @{ Name='missing retained-stage blocker'; Edit={ param($r) $r.Blockers=@() } },
            @{ Name='additional unrelated blocker'; Edit={ param($r) $r.Blockers+= 'another blocker' } },
            @{ Name='different blocker text'; Edit={ param($r) $r.Blockers[0]='EFI\insecure-boot is ready to overwrite.' } }
        )
        foreach ($mutation in $mutations) {
            $fixture = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
            & $mutation.Edit $fixture.Report
            Set-TestReadinessProfilePins -Module $module -Fixture $fixture
            Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement193833Readiness -Readiness $value -ReadinessFullPath $path } $fixture.Report $fixture.Path } '193833|readiness|BLOCKED_READINESS|preflight|stage|inventory|pin|unknown|duplicate|selector|ESP|mounted|source|cleanup|blocker|effect|residue' "Readiness accepted $($mutation.Name)."
        }

        $tamperedSource = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        Set-TestReadinessProfilePins -Module $module -Fixture $tamperedSource
        $sourcePath = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\GpuHostState.psm1'
        [IO.File]::AppendAllText($sourcePath, "`n", [Text.UTF8Encoding]::new($false))
        Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement193833Readiness -Readiness $value -ReadinessFullPath $path } $tamperedSource.Report $tamperedSource.Path } 'source file changed|pin' 'A changed readiness source file was accepted.'

        $tamperedPreflight = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        Set-TestReadinessProfilePins -Module $module -Fixture $tamperedPreflight
        [IO.File]::WriteAllText((Join-Path $tamperedPreflight.Directory 'gpu-preflight-readiness.json'), '{"tampered":true}', [Text.UTF8Encoding]::new($false))
        Assert-Throws { & $module { param($value,$path) Assert-GpuStageRetirement193833Readiness -Readiness $value -ReadinessFullPath $path } $tamperedPreflight.Report $tamperedPreflight.Path } 'preflight report changed|pin' 'A changed GPU preflight report was accepted.'
    }

    Invoke-TestCase '193833 stage inventory rejects count, name, path, hash, size, and kind drift' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $entries = @($state.GpuRetirement193833StagePins | ForEach-Object {
            [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Hashed=$true; Bytes=$_.Bytes; Sha256=$_.Sha256 }
        })
        $accepted = & $module { param($items) Assert-GpuStageRetirement193833StageInventory -Entries $items -Description 'fixture' } $entries
        Assert-True (@($accepted).Count -eq 7) 'The exact fixed seven-file inventory was not accepted.'
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
            Assert-Throws { & $module { param($items) Assert-GpuStageRetirement193833StageInventory -Entries $items -Description 'fixture' } $candidate } 'exactly (the )?seven|unknown|duplicate|pin differs|non-file|unhashed|outside' "Stage inventory accepted $($mutation.Name)."
        }
    }

    Invoke-TestCase '193833 boot-selection proof fixes null BootNext, order 0 through 2, and zero findings' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $makeCapture = {
            param([object[]]$Order=@(0,1,2), [object]$BootNext=$null, [string]$Findings='00000000')
            [pscustomobject]@{ Baseline=[pscustomobject]@{
                Schema=1; BootNext=$BootNext; BootOrder=[object[]]$Order
                BootManagerSha256=$state.GpuRetirement193833BootManagerSha256
                ReferenceLogSha256=$state.GpuRetirement193833ReferenceLogSha256
                IntegrityFindings=$Findings
            } }
        }
        $valid = & $makeCapture
        Assert-True ($null -ne $valid.Baseline.PSObject.Properties['BootNext']) 'The raw boot capture omitted BootNext.'
        $null = & $module { param($capture) Assert-GpuStageRetirement193833BootSelection -Capture $capture -Checkpoint 'fixture' } $valid
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement193833BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -Order @(0,2,1)) } 'BootOrder|sequence 0 through 2' 'A changed BootOrder was accepted.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement193833BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -Order @()) } 'BootOrder|sequence 0 through 2|raw boot-selection' 'An empty BootOrder was accepted.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement193833BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -Order @(0,1,1)) } 'BootOrder|sequence 0 through 2' 'A duplicate BootOrder entry was accepted.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement193833BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -BootNext 4) } 'BootNext|raw boot-selection' 'A set BootNext was accepted.'
        Assert-Throws { & $module { param($capture) Assert-GpuStageRetirement193833BootSelection -Capture $capture -Checkpoint 'fixture' } (& $makeCapture -Findings '00000001') } 'findings|raw boot-selection' 'A nonzero integrity finding was accepted.'
    }

    Invoke-TestCase '193833 raw capture binds actual null BootNext, ordered boot entries, and zero findings' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        $valid = New-RawCaptureFixture -Module $module -State $state -WorkspaceRoot $workspaceRoot
        $null = & $module {
            param($capture,$root,$relative,$leaf)
            Assert-GpuStageRetirement193833RawCapture -Capture $capture -WorkspaceRoot $root -ExpectedRelativePath $relative `
                -ExpectedIntegrityReceiptLeaf $leaf -Description 'fixture'
        } $valid.Capture $workspaceRoot $valid.RelativePath 'before-bootmgr-integrity-fixture.receipt.json'
        Assert-True ($valid.Capture.BaselinePin.Bytes -eq 253 -and [string]::Join(',', [string[]]$valid.Capture.Baseline.BootOrder) -ceq '0,1,2') 'The valid fixture is not the reviewed 253-byte 0/1/2 raw capture.'
        $metadataDrift = New-RawCaptureFixture -Module $module -State $state -WorkspaceRoot $workspaceRoot
        $metadataDrift.Capture.Baseline.BootOrder[1] = 2
        Assert-Throws {
            & $module { param($capture,$root,$relative,$leaf) Assert-GpuStageRetirement193833RawCapture -Capture $capture -WorkspaceRoot $root -ExpectedRelativePath $relative -ExpectedIntegrityReceiptLeaf $leaf -Description 'fixture' } `
                $metadataDrift.Capture $workspaceRoot $metadataDrift.RelativePath 'before-bootmgr-integrity-fixture.receipt.json'
        } 'BootOrder|raw boot-selection' 'Changed capture metadata passed while the raw file remained pinned.'
        $tamperedRaw = New-RawCaptureFixture -Module $module -State $state -WorkspaceRoot $workspaceRoot
        [IO.File]::WriteAllText($tamperedRaw.Capture.BaselinePath, '{"schema":1}', [Text.UTF8Encoding]::new($false))
        Assert-Throws {
            & $module { param($capture,$root,$relative,$leaf) Assert-GpuStageRetirement193833RawCapture -Capture $capture -WorkspaceRoot $root -ExpectedRelativePath $relative -ExpectedIntegrityReceiptLeaf $leaf -Description 'fixture' } `
                $tamperedRaw.Capture $workspaceRoot $tamperedRaw.RelativePath 'before-bootmgr-integrity-fixture.receipt.json'
        } 'pin|hash|fixed 193833|raw capture|fixed reviewed value|differs' 'A changed raw-capture file was accepted.'
        foreach ($case in @(
            @{ Name='raw BootNext'; BootNext=0; Order=@(0,1,2); Findings=0; Pattern='BootNext|raw capture lacks' },
            @{ Name='raw BootOrder'; BootNext=$null; Order=@(0,2,1); Findings=0; Pattern='BootOrder differs' },
            @{ Name='raw integrity findings'; BootNext=$null; Order=@(0,1,2); Findings=1; Pattern='findings|raw capture lacks' }
        )) {
            $bad = New-RawCaptureFixture -Module $module -State $state -WorkspaceRoot $workspaceRoot `
                -RawBootNext $case.BootNext -RawOrder $case.Order -RawFindings $case.Findings
            Assert-Throws {
                & $module {
                    param($capture,$root,$relative,$leaf)
                    Assert-GpuStageRetirement193833RawCapture -Capture $capture -WorkspaceRoot $root -ExpectedRelativePath $relative `
                        -ExpectedIntegrityReceiptLeaf $leaf -Description 'fixture'
                } $bad.Capture $workspaceRoot $bad.RelativePath 'before-bootmgr-integrity-fixture.receipt.json'
            } $case.Pattern "The raw capture accepted $($case.Name)."
        }
    }

    Invoke-TestCase '193833 collection requires seven present pins and preserves the five Stage action rows' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        $runDirectory = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\runs') $state.GpuRetirement193833RunId
        $journalPath = Join-Path $runDirectory 'journal.json'
        $sourceRoot = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'
        $stageEntries = @($state.GpuRetirement193833StagePins | ForEach-Object {
            [pscustomobject]@{ RelativePath=('EFI\insecure-boot\' + $_.Name); Kind='File'; Bytes=$_.Bytes; Sha256=$_.Sha256; Hashed=$true }
        })
        $recordedStage = @($state.GpuRetirement193833StagePins | Where-Object { $_.Name -notin @('handoff-status.txt','payload-diagnostic.txt') } | ForEach-Object {
            [pscustomobject]@{ Name=$_.Name; RelativePath=('EFI\insecure-boot\' + $_.Name); Sha256=$_.Sha256; Bytes=$_.Bytes }
        })
        $collectionRows = @($state.GpuRetirement193833StagePins | ForEach-Object {
            $path = Join-Path (Join-Path $workspaceRoot $state.GpuRetirement193833CollectionActionRelativePath) ('esp-' + $_.Name)
            [pscustomobject]@{ Name=$_.Name; Present=$true; Path=$path; Pin=[pscustomobject]@{ Path=$path; Sha256=$_.Sha256; Bytes=$_.Bytes } }
        })
        $audit = [pscustomobject]@{
            SchemaVersion=1; Status='BLOCKED_READINESS'
            Esp=[pscustomobject]@{
                Identity=[pscustomobject]@{ Volume=$state.GpuRetirement193833EspVolume; DiskNumber=$state.GpuRetirement193833EspDiskNumber; PartitionGuid=$state.GpuRetirement193833EspPartitionGuid }
                BootManagerPin=[pscustomobject]@{ Sha256=$state.GpuRetirement193833BootManagerSha256; Bytes=3086848L }
                MountStateAfterCleanup='Unmounted'
            }
            ExistingStage=[pscustomobject]@{ Exists=$true; Complete=$true; EntryCount=7; FileCount=7; Entries=$stageEntries; UnknownEntries=@(); UnsafeEntries=@(); Errors=@() }
            ExistingResidue=[pscustomobject]@{ Clear=$true; Residues=@() }
        }
        $journal = [pscustomobject]@{
            SchemaVersion=1; RunId=$state.GpuRetirement193833RunId; SourceCommit=$state.GpuRetirement193833SourceCommit; SourceRoot=$sourceRoot
            RunDirectory=$runDirectory; Phase='Complete'; InProgress=$null; DisarmedBootId=$state.GpuRetirement193833DisarmedBootId
            LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status=$state.GpuRetirement193833FinalResultStatus; Directory=(Join-Path $workspaceRoot $state.GpuRetirement193833FinalActionRelativePath) }
            Host=[pscustomobject]@{ LogicalCpus=20 }
            FinalVerification=[pscustomobject]@{
                ResetBoundaryPassed=$true; ActivationAttempted=$true; SecureBoot=$true
                Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20; CpuPassCount=0 }
                Esp=[pscustomobject]@{ MatchesBaseline=$true }
                FirmwareEnumeration=[pscustomobject]@{
                    TextEqual=$false; ReviewedDifferenceAccepted=$true
                    BaselineSha256=$state.GpuRetirement193833OriginalFirmwareSha256
                    CurrentSha256=$state.GpuRetirement193833ReviewedFirmwareSha256
                }
                Errors=@()
            }
            Backup=[pscustomobject]@{
                EspVolume=$state.GpuRetirement193833EspVolume; EspDiskNumber=$state.GpuRetirement193833EspDiskNumber
                EspPartitionGuid=$state.GpuRetirement193833EspPartitionGuid; BootManagerSha256=$state.GpuRetirement193833BootManagerSha256
            }
            Stage=[pscustomobject]@{ Files=$recordedStage }
            Collection=[pscustomobject]@{ Esp=[pscustomobject]@{ StageFiles=$collectionRows } }
        }
        $archive = [pscustomobject]@{ SchemaVersion=1; RunId=$state.GpuRetirement193833RunId; RunDirectory=$runDirectory; JournalPath=$journalPath; SourceRoot=$sourceRoot }
        $arguments = @{
            Audit=$audit; Journal=$journal; ArchivedSelector=$archive; ExpectedAuditSha256=('A'*64)
            ExpectedRunId=$state.GpuRetirement193833RunId; ExpectedRunDirectory=$runDirectory
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
        Assert-True (@($accepted).Count -eq 7) 'The exact seven present files were not accepted for retirement.'

        foreach ($case in @(
            @{ Name='missing diagnostic row'; Edit={ param($j) $j.Collection.Esp.StageFiles=@($j.Collection.Esp.StageFiles | Where-Object Name -ne 'payload-diagnostic.txt') } },
            @{ Name='diagnostic row marked absent'; Edit={ param($j) ($j.Collection.Esp.StageFiles | Where-Object Name -ceq 'handoff-status.txt' | Select-Object -First 1).Present=$false } },
            @{ Name='diagnostic collection pin changed'; Edit={ param($j) ($j.Collection.Esp.StageFiles | Where-Object Name -ceq 'payload-diagnostic.txt' | Select-Object -First 1).Pin.Sha256='0'*64 } },
            @{ Name='stage action pin changed'; Edit={ param($j) ($j.Stage.Files | Where-Object Name -ceq 'shimx64.efi' | Select-Object -First 1).Sha256='0'*64 } },
            @{ Name='collection copy path outside run'; Edit={ param($j) ($j.Collection.Esp.StageFiles | Where-Object Name -ceq 'payload.efi' | Select-Object -First 1).Path='C:\outside\payload.efi' } },
            @{ Name='duplicate collection row'; Edit={ param($j) $j.Collection.Esp.StageFiles+= $j.Collection.Esp.StageFiles[0] } }
        )) {
            $bad = $journal | ConvertTo-Json -Depth 32 | ConvertFrom-Json
            & $case.Edit $bad
            $badArguments = @{} + $arguments; $badArguments.Journal=$bad
            Assert-Throws { & $invoke $module $badArguments } 'exactly seven|collected|stage pin|diagnostic|missing|unknown|duplicate|outside|collection differs|snapshot' "Collection accepted $($case.Name)."
        }
    }

    Invoke-TestCase '193833 closed-run proof rejects unrelated and failed completion variants' {
        $different = [pscustomobject]@{ RunId='gpu-unrelated-run' }
        $stateModule = Import-TestModule
        Assert-Throws { & $stateModule { param($journal,$root) Assert-GpuStageRetirement193833Proof -Journal $journal -WorkspaceRoot $root } $different $workspaceRoot } 'fixed archived 193833|193833 run identity' 'The proof accepted a different run id.'
        foreach ($failure in @('phase','source','status','action','action-path','errors','health','reset','boot','esp','firmware','review')) {
            $stateModule = Import-TestModule
            $state = Get-ProfileState $stateModule
            $fixture = New-ProofFixture -WorkspaceRoot $workspaceRoot -State $state -Failure $failure
            $pattern = 'The 193833 journal lacks'
            Assert-Throws { & $fixture.Module { param($journal,$root) Assert-GpuStageRetirement193833Proof -Journal $journal -WorkspaceRoot $root } $fixture.Journal $workspaceRoot } $pattern "Proof accepted the $failure fixture."
        }
    }

    Invoke-TestCase '193833 Apply rejects malformed evidence before mount or file deletion' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path

        # Keep the overall audit shape intact and corrupt one reviewed pin so
        # failure occurs inside the closed inventory gate before platform calls.
        $readiness.Report.ExistingStage.Entries[0].Sha256 = ('0' * 64)
        Write-TestJson -Path $readiness.Path -Value $readiness.Report
        $runId = $state.GpuRetirement193833RunId
        $runDirectory = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\runs') $runId
        $journalPath = Join-Path $runDirectory 'journal.json'
        $archivePath = Join-Path (Join-Path $workspaceRoot '.analysis\gpu-baremetal\archive') ($runId + '.json')
        $sourceRoot = Join-Path $workspaceRoot '.codex-build\worktrees\gpu-windows-validation'
        $journal = [pscustomobject]@{
            SchemaVersion=1; RunId=$runId; RunDirectory=$runDirectory; SourceRoot=$sourceRoot
            SourceCommit=$state.GpuRetirement193833SourceCommit; Phase='Complete'; InProgress=$null
            DisarmedBootId=$state.GpuRetirement193833DisarmedBootId
            LastAction=[pscustomobject]@{ Action='FinalVerify'; Succeeded=$true; Status=$state.GpuRetirement193833FinalResultStatus }
            Host=[pscustomobject]@{ LogicalCpus=20 }
            FinalVerification=[pscustomobject]@{
                ResetBoundaryPassed=$true; SecureBoot=$true; ActivationAttempted=$true
                Health=[pscustomobject]@{ Passed=$true; ExpectedResult='ABSENT'; Aggregate='ABSENT'; CpuCount=20; ExpectedCpus=20 }
                Esp=[pscustomobject]@{ MatchesBaseline=$true }
                FirmwareEnumeration=[pscustomobject]@{
                    TextEqual=$false; ReviewedDifferenceAccepted=$true
                    BaselineSha256=$state.GpuRetirement193833OriginalFirmwareSha256
                    CurrentSha256=$state.GpuRetirement193833ReviewedFirmwareSha256
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
                -ExpectedRunId $runId -ExpectedProbeSha256 $state.GpuRetirement193833ProbeSha256 `
                -ExpectedProbeBytes $state.GpuRetirement193833ProbeBytes
        } $ToolRoot $workspaceRoot $operations $auditPin $journalPin $archivePin $runId $runDirectory $journalPath $sourceRoot $state
        Assert-True (-not $report.Succeeded -and @($report.Errors).Count -gt 0) 'Apply accepted a corrupted readiness inventory.'
        Assert-True ($calls.Mount -eq 0 -and $calls.Delete -eq 0 -and $null -eq $report.LocalReceiptDirectory) 'A mount, deletion, or receipt-directory creation happened before evidence validation.'
    }

    Invoke-TestCase '193833 live stage inventory accepts seven private files and rejects extra entries and directories' {
        $module = Import-TestModule
        $state = Get-ProfileState $module
        $readiness = New-ReadinessFixture -WorkspaceRoot $workspaceRoot -StagePins $state.GpuRetirement193833StagePins
        $null = Select-TestProfile -Module $module -WorkspaceRoot $workspaceRoot -ReadinessPath $readiness.Path
        $stageDirectory = Join-Path $fixtureRoot 'inventory-valid'
        [void][IO.Directory]::CreateDirectory($stageDirectory)
        foreach ($pin in $state.GpuRetirement193833StagePins) { [IO.File]::WriteAllText((Join-Path $stageDirectory $pin.Name), "fixture $($pin.Name)", [Text.UTF8Encoding]::new($false)) }
        $inventory = & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory
        Assert-True ($inventory.Count -eq 7 -and @($inventory.Keys).Count -eq 7) 'The seven-file private live inventory was not accepted.'

        [IO.File]::WriteAllText((Join-Path $stageDirectory 'unexpected.bin'), 'fixture', [Text.UTF8Encoding]::new($false))
        Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory } 'exactly seven|unknown or duplicate' 'An extra live stage file was accepted.'
        Remove-Item -LiteralPath (Join-Path $stageDirectory 'unexpected.bin') -Force
        [void][IO.Directory]::CreateDirectory((Join-Path $stageDirectory 'nested'))
        Assert-Throws { & $module { param($path) Get-GpuStageRetirementLiveInventory -StageDirectory $path } $stageDirectory } 'exactly seven|directory or reparse' 'A nested live-stage directory was accepted.'
    }

    $reparseRoot = Join-Path $fixtureRoot 'inventory-reparse'
    $reparseTarget = Join-Path $fixtureRoot 'inventory-reparse-target'
    [void][IO.Directory]::CreateDirectory($reparseTarget)
    $junctionAvailable = $false
    try { New-Item -ItemType Junction -Path $reparseRoot -Target $reparseTarget -ErrorAction Stop | Out-Null; $junctionAvailable = $true } catch { }
    if ($junctionAvailable) {
        Invoke-TestCase '193833 live inventory rejects a reparse-point root' {
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
    Write-Output 'All 193833 retirement contract tests passed.'
}
finally {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $fixtureRoot))
    if ([string]::Equals($resolvedParent, $fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fixtureRoot) -and
        ([IO.File]::GetAttributes($fixtureRoot) -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
