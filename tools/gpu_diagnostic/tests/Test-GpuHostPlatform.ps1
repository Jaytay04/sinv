[CmdletBinding()]
param(
    [string]$ToolRoot
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Split-Path -Parent $PSScriptRoot
}
$ToolRoot = [System.IO.Path]::GetFullPath($ToolRoot)
$platformPath = Join-Path $ToolRoot 'GpuHostPlatform.psm1'
$stateModulePath = Join-Path $ToolRoot 'GpuHostState.psm1'
foreach ($requiredPath in @($platformPath, $stateModulePath)) {
    if (-not [System.IO.File]::Exists($requiredPath)) {
        throw "Required GPU diagnostic helper is missing: $requiredPath"
    }
}

Import-Module -Name $stateModulePath -Force -ErrorAction Stop

$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = New-Object 'System.Collections.Generic.List[string]'

function Assert-PlatformTrue {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-PlatformEqual {
    param($Expected, $Actual, [string]$Message)
    $script:Assertions++
    if ($Expected -is [string] -or $Actual -is [string]) {
        if ([string]$Expected -cne [string]$Actual) {
            throw "$Message Expected '$Expected', got '$Actual'."
        }
    }
    elseif ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

function Assert-PlatformThrows {
    param([scriptblock]$Action, [string]$Message, [string]$ErrorPattern)
    $threw = $false
    $actualMessage = ''
    try {
        & $Action
    }
    catch {
        $threw = $true
        $actualMessage = $_.Exception.Message
    }
    Assert-PlatformTrue $threw $Message
    if (-not [string]::IsNullOrWhiteSpace($ErrorPattern)) {
        Assert-PlatformTrue ($actualMessage -match $ErrorPattern) "$Message Failed for an unexpected reason: $actualMessage"
    }
}

function Invoke-PlatformTestCase {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:PassedCases++
    }
    catch {
        [void]$script:FailedCases.Add("$Name`: $($_.Exception.Message)")
    }
}

function Get-PlatformFunctionTexts {
    param([string]$Path, [string[]]$Names)

    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$parseErrors)
    if ($null -ne $parseErrors -and $parseErrors.Count -gt 0) {
        throw "Platform module does not parse: $($parseErrors[0].Message)"
    }

    $functions = @($ast.FindAll({
        param($node)
        return ($node -is [System.Management.Automation.Language.FunctionDefinitionAst])
    }, $true))
    $texts = New-Object 'System.Collections.Generic.List[string]'
    foreach ($name in $Names) {
        $matches = @($functions | Where-Object { $_.Name -ceq $name })
        if ($matches.Count -ne 1) {
            throw "Expected exactly one function definition named $name; found $($matches.Count)."
        }
        [void]$texts.Add($matches[0].Extent.Text)
    }
    return $texts.ToArray()
}

function New-PlatformHelperModule {
    param([string]$ModulePath, [string]$StatePath)

    $functionNames = @(
        'Get-GpuObjectProperty',
        'Assert-GpuFreshPath',
        'Get-GpuSafeLeaf',
        'Get-GpuRunIdLeaf',
        'Invoke-GpuNativeCommand',
        'Assert-GpuPreflightAdapters',
        'Get-GpuDiskPhysicalClassification',
        'Assert-GpuNoReparseAncestors',
        'Copy-GpuEspTreeWithManifest',
        'Get-GpuRunPath',
        'Get-GpuBuildDirectories',
        'Get-GpuExpectedToolPaths',
        'Get-GpuExpectedSignedArtifactPaths',
        'Assert-GpuNamedPin',
        'Get-GpuStageExpectedFiles',
        'Get-GpuStageReadback',
        'Assert-GpuStageInputPins',
        'Assert-GpuMountedEspMatchesBackup',
        'Assert-GpuStageState'
    )
    $definitions = @(Get-PlatformFunctionTexts -Path $ModulePath -Names $functionNames)
    $stateTokens = $null
    $stateParseErrors = $null
    $stateAst = [System.Management.Automation.Language.Parser]::ParseFile($StatePath, [ref]$stateTokens, [ref]$stateParseErrors)
    if ($null -ne $stateParseErrors -and $stateParseErrors.Count -gt 0) {
        throw "State helper module does not parse: $($stateParseErrors[0].Message)"
    }
    $stateFunctions = @($stateAst.FindAll({
        param($node)
        return ($node -is [System.Management.Automation.Language.FunctionDefinitionAst])
    }, $true))
    if ($stateFunctions.Count -eq 0) { throw 'No pure state helper functions were found for the isolated module.' }
    $stateDefinitions = @($stateFunctions | ForEach-Object { $_.Extent.Text })

    # These mocks stand in only for helpers that query a mounted system ESP or
    # validate the external-disk backup. Both deny access by default. The stage
    # fixture explicitly enables its in-memory backup object.
    $mockDefinitions = @'
$script:GpuPlatformTestBackup = $null
$script:GpuPlatformTestBackupEnabled = $false
$script:GpuPlatformTestMountedHashCalls = 0

function Set-GpuPlatformTestBackup {
    param([Parameter(Mandatory = $true)][object]$Backup)
    $script:GpuPlatformTestBackup = $Backup
    $script:GpuPlatformTestBackupEnabled = $true
}

function Reset-GpuPlatformTestMocks {
    $script:GpuPlatformTestBackup = $null
    $script:GpuPlatformTestBackupEnabled = $false
    $script:GpuPlatformTestMountedHashCalls = 0
}

function Assert-GpuBackupState {
    param([object]$State)
    if (-not $script:GpuPlatformTestBackupEnabled -or $null -eq $script:GpuPlatformTestBackup) {
        throw 'TEST MOCK DENIED: backup validation was not explicitly enabled.'
    }
    return $script:GpuPlatformTestBackup
}

function Get-GpuMountedEspBootManagerHash {
    param([string]$EspRoot)
    $script:GpuPlatformTestMountedHashCalls++
    throw 'TEST MOCK DENIED: no mounted ESP hash read is allowed.'
}

function Get-GpuPlatformTestMountedHashCalls {
    return $script:GpuPlatformTestMountedHashCalls
}
'@

    $exports = @($functionNames + @(
        'Set-GpuPlatformTestBackup',
        'Reset-GpuPlatformTestMocks',
        'Get-GpuPlatformTestMountedHashCalls'
    ))
    $exportLine = 'Export-ModuleMember -Function @(' + (($exports | ForEach-Object { "'$_'" }) -join ',') + ')'
    $source = @(
        'Set-StrictMode -Version 2.0'
        $stateDefinitions
        $definitions
        $mockDefinitions
        $exportLine
    ) -join "`r`n"

    $isolatedModule = New-Module -Name ('GpuHostPlatformFixture' + [guid]::NewGuid().ToString('N')) `
        -ScriptBlock ([scriptblock]::Create($source))
    Import-Module -ModuleInfo $isolatedModule -Force -ErrorAction Stop
    return $isolatedModule
}

$workspaceRoot = Split-Path -Parent (Split-Path -Parent $ToolRoot)
$scratchParent = Join-Path $workspaceRoot '.codex-build\gpu-host-platform-tests'
$null = Assert-GpuPathWithin -Path $scratchParent -Root $workspaceRoot
if (-not [System.IO.Directory]::Exists($scratchParent)) {
    [void](New-Item -ItemType Directory -Path $scratchParent -ErrorAction Stop)
}
$null = Assert-GpuPathWithin -Path $scratchParent -Root $workspaceRoot -MustExist
$runRoot = Join-Path $scratchParent ('run-' + [guid]::NewGuid().ToString('N'))
$null = Assert-GpuPathWithin -Path $runRoot -Root $scratchParent
[void](New-Item -ItemType Directory -Path $runRoot -ErrorAction Stop)
$junctionPath = $null
$isolatedModule = $null

try {
    $isolatedModule = New-PlatformHelperModule -ModulePath $platformPath -StatePath $stateModulePath

    Invoke-PlatformTestCase 'Real module force imports preserve caller State commands and failure receipt writes' {
        $powershellPath = Join-Path $PSHOME 'powershell.exe'
        if (-not [System.IO.File]::Exists($powershellPath)) { throw "Windows PowerShell child is unavailable: $powershellPath" }

        # Use a clean process and caller script scope. Force-importing Platform
        # must preserve State exports already available to the caller.
        $childScript = Join-Path $runRoot 'module-scope-regression.ps1'
        $childSource = @'
param(
    [Parameter(Mandatory = $true)][string]$StateModulePath,
    [Parameter(Mandatory = $true)][string]$PlatformModulePath,
    [Parameter(Mandatory = $true)][string]$FixtureRoot
)
$ErrorActionPreference = 'Stop'
$assertions = 0
function Assert-ChildCondition([bool]$Condition, [string]$Message) {
    $script:assertions++
    if (-not $Condition) { throw $Message }
}
try {
    Import-Module -Name $StateModulePath -Force -ErrorAction Stop
    Assert-ChildCondition ($null -ne (Get-Command Write-GpuJson -ErrorAction SilentlyContinue)) 'Write-GpuJson is unavailable after the initial State import.'
    Assert-ChildCondition ($null -ne (Get-Command Read-GpuJson -ErrorAction SilentlyContinue)) 'Read-GpuJson is unavailable after the initial State import.'

    Import-Module -Name $PlatformModulePath -Force -ErrorAction Stop
    Assert-ChildCondition ($null -ne (Get-Command Write-GpuJson -ErrorAction SilentlyContinue)) 'Platform import removed caller Write-GpuJson.'
    Assert-ChildCondition ($null -ne (Get-Command Read-GpuJson -ErrorAction SilentlyContinue)) 'Platform import removed caller Read-GpuJson.'
    Import-Module -Name $PlatformModulePath -Force -ErrorAction Stop
    Assert-ChildCondition ($null -ne (Get-Command Write-GpuJson -ErrorAction SilentlyContinue)) 'Repeated Platform force import removed caller Write-GpuJson.'
    Assert-ChildCondition ($null -ne (Get-Command Read-GpuJson -ErrorAction SilentlyContinue)) 'Repeated Platform force import removed caller Read-GpuJson.'

    $runDirectory = Join-Path $FixtureRoot 'module-scope-run'
    $actionDirectory = Join-Path $runDirectory 'actions\prepare-fixture'
    [void](New-Item -ItemType Directory -Path $actionDirectory -Force)
    $fixtureState = [pscustomobject]@{
        RunDirectory = $runDirectory
        SourceRoot = ''
        Phase = 'Preparing'
        InProgress = [pscustomobject]@{ Action = 'Prepare' }
        LastFailure = $null
    }
    $failureMessage = $null
    try {
        Invoke-GpuHostPlatformAction -Action Prepare -State $fixtureState -ActionDirectory $actionDirectory
        throw 'Synthetic platform action unexpectedly succeeded.'
    }
    catch {
        $failureMessage = $_.Exception.Message
        if ($failureMessage -eq 'Synthetic platform action unexpectedly succeeded.') { throw }
    }
    Assert-ChildCondition ($failureMessage -match 'State\.SourceRoot is empty') 'The harmless synthetic platform failure did not stop at the expected local validation.'

    $fixtureState.LastFailure = $failureMessage
    $journalPath = Join-Path $runDirectory 'journal.json'
    $receiptPath = Join-Path $actionDirectory 'failure.json'
    Write-GpuJson -Path $journalPath -Value $fixtureState
    Write-GpuJson -Path $receiptPath -NoOverwrite -Value ([ordered]@{
        Action = 'Prepare'
        Failure = $failureMessage
        RunDirectory = $runDirectory
        InProgress = $fixtureState.InProgress
    })
    $journal = Read-GpuJson -Path $journalPath
    $receipt = Read-GpuJson -Path $receiptPath
    Assert-ChildCondition ($journal.LastFailure -match 'State\.SourceRoot is empty') 'Fixture journal did not preserve the platform failure.'
    Assert-ChildCondition ($receipt.Action -ceq 'Prepare' -and $receipt.Failure -match 'State\.SourceRoot is empty') 'Fixture failure receipt did not round-trip.'

    $platformModule = Get-Module | Where-Object {
        $_.Path -and [string]::Equals([System.IO.Path]::GetFullPath($_.Path), [System.IO.Path]::GetFullPath($PlatformModulePath), [System.StringComparison]::OrdinalIgnoreCase)
    } | Select-Object -First 1
    Assert-ChildCondition ($null -ne $platformModule) 'The real Platform module instance was not available for the recovery-path check.'
    $missingRecoveryBase = Join-Path $FixtureRoot 'does-not-exist-recovery-directory'
    $recoveryState = [pscustomobject]@{ RecoveryBase = $missingRecoveryBase }
    $recoveryProbe = & $platformModule {
        param($State)
        $script:GpuFixtureDiskQueries = 0
        function Get-GpuDiskForDrivePath {
            $script:GpuFixtureDiskQueries++
            throw 'Fixture disk-query guard: missing-directory check queried a disk.'
        }
        $message = $null
        try { Assert-GpuRecoveryLocation -State $State }
        catch { $message = $_.Exception.Message }
        [pscustomobject]@{ Message = $message; DiskQueries = $script:GpuFixtureDiskQueries }
    } $recoveryState
    Assert-ChildCondition ($recoveryProbe.DiskQueries -eq 0) 'Missing recovery directory reached physical disk lookup.'
    Assert-ChildCondition ($recoveryProbe.Message.Contains($missingRecoveryBase)) 'Missing recovery directory error did not identify the configured path.'
    Assert-ChildCondition ($recoveryProbe.Message -match '(?i)create|existing directory') 'Missing recovery directory error omitted actionable setup guidance.'

    Write-Output ("GPU_MODULE_SCOPE_RESULT:PASS assertions={0}" -f $assertions)
}
catch {
    [Console]::Error.WriteLine(("GPU_MODULE_SCOPE_RESULT:FAIL assertions={0} error={1}" -f $assertions, $_.Exception.Message))
    exit 1
}
'@
        [System.IO.File]::WriteAllText($childScript, $childSource, (New-Object System.Text.UTF8Encoding($false)))

        $childOutput = @(& $powershellPath -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $childScript `
            -StateModulePath $stateModulePath -PlatformModulePath $platformPath -FixtureRoot $runRoot 2>&1 | ForEach-Object { [string]$_ })
        $childExitCode = $LASTEXITCODE
        $childText = $childOutput -join "`n"
        if ($childExitCode -ne 0) { throw "Fresh-process module scope regression failed (exit $childExitCode): $childText" }

        $scopeMarker = @($childOutput | Where-Object { $_ -match '^GPU_MODULE_SCOPE_RESULT:PASS assertions=(\d+)$' })
        Assert-PlatformEqual 1 $scopeMarker.Count "Fresh-process module scope regression omitted its PASS marker: $childText"
        $scopeMarkerMatch = [regex]::Match([string]$scopeMarker[0], '^GPU_MODULE_SCOPE_RESULT:PASS assertions=(\d+)$')
        $script:Assertions += [int]$scopeMarkerMatch.Groups[1].Value
    }

    Invoke-PlatformTestCase 'Physical disk classifier handles normal MSFT_Disk shape and rejects virtual aliases and codes' {
        $normalDisk = [pscustomobject]@{
            Number = 3
            UniqueId = 'test-disk-unique-id'
            SerialNumber = 'test-disk-serial'
            FriendlyName = 'Fixture NVMe'
            BusType = 'NVMe'
        }
        Assert-PlatformTrue ($null -eq $normalDisk.PSObject.Properties['IsVirtual']) 'Normal MSFT_Disk fixture unexpectedly has IsVirtual.'
        $normal = Get-GpuDiskPhysicalClassification -Disk $normalDisk
        Assert-PlatformTrue ([bool]$normal.SupportedPhysical) 'A normal physical MSFT_Disk shape without IsVirtual was rejected.'
        Assert-PlatformEqual 'NVMe' $normal.BusType 'Normal disk bus type was not preserved.'

        $explicitVirtual = [pscustomobject]@{ BusType = 'NVMe'; IsVirtual = $true }
        $virtualResult = Get-GpuDiskPhysicalClassification -Disk $explicitVirtual
        Assert-PlatformTrue (-not [bool]$virtualResult.SupportedPhysical) 'IsVirtual=true was accepted.'

        foreach ($busType in @('Unknown', 'Virtual', 'FileBackedVirtual', 'Spaces', 'Storage Spaces', 'Spaces Storage Spaces', 0, 14, 15, 16, 20, 255)) {
            $disk = [pscustomobject]@{ BusType = $busType }
            $classification = Get-GpuDiskPhysicalClassification -Disk $disk
            Assert-PlatformTrue (-not [bool]$classification.SupportedPhysical) "Virtual/unknown bus type '$busType' was accepted."
        }
        foreach ($busType in @('SCSI', 'SATA', 'NVMe', 1, 11, 17, 18, 19)) {
            $disk = [pscustomobject]@{ BusType = $busType }
            $classification = Get-GpuDiskPhysicalClassification -Disk $disk
            Assert-PlatformTrue ([bool]$classification.SupportedPhysical) "Known physical bus type '$busType' was rejected."
        }
    }

    Invoke-PlatformTestCase 'Synthetic preflight adapter inventory validates exact Intel and NVIDIA candidates' {
        $validReport = [pscustomobject]@{
            InventoryComplete = $true
            DiagnosticCandidate = [pscustomobject]@{ PciId = 'PCI\VEN_8086&DEV_4680'; Present = $true }
            Nvidia = [pscustomobject]@{ PciId = 'PCI\VEN_10DE&DEV_2206'; Present = $true }
        }
        Assert-GpuPreflightAdapters -Report $validReport

        Assert-PlatformThrows { Assert-GpuPreflightAdapters -Report ([pscustomobject]@{ InventoryComplete = $false }) } `
            'Incomplete display inventory was accepted.' 'inventory is incomplete'
        $missingIntel = [pscustomobject]@{
            InventoryComplete = $true
            DiagnosticCandidate = [pscustomobject]@{ PciId = 'PCI\VEN_8086&DEV_1234'; Present = $true }
            Nvidia = $validReport.Nvidia
        }
        Assert-PlatformThrows { Assert-GpuPreflightAdapters -Report $missingIntel } 'Wrong Intel candidate was accepted.' 'UHD 770'
        $missingNvidia = [pscustomobject]@{
            InventoryComplete = $true
            DiagnosticCandidate = $validReport.DiagnosticCandidate
            Nvidia = [pscustomobject]@{ PciId = 'PCI\VEN_1002&DEV_744C'; Present = $true }
        }
        Assert-PlatformThrows { Assert-GpuPreflightAdapters -Report $missingNvidia } 'Non-NVIDIA candidate was accepted.' 'NVIDIA'
    }

    Invoke-PlatformTestCase 'ESP tree copy verifies manifest bytes and refuses existing or reparse destinations' {
        $sourceRoot = Join-Path $runRoot 'esp-source'
        $actionDirectory = Join-Path $runRoot 'copy-action'
        [void](New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'EFI\Microsoft\Boot') -Force)
        [void](New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'EFI\Boot') -Force)
        [void](New-Item -ItemType Directory -Path $actionDirectory)
        [System.IO.File]::WriteAllBytes((Join-Path $sourceRoot 'EFI\Microsoft\Boot\bootmgfw.efi'), [byte[]]@(1, 2, 3, 4, 5))
        [System.IO.File]::WriteAllText((Join-Path $sourceRoot 'EFI\Boot\bootx64.efi'), 'fixture-boot-file', (New-Object System.Text.UTF8Encoding($false)))

        $destinationRoot = Join-Path $runRoot 'esp-copy'
        $manifest = Copy-GpuEspTreeWithManifest -SourceRoot $sourceRoot -DestinationRoot $destinationRoot -ActionDirectory $actionDirectory
        Assert-PlatformEqual 2 $manifest.FileCount 'ESP copy manifest file count differs.'
        Assert-PlatformTrue (Test-Path -LiteralPath (Join-Path $destinationRoot 'EFI\Microsoft\Boot\bootmgfw.efi') -PathType Leaf) 'ESP boot-manager copy is missing.'
        Assert-PlatformEqual (Get-GpuFilePin -Path (Join-Path $sourceRoot 'EFI\Microsoft\Boot\bootmgfw.efi')).Sha256 `
            (Get-GpuFilePin -Path (Join-Path $destinationRoot 'EFI\Microsoft\Boot\bootmgfw.efi')).Sha256 'ESP copy readback hash differs.'
        $manifestEntry = @($manifest.Files | Where-Object { $_.RelativePath -ieq 'EFI\Boot\bootx64.efi' })
        Assert-PlatformEqual 1 $manifestEntry.Count 'Manifest omitted the second ESP file.'
        Assert-PlatformEqual (Get-GpuFilePin -Path (Join-Path $sourceRoot 'EFI\Boot\bootx64.efi')).Bytes $manifestEntry[0].Bytes 'Manifest byte count differs.'

        Assert-PlatformThrows {
            Copy-GpuEspTreeWithManifest -SourceRoot $sourceRoot -DestinationRoot $destinationRoot -ActionDirectory $actionDirectory
        } 'Copy helper overwrote a non-fresh destination.' 'Refusing to overwrite'

        $sourceWithLink = Join-Path $runRoot 'esp-source-reparse'
        $junctionTarget = Join-Path $runRoot 'junction-target'
        [void](New-Item -ItemType Directory -Path $sourceWithLink)
        [void](New-Item -ItemType Directory -Path $junctionTarget)
        [System.IO.File]::WriteAllText((Join-Path $junctionTarget 'linked.txt'), 'not-to-copy', (New-Object System.Text.UTF8Encoding($false)))
        $junctionPath = Join-Path $sourceWithLink 'EFI-link'
        [void](New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget -ErrorAction Stop)
        $reparseDestination = Join-Path $runRoot 'esp-copy-reparse'
        Assert-PlatformThrows {
            Copy-GpuEspTreeWithManifest -SourceRoot $sourceWithLink -DestinationRoot $reparseDestination -ActionDirectory $actionDirectory
        } 'Copy helper followed a reparse point.' 'reparse point'
        Assert-PlatformTrue (-not (Test-Path -LiteralPath $reparseDestination)) 'Rejected reparse source left a partial destination tree.'
    }

    Invoke-PlatformTestCase 'Stage readback validates all five owned files and detects payload drift' {
        $espRoot = Join-Path $runRoot 'stage-esp'
        $null = New-Item -ItemType Directory -Path $espRoot
        $sourceRoot = Join-Path $runRoot 'stage-source'
        $shimPath = Join-Path $sourceRoot 'components\uefi-loader\src\vendor\shimx64.efi'
        $loaderPath = Join-Path $runRoot 'signed\loader.efi'
        $payloadPath = Join-Path $runRoot 'signed\payload.efi'
        $referenceLogPath = Join-Path $runRoot 'reference-tcglog.ib'
        foreach ($filePath in @($shimPath, $loaderPath, $payloadPath, $referenceLogPath)) {
            [void](New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($filePath)) -Force)
        }
        [System.IO.File]::WriteAllText($shimPath, 'fixture-shim', (New-Object System.Text.UTF8Encoding($false)))
        [System.IO.File]::WriteAllText($loaderPath, 'fixture-signed-loader', (New-Object System.Text.UTF8Encoding($false)))
        [System.IO.File]::WriteAllText($payloadPath, 'fixture-signed-payload', (New-Object System.Text.UTF8Encoding($false)))
        [System.IO.File]::WriteAllText($referenceLogPath, 'fixture-tcg-log', (New-Object System.Text.UTF8Encoding($false)))

        $stageInputPaths = @{
            'EFI\insecure-boot\shimx64.efi' = $shimPath
            'EFI\insecure-boot\grubx64.efi' = $loaderPath
            'EFI\insecure-boot\payload.efi' = $payloadPath
            'EFI\insecure-boot\tcglog.ib' = $referenceLogPath
        }
        foreach ($expected in (Get-GpuStageExpectedFiles)) {
            $filePath = Join-Path $espRoot $expected.RelativePath
            $parent = [System.IO.Path]::GetDirectoryName($filePath)
            if (-not [System.IO.Directory]::Exists($parent)) { [void](New-Item -ItemType Directory -Path $parent -Force) }
            if ($stageInputPaths.ContainsKey($expected.RelativePath)) {
                [System.IO.File]::Copy($stageInputPaths[$expected.RelativePath], $filePath, $false)
            }
            else {
                [System.IO.File]::WriteAllText($filePath, ('fixture:' + $expected.Name), (New-Object System.Text.UTF8Encoding($false)))
            }
        }

        $espVolume = '\\?\Volume{11111111-2222-3333-4444-555555555555}\'
        $backup = [pscustomobject]@{
            EspVolume = $espVolume
            BootManagerSha256 = ('A' * 64)
            EspDiskNumber = 1
            EspPartitionGuid = '{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}'
            ReferenceLog = Get-GpuFilePin -Path $referenceLogPath
        }
        $stageFiles = @(Get-GpuStageReadback -EspRoot $espRoot)
        Assert-PlatformEqual 5 $stageFiles.Count 'Stage helper did not read exactly five files.'
        $stageRunId = ('platform-fixture-' + [guid]::NewGuid().ToString('N'))
        $stageBuildParent = Join-Path (Join-Path $runRoot '.codex-build\gb') $stageRunId.Substring($stageRunId.Length - 16)
        $state = [pscustomobject]@{
            RunDirectory = $runRoot
            WorkspaceRoot = $runRoot
            RunId = $stageRunId
            SourceRoot = $sourceRoot
            Paths = [pscustomobject]@{
                HypervisorBuild = (Join-Path $stageBuildParent 'h')
                LoaderBuild = (Join-Path $stageBuildParent 'l')
                SignedDirectory = (Join-Path $runRoot 'signed')
            }
            Tools = [pscustomobject]@{ Shim = (Get-GpuFilePin -Path $shimPath) }
            SignedArtifacts = [pscustomobject]@{
                Loader = (Get-GpuFilePin -Path $loaderPath)
                Payload = (Get-GpuFilePin -Path $payloadPath)
            }
            Backup = $backup
            Stage = [pscustomobject]@{ EspVolume = $espVolume; Files = $stageFiles }
        }
        Set-GpuPlatformTestBackup -Backup $backup
        $validated = @(Assert-GpuStageState -State $state -EspRoot $espRoot)
        Assert-PlatformEqual 5 $validated.Count 'Valid stage did not return all five file pins.'

        [System.IO.File]::AppendAllText((Join-Path $espRoot 'EFI\insecure-boot\payload.efi'), '-modified', (New-Object System.Text.UTF8Encoding($false)))
        Assert-PlatformThrows { Assert-GpuStageState -State $state -EspRoot $espRoot } `
            'Modified staged payload passed validation.' 'Staged file changed'
    }

    Invoke-PlatformTestCase 'ESP identity mismatch fails before any mounted boot-manager hash read' {
        Reset-GpuPlatformTestMocks
        $backup = [pscustomobject]@{
            EspVolume = '\\?\Volume{11111111-2222-3333-4444-555555555555}\'
            EspDiskNumber = 1
            EspPartitionGuid = '{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}'
            BootManagerSha256 = ('A' * 64)
        }
        $mounted = [pscustomobject]@{
            Volume = '\\?\Volume{99999999-2222-3333-4444-555555555555}\'
            DiskNumber = 1
            PartitionGuid = '{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}'
        }
        Assert-PlatformThrows {
            Assert-GpuMountedEspMatchesBackup -MountedEsp $mounted -Backup $backup -Description 'Fixture identity check'
        } 'Mismatched ESP volume identity was accepted.' 'do not match'
        Assert-PlatformEqual 0 (Get-GpuPlatformTestMountedHashCalls) 'Mounted boot-manager hash was read before the ESP GUID was checked.'
    }

    Invoke-PlatformTestCase 'Native command helper captures a harmless child and validates allowed exit codes' {
        $powershellPath = Join-Path $PSHOME 'powershell.exe'
        if (-not [System.IO.File]::Exists($powershellPath)) { throw "Windows PowerShell child is unavailable: $powershellPath" }
        $actionDirectory = Join-Path $runRoot 'native-action'
        [void](New-Item -ItemType Directory -Path $actionDirectory)
        $childScript = Join-Path $runRoot 'emit-fixture.ps1'
        $childSource = @'
param([string]$Token, [int]$Code)
[Console]::Out.WriteLine("OUT:$Token")
[Console]::Error.WriteLine("ERR:$Token")
exit $Code
'@
        [System.IO.File]::WriteAllText($childScript, $childSource, (New-Object System.Text.UTF8Encoding($false)))

        $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $childScript, '-Token', 'gpu-platform-fixture', '-Code', '7')
        $accepted = Invoke-GpuNativeCommand -Program $powershellPath -Arguments $arguments -ActionDirectory $actionDirectory -Label 'harmless child' -AllowedExitCodes @(7)
        Assert-PlatformTrue ([bool]$accepted.Succeeded) 'Allowed child exit code was reported as failure.'
        Assert-PlatformEqual 7 $accepted.ExitCode 'Native command receipt recorded the wrong exit code.'
        Assert-PlatformTrue ([System.IO.File]::ReadAllText($accepted.StdoutPath).Contains('OUT:gpu-platform-fixture')) 'Native command stdout was not captured.'
        Assert-PlatformTrue ([System.IO.File]::ReadAllText($accepted.StderrPath).Contains('ERR:gpu-platform-fixture')) 'Native command stderr was not captured.'
        $acceptedReceipt = Read-GpuJson -Path $accepted.ReceiptPath
        Assert-PlatformTrue (@($acceptedReceipt.AllowedExitCodes) -contains 7) 'Native command receipt omitted its allowed exit code.'
        Assert-PlatformTrue ([bool]$acceptedReceipt.Succeeded) 'Native command receipt did not record success.'

        $rejected = Invoke-GpuNativeCommand -Program $powershellPath -Arguments $arguments -ActionDirectory $actionDirectory -Label 'disallowed child' -AllowedExitCodes @(0)
        Assert-PlatformTrue (-not [bool]$rejected.Succeeded) 'Disallowed child exit code was accepted.'
        Assert-PlatformEqual 7 $rejected.ExitCode 'Disallowed exit receipt recorded the wrong exit code.'
        $rejectedReceipt = Read-GpuJson -Path $rejected.ReceiptPath
        Assert-PlatformTrue (-not [bool]$rejectedReceipt.Succeeded) 'Disallowed exit receipt reported success.'
        Assert-PlatformTrue (-not (@($rejectedReceipt.AllowedExitCodes) -contains 7)) 'Disallowed exit code was incorrectly listed as allowed.'

        $global:LASTEXITCODE = 7
        $missingProgram = Join-Path $runRoot 'does-not-exist\missing-program.exe'
        $missing = Invoke-GpuNativeCommand -Program $missingProgram -Arguments @() -ActionDirectory $actionDirectory -Label 'missing program' -AllowedExitCodes @(7)
        Assert-PlatformTrue (-not [bool]$missing.Succeeded) 'A nonexistent program was reported as successful from stale LASTEXITCODE.'
        Assert-PlatformTrue (-not [string]::IsNullOrWhiteSpace([string]$missing.LaunchError)) 'A nonexistent program did not record a launch error.'
        Assert-PlatformTrue ($null -eq $missing.ExitCode) 'A nonexistent program reused a stale exit code.'
        $missingReceipt = Read-GpuJson -Path $missing.ReceiptPath
        Assert-PlatformTrue (-not [bool]$missingReceipt.Succeeded) 'Missing-program receipt reported success.'
        Assert-PlatformTrue (-not [string]::IsNullOrWhiteSpace([string]$missingReceipt.LaunchError)) 'Missing-program receipt omitted its launch error.'
    }

    if ($script:FailedCases.Count -gt 0) {
        Write-Output ("GPU_HOST_PLATFORM_TESTS:FAIL cases={0}" -f $script:FailedCases.Count)
        foreach ($failure in $script:FailedCases) { Write-Output ("GPU_HOST_PLATFORM_TESTS:FAIL {0}" -f $failure) }
        exit 1
    }
    Write-Output ("GPU_HOST_PLATFORM_TESTS:PASS cases={0} assertions={1} skipped=0" -f $script:PassedCases, $script:Assertions)
}
finally {
    if ($null -ne $isolatedModule) {
        Remove-Module -ModuleInfo $isolatedModule -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $junctionPath -and (Test-Path -LiteralPath $junctionPath)) {
        Remove-Item -LiteralPath $junctionPath -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $runRoot -PathType Container) {
        $resolvedRunRoot = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $runRoot).Path)
        $runPrefix = [System.IO.Path]::GetFullPath($scratchParent).TrimEnd('\') + '\run-'
        if (-not $resolvedRunRoot.StartsWith($runPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($resolvedRunRoot, [System.IO.Path]::GetFullPath($scratchParent), [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean fixture path outside this test run: $resolvedRunRoot"
        }
        Remove-Item -LiteralPath $resolvedRunRoot -Recurse -Force -ErrorAction Stop
    }
}
