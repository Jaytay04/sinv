[CmdletBinding()]
param([switch]$SimulateLegacyForce)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:Assertions = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-WriterCommandsAvailable {
    param([switch]$RequirePlatformAction)
    $writer = Get-Command -Name 'Write-GpuJson' -CommandType Function -ErrorAction Stop
    Assert-True ($null -ne $writer) 'Write-GpuJson is no longer visible in the dispatcher scope.'
    if ($RequirePlatformAction) {
        $platformAction = Get-Command -Name 'Invoke-GpuHostPlatformAction' -CommandType Function -ErrorAction Stop
        Assert-True ($null -ne $platformAction) 'Invoke-GpuHostPlatformAction is no longer visible in the dispatcher scope.'
    }
}

function Assert-JsonMarker {
    param([string]$Path, [string]$ExpectedMarker)
    Write-GpuJson -Path $Path -Value ([pscustomobject]@{ Marker = $ExpectedMarker }) -NoOverwrite
    $saved = Read-GpuJson -Path $Path
    Assert-True ([string]$saved.Marker -ceq $ExpectedMarker) "JSON written through Write-GpuJson did not round-trip: $Path"
}

$actualToolRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$actualFiles = @('GpuHostState.psm1', 'GpuHostPlatform.psm1', 'GpuStageRetirement.psm1', 'GpuFinalFirmwareReview.psm1')
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot ('GpuFinalFirmwareModuleScope-' + [guid]::NewGuid().ToString('N'))))
$fixtureWorkspace = Join-Path $fixtureRoot 'workspace'
$fixtureSourceRoot = Join-Path $fixtureWorkspace '.codex-build\worktrees\gpu-windows-validation'
$fixtureToolRoot = Join-Path $fixtureSourceRoot 'tools\gpu_diagnostic'
$stateModule = $null
$platformModule = $null
$retirementModule = $null
$reviewModule = $null

try {
    foreach ($directory in @($fixtureToolRoot, (Join-Path $fixtureWorkspace '.analysis\gpu-final-firmware-review\8abb7b84ca644a7c84019a1f29b35bb4'), (Join-Path $fixtureWorkspace '.analysis\module-scope-test'))) {
        New-Item -ItemType Directory -Path $directory -Force -ErrorAction Stop | Out-Null
    }
    foreach ($name in $actualFiles) {
        $source = Join-Path $actualToolRoot $name
        $destination = Join-Path $fixtureToolRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required source module is missing: $source" }
        Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
    }

    if ($SimulateLegacyForce) {
        $fixtureReviewPath = Join-Path $fixtureToolRoot 'GpuFinalFirmwareReview.psm1'
        $reviewText = [IO.File]::ReadAllText($fixtureReviewPath)
        $legacyImport = 'Import-Module -Name $stateModulePath -DisableNameChecking -PassThru -ErrorAction Stop'
        $forceImport = 'Import-Module -Name $stateModulePath -Force -DisableNameChecking -PassThru -ErrorAction Stop'
        if (-not $reviewText.Contains($legacyImport)) { throw 'Cannot apply the isolated legacy -Force regression mutation to the copied review helper.' }
        $reviewText = $reviewText.Replace($legacyImport, $forceImport)
        [IO.File]::WriteAllText($fixtureReviewPath, $reviewText, (New-Object Text.UTF8Encoding($false)))
    }

    $statePath = Join-Path $fixtureToolRoot 'GpuHostState.psm1'
    $platformPath = Join-Path $fixtureToolRoot 'GpuHostPlatform.psm1'
    $retirementPath = Join-Path $fixtureToolRoot 'GpuStageRetirement.psm1'
    $reviewPath = Join-Path $fixtureToolRoot 'GpuFinalFirmwareReview.psm1'

    # Simulate the dispatcher: it owns the state writer before the platform module loads.
    $stateModule = Import-Module -Name $statePath -PassThru -ErrorAction Stop | Select-Object -First 1
    Assert-WriterCommandsAvailable
    $platformModule = Import-Module -Name $platformPath -PassThru -DisableNameChecking -ErrorAction Stop | Select-Object -First 1
    Assert-WriterCommandsAvailable -RequirePlatformAction
    $retirementModule = Import-Module -Name $retirementPath -PassThru -DisableNameChecking -ErrorAction Stop | Select-Object -First 1

    # The helper must wire real private-module handles into its callbacks. Replace
    # only the callback targets so the exercise performs no native or host queries.
    & $retirementModule {
        $captureStub = {
            param([object]$State, [string]$ActionDirectory, [string]$Checkpoint,
                  [string]$AuditToolPath, [object]$PlatformModule,
                  [string]$ExpectedToolHash, [long]$ExpectedToolBytes,
                  [string]$ExpectedLogHash, [long]$ExpectedLogBytes)
            if ([string]::IsNullOrWhiteSpace($ActionDirectory) -or
                [string]::IsNullOrWhiteSpace($AuditToolPath) -or $ExpectedToolBytes -le 0 -or $ExpectedLogBytes -le 0) {
                throw 'Boot-selection callback did not receive its pinned operation inputs.'
            }
            return [pscustomobject]@{
                Checkpoint = $Checkpoint
                Baseline = [pscustomobject]@{ BootNext = $null; BootOrder = [uint16[]]@(0, 1, 2, 3, 4, 5) }
            }
        }
        $stableStub = {
            param([object]$Before, [object]$Current, [string]$Checkpoint)
            if ($null -eq $Before -or $null -eq $Current -or
                [string]::Join(',', [string[]]$Before.BootOrder) -cne [string]::Join(',', [string[]]$Current.BootOrder) -or
                $null -ne $Before.BootNext -or $null -ne $Current.BootNext) {
                throw "Boot-selection stability callback rejected its fixture at $Checkpoint."
            }
            return $true
        }
        Set-Item -LiteralPath 'Function:\script:Invoke-GpuStageRetirementBootSelectionCapture' -Value $captureStub -ErrorAction Stop
        Set-Item -LiteralPath 'Function:\script:Assert-GpuStageRetirementBootSelectionStable' -Value $stableStub -ErrorAction Stop
    }
    & $platformModule {
        $pathLookupStub = { param([string]$Name) return 'fixture-bcdedit' }
        $nativeCommandStub = {
            param([string]$Program, [string[]]$Arguments, [string]$ActionDirectory,
                  [string]$Label, [int[]]$AllowedExitCodes)
            if ($Program -cne 'fixture-bcdedit' -or -not (Test-Path -LiteralPath $ActionDirectory -PathType Container)) {
                throw 'Firmware callback attempted to use an unexpected command or directory.'
            }
            $stdoutPath = Join-Path $ActionDirectory ('mock-' + $Label + '-' + [guid]::NewGuid().ToString('N') + '.stdout.txt')
            $stderrPath = Join-Path $ActionDirectory ('mock-' + $Label + '-' + [guid]::NewGuid().ToString('N') + '.stderr.txt')
            [IO.File]::WriteAllText($stdoutPath, 'fixture firmware enumeration', (New-Object Text.UTF8Encoding($false)))
            [IO.File]::WriteAllText($stderrPath, '', (New-Object Text.UTF8Encoding($false)))
            return [pscustomobject]@{ Succeeded = $true; ExitCode = 0; StdoutPath = $stdoutPath; StderrPath = $stderrPath; ReceiptPath = $stdoutPath }
        }
        Set-Item -LiteralPath 'Function:\script:Get-GpuApplicationPath' -Value $pathLookupStub -ErrorAction Stop
        Set-Item -LiteralPath 'Function:\script:Invoke-GpuNativeCommand' -Value $nativeCommandStub -ErrorAction Stop
    }

    # Preload the actual copied review module so only its hardware/evidence core can be replaced.
    $reviewModule = Import-Module -Name $reviewPath -PassThru -DisableNameChecking -ErrorAction Stop | Select-Object -First 1
    $statePin = Get-FileHash -LiteralPath $statePath -Algorithm SHA256 -ErrorAction Stop
    $stateBytes = [long](Get-Item -LiteralPath $statePath -ErrorAction Stop).Length
    $retirementPin = Get-FileHash -LiteralPath $retirementPath -Algorithm SHA256 -ErrorAction Stop
    $retirementBytes = [long](Get-Item -LiteralPath $retirementPath -ErrorAction Stop).Length

    $reviewDirectory = Join-Path $fixtureWorkspace '.analysis\gpu-final-firmware-review\8abb7b84ca644a7c84019a1f29b35bb4'
    $receiptPath = Join-Path $reviewDirectory 'result.json'
    $receiptJson = '{"Fixture":"module-scope-regression"}'
    [IO.File]::WriteAllText($receiptPath, $receiptJson, (New-Object Text.UTF8Encoding($false)))
    $receiptHash = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
    $receiptBytes = [long](Get-Item -LiteralPath $receiptPath -ErrorAction Stop).Length

    $testPins = [pscustomobject]@{
        AuditSha256 = $receiptHash; AuditBytes = $receiptBytes
        StateSha256 = $statePin.Hash.ToUpperInvariant(); StateBytes = $stateBytes
        RetirementSha256 = $retirementPin.Hash.ToUpperInvariant(); RetirementBytes = $retirementBytes
    }
    & $reviewModule {
        param($pins)
        $script:GpuFinalReviewAuditResultSha256 = $pins.AuditSha256
        $script:GpuFinalReviewAuditResultBytes = [long]$pins.AuditBytes
        $script:GpuFinalReviewStateSha256 = $pins.StateSha256
        $script:GpuFinalReviewStateBytes = [long]$pins.StateBytes
        $script:GpuFinalReviewRetirementSha256 = $pins.RetirementSha256
        $script:GpuFinalReviewRetirementBytes = [long]$pins.RetirementBytes
        $script:GpuFinalReviewModuleScopeTestThrows = $false
        $coreStub = {
            param([object]$State, [object]$CurrentHost, [object]$CurrentFirmwarePin,
                  [string]$ActionDirectory, [hashtable]$Operations)
            $bootBefore = & $Operations.CaptureBootSelection $State $ActionDirectory 'before'
            $bootAfter = & $Operations.CaptureBootSelection $State $ActionDirectory 'after'
            $null = & $Operations.AssertBootSelectionStable $bootBefore.Baseline $bootAfter.Baseline
            $firmwareCapture = & $Operations.CaptureFirmware $ActionDirectory
            if ($null -eq $bootBefore -or $null -eq $bootAfter -or
                [string]$firmwareCapture.Text -cne 'fixture firmware enumeration' -or
                -not [IO.File]::Exists([string]$firmwareCapture.Pin.Path)) {
                throw 'The review helper did not wire its read-only callback closures to the copied modules.'
            }
            if ($script:GpuFinalReviewModuleScopeTestThrows) { throw 'Expected module-scope regression fixture failure.' }
            return [pscustomobject]@{
                Accepted = $true; Fixture = 'hardware core stubbed'
                BeforeCheckpoint = $bootBefore.Checkpoint; AfterCheckpoint = $bootAfter.Checkpoint
                FirmwareSha256 = $firmwareCapture.Pin.Sha256
            }
        }
        Set-Item -LiteralPath 'Function:\script:Invoke-GpuFinalFirmwareReviewCore' -Value $coreStub -ErrorAction Stop
    } $testPins

    $runId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
    $runDirectory = Join-Path $fixtureWorkspace ('.analysis\gpu-baremetal\runs\' + $runId)
    $actionDirectory = Join-Path (Join-Path $runDirectory 'actions') '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
    $null = New-Item -ItemType Directory -Path $actionDirectory -Force -ErrorAction Stop
    $state = [pscustomobject]@{
        WorkspaceRoot = $fixtureWorkspace
        SourceRoot = $fixtureSourceRoot
        RunId = $runId
        RunDirectory = $runDirectory
    }
    $currentHost = [pscustomobject]@{ BootId = 'fixture-boot'; LogicalCpus = 20; SecureBoot = $true }
    $currentFirmwarePin = [pscustomobject]@{ Path = Join-Path $actionDirectory 'unused-firmware.txt'; Sha256 = ('A' * 64); Bytes = 1L }

    Assert-WriterCommandsAvailable -RequirePlatformAction
    $accepted = & $platformModule {
        param($runState, $hostSnapshot, $firmwarePin, $directory)
        Invoke-GpuFinalFirmwareReview -State $runState -CurrentHost $hostSnapshot -CurrentFirmwarePin $firmwarePin -ActionDirectory $directory
    } $state $currentHost $currentFirmwarePin $actionDirectory
    Assert-True ($accepted.Accepted -eq $true) 'The actual wrapper/public review helper did not return the stub core result.'
    Assert-True ($accepted.BeforeCheckpoint -ceq 'before' -and $accepted.AfterCheckpoint -ceq 'after') 'Boot-selection operation callbacks did not reach the copied retirement module.'
    Assert-True ([string]$accepted.FirmwareSha256 -match '^[0-9A-F]{64}$') 'Firmware operation callback did not return a pinned fixture capture.'
    Assert-WriterCommandsAvailable -RequirePlatformAction

    $journalPath = Join-Path $fixtureWorkspace '.analysis\module-scope-test\journal.json'
    Assert-JsonMarker -Path $journalPath -ExpectedMarker 'after-success'

    & $reviewModule { $script:GpuFinalReviewModuleScopeTestThrows = $true }
    $observedFailure = $null
    try {
        $null = & $platformModule {
            param($runState, $hostSnapshot, $firmwarePin, $directory)
            Invoke-GpuFinalFirmwareReview -State $runState -CurrentHost $hostSnapshot -CurrentFirmwarePin $firmwarePin -ActionDirectory $directory
        } $state $currentHost $currentFirmwarePin $actionDirectory
    }
    catch { $observedFailure = [string]$_.Exception.Message }
    Assert-True ($observedFailure -match 'Expected module-scope regression fixture failure') 'The mock review core failure did not propagate through the actual wrapper.'
    Assert-WriterCommandsAvailable -RequirePlatformAction

    $failureReceiptPath = Join-Path $fixtureWorkspace '.analysis\module-scope-test\failure-receipt.json'
    Assert-JsonMarker -Path $failureReceiptPath -ExpectedMarker 'after-expected-failure'
    Assert-WriterCommandsAvailable -RequirePlatformAction

    [pscustomobject]@{ Status = 'PASS'; Assertions = $script:Assertions; ActualModulesCopied = $actualFiles } | ConvertTo-Json -Depth 5
}
finally {
    foreach ($module in @($reviewModule, $retirementModule, $platformModule, $stateModule)) {
        if ($null -ne $module) {
            try { Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue } catch { }
        }
    }

    $tempPrefix = $tempRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if ([IO.Directory]::Exists($fixtureRoot) -and
        $fixtureRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -and
        -not [string]::Equals($fixtureRoot, $tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
