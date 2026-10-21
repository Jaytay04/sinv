[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$platformPath = Join-Path $ToolRoot 'GpuHostPlatform.psm1'
$statePath = Join-Path $ToolRoot 'GpuHostState.psm1'
$comparisonPath = Join-Path $ToolRoot 'GpuFirmwareComparison.psm1'
foreach ($requiredPath in @($platformPath, $statePath, $comparisonPath)) {
    if (-not [IO.File]::Exists($requiredPath)) { throw "Required firmware policy helper is missing: $requiredPath" }
}

$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()
$script:UsbAId = '6cc7730c-c9c3-11f1-847b-806e6f6e6963'
$script:UsbBId = '6cc7730d-c9c3-11f1-847b-806e6f6e6963'
$script:KnownWindowsFirmwareId = '9dea862c-5cdd-4e70-acc1-f32b344d4795'
$script:BootManagerHash = 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
$script:GenericIds = [ordered]@{
    'CD/DVD' = 'd81f168e-cd6f-11f1-8b27-806e6f6e6963'
    'Removable' = 'd81f168f-cd6f-11f1-8b27-806e6f6e6963'
    'Network' = 'd81f1690-cd6f-11f1-8b27-806e6f6e6963'
}
$script:GenericDescriptions = [ordered]@{
    'CD/DVD' = 'UEFI:CD/DVD Drive'
    'Removable' = 'UEFI:Removable Device'
    'Network' = 'UEFI:Network Device'
}

function Assert-PolicyTrue {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-PolicyEqual {
    param($Expected, $Actual, [string]$Message)
    $script:Assertions++
    if ($Expected -is [string] -or $Actual -is [string]) {
        if ([string]$Expected -cne [string]$Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
    }
    elseif ($Expected -ne $Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
}

function Assert-PolicyThrows {
    param([scriptblock]$Operation, [string]$Message, [string]$ErrorPattern = '')
    $observed = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message }
    Assert-PolicyTrue ($null -ne $observed) "$Message (operation did not reject the input)"
    if (-not [string]::IsNullOrWhiteSpace($ErrorPattern)) {
        Assert-PolicyTrue ($observed -match $ErrorPattern) "$Message (unexpected rejection: $observed)"
    }
}

function Invoke-PolicyTestCase {
    param([string]$Name, [scriptblock]$Body)
    $fixture = $null
    try {
        $fixture = New-GpuFirmwarePolicyFixture
        & $Body $fixture
        $script:PassedCases++
        Write-Output "PASS: $Name"
    }
    catch {
        [void]$script:FailedCases.Add("$Name`: $($_.Exception.Message) [$($_.ScriptStackTrace)]")
        Write-Output "FAIL: $Name`: $($_.Exception.Message) [$($_.ScriptStackTrace)]"
    }
    finally {
        if ($null -ne $fixture) { Remove-GpuFirmwarePolicyFixture -Fixture $fixture }
    }
}

function Get-PolicyFunctionTexts {
    param([string]$Path, [string[]]$Names)
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$parseErrors)
    if ($null -ne $parseErrors -and $parseErrors.Count -gt 0) { throw "Helper does not parse: $Path ($($parseErrors[0].Message))" }
    $functions = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $true))
    $definitions = [Collections.Generic.List[string]]::new()
    foreach ($name in $Names) {
        $matches = @($functions | Where-Object { $_.Name -ceq $name })
        if ($matches.Count -ne 1) { throw "Expected one function '$name' in $Path; found $($matches.Count)." }
        $definitions.Add([string]$matches[0].Extent.Text)
    }
    return $definitions.ToArray()
}

function Get-PolicyFilePin {
    param([string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $stream = [IO.File]::OpenRead($fullPath)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
        return [pscustomobject]@{ Path = $fullPath; Sha256 = $hash; Bytes = [long]$stream.Length }
    }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Add-PolicyFirmwareBlock {
    param([Collections.Generic.List[string]]$Lines, [string]$Title, [string[]]$Body)
    if ($Lines.Count -gt 0) { $Lines.Add('') }
    $Lines.Add($Title)
    $Lines.Add('-' * $Title.Length)
    foreach ($line in $Body) { $Lines.Add([string]$line) }
}

function New-PolicyFirmwareText {
    param(
        [string[]]$GenericTypes = @('CD/DVD', 'Removable', 'Network'),
        [Collections.IDictionary]$GenericIdMap = $script:GenericIds,
        [string[]]$UsbReferenceOrder = @('A', 'B'),
        [switch]$ChangedUsbPath,
        [switch]$ChangedWindowsPath
    )

    $lines = [Collections.Generic.List[string]]::new()
    $managerRefs = [Collections.Generic.List[string]]::new()
    $managerRefs.Add($script:KnownWindowsFirmwareId)
    foreach ($usb in $UsbReferenceOrder) {
        if ($usb -ceq 'A') { $managerRefs.Add($script:UsbAId) }
        elseif ($usb -ceq 'B') { $managerRefs.Add($script:UsbBId) }
        else { throw "Unknown USB fixture reference '$usb'." }
    }
    foreach ($type in @('CD/DVD', 'Removable', 'Network')) {
        if ($GenericTypes -contains $type) { $managerRefs.Add([string]$GenericIdMap[$type]) }
    }

    $managerBody = [Collections.Generic.List[string]]::new()
    $managerBody.Add('identifier              {a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}')
    $managerBody.Add(('displayorder            {' + $managerRefs[0] + '}'))
    for ($index = 1; $index -lt $managerRefs.Count; $index++) { $managerBody.Add(('                        {' + $managerRefs[$index] + '}')) }
    $managerBody.Add('timeout                 0')
    Add-PolicyFirmwareBlock $lines 'Firmware Boot Manager' $managerBody.ToArray()

    $windowsPath = if ($ChangedWindowsPath) { '\EFI\Microsoft\Boot\changed.efi' } else { '\EFI\Microsoft\Boot\bootmgfw.efi' }
    Add-PolicyFirmwareBlock $lines 'Windows Boot Manager' @(
        ('identifier              {' + $script:KnownWindowsFirmwareId + '}')
        'device                  partition=S:'
        ('path                    ' + $windowsPath)
        'description             Windows Boot Manager'
        'locale                  en-US'
        'inherit                 {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}'
        'default                 {71152aeb-45e9-11ed-8140-a26d57e4c014}'
        'resumeobject            {71152aea-45e9-11ed-8140-a26d57e4c014}'
        'displayorder            {71152aeb-45e9-11ed-8140-a26d57e4c014}'
        'toolsdisplayorder       {b2721d73-1db4-4c62-bf78-c548a880142d}'
        'timeout                 30'
    )

    $usbAPath = if ($ChangedUsbPath) { 'device                  partition=F:' } else { 'device                  partition=D:' }
    Add-PolicyFirmwareBlock $lines 'Firmware Application (101fffff)' @(
        ('identifier              {' + $script:UsbAId + '}')
        $usbAPath
        'description             UEFI: Memorex USB Flash Drive PMAP, Partition 1'
    )
    Add-PolicyFirmwareBlock $lines 'Firmware Application (101fffff)' @(
        ('identifier              {' + $script:UsbBId + '}')
        'device                  partition=E:'
        'description             UEFI: VendorCo ProductCode 2.00, Partition 1'
    )
    foreach ($type in @('CD/DVD', 'Removable', 'Network')) {
        if ($GenericTypes -notcontains $type) { continue }
        Add-PolicyFirmwareBlock $lines 'Firmware Application (101fffff)' @(
            ('identifier              {' + [string]$GenericIdMap[$type] + '}')
            ('description             ' + [string]$script:GenericDescriptions[$type])
        )
    }
    $lines.Insert(0, '')
    return [string]::Join("`n", $lines.ToArray())
}

function New-PolicyRawDocument {
    param(
        [int[]]$BootOrder = @(0, 1, 2, 3, 4, 5),
        [AllowNull()][object]$BootNext = $null,
        [int]$Findings = 0,
        [string]$BootManagerSha256 = $script:BootManagerHash,
        [string]$ReferenceLogSha256
    )
    return [pscustomobject]@{
        schema = 1
        kind = 'boot-selection'
        boot_next = $BootNext
        boot_order = @($BootOrder)
        findings = $Findings
        bootmgfw_sha256 = $BootManagerSha256
        log_sha256 = $ReferenceLogSha256
    }
}

function New-GpuFirmwarePolicyFixtureModule {
    param([string]$PlatformModulePath, [string]$StateModulePath)

    $stateTokens = $null
    $stateErrors = $null
    $stateAst = [Management.Automation.Language.Parser]::ParseFile($StateModulePath, [ref]$stateTokens, [ref]$stateErrors)
    if ($null -ne $stateErrors -and $stateErrors.Count -gt 0) { throw "State helper does not parse: $($stateErrors[0].Message)" }
    $stateFunctions = @($stateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $true))
    $stateDefinitions = @($stateFunctions | ForEach-Object { [string]$_.Extent.Text })
    $platformNames = @(
        'Get-GpuObjectProperty', 'Get-GpuRunIdLeaf', 'Get-GpuRunPath', 'Get-GpuBuildDirectories', 'Get-GpuExpectedToolPaths',
        'Assert-GpuNamedPin', 'Assert-GpuFreshPath', 'Assert-GpuNativeSuccess',
        'Get-GpuFirmwareComparisonModule', 'Get-GpuFirmwareBootSelectionSnapshot',
        'Invoke-GpuFirmwareBootSelectionCapture', 'New-GpuFirmwareComparisonBackup',
        'Get-GpuStableFirmwareBaseline', 'Invoke-GpuStableFirmwareFinal'
    )
    $platformDefinitions = @(Get-PolicyFunctionTexts -Path $PlatformModulePath -Names $platformNames)
    $fixtureDefinitions = @'
$script:GpuFirmwarePolicyScenario = $null
$script:GpuFirmwarePolicyNativeCalls = [Collections.Generic.List[object]]::new()

function Set-GpuFirmwarePolicyScenario {
    param([Parameter(Mandatory = $true)][object]$Scenario)
    $script:GpuFirmwarePolicyScenario = $Scenario
    $script:GpuFirmwarePolicyNativeCalls.Clear()
}

function Get-GpuFirmwarePolicyObservations {
    return [pscustomobject]@{ NativeCalls = @($script:GpuFirmwarePolicyNativeCalls.ToArray()) }
}

function Invoke-GpuNativeCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ActionDirectory,
        [Parameter(Mandatory = $true)][string]$Label,
        [int[]]$AllowedExitCodes = @(0)
    )
    if ($null -eq $script:GpuFirmwarePolicyScenario) { throw 'TEST MOCK DENIED: scenario was not set.' }
    if ($Label -cnotmatch '^firmware-(baseline|final-before|final-after)$') { throw "TEST MOCK DENIED: unexpected audit label '$Label'." }
    [void]$script:GpuFirmwarePolicyNativeCalls.Add([pscustomobject]@{
        Program = $Program; Arguments = @($Arguments); ActionDirectory = $ActionDirectory; Label = $Label
    })
    $outputIndex = [Array]::IndexOf([string[]]$Arguments, '--output')
    if ($outputIndex -lt 0 -or $outputIndex + 1 -ge $Arguments.Count) { throw 'TEST MOCK DENIED: audit output path is missing.' }
    $rawPath = [string]$Arguments[$outputIndex + 1]
    $stdoutPath = Join-Path $ActionDirectory ($Label + '-mock.stdout.txt')
    $stderrPath = Join-Path $ActionDirectory ($Label + '-mock.stderr.txt')
    $receiptPath = Join-Path $ActionDirectory ($Label + '-mock.receipt.json')
    $failed = @($script:GpuFirmwarePolicyScenario.FailedLabels) -contains $Label
    $rawValue = $script:GpuFirmwarePolicyScenario.RawByLabel[$Label]
    if ($null -eq $rawValue) { throw "TEST MOCK DENIED: no raw fixture was supplied for '$Label'." }
    $rawText = if ($rawValue -is [string]) { [string]$rawValue } else { ConvertTo-Json -InputObject $rawValue -Depth 8 -Compress }
    [IO.File]::WriteAllText($rawPath, $rawText, (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($stdoutPath, 'fixture audit output', (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($stderrPath, '', (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($receiptPath, '{"fixture":true}', (New-Object Text.UTF8Encoding($false)))
    return [pscustomobject]@{
        Succeeded = (-not $failed); ExitCode = if ($failed) { 17 } else { 0 }
        ReceiptPath = $receiptPath; StdoutPath = $stdoutPath; StderrPath = $stderrPath
    }
}

function Invoke-GpuFirmwarePolicyBackupFixture {
    param($State, $FirmwarePin, $ReferenceLogPin, [string]$BootManagerSha256, [string]$RecoveryRunDirectory, [string]$ActionDirectory)
    return New-GpuFirmwareComparisonBackup -State $State -FirmwarePin $FirmwarePin -ReferenceLogPin $ReferenceLogPin `
        -BootManagerSha256 $BootManagerSha256 -RecoveryRunDirectory $RecoveryRunDirectory -ActionDirectory $ActionDirectory
}

function Invoke-GpuFirmwarePolicyCaptureFixture {
    param($State, $ReferenceLogPin, [string]$BootManagerSha256, [string]$ActionDirectory, [string]$Label)
    return Invoke-GpuFirmwareBootSelectionCapture -State $State -ReferenceLogPin $ReferenceLogPin `
        -BootManagerSha256 $BootManagerSha256 -ActionDirectory $ActionDirectory -Label $Label
}

function Invoke-GpuFirmwarePolicyFinalFixture {
    param($State, $CurrentFirmwarePin, [string]$ActionDirectory, $RawBefore)
    return Invoke-GpuStableFirmwareFinal -State $State -CurrentFirmwarePin $CurrentFirmwarePin `
        -ActionDirectory $ActionDirectory -RawBefore $RawBefore
}
'@
    $sourceLines = [Collections.Generic.List[string]]::new()
    $sourceLines.Add('Set-StrictMode -Version 2.0')
    foreach ($definition in $stateDefinitions) { $sourceLines.Add([string]$definition) }
    foreach ($definition in $platformDefinitions) { $sourceLines.Add([string]$definition) }
    $sourceLines.Add([string]$fixtureDefinitions)
    $sourceLines.Add("Export-ModuleMember -Function 'Set-GpuFirmwarePolicyScenario','Get-GpuFirmwarePolicyObservations','Invoke-GpuFirmwarePolicyBackupFixture','Invoke-GpuFirmwarePolicyCaptureFixture','Invoke-GpuFirmwarePolicyFinalFixture'")
    $source = [string]::Join("`r`n", $sourceLines.ToArray())
    $module = New-Module -Name ('GpuFirmwarePolicyFixture' + [guid]::NewGuid().ToString('N')) -ScriptBlock ([scriptblock]::Create($source))
    Import-Module -ModuleInfo $module -Force -ErrorAction Stop
    return $module
}

function New-GpuFirmwarePolicyFixture {
    $fixtureRoot = Join-Path $script:runRoot ('case-' + [guid]::NewGuid().ToString('N'))
    $runDirectory = Join-Path $fixtureRoot 'run'
    $recoveryRunDirectory = Join-Path $fixtureRoot 'external-recovery\gpu-fixture-run'
    $sourceRoot = Join-Path $fixtureRoot 'source'
    $runId = 'gpu-policy-fixture-' + [guid]::NewGuid().ToString('N')
    $shortRunId = $runId.Substring($runId.Length - 16)
    $loaderBuild = Join-Path (Join-Path (Join-Path $fixtureRoot '.codex-build\gb') $shortRunId) 'l'
    $hypervisorBuild = Join-Path (Join-Path (Join-Path $fixtureRoot '.codex-build\gb') $shortRunId) 'h'
    $signedDirectory = Join-Path $runDirectory 'signed'
    $backupActionLeaf = '20261101T120000000Z-Backup-' + [guid]::NewGuid().ToString('N')
    $finalActionLeaf = '20261101T120100000Z-FinalVerify-' + [guid]::NewGuid().ToString('N')
    $backupAction = Join-Path (Join-Path $runDirectory 'actions') $backupActionLeaf
    $finalAction = Join-Path (Join-Path $runDirectory 'actions') $finalActionLeaf
    foreach ($directory in @(
        $runDirectory, $recoveryRunDirectory, $sourceRoot, $loaderBuild, $hypervisorBuild,
        $signedDirectory, $backupAction, $finalAction, (Join-Path $sourceRoot 'tools\gpu_diagnostic')
    )) { [void](New-Item -ItemType Directory -Path $directory -Force) }

    $fixtureComparisonPath = Join-Path $sourceRoot 'tools\gpu_diagnostic\GpuFirmwareComparison.psm1'
    [IO.File]::Copy($comparisonPath, $fixtureComparisonPath, $false)
    $fixtureModulePath = Join-Path $loaderBuild 'ibaudit.exe'
    [IO.File]::WriteAllText($fixtureModulePath, 'fixture audit bytes; never executed', (New-Object Text.UTF8Encoding($false)))
    $referenceLogPath = Join-Path $backupAction 'tcglog.ib'
    [IO.File]::WriteAllText($referenceLogPath, 'fixture reference measured boot log', (New-Object Text.UTF8Encoding($false)))
    $firmwarePath = Join-Path $backupAction 'firmware-enumeration-backup.stdout.txt'
    $baselineText = New-PolicyFirmwareText
    [IO.File]::WriteAllText($firmwarePath, $baselineText, (New-Object Text.UTF8Encoding($false)))

    $module = New-GpuFirmwarePolicyFixtureModule -PlatformModulePath $platformPath -StateModulePath $statePath
    $modulePin = Get-PolicyFilePin -Path $fixtureComparisonPath
    $auditPin = Get-PolicyFilePin -Path $fixtureModulePath
    $referenceLogPin = Get-PolicyFilePin -Path $referenceLogPath
    $firmwarePin = Get-PolicyFilePin -Path $firmwarePath
    $bootManagerHash = $script:BootManagerHash
    $rawMap = @{
        'firmware-baseline' = (New-PolicyRawDocument -ReferenceLogSha256 $referenceLogPin.Sha256)
        'firmware-final-before' = (New-PolicyRawDocument -ReferenceLogSha256 $referenceLogPin.Sha256)
        'firmware-final-after' = (New-PolicyRawDocument -ReferenceLogSha256 $referenceLogPin.Sha256)
    }
    $scenario = [pscustomobject]@{ RawByLabel = $rawMap; FailedLabels = @() }
    & $module { param($scenarioValue) Set-GpuFirmwarePolicyScenario -Scenario $scenarioValue } $scenario
    $paths = [pscustomobject]@{ HypervisorBuild = $hypervisorBuild; LoaderBuild = $loaderBuild; SignedDirectory = $signedDirectory }
    $state = [pscustomobject]@{
        RunId = $runId; RunDirectory = $runDirectory; WorkspaceRoot = $fixtureRoot; SourceRoot = $sourceRoot
        RecoveryRunDirectory = $recoveryRunDirectory; FirmwareComparisonVersion = 1; Paths = $paths
        SourcePins = @($modulePin); Tools = [pscustomobject]@{ Audit = $auditPin }
    }
    $backup = & $module {
        param($stateValue, $firmwareValue, $referenceValue, $managerHash, $recoveryPath, $actionPath)
        Invoke-GpuFirmwarePolicyBackupFixture -State $stateValue -FirmwarePin $firmwareValue -ReferenceLogPin $referenceValue `
            -BootManagerSha256 $managerHash -RecoveryRunDirectory $recoveryPath -ActionDirectory $actionPath
    } $state $firmwarePin $referenceLogPin $bootManagerHash $recoveryRunDirectory $backupAction
    $state | Add-Member -NotePropertyName Backup -NotePropertyValue ([pscustomobject]@{
        FirmwareEnumeration = $firmwarePin; ReferenceLog = $referenceLogPin; BootManagerSha256 = $bootManagerHash
        FirmwareComparison = $backup
    })
    return [pscustomobject]@{
        Root = $fixtureRoot; Module = $module; State = $state; Backup = $backup; BaselineText = $baselineText
        FirmwarePin = $firmwarePin; ReferenceLogPin = $referenceLogPin; BootManagerSha256 = $bootManagerHash
        BackupAction = $backupAction; FinalAction = $finalAction; RecoveryRunDirectory = $recoveryRunDirectory
        ModulePath = $fixtureComparisonPath; AuditPath = $fixtureModulePath
    }
}

function Remove-GpuFirmwarePolicyFixture {
    param($Fixture)
    if ($null -ne $Fixture.Module) { Remove-Module -ModuleInfo $Fixture.Module -Force -ErrorAction SilentlyContinue }
    if ([IO.Directory]::Exists([string]$Fixture.Root)) {
        $resolved = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Fixture.Root).Path)
        $prefix = [IO.Path]::GetFullPath($script:runRoot).TrimEnd('\') + '\case-'
        if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing to clean fixture outside its test run: $resolved" }
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Stop
    }
}

function Set-PolicyScenario {
    param($Fixture, [hashtable]$RawOverrides = @{}, [string[]]$FailedLabels = @())
    $referenceHash = [string]$Fixture.ReferenceLogPin.Sha256
    $rawMap = @{
        'firmware-baseline' = (New-PolicyRawDocument -ReferenceLogSha256 $referenceHash)
        'firmware-final-before' = (New-PolicyRawDocument -ReferenceLogSha256 $referenceHash)
        'firmware-final-after' = (New-PolicyRawDocument -ReferenceLogSha256 $referenceHash)
    }
    foreach ($key in $RawOverrides.Keys) { $rawMap[$key] = $RawOverrides[$key] }
    $scenario = [pscustomobject]@{ RawByLabel = $rawMap; FailedLabels = @($FailedLabels) }
    & $Fixture.Module { param($scenarioValue) Set-GpuFirmwarePolicyScenario -Scenario $scenarioValue } $scenario
}

function Invoke-PolicyFinal {
    param($Fixture, [string]$CurrentText, [hashtable]$RawOverrides = @{}, [string[]]$FailedLabels = @())
    Set-PolicyScenario -Fixture $Fixture -RawOverrides $RawOverrides -FailedLabels $FailedLabels
    $currentPath = Join-Path $Fixture.FinalAction 'firmware-enumeration-final.stdout.txt'
    [IO.File]::WriteAllText($currentPath, $CurrentText, (New-Object Text.UTF8Encoding($false)))
    $currentPin = Get-PolicyFilePin -Path $currentPath
    $before = & $Fixture.Module {
        param($stateValue, $referenceValue, $managerHash, $actionPath)
        Invoke-GpuFirmwarePolicyCaptureFixture -State $stateValue -ReferenceLogPin $referenceValue `
            -BootManagerSha256 $managerHash -ActionDirectory $actionPath -Label 'firmware-final-before'
    } $Fixture.State $Fixture.ReferenceLogPin $Fixture.BootManagerSha256 $Fixture.FinalAction
    $result = & $Fixture.Module {
        param($stateValue, $currentValue, $actionPath, $beforeValue)
        Invoke-GpuFirmwarePolicyFinalFixture -State $stateValue -CurrentFirmwarePin $currentValue -ActionDirectory $actionPath -RawBefore $beforeValue
    } $Fixture.State $currentPin $Fixture.FinalAction $before
    return [pscustomobject]@{ Result = $result; CurrentPin = $currentPin; RawBefore = $before }
}

$workspaceRoot = Split-Path -Parent (Split-Path -Parent $ToolRoot)
$scratchParent = Join-Path $workspaceRoot '.codex-build\gpu-firmware-policy-tests'
$null = New-Item -ItemType Directory -Path $scratchParent -Force
$runRoot = Join-Path $scratchParent ('run-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $runRoot -ErrorAction Stop)

try {
    Invoke-PolicyTestCase 'Backup captures and externally pins the raw baseline evidence' {
        param($fixture)
        $record = $fixture.Backup
        Assert-PolicyEqual 1 ([int]$record.SchemaVersion) 'Versioned baseline schema is wrong.'
        Assert-PolicyEqual 'StableEntriesAndGenericTailV1' ([string]$record.Policy) 'Comparison policy is wrong.'
        Assert-PolicyEqual 6 @($record.ExternalCopies).Count 'Backup did not create all six required external evidence copies.'
        Assert-PolicyTrue ([IO.Directory]::Exists([string]$record.ExternalDirectory)) 'External baseline evidence directory is missing.'
        Assert-PolicyEqual $fixture.FirmwarePin.Sha256 ([string]$fixture.State.Backup.FirmwareEnumeration.Sha256) 'Original text baseline pin was replaced.'
        Assert-PolicyEqual 0 ([int]$record.BaselineRaw.Snapshot.Findings) 'Baseline audit reported integrity findings.'
        Assert-PolicyEqual $null $record.BaselineRaw.Snapshot.BootNext 'Baseline audit accepted pending BootNext.'
        Assert-PolicyEqual '0,1,2,3,4,5' (@($record.BaselineRaw.Snapshot.BootOrder) -join ',') 'Baseline raw BootOrder was not preserved.'
        foreach ($copy in $record.ExternalCopies) {
            Assert-PolicyEqual $copy.Source.Sha256 $copy.External.Sha256 'An external evidence copy does not match its pinned source.'
            Assert-PolicyEqual $copy.Source.Bytes $copy.External.Bytes 'An external evidence copy has an unexpected length.'
        }
        $observations = & $fixture.Module { Get-GpuFirmwarePolicyObservations }
        Assert-PolicyEqual 1 @($observations.NativeCalls).Count 'Backup did not use exactly one fake audit invocation.'
        Assert-PolicyTrue ([string]$observations.NativeCalls[0].Program -ceq [string]$fixture.AuditPath) 'Audit fixture path was not passed to the mock.'
    }

    Invoke-PolicyTestCase 'Generic firmware identifier churn is accepted only when stable Windows and USB entries match' {
        param($fixture)
        $changedGenericIds = [ordered]@{
            'CD/DVD' = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
            'Removable' = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'
            'Network' = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
        }
        $final = Invoke-PolicyFinal -Fixture $fixture -CurrentText (New-PolicyFirmwareText -GenericIdMap $changedGenericIds)
        Assert-PolicyTrue ([bool]$final.Result.Accepted) 'Generic placeholder GUID churn was not accepted.'
        Assert-PolicyTrue (-not [bool]$final.Result.Comparison.TextEqual) 'The policy hid the original textual difference.'
        Assert-PolicyEqual $fixture.FirmwarePin.Sha256 ([string]$fixture.State.Backup.FirmwareEnumeration.Sha256) 'Final comparison mutated the original baseline record.'
        Assert-PolicyEqual $fixture.FirmwarePin.Sha256 (Get-PolicyFilePin $fixture.FirmwarePin.Path).Sha256 'Final comparison modified the original baseline file.'
        Assert-PolicyEqual $true ([bool]$final.Result.CurrentRawBootSelectionStable) 'Current raw BootOrder stability was not recorded.'
        Assert-PolicyEqual $false ([bool]$final.Result.HistoricalRawBootOrderUnchangedProven) 'The workflow claimed historical raw BootOrder proof.'
        Assert-PolicyEqual $null $final.Result.RawBefore.Snapshot.BootNext 'Final before capture had non-null BootNext.'
        Assert-PolicyEqual 0 ([int]$final.Result.RawBefore.Snapshot.Findings) 'Final before capture had integrity findings.'
        Assert-PolicyEqual $null $final.Result.RawAfter.Snapshot.BootNext 'Final after capture had non-null BootNext.'
        Assert-PolicyEqual 0 ([int]$final.Result.RawAfter.Snapshot.Findings) 'Final after capture had integrity findings.'
        Assert-PolicyEqual '0,1,2,3,4,5' (@($final.Result.RawBefore.Snapshot.BootOrder) -join ',') 'Final before raw BootOrder was unexpected.'
        Assert-PolicyEqual '0,1,2,3,4,5' (@($final.Result.RawAfter.Snapshot.BootOrder) -join ',') 'Final after raw BootOrder was unexpected.'
        Assert-PolicyEqual $fixture.BootManagerSha256 ([string]$final.Result.RawAfter.Snapshot.BootManagerSha256) 'Final raw capture omitted the boot-manager hash pin.'
        Assert-PolicyEqual $fixture.ReferenceLogPin.Sha256 ([string]$final.Result.RawAfter.Snapshot.ReferenceLogSha256) 'Final raw capture omitted the measured-boot log hash pin.'
        Assert-PolicyEqual 4 @($final.Result.RawBefore.EvidencePins).Count 'Final before raw evidence did not include four pinned files.'
        Assert-PolicyEqual 4 @($final.Result.RawAfter.EvidencePins).Count 'Final after raw evidence did not include four pinned files.'
        foreach ($pin in @($final.Result.RawBefore.EvidencePins) + @($final.Result.RawAfter.EvidencePins)) {
            Assert-PolicyEqual $pin.Sha256 (Get-PolicyFilePin $pin.Path).Sha256 'A final raw evidence pin did not match its file.'
        }
    }

    Invoke-PolicyTestCase 'Removing generic placeholders is accepted while physical entries remain stable' {
        param($fixture)
        $final = Invoke-PolicyFinal -Fixture $fixture -CurrentText (New-PolicyFirmwareText -GenericTypes @())
        Assert-PolicyTrue ([bool]$final.Result.Accepted) 'Removing only generic tail placeholders was rejected.'
        Assert-PolicyEqual 0 @($final.Result.Comparison.CurrentGenericEntries).Count 'Removed generic placeholders remained in the comparison.'
    }

    Invoke-PolicyTestCase 'Physical USB path changes are rejected' {
        param($fixture)
        $final = Invoke-PolicyFinal -Fixture $fixture -CurrentText (New-PolicyFirmwareText -ChangedUsbPath)
        Assert-PolicyTrue (-not [bool]$final.Result.Accepted) 'A physical USB device path change was normalized away.'
    }

    Invoke-PolicyTestCase 'Physical USB display-order changes are rejected' {
        param($fixture)
        $final = Invoke-PolicyFinal -Fixture $fixture -CurrentText (New-PolicyFirmwareText -UsbReferenceOrder @('B', 'A'))
        Assert-PolicyTrue (-not [bool]$final.Result.Accepted) 'A physical USB display-order change was normalized away.'
    }

    Invoke-PolicyTestCase 'Windows Boot Manager path changes are rejected' {
        param($fixture)
        $final = Invoke-PolicyFinal -Fixture $fixture -CurrentText (New-PolicyFirmwareText -ChangedWindowsPath)
        Assert-PolicyTrue (-not [bool]$final.Result.Accepted) 'A Windows Boot Manager path change was normalized away.'
    }

    Invoke-PolicyTestCase 'Pending BootNext is rejected' {
        param($fixture)
        $pending = New-PolicyRawDocument -ReferenceLogSha256 $fixture.ReferenceLogPin.Sha256 -BootNext 2
        Assert-PolicyThrows {
            Set-PolicyScenario -Fixture $fixture -RawOverrides @{ 'firmware-final-before' = $pending }
            & $fixture.Module {
                param($stateValue, $referenceValue, $managerHash, $actionPath)
                Invoke-GpuFirmwarePolicyCaptureFixture -State $stateValue -ReferenceLogPin $referenceValue `
                    -BootManagerSha256 $managerHash -ActionDirectory $actionPath -Label 'firmware-final-before'
            } $fixture.State $fixture.ReferenceLogPin $fixture.BootManagerSha256 $fixture.FinalAction
        } 'Pending BootNext passed the final raw capture.' 'pending BootNext'
    }

    Invoke-PolicyTestCase 'Raw integrity findings are rejected' {
        param($fixture)
        $findings = New-PolicyRawDocument -ReferenceLogSha256 $fixture.ReferenceLogPin.Sha256 -Findings 1
        Assert-PolicyThrows {
            Set-PolicyScenario -Fixture $fixture -RawOverrides @{ 'firmware-final-before' = $findings }
            & $fixture.Module {
                param($stateValue, $referenceValue, $managerHash, $actionPath)
                Invoke-GpuFirmwarePolicyCaptureFixture -State $stateValue -ReferenceLogPin $referenceValue `
                    -BootManagerSha256 $managerHash -ActionDirectory $actionPath -Label 'firmware-final-before'
            } $fixture.State $fixture.ReferenceLogPin $fixture.BootManagerSha256 $fixture.FinalAction
        } 'Firmware integrity findings passed the raw capture.' 'integrity findings'
    }

    Invoke-PolicyTestCase 'Boot-manager or measured-boot hash mismatch is rejected' {
        param($fixture)
        $wrongHash = New-PolicyRawDocument -ReferenceLogSha256 $fixture.ReferenceLogPin.Sha256 -BootManagerSha256 ('B' * 64)
        Assert-PolicyThrows {
            Set-PolicyScenario -Fixture $fixture -RawOverrides @{ 'firmware-final-before' = $wrongHash }
            & $fixture.Module {
                param($stateValue, $referenceValue, $managerHash, $actionPath)
                Invoke-GpuFirmwarePolicyCaptureFixture -State $stateValue -ReferenceLogPin $referenceValue `
                    -BootManagerSha256 $managerHash -ActionDirectory $actionPath -Label 'firmware-final-before'
            } $fixture.State $fixture.ReferenceLogPin $fixture.BootManagerSha256 $fixture.FinalAction
        } 'A mismatched boot-manager hash passed the raw capture.' 'mismatched hashes'
    }

    Invoke-PolicyTestCase 'Raw BootOrder drift during FinalVerify is rejected' {
        param($fixture)
        $drifted = New-PolicyRawDocument -ReferenceLogSha256 $fixture.ReferenceLogPin.Sha256 -BootOrder @(0, 1, 2, 3, 5, 4)
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText -RawOverrides @{ 'firmware-final-after' = $drifted }
        } 'A changing raw BootOrder was accepted.' 'BootOrder changed'
    }

    Invoke-PolicyTestCase 'Failed fake audit command is rejected' {
        param($fixture)
        Assert-PolicyThrows {
            Set-PolicyScenario -Fixture $fixture -FailedLabels @('firmware-final-after')
            $currentPath = Join-Path $fixture.FinalAction 'firmware-native-failure.txt'
            [IO.File]::WriteAllText($currentPath, $fixture.BaselineText, (New-Object Text.UTF8Encoding($false)))
            $currentPin = Get-PolicyFilePin $currentPath
            $before = & $fixture.Module {
                param($stateValue, $referenceValue, $managerHash, $actionPath)
                Invoke-GpuFirmwarePolicyCaptureFixture -State $stateValue -ReferenceLogPin $referenceValue `
                    -BootManagerSha256 $managerHash -ActionDirectory $actionPath -Label 'firmware-final-before'
            } $fixture.State $fixture.ReferenceLogPin $fixture.BootManagerSha256 $fixture.FinalAction
            & $fixture.Module {
                param($stateValue, $currentValue, $actionPath, $beforeValue)
                Invoke-GpuFirmwarePolicyFinalFixture -State $stateValue -CurrentFirmwarePin $currentValue -ActionDirectory $actionPath -RawBefore $beforeValue
            } $fixture.State $currentPin $fixture.FinalAction $before
        } 'A failed firmware audit command passed final comparison.' 'failed with exit 17'
        $observations = & $fixture.Module { Get-GpuFirmwarePolicyObservations }
        Assert-PolicyTrue (@($observations.NativeCalls).Count -ge 2) 'Fake native boundary was not exercised.'
    }

    Invoke-PolicyTestCase 'Malformed raw JSON is rejected' {
        param($fixture)
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText -RawOverrides @{ 'firmware-final-after' = '{not-json' }
        } 'Malformed raw firmware JSON was accepted.' 'JSON|ConvertFrom-Json|Unexpected'
    }

    Invoke-PolicyTestCase 'Baseline evidence is not accepted after its pinned bytes change' {
        param($fixture)
        [IO.File]::AppendAllText($fixture.Backup.BaselinePin.Path, 'tampered', (New-Object Text.UTF8Encoding($false)))
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Modified comparison baseline bytes were accepted.' 'hash|pin|changed'
    }

    Invoke-PolicyTestCase 'A forged comparison-baseline hash is rejected' {
        param($fixture)
        $fixture.State.Backup.FirmwareComparison.BaselinePin.Sha256 = '0' * 64
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Forged comparison baseline hash metadata was accepted.' 'hash|pin|changed'
    }

    Invoke-PolicyTestCase 'Original textual baseline tampering is rejected and never refreshed' {
        param($fixture)
        [IO.File]::AppendAllText($fixture.FirmwarePin.Path, 'tampered', (New-Object Text.UTF8Encoding($false)))
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Modified original bcdedit baseline bytes were accepted.' 'hash|pin|changed'
    }

    Invoke-PolicyTestCase 'Comparison module source pin tampering is rejected' {
        param($fixture)
        $fixture.State.SourcePins[0].Sha256 = '0' * 64
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'A modified comparison module source pin was accepted.' 'hash|pin|changed'
    }

    Invoke-PolicyTestCase 'Comparison module file tampering is rejected' {
        param($fixture)
        [IO.File]::AppendAllText($fixture.ModulePath, "`n# modified", (New-Object Text.UTF8Encoding($false)))
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Modified comparison module bytes were accepted.' 'hash|pin|changed'
    }

    Invoke-PolicyTestCase 'Pinned audit tool tampering is rejected without executing it' {
        param($fixture)
        [IO.File]::AppendAllText($fixture.AuditPath, 'tampered', (New-Object Text.UTF8Encoding($false)))
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Modified fake audit tool bytes were accepted.' 'hash|pin|changed'
        $observations = & $fixture.Module { Get-GpuFirmwarePolicyObservations }
        Assert-PolicyEqual 0 @($observations.NativeCalls).Count 'Audit ran after its pinned bytes changed.'
    }

    Invoke-PolicyTestCase 'External evidence copy tampering is rejected' {
        param($fixture)
        $copy = $fixture.Backup.ExternalCopies[2]
        [IO.File]::AppendAllText($copy.External.Path, 'tampered', (New-Object Text.UTF8Encoding($false)))
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Modified external evidence was accepted.' 'hash|pin|changed'
    }

    Invoke-PolicyTestCase 'Missing an external baseline evidence copy is rejected' {
        param($fixture)
        [IO.File]::Delete($fixture.Backup.ExternalCopies[0].External.Path)
        Assert-PolicyThrows {
            $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText $fixture.BaselineText
        } 'Missing external baseline copy was accepted.' 'file|copy|does not exist|six'
    }

    Invoke-PolicyTestCase 'The historical text baseline pin and bytes stay unchanged after successful final comparison' {
        param($fixture)
        $beforePin = Get-PolicyFilePin $fixture.FirmwarePin.Path
        $beforeText = [IO.File]::ReadAllText($fixture.FirmwarePin.Path)
        $null = Invoke-PolicyFinal -Fixture $fixture -CurrentText (New-PolicyFirmwareText -GenericTypes @())
        $afterPin = Get-PolicyFilePin $fixture.FirmwarePin.Path
        $afterText = [IO.File]::ReadAllText($fixture.FirmwarePin.Path)
        Assert-PolicyEqual $beforePin.Sha256 $afterPin.Sha256 'Successful comparison rewrote the baseline pin.'
        Assert-PolicyEqual $beforeText $afterText 'Successful comparison rewrote the original textual baseline.'
    }

    Assert-PolicyTrue ($script:PassedCases -ge 15) 'Expected all firmware policy integration cases to run.'
    if ($script:FailedCases.Count -gt 0) {
        Write-Output ("GPU_FIRMWARE_POLICY_TESTS:FAIL cases={0}" -f $script:FailedCases.Count)
        foreach ($failure in $script:FailedCases) { Write-Output ("GPU_FIRMWARE_POLICY_TESTS:FAIL {0}" -f $failure) }
        throw "Firmware policy integration tests failed ($($script:FailedCases.Count)/$($script:Assertions) assertions)."
    }
    Write-Output ("GPU_FIRMWARE_POLICY_TESTS:PASS cases={0} assertions={1} fakeAuditCallsOnly=true" -f $script:PassedCases, $script:Assertions)
}
finally {
    if ([IO.Directory]::Exists($runRoot)) {
        $resolvedRunRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $runRoot).Path)
        $runPrefix = [IO.Path]::GetFullPath($scratchParent).TrimEnd('\') + '\run-'
        if (-not $resolvedRunRoot.StartsWith($runPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($resolvedRunRoot, [IO.Path]::GetFullPath($scratchParent), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean test run outside its scratch root: $resolvedRunRoot"
        }
        Remove-Item -LiteralPath $resolvedRunRoot -Recurse -Force -ErrorAction Stop
    }
}
