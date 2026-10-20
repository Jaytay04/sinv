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
        'Get-GpuDisarmBoundaryBootId',
        'Assert-GpuFreshPath',
        'Get-GpuSafeLeaf',
        'Get-GpuRunIdLeaf',
        'Invoke-GpuNativeCommand',
        'Assert-GpuPreflightAdapters',
        'Get-GpuDiskPhysicalClassification',
        'Assert-GpuNoReparseAncestors',
        'Copy-GpuEspTreeWithManifest',
        'Assert-GpuSystemBcdBackup',
        'Get-GpuRunPath',
        'Get-GpuBuildDirectories',
        'Get-GpuExpectedToolPaths',
        'Get-GpuExpectedSignedArtifactPaths',
        'Assert-GpuNamedPin',
        'Assert-GpuThumbprintAndPrivateKey',
        'Get-GpuStageExpectedFiles',
        'Get-GpuStageReadback',
        'Copy-GpuStageEvidence',
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
$script:GpuPlatformTestStoreHasPrivateKey = $true
$script:GpuPlatformTestStorePaths = New-Object 'System.Collections.Generic.List[string]'

function Set-GpuPlatformTestCertificateContext {
    param([Parameter(Mandatory = $true)][string]$SourceRoot)
    $script:CurrentSourceRoot = $SourceRoot
    $script:GpuPlatformTestStoreHasPrivateKey = $true
    $script:GpuPlatformTestStorePaths.Clear()
}

function Set-GpuPlatformTestPrivateKeyState {
    param([Parameter(Mandatory = $true)][bool]$HasPrivateKey)
    $script:GpuPlatformTestStoreHasPrivateKey = $HasPrivateKey
}

function Reset-GpuPlatformTestStoreQueries {
    $script:GpuPlatformTestStorePaths.Clear()
}

function Get-GpuPlatformTestStoreQueries {
    return $script:GpuPlatformTestStorePaths.ToArray()
}

function Get-Item {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$LiteralPath, [switch]$Force)

    if ($LiteralPath -match '^Cert:') {
        if ($LiteralPath -notlike 'Cert:\CurrentUser\My\*') {
            throw "TEST MOCK DENIED: unexpected certificate-store path: $LiteralPath"
        }
        [void]$script:GpuPlatformTestStorePaths.Add($LiteralPath)
        return [pscustomobject]@{ HasPrivateKey = $script:GpuPlatformTestStoreHasPrivateKey }
    }

    $arguments = @{ LiteralPath = $LiteralPath; Force = [bool]$Force }
    if ($PSBoundParameters.ContainsKey('ErrorAction')) { $arguments.ErrorAction = $PSBoundParameters['ErrorAction'] }
    return Microsoft.PowerShell.Management\Get-Item @arguments
}

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
        'Set-GpuPlatformTestCertificateContext',
        'Set-GpuPlatformTestPrivateKeyState',
        'Reset-GpuPlatformTestStoreQueries',
        'Get-GpuPlatformTestStoreQueries',
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

function New-GpuEspFixtureModule {
    param([string]$ModulePath, [string]$StatePath)

    $platformNames = @(
        'Get-GpuObjectProperty',
        'Assert-GpuNativeSuccess',
        'Resolve-GpuEspPartition',
        'Get-GpuEspMount',
        'Dismount-GpuEspMount'
    )
    $platformDefinitions = @(Get-PlatformFunctionTexts -Path $ModulePath -Names $platformNames)
    $stateTokens = $null
    $stateParseErrors = $null
    $stateAst = [System.Management.Automation.Language.Parser]::ParseFile($StatePath, [ref]$stateTokens, [ref]$stateParseErrors)
    if ($null -ne $stateParseErrors -and $stateParseErrors.Count -gt 0) {
        throw "State helper module does not parse: $($stateParseErrors[0].Message)"
    }
    $stateFunctions = @($stateAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true))
    $stateDefinitions = @($stateFunctions | ForEach-Object { $_.Extent.Text })

    # Every platform boundary is replaced by a deterministic fixture. The only
    # real filesystem access is the synthetic mountvol stdout file under runRoot.
    $fixtureDefinitions = @'
$script:GpuEspFixtureScenario = $null
$script:GpuEspFixturePartitionQueries = 0
$script:GpuEspFixturePartitionUsedDriveLetter = $false
$script:GpuEspFixtureDiskQueries = 0
$script:GpuEspFixtureNativeCalls = New-Object 'System.Collections.Generic.List[object]'

function Set-GpuEspFixtureScenario {
    param([Parameter(Mandatory = $true)][object]$Scenario)
    $script:GpuEspFixtureScenario = $Scenario
    $script:GpuEspFixturePartitionQueries = 0
    $script:GpuEspFixturePartitionUsedDriveLetter = $false
    $script:GpuEspFixtureDiskQueries = 0
    $script:GpuEspFixtureNativeCalls.Clear()
}

function Get-GpuEspFixtureObservations {
    return [pscustomobject]@{
        PartitionQueries = $script:GpuEspFixturePartitionQueries
        PartitionUsedDriveLetter = $script:GpuEspFixturePartitionUsedDriveLetter
        DiskQueries = $script:GpuEspFixtureDiskQueries
        NativeCalls = @($script:GpuEspFixtureNativeCalls.ToArray())
    }
}

function Get-PSDrive {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Name)
    if ($Name -ieq 'S' -and [bool]$script:GpuEspFixtureScenario.SDriveOccupied) {
        return [pscustomobject]@{ Name = 'S' }
    }
    return $null
}

function Test-Path {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $false)][string[]]$Path,
        [Parameter(Mandatory = $false)][string[]]$LiteralPath,
        [Parameter(Mandatory = $false)][ValidateSet('Any', 'Container', 'Leaf')][string]$PathType = 'Any'
    )
    $candidate = if ($PSBoundParameters.ContainsKey('LiteralPath')) { [string]$LiteralPath[0] } else { [string]$Path[0] }
    if ([string]::Equals($candidate, 'S:\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return [bool]$script:GpuEspFixtureScenario.SPathOccupied
    }
    $arguments = @{}
    if ($PSBoundParameters.ContainsKey('LiteralPath')) { $arguments.LiteralPath = $LiteralPath } else { $arguments.Path = $Path }
    if ($PSBoundParameters.ContainsKey('PathType')) { $arguments.PathType = $PathType }
    if ($PSBoundParameters.ContainsKey('ErrorAction')) { $arguments.ErrorAction = $PSBoundParameters['ErrorAction'] }
    return Microsoft.PowerShell.Management\Test-Path @arguments
}

function Get-GpuApplicationPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    if ($Name -cne 'mountvol.exe') { throw "TEST MOCK DENIED: unexpected executable lookup: $Name" }
    return 'fixture-mountvol.exe'
}

function Invoke-GpuNativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ActionDirectory,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Program -cne 'fixture-mountvol.exe') { throw "TEST MOCK DENIED: unexpected native program: $Program" }
    [void]$script:GpuEspFixtureNativeCalls.Add([pscustomobject]@{ Label = $Label; Arguments = @($Arguments) })
    $succeeded = $true
    if ($Label -ceq 'mount-system-esp') { $succeeded = [bool]$script:GpuEspFixtureScenario.MountSucceeded }
    elseif ($Label -ceq 'identify-system-esp') { $succeeded = [bool]$script:GpuEspFixtureScenario.IdentifySucceeded }
    elseif ($Label -cne 'unmount-owned-system-esp') { throw "TEST MOCK DENIED: unexpected native action: $Label" }

    $stdoutPath = $null
    if ($Label -ceq 'identify-system-esp' -and $succeeded) {
        $stdoutPath = Join-Path $script:GpuEspFixtureScenario.OutputRoot 'mountvol-volume.txt'
        [System.IO.File]::WriteAllText($stdoutPath, [string]$script:GpuEspFixtureScenario.VolumeText, (New-Object System.Text.UTF8Encoding($false)))
    }
    return [pscustomobject]@{ Succeeded = $succeeded; ExitCode = if ($succeeded) { 0 } else { 1 }; ReceiptPath = 'fixture-native-receipt.json'; StdoutPath = $stdoutPath }
}

function Get-Partition {
    [CmdletBinding()]
    param([Parameter(Mandatory = $false)][string]$DriveLetter)
    $script:GpuEspFixturePartitionQueries++
    $script:GpuEspFixturePartitionUsedDriveLetter = $PSBoundParameters.ContainsKey('DriveLetter')
    if (-not [string]::IsNullOrWhiteSpace([string]$script:GpuEspFixtureScenario.PartitionQueryError)) {
        throw [string]$script:GpuEspFixtureScenario.PartitionQueryError
    }
    return @($script:GpuEspFixtureScenario.Partitions)
}

function Get-Disk {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][int]$Number)
    $script:GpuEspFixtureDiskQueries++
    if (-not [string]::IsNullOrWhiteSpace([string]$script:GpuEspFixtureScenario.DiskQueryError)) {
        throw [string]$script:GpuEspFixtureScenario.DiskQueryError
    }
    if ($Number -ne [int]$script:GpuEspFixtureScenario.Disk.Number) { throw "TEST MOCK DENIED: unexpected disk number $Number" }
    return $script:GpuEspFixtureScenario.Disk
}
'@
    $source = @(
        'Set-StrictMode -Version 2.0'
        $stateDefinitions
        $platformDefinitions
        $fixtureDefinitions
        "Export-ModuleMember -Function 'Set-GpuEspFixtureScenario','Get-GpuEspFixtureObservations'"
    ) -join "`r`n"
    $fixtureModule = New-Module -Name ('GpuEspFixture' + [guid]::NewGuid().ToString('N')) `
        -ScriptBlock ([scriptblock]::Create($source))
    Import-Module -ModuleInfo $fixtureModule -Force -ErrorAction Stop
    return $fixtureModule
}

function New-GpuSystemBcdExportFixtureModule {
    param([string]$ModulePath, [string]$StatePath)

    $platformNames = @(
        'Export-GpuSystemBcdBackup',
        'Assert-GpuNativeSuccess',
        'Assert-GpuFreshPath',
        'Assert-GpuNoReparseAncestors'
    )
    $platformDefinitions = @(Get-PlatformFunctionTexts -Path $ModulePath -Names $platformNames)
    $stateTokens = $null
    $stateParseErrors = $null
    $stateAst = [System.Management.Automation.Language.Parser]::ParseFile($StatePath, [ref]$stateTokens, [ref]$stateParseErrors)
    if ($null -ne $stateParseErrors -and $stateParseErrors.Count -gt 0) {
        throw "State helper module does not parse: $($stateParseErrors[0].Message)"
    }
    $stateFunctions = @($stateAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true))
    $stateDefinitions = @($stateFunctions | ForEach-Object { $_.Extent.Text })
    $mockDefinitions = @'
$script:GpuBcdExportFixtureScenario = $null
$script:GpuBcdExportFixtureNativeCalls = New-Object 'System.Collections.Generic.List[object]'

function Set-GpuBcdExportFixtureScenario {
    param([Parameter(Mandatory = $true)][object]$Scenario)
    $script:GpuBcdExportFixtureScenario = $Scenario
    $script:GpuBcdExportFixtureNativeCalls.Clear()
}

function Get-GpuBcdExportFixtureNativeCalls {
    return @($script:GpuBcdExportFixtureNativeCalls.ToArray())
}

function Get-GpuApplicationPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    if ($Name -cne 'bcdedit.exe') { throw "TEST MOCK DENIED: unexpected executable lookup: $Name" }
    return 'fixture-bcdedit.exe'
}

function Invoke-GpuNativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ActionDirectory,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Program -cne 'fixture-bcdedit.exe') { throw "TEST MOCK DENIED: unexpected native program: $Program" }
    if ($Label -cne 'export-system-bcd') { throw "TEST MOCK DENIED: unexpected native action: $Label" }
    [void]$script:GpuBcdExportFixtureNativeCalls.Add([pscustomobject]@{
        Program = $Program
        Arguments = @($Arguments)
        ActionDirectory = $ActionDirectory
        Label = $Label
    })

    $mode = [string]$script:GpuBcdExportFixtureScenario.OutputMode
    if ($mode -ceq 'Bytes') {
        [System.IO.File]::WriteAllBytes($Arguments[1], [byte[]]$script:GpuBcdExportFixtureScenario.Bytes)
    }
    elseif ($mode -ceq 'Empty') {
        [System.IO.File]::WriteAllBytes($Arguments[1], [byte[]]@())
    }
    elseif ($mode -cne 'Missing') {
        throw "TEST MOCK DENIED: unknown BCD export output mode: $mode"
    }

    $exitCode = [int]$script:GpuBcdExportFixtureScenario.ExitCode
    return [pscustomobject]@{
        Succeeded = [bool]$script:GpuBcdExportFixtureScenario.Succeeded
        ExitCode = $exitCode
        ReceiptPath = Join-Path $ActionDirectory 'fixture-export-system-bcd.receipt.json'
    }
}
'@
    $source = @(
        'Set-StrictMode -Version 2.0'
        $stateDefinitions
        $platformDefinitions
        $mockDefinitions
        "Export-ModuleMember -Function 'Export-GpuSystemBcdBackup','Set-GpuBcdExportFixtureScenario','Get-GpuBcdExportFixtureNativeCalls'"
    ) -join "`r`n"
    $fixtureModule = New-Module -Name ('GpuBcdExportFixture' + [guid]::NewGuid().ToString('N')) `
        -ScriptBlock ([scriptblock]::Create($source))
    Import-Module -ModuleInfo $fixtureModule -Force -ErrorAction Stop
    return $fixtureModule
}

function New-GpuFinalVerifyFixtureModule {
    param([string]$ModulePath, [string]$StatePath)

    $platformNames = @(
        'Get-GpuObjectProperty',
        'Set-GpuStateProperty',
        'New-GpuPlatformResult',
        'Assert-GpuSecureBootOn',
        'Invoke-GpuFinalVerify'
    )
    $platformDefinitions = @(Get-PlatformFunctionTexts -Path $ModulePath -Names $platformNames)
    $stateTokens = $null
    $stateParseErrors = $null
    $stateAst = [System.Management.Automation.Language.Parser]::ParseFile($StatePath, [ref]$stateTokens, [ref]$stateParseErrors)
    if ($null -ne $stateParseErrors -and $stateParseErrors.Count -gt 0) {
        throw "State helper module does not parse: $($stateParseErrors[0].Message)"
    }
    $stateFunctions = @($stateAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true))
    $stateDefinitions = @($stateFunctions | ForEach-Object { $_.Extent.Text })
    $fixtureDefinitions = @'
$script:GpuFinalVerifyFixtureScenario = $null
$script:GpuFinalVerifyFixtureMounted = $false
$script:GpuFinalVerifyFixtureMountCount = 0
$script:GpuFinalVerifyFixtureDismountCount = 0
$script:GpuFinalVerifyFixtureFirmwareMountStates = New-Object 'System.Collections.Generic.List[bool]'

function Set-GpuFinalVerifyFixtureScenario {
    param([Parameter(Mandatory = $true)][object]$Scenario)
    $script:GpuFinalVerifyFixtureScenario = $Scenario
    $script:GpuFinalVerifyFixtureMounted = $false
    $script:GpuFinalVerifyFixtureMountCount = 0
    $script:GpuFinalVerifyFixtureDismountCount = 0
    $script:GpuFinalVerifyFixtureFirmwareMountStates.Clear()
}

function Get-GpuFinalVerifyFixtureObservations {
    return [pscustomobject]@{
        Mounted = $script:GpuFinalVerifyFixtureMounted
        MountCount = $script:GpuFinalVerifyFixtureMountCount
        DismountCount = $script:GpuFinalVerifyFixtureDismountCount
        FirmwareEnumerationMounted = @($script:GpuFinalVerifyFixtureFirmwareMountStates.ToArray())
    }
}

function Assert-GpuAdministrator { }

function Get-GpuCurrentMachineSnapshot {
    param([switch]$IncludeSecureBoot)
    return $script:GpuFinalVerifyFixtureScenario.Current
}

function Assert-GpuSameMachine {
    param([object]$State, [object]$Current, [switch]$RequireCpuCount)
}

function Get-GpuExpectedToolPaths {
    param([object]$State)
    return [pscustomobject]@{ Probe = [string]$script:GpuFinalVerifyFixtureScenario.ProbePin.Path }
}

function Assert-GpuNamedPin {
    param([object]$PinSet, [string]$Name, [string]$ExpectedPath, [string]$Description)
    $pin = $PinSet.PSObject.Properties[$Name]
    if ($null -eq $pin -or [string]$pin.Value.Path -cne $ExpectedPath) {
        throw "Fixture $Description pin does not match the expected path."
    }
    return $pin.Value
}

function Get-GpuPreflight {
    param([object]$State, [string]$ActionDirectory, [string]$Name)
    return [pscustomobject]@{
        Report = [pscustomobject]@{
            InventoryComplete = $true
            DiagnosticCandidate = [pscustomobject]@{ PciId = 'PCI\VEN_8086&DEV_4680'; Present = $true }
            Nvidia = [pscustomobject]@{ PciId = 'PCI\VEN_10DE&DEV_2206'; Present = $true }
        }
        Pin = $null
    }
}

function Assert-GpuPreflightAdapters {
    param([object]$Report)
    if (-not [bool]$script:GpuFinalVerifyFixtureScenario.PreflightValid) { throw 'Synthetic preflight was rejected.' }
}

function Get-GpuApplicationPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    if ($Name -cne 'bcdedit.exe') { throw "TEST MOCK DENIED: unexpected executable lookup: $Name" }
    return 'fixture-bcdedit.exe'
}

function Assert-GpuBackupState {
    param([object]$State)
}

function Get-GpuEspMount {
    param([string]$ActionDirectory)
    if ($script:GpuFinalVerifyFixtureMounted) { throw 'Fixture ESP was already mounted.' }
    $script:GpuFinalVerifyFixtureMounted = $true
    $script:GpuFinalVerifyFixtureMountCount++
    return $script:GpuFinalVerifyFixtureScenario.MountedEsp
}

function Assert-GpuMountedEspMatchesBackup {
    param([object]$MountedEsp, [object]$Backup, [string]$Description)
    return [pscustomobject]@{ Sha256 = [string]$Backup.BootManagerSha256 }
}

function Dismount-GpuEspMount {
    param([string]$ActionDirectory)
    if (-not $script:GpuFinalVerifyFixtureMounted) { throw 'Fixture attempted to dismount an ESP it did not mount.' }
    $script:GpuFinalVerifyFixtureMounted = $false
    $script:GpuFinalVerifyFixtureDismountCount++
}

function Invoke-GpuNativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ActionDirectory,
        [Parameter(Mandatory = $true)][string]$Label,
        [int[]]$AllowedExitCodes = @(0)
    )
    if ($Label -ceq 'post-disarm-absent-health') {
        $stdoutPath = Join-Path $ActionDirectory 'fixture-post-disarm-health.stdout.txt'
        $stderrPath = Join-Path $ActionDirectory 'fixture-post-disarm-health.stderr.txt'
        [System.IO.File]::WriteAllText($stdoutPath, "UEFI_HYPERVISOR_WINDOWS_CPU:0:0:ABSENT`r`nUEFI_HYPERVISOR_WINDOWS_CPUID:ABSENT`r`n")
        [System.IO.File]::WriteAllText($stderrPath, '')
        return [pscustomobject]@{ Succeeded = $true; ExitCode = 0; StdoutPath = $stdoutPath; StderrPath = $stderrPath; ReceiptPath = $stdoutPath }
    }
    if ($Label -ceq 'firmware-enumeration-final') {
        [void]$script:GpuFinalVerifyFixtureFirmwareMountStates.Add([bool]$script:GpuFinalVerifyFixtureMounted)
        $stdoutPath = Join-Path $ActionDirectory 'fixture-firmware-final.stdout.txt'
        $stderrPath = Join-Path $ActionDirectory 'fixture-firmware-final.stderr.txt'
        $text = if ($script:GpuFinalVerifyFixtureMounted) {
            [string]$script:GpuFinalVerifyFixtureScenario.FirmwareTextWhenMounted
        }
        else {
            $alternate = [string]$script:GpuFinalVerifyFixtureScenario.FirmwareTextWhenUnmounted
            if ([string]::IsNullOrEmpty($alternate)) { $alternate = "device partition=\Device\HarddiskVolume2`r`n" }
            $alternate
        }
        [System.IO.File]::WriteAllText($stdoutPath, $text)
        [System.IO.File]::WriteAllText($stderrPath, '')
        $succeeded = [bool]$script:GpuFinalVerifyFixtureScenario.FirmwareNativeSucceeded
        return [pscustomobject]@{
            Succeeded = $succeeded
            ExitCode = if ($succeeded) { 0 } else { 1 }
            StdoutPath = $stdoutPath
            StderrPath = $stderrPath
            ReceiptPath = $stdoutPath
        }
    }
    throw "TEST MOCK DENIED: unexpected native command label: $Label"
}
'@
    $source = @(
        'Set-StrictMode -Version 2.0'
        $stateDefinitions
        $platformDefinitions
        $fixtureDefinitions
        "Export-ModuleMember -Function 'Invoke-GpuFinalVerify','Set-GpuFinalVerifyFixtureScenario','Get-GpuFinalVerifyFixtureObservations'"
    ) -join "`r`n"
    $fixtureModule = New-Module -Name ('GpuFinalVerifyFixture' + [guid]::NewGuid().ToString('N')) `
        -ScriptBlock ([scriptblock]::Create($source))
    Import-Module -ModuleInfo $fixtureModule -Force -ErrorAction Stop
    return $fixtureModule
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
$bcdExportFixtureModule = $null
$finalVerifyFixtureModule = $null

try {
    $isolatedModule = New-PlatformHelperModule -ModulePath $platformPath -StatePath $stateModulePath
    $bcdExportFixtureModule = New-GpuSystemBcdExportFixtureModule -ModulePath $platformPath -StatePath $stateModulePath
    $finalVerifyFixtureModule = New-GpuFinalVerifyFixtureModule -ModulePath $platformPath -StatePath $stateModulePath

    Invoke-PlatformTestCase 'Stage evidence captures optional payload diagnostics without changing legacy output' {
        $fixtureEsp = Join-Path $runRoot 'evidence-esp'
        $fixtureStage = Join-Path $fixtureEsp 'EFI\insecure-boot'
        $legacyAction = Join-Path $runRoot 'evidence-legacy'
        $diagnosticAction = Join-Path $runRoot 'evidence-diagnostic'
        foreach ($directory in @($fixtureStage, $legacyAction, $diagnosticAction)) {
            [void](New-Item -ItemType Directory -Path $directory -Force)
        }
        $legacyText = "schema=1`nstate=READY_BOOT_MANAGER`nreplay=ABORTED`nrestore=PASS`npayload=FAILED`nexit_boot_services_calls=0`n"
        [IO.File]::WriteAllText((Join-Path $fixtureStage 'handoff-status.txt'), $legacyText)
        $legacyEvidence = @(Copy-GpuStageEvidence -EspRoot $fixtureEsp -ActionDirectory $legacyAction)
        $absent = @($legacyEvidence | Where-Object { $_.Name -ceq 'payload-diagnostic.txt' })
        Assert-PlatformEqual 1 $absent.Count 'Legacy stage omitted the optional diagnostic observation.'
        Assert-PlatformTrue (-not $absent[0].Present) 'Legacy stage invented a payload diagnostic.'
        Assert-PlatformTrue ($null -eq $absent[0].Pin) 'Absent diagnostic was assigned a file pin.'

        $diagnosticText = "schema=1`npayload_launch=load`npayload_status=0x800000000000001A`nshim_load_phase=shim_verify`nshim_load_status=0x800000000000001A`n"
        $sourcePath = Join-Path $fixtureStage 'payload-diagnostic.txt'
        [IO.File]::WriteAllText($sourcePath, $diagnosticText)
        $evidence = @(Copy-GpuStageEvidence -EspRoot $fixtureEsp -ActionDirectory $diagnosticAction)
        $captured = @($evidence | Where-Object { $_.Name -ceq 'payload-diagnostic.txt' })
        Assert-PlatformEqual 1 $captured.Count 'Payload diagnostic was not captured exactly once.'
        Assert-PlatformTrue $captured[0].Present 'Existing payload diagnostic was reported absent.'
        Assert-PlatformEqual $diagnosticText ([IO.File]::ReadAllText($captured[0].Path)) 'Payload diagnostic bytes changed during collection.'
        Assert-PlatformEqual (Get-GpuFilePin -Path $sourcePath).Sha256 $captured[0].Pin.Sha256 'Captured payload diagnostic hash differs from its source.'
        Assert-PlatformEqual $diagnosticText ([IO.File]::ReadAllText($sourcePath)) 'Collection changed its diagnostic source.'
        Assert-PlatformEqual $legacyText ([IO.File]::ReadAllText((Join-Path $diagnosticAction 'esp-handoff-status.txt'))) 'Diagnostic collection changed the legacy handoff status.'
        Assert-PlatformThrows { Copy-GpuStageEvidence -EspRoot $fixtureEsp -ActionDirectory $diagnosticAction } 'Collection overwrote existing evidence.' 'existing path'
    }

    Invoke-PlatformTestCase 'Repeated disarm preserves the original reset boundary only for an already disarmed run' {
        Assert-PlatformEqual 'boot-current' `
            (Get-GpuDisarmBoundaryBootId -State ([pscustomobject]@{ Phase = 'Signed'; DisarmedBootId = 'boot-old' }) -CurrentBootId 'boot-current') `
            'A new activation phase reused a stale disarm boundary.'
        Assert-PlatformEqual 'boot-original-disarm' `
            (Get-GpuDisarmBoundaryBootId -State ([pscustomobject]@{ Phase = 'Disarmed'; DisarmedBootId = 'boot-original-disarm' }) -CurrentBootId 'boot-after-reset') `
            'Repeated disarm replaced the original reset boundary.'
        Assert-PlatformEqual 'boot-current' `
            (Get-GpuDisarmBoundaryBootId -State ([pscustomobject]@{ Phase = 'Disarmed'; DisarmedBootId = '' }) -CurrentBootId 'boot-current') `
            'A disarmed run without a saved boundary did not establish the current boot ID.'
        Assert-PlatformEqual 'boot-current' `
            (Get-GpuDisarmBoundaryBootId -State ([pscustomobject]@{ Phase = 'Prepared' }) -CurrentBootId 'boot-current') `
            'A state without a saved boundary did not use the current boot ID.'
    }

    Invoke-PlatformTestCase 'Pinned certificate DER and CurrentUser private-key check use the intended store path' {
        $fixtureSourceRoot = Join-Path $runRoot 'certificate-source'
        [void](New-Item -ItemType Directory -Path $fixtureSourceRoot)
        $fixtureCertificatePath = Join-Path $fixtureSourceRoot 'hypersinv.cer'
        $repositoryCertificatePath = Join-Path $workspaceRoot 'components\hypervisor\hypersinv.cer'
        if (-not [System.IO.File]::Exists($repositoryCertificatePath)) { throw "Pinned public certificate fixture is missing: $repositoryCertificatePath" }
        [System.IO.File]::Copy($repositoryCertificatePath, $fixtureCertificatePath)
        Set-GpuPlatformTestCertificateContext -SourceRoot $fixtureSourceRoot

        $expectedThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
        $actualThumbprint = Assert-GpuThumbprintAndPrivateKey -CertificatePath $fixtureCertificatePath
        Assert-PlatformEqual $expectedThumbprint $actualThumbprint 'Pinned public certificate was not accepted.'
        $storeQueries = @(Get-GpuPlatformTestStoreQueries)
        Assert-PlatformEqual 1 $storeQueries.Count 'Valid pinned certificate did not query exactly one store path.'
        Assert-PlatformEqual ('Cert:\CurrentUser\My\' + $expectedThumbprint) $storeQueries[0] 'Certificate helper queried an unexpected store path.'

        Reset-GpuPlatformTestStoreQueries
        Set-GpuPlatformTestPrivateKeyState -HasPrivateKey $false
        Assert-PlatformThrows { Assert-GpuThumbprintAndPrivateKey -CertificatePath $fixtureCertificatePath } `
            'Certificate without an accessible private key was accepted.' 'no accessible private key'
        $storeQueries = @(Get-GpuPlatformTestStoreQueries)
        Assert-PlatformEqual 1 $storeQueries.Count 'Private-key check did not query exactly one store path.'
        Assert-PlatformEqual ('Cert:\CurrentUser\My\' + $expectedThumbprint) $storeQueries[0] 'Private-key check queried an unexpected store path.'

        $malformedCertificatePath = Join-Path $fixtureSourceRoot 'malformed.cer'
        [System.IO.File]::WriteAllBytes($malformedCertificatePath, [byte[]]@(0, 1, 2, 3, 4))
        Reset-GpuPlatformTestStoreQueries
        Assert-PlatformThrows { Assert-GpuThumbprintAndPrivateKey -CertificatePath $malformedCertificatePath } `
            'Malformed public certificate data was accepted.' ''
        Assert-PlatformEqual 0 (@(Get-GpuPlatformTestStoreQueries).Count) 'Malformed certificate reached the certificate store lookup.'

        $wrongCertificatePath = Join-Path $fixtureSourceRoot 'wrong-valid.cer'
        $wrongRsa = $null
        $wrongCertificate = $null
        try {
            $wrongRsa = [System.Security.Cryptography.RSA]::Create()
            $wrongSubject = [System.Security.Cryptography.X509Certificates.X500DistinguishedName]::new('CN=GPU host test ephemeral certificate')
            $wrongRequest = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
                $wrongSubject,
                $wrongRsa,
                [System.Security.Cryptography.HashAlgorithmName]::SHA256,
                [System.Security.Cryptography.RSASignaturePadding]::Pkcs1
            )
            $wrongCertificate = $wrongRequest.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddDays(1))
            [System.IO.File]::WriteAllBytes($wrongCertificatePath, $wrongCertificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
        }
        finally {
            if ($null -ne $wrongCertificate) { $wrongCertificate.Dispose() }
            if ($null -ne $wrongRsa) { $wrongRsa.Dispose() }
        }

        Reset-GpuPlatformTestStoreQueries
        Assert-PlatformThrows { Assert-GpuThumbprintAndPrivateKey -CertificatePath $wrongCertificatePath } `
            'A different valid public certificate was accepted.' 'does not match the pinned project thumbprint'
        Assert-PlatformEqual 0 (@(Get-GpuPlatformTestStoreQueries).Count) 'Wrong valid certificate reached the certificate store lookup.'
    }

    Invoke-PlatformTestCase 'Exact ESP volume resolver validates partition identity and rejects ambiguity or malformed metadata' {
        $espFixtureModule = New-GpuEspFixtureModule -ModulePath $platformPath -StatePath $stateModulePath
        try {
            $volume = '\\?\Volume{187BC913-7576-4E6E-8D54-18EEB5C38FCD}\'
            $otherVolume = '\\?\Volume{287BC913-7576-4E6E-8D54-18EEB5C38FCD}\'
            $validGuid = 'A12B3456-7890-4ABC-8DEF-0123456789AB'
            $makePartition = {
                param([string[]]$AccessPaths, [hashtable]$Overrides)
                $values = [ordered]@{
                    DriveLetter = $null
                    AccessPaths = @($AccessPaths)
                    GptType = '{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}'
                    Guid = '{A12B3456-7890-4ABC-8DEF-0123456789AB}'
                    DiskNumber = 0
                    PartitionNumber = 2
                }
                if ($null -ne $Overrides) { foreach ($key in $Overrides.Keys) { $values[$key] = $Overrides[$key] } }
                return [pscustomobject]$values
            }
            $makeScenario = {
                param([object[]]$Partitions, [string]$PartitionQueryError)
                return [pscustomobject]@{
                    Partitions = @($Partitions)
                    PartitionQueryError = $PartitionQueryError
                    DiskQueryError = ''
                    Disk = [pscustomobject]@{ Number = 0; UniqueId = 'fixture-disk-unique-id' }
                    VolumeText = $volume
                    OutputRoot = $runRoot
                    MountSucceeded = $true
                    IdentifySucceeded = $true
                    SDriveOccupied = $false
                    SPathOccupied = $false
                }
            }
            $invokeResolve = {
                param([string]$RequestedVolume)
                return & $espFixtureModule { param($Value) Resolve-GpuEspPartition -Volume $Value } $RequestedVolume
            }
            $expectResolveFailure = {
                param([string]$Name, [string]$RequestedVolume, [object[]]$Partitions, [string]$Pattern, [int]$ExpectedQueries, [string]$QueryError)
                $scenario = & $makeScenario $Partitions $QueryError
                Set-GpuEspFixtureScenario -Scenario $scenario
                Assert-PlatformThrows { & $invokeResolve $RequestedVolume } "$Name was accepted." $Pattern
                $observations = Get-GpuEspFixtureObservations
                Assert-PlatformEqual $ExpectedQueries $observations.PartitionQueries "$Name issued the wrong number of Storage queries."
                Assert-PlatformTrue (-not [bool]$observations.PartitionUsedDriveLetter) "$Name attempted letter-based partition lookup."
            }

            $validPartition = & $makePartition @($volume.ToLowerInvariant()) @{}
            $validScenario = & $makeScenario @($validPartition) ''
            Set-GpuEspFixtureScenario -Scenario $validScenario
            $resolved = & $invokeResolve $volume
            Assert-PlatformTrue ([object]::ReferenceEquals($resolved, $validPartition)) 'Resolver did not return the original partition object for a case-insensitive exact volume match.'
            $observations = Get-GpuEspFixtureObservations
            Assert-PlatformEqual 1 $observations.PartitionQueries 'Valid resolution did not make one partition query.'
            Assert-PlatformTrue (-not [bool]$observations.PartitionUsedDriveLetter) 'Valid resolution used a drive-letter partition query.'

            $wrongVolumePartition = & $makePartition @($otherVolume) @{}
            & $expectResolveFailure 'Wrong volume mapping' $volume @($wrongVolumePartition) 'exactly one partition' 1 ''
            & $expectResolveFailure 'No volume match' $volume @() 'exactly one partition' 1 ''
            $ambiguousPartitions = @((& $makePartition @($volume) @{}), (& $makePartition @($volume.ToLowerInvariant()) @{ PartitionNumber = 3 }))
            & $expectResolveFailure 'Ambiguous volume mapping' $volume $ambiguousPartitions 'exactly one partition' 1 ''
            & $expectResolveFailure 'Malformed volume identity' 'not-a-volume-guid' @($validPartition) 'ESP identity' 0 ''
            & $expectResolveFailure 'Wrong GPT partition type' $volume @((& $makePartition @($volume) @{ GptType = 'basic-data' })) 'not the GPT EFI System Partition' 1 ''
            & $expectResolveFailure 'Missing partition GUID' $volume @((& $makePartition @($volume) @{ Guid = $null })) 'GUID is missing or malformed' 1 ''
            & $expectResolveFailure 'Malformed partition GUID' $volume @((& $makePartition @($volume) @{ Guid = 'bad-guid' })) 'GUID is missing or malformed' 1 ''
            & $expectResolveFailure 'Zero partition GUID' $volume @((& $makePartition @($volume) @{ Guid = '00000000-0000-0000-0000-000000000000' })) 'GUID is missing or malformed' 1 ''
            & $expectResolveFailure 'Invalid disk number' $volume @((& $makePartition @($volume) @{ DiskNumber = -1 })) 'disk or partition number is missing or invalid' 1 ''
            & $expectResolveFailure 'Invalid partition number' $volume @((& $makePartition @($volume) @{ PartitionNumber = 0 })) 'disk or partition number is missing or invalid' 1 ''
            & $expectResolveFailure 'Null disk number' $volume @((& $makePartition @($volume) @{ DiskNumber = $null })) 'disk or partition number is missing or invalid' 1 ''
            & $expectResolveFailure 'Empty disk number' $volume @((& $makePartition @($volume) @{ DiskNumber = '' })) 'disk or partition number is missing or invalid' 1 ''
            & $expectResolveFailure 'Null partition number' $volume @((& $makePartition @($volume) @{ PartitionNumber = $null })) 'disk or partition number is missing or invalid' 1 ''
            & $expectResolveFailure 'Empty partition number' $volume @((& $makePartition @($volume) @{ PartitionNumber = '' })) 'disk or partition number is missing or invalid' 1 ''
            & $expectResolveFailure 'Storage query error' $volume @() 'synthetic partition query failure' 1 'synthetic partition query failure'
        }
        finally {
            if ($null -ne $espFixtureModule) { Remove-Module -ModuleInfo $espFixtureModule -Force -ErrorAction SilentlyContinue }
        }
    }

    Invoke-PlatformTestCase 'ESP mount uses exact volume resolution and dismounts only mappings acquired by this action' {
        $espFixtureModule = New-GpuEspFixtureModule -ModulePath $platformPath -StatePath $stateModulePath
        try {
            $volume = '\\?\Volume{187BC913-7576-4E6E-8D54-18EEB5C38FCD}\'
            $validPartition = [pscustomobject]@{
                DriveLetter = $null
                AccessPaths = @($volume)
                GptType = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
                Guid = '{A12B3456-7890-4ABC-8DEF-0123456789AB}'
                DiskNumber = 0
                PartitionNumber = 2
            }
            $disk = [pscustomobject]@{ Number = 0; UniqueId = 'fixture-disk-unique-id' }
            $stdoutRoot = Join-Path $runRoot 'esp-mount-fixture'
            [void](New-Item -ItemType Directory -Path $stdoutRoot -Force)
            $scenario = [pscustomobject]@{
                Partitions = @($validPartition)
                PartitionQueryError = ''
                DiskQueryError = ''
                Disk = $disk
                VolumeText = $volume
                OutputRoot = $stdoutRoot
                MountSucceeded = $true
                IdentifySucceeded = $true
                SDriveOccupied = $false
                SPathOccupied = $false
            }
            $invokeMount = {
                return & $espFixtureModule { param($Directory) Get-GpuEspMount -ActionDirectory $Directory } $stdoutRoot
            }
            $getObservations = { Get-GpuEspFixtureObservations }
            $setScenario = { param($Value) Set-GpuEspFixtureScenario -Scenario $Value }

            & $setScenario $scenario
            $mount = & $invokeMount
            Assert-PlatformEqual $volume $mount.Volume 'ESP mount returned the wrong mountvol volume identity.'
            Assert-PlatformEqual 0 $mount.DiskNumber 'ESP mount returned the wrong disk number.'
            Assert-PlatformEqual 'fixture-disk-unique-id' $mount.DiskUniqueId 'ESP mount returned the wrong disk identity.'
            Assert-PlatformEqual 2 $mount.PartitionNumber 'ESP mount returned the wrong partition number.'
            Assert-PlatformEqual 'A12B3456-7890-4ABC-8DEF-0123456789AB' $mount.PartitionGuid 'ESP mount did not normalize the partition GUID.'
            $observations = & $getObservations
            Assert-PlatformEqual 1 $observations.PartitionQueries 'ESP mount did not resolve one exact partition.'
            Assert-PlatformTrue (-not [bool]$observations.PartitionUsedDriveLetter) 'ESP mount queried partition by S: instead of AccessPaths.'
            Assert-PlatformEqual 1 $observations.DiskQueries 'ESP mount did not query exactly the resolved disk.'
            Assert-PlatformEqual 0 (@($observations.NativeCalls | Where-Object { $_.Label -ceq 'unmount-owned-system-esp' }).Count) 'Successful mount was unexpectedly dismounted before returning.'

            $scenario.Partitions = @()
            & $setScenario $scenario
            Assert-PlatformThrows { & $invokeMount } 'Unresolvable mounted ESP did not fail.' 'exactly one partition'
            $observations = & $getObservations
            Assert-PlatformEqual 1 (@($observations.NativeCalls | Where-Object { $_.Label -ceq 'unmount-owned-system-esp' }).Count) 'Resolution failure did not dismount exactly once.'
            Assert-PlatformEqual 0 $observations.DiskQueries 'Unresolvable ESP reached Get-Disk.'

            $scenario.SDriveOccupied = $true
            & $setScenario $scenario
            Assert-PlatformThrows { & $invokeMount } 'Occupied S: drive was accepted.' 'S: is already assigned'
            $observations = & $getObservations
            Assert-PlatformEqual 0 $observations.NativeCalls.Count 'Occupied S: triggered mountvol or dismount.'

            $scenario.SDriveOccupied = $false
            $scenario.SPathOccupied = $true
            & $setScenario $scenario
            Assert-PlatformThrows { & $invokeMount } 'Occupied S: path was accepted.' 'S: already resolves'
            $observations = & $getObservations
            Assert-PlatformEqual 0 $observations.NativeCalls.Count 'Occupied S: path triggered mountvol or dismount.'

            $scenario.SPathOccupied = $false
            $scenario.MountSucceeded = $false
            & $setScenario $scenario
            Assert-PlatformThrows { & $invokeMount } 'Failed mountvol /S was accepted.' 'Mounting the system ESP at S: failed'
            $observations = & $getObservations
            Assert-PlatformEqual 1 $observations.NativeCalls.Count 'Failed mount issued unexpected follow-up native commands.'
            Assert-PlatformEqual 0 (@($observations.NativeCalls | Where-Object { $_.Label -ceq 'unmount-owned-system-esp' }).Count) 'Failed mount attempted to dismount a mapping it never acquired.'
        }
        finally {
            if ($null -ne $espFixtureModule) { Remove-Module -ModuleInfo $espFixtureModule -Force -ErrorAction SilentlyContinue }
        }
    }

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

    Invoke-PlatformTestCase 'Logical BCD export permits locked live BCD logs while preserving unrelated ESP files' {
        $sourceRoot = Join-Path $runRoot 'esp-source-bcd-export'
        $bootRoot = Join-Path $sourceRoot 'Microsoft\Boot'
        $otherRoot = Join-Path $sourceRoot 'Other'
        $actionDirectory = Join-Path $runRoot 'bcd-copy-action'
        [void](New-Item -ItemType Directory -Path $bootRoot -Force)
        [void](New-Item -ItemType Directory -Path $otherRoot -Force)
        [void](New-Item -ItemType Directory -Path $actionDirectory -Force)

        $sourceBytes = @{
            'Microsoft\Boot\bootmgfw.efi' = [byte[]]@(1, 2, 3, 4)
            'Microsoft\Boot\BCD' = [byte[]]@(11, 12, 13, 14, 15)
            'Microsoft\Boot\BCD.LOG' = [byte[]]@(21, 22)
            'Microsoft\Boot\BCD.LOG1' = [byte[]]@(31, 32)
            'Microsoft\Boot\BCD.LOG2' = [byte[]]@(41, 42)
            'Microsoft\Boot\BCD.LOG3' = [byte[]]@(51, 52)
            'Microsoft\Boot\BCD.LOG.old' = [byte[]]@(61, 62)
            'Other\diagnostic.bin' = [byte[]]@(71, 72, 73)
        }
        foreach ($relativePath in $sourceBytes.Keys) {
            $sourcePath = Join-Path $sourceRoot $relativePath
            [System.IO.File]::WriteAllBytes($sourcePath, [byte[]]$sourceBytes[$relativePath])
        }

        $systemBcdPath = Join-Path $actionDirectory 'system-bcd-export.bcd'
        [System.IO.File]::WriteAllBytes($systemBcdPath, [byte[]]@(81, 82, 83, 84, 85, 86))
        $systemBcdPin = Get-GpuFilePin -Path $systemBcdPath
        $lockedPaths = @(
            (Join-Path $bootRoot 'BCD'),
            (Join-Path $bootRoot 'BCD.LOG'),
            (Join-Path $bootRoot 'BCD.LOG1'),
            (Join-Path $bootRoot 'BCD.LOG2')
        )
        $lockStreams = New-Object 'System.Collections.Generic.List[System.IO.FileStream]'
        try {
            foreach ($lockedPath in $lockedPaths) {
                $lockStreams.Add([System.IO.File]::Open(
                    $lockedPath,
                    [System.IO.FileMode]::Open,
                    [System.IO.FileAccess]::ReadWrite,
                    [System.IO.FileShare]::None
                ))
            }

            $destinationRoot = Join-Path $runRoot 'esp-copy-bcd-export'
            $manifest = Copy-GpuEspTreeWithManifest `
                -SourceRoot $sourceRoot `
                -DestinationRoot $destinationRoot `
                -ActionDirectory $actionDirectory `
                -SystemBcdExport $systemBcdPin

            Assert-PlatformEqual 4 $manifest.OmittedFiles.Count 'Logical BCD export omitted an unexpected number of live BCD files.'
            Assert-PlatformEqual 4 $manifest.FileCount 'Logical BCD export manifest file count differs.'
            $expectedOmissions = @{
                'Microsoft\Boot\BCD' = 'SystemBcdLogicalExport'
                'Microsoft\Boot\BCD.LOG' = 'VolatileSystemBcdLog'
                'Microsoft\Boot\BCD.LOG1' = 'VolatileSystemBcdLog'
                'Microsoft\Boot\BCD.LOG2' = 'VolatileSystemBcdLog'
            }
            foreach ($omission in $manifest.OmittedFiles) {
                $normalizedPath = ([string]$omission.RelativePath).Replace('/', '\')
                Assert-PlatformTrue $expectedOmissions.ContainsKey($normalizedPath) "Unexpected omitted ESP file: $normalizedPath"
                Assert-PlatformEqual $expectedOmissions[$normalizedPath] $omission.Reason "Wrong omission reason for $normalizedPath."
            }

            foreach ($relativePath in @(
                'Microsoft\Boot\bootmgfw.efi',
                'Microsoft\Boot\BCD.LOG3',
                'Microsoft\Boot\BCD.LOG.old',
                'Other\diagnostic.bin'
            )) {
                $sourcePath = Join-Path $sourceRoot $relativePath
                $destinationPath = Join-Path $destinationRoot $relativePath
                Assert-PlatformTrue ([System.IO.File]::Exists($destinationPath)) "Unrelated ESP file was not copied: $relativePath"
                Assert-PlatformEqual (Get-GpuFilePin -Path $sourcePath).Sha256 `
                    (Get-GpuFilePin -Path $destinationPath).Sha256 "Copied ESP file hash differs: $relativePath"
            }
            Assert-PlatformTrue (-not [System.IO.File]::Exists((Join-Path $destinationRoot 'Microsoft\Boot\BCD'))) 'Live BCD unexpectedly appeared in the raw ESP copy.'
        }
        finally {
            foreach ($lockStream in $lockStreams) { $lockStream.Dispose() }
        }

        $lockedUnrelatedRoot = Join-Path $runRoot 'esp-source-locked-unrelated'
        $lockedUnrelatedBoot = Join-Path $lockedUnrelatedRoot 'Microsoft\Boot'
        $lockedUnrelatedOther = Join-Path $lockedUnrelatedRoot 'Other'
        [void](New-Item -ItemType Directory -Path $lockedUnrelatedBoot -Force)
        [void](New-Item -ItemType Directory -Path $lockedUnrelatedOther -Force)
        [System.IO.File]::WriteAllBytes((Join-Path $lockedUnrelatedBoot 'BCD'), [byte[]]@(1, 2, 3))
        $lockedUnrelatedPath = Join-Path $lockedUnrelatedOther 'unrelated.locked'
        [System.IO.File]::WriteAllBytes($lockedUnrelatedPath, [byte[]]@(4, 5, 6))
        $unrelatedLock = [System.IO.File]::Open(
            $lockedUnrelatedPath,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
        try {
            Assert-PlatformThrows {
                Copy-GpuEspTreeWithManifest `
                    -SourceRoot $lockedUnrelatedRoot `
                    -DestinationRoot (Join-Path $runRoot 'esp-copy-locked-unrelated') `
                    -ActionDirectory $actionDirectory `
                    -SystemBcdExport $systemBcdPin
            } 'Copy helper ignored a locked unrelated ESP file.' 'unrelated\.locked.*being used by another process|being used by another process.*unrelated\.locked|sharing violation'
        }
        finally {
            $unrelatedLock.Dispose()
        }

        $noExportLock = [System.IO.File]::Open(
            (Join-Path $bootRoot 'BCD'),
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
        try {
            Assert-PlatformThrows {
                Copy-GpuEspTreeWithManifest `
                    -SourceRoot $sourceRoot `
                    -DestinationRoot (Join-Path $runRoot 'esp-copy-locked-bcd-no-export') `
                    -ActionDirectory $actionDirectory
            } 'Raw ESP copy silently omitted a locked BCD without a system export.' 'BCD.*being used by another process|being used by another process.*BCD|sharing violation'
        }
        finally {
            $noExportLock.Dispose()
        }
    }

    Invoke-PlatformTestCase 'Logical BCD export rejects missing source BCD and invalid export pins before destination creation' {
        $actionDirectory = Join-Path $runRoot 'bcd-pin-action'
        $sourceRoot = Join-Path $runRoot 'esp-source-bcd-pin-validation'
        [void](New-Item -ItemType Directory -Path $actionDirectory -Force)
        [void](New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'Microsoft\Boot') -Force)
        [System.IO.File]::WriteAllBytes((Join-Path $sourceRoot 'Microsoft\Boot\BCD'), [byte[]]@(1, 2, 3))
        [System.IO.File]::WriteAllBytes((Join-Path $sourceRoot 'Microsoft\Boot\bootmgfw.efi'), [byte[]]@(4, 5, 6))

        $validExportPath = Join-Path $actionDirectory 'system-bcd-export.bcd'
        [System.IO.File]::WriteAllBytes($validExportPath, [byte[]]@(7, 8, 9, 10))
        $validExportPin = Get-GpuFilePin -Path $validExportPath
        $invalidHashPin = [pscustomobject]@{
            Path = $validExportPin.Path
            Sha256 = ('0' * 64)
            Bytes = $validExportPin.Bytes
        }
        $missingExportPath = Join-Path $actionDirectory 'missing-system-bcd-export.bcd'
        $missingExportPin = [pscustomobject]@{
            Path = $missingExportPath
            Sha256 = $validExportPin.Sha256
            Bytes = $validExportPin.Bytes
        }
        $zeroExportPath = Join-Path $actionDirectory 'zero-system-bcd-export.bcd'
        [System.IO.File]::WriteAllBytes($zeroExportPath, [byte[]]@())
        $zeroExportPin = Get-GpuFilePin -Path $zeroExportPath

        foreach ($case in @(
            [pscustomobject]@{ Name = 'wrong hash'; Pin = $invalidHashPin }
            [pscustomobject]@{ Name = 'missing path'; Pin = $missingExportPin }
            [pscustomobject]@{ Name = 'zero-byte export'; Pin = $zeroExportPin }
        )) {
            $destinationRoot = Join-Path $runRoot ('esp-copy-invalid-pin-' + ($case.Name -replace '[^a-z]', '-'))
            Assert-PlatformThrows {
                Copy-GpuEspTreeWithManifest `
                    -SourceRoot $sourceRoot `
                    -DestinationRoot $destinationRoot `
                    -ActionDirectory $actionDirectory `
                    -SystemBcdExport $case.Pin
            } "Copy helper accepted a $($case.Name) system BCD export pin." ''
            Assert-PlatformTrue (-not [System.IO.Directory]::Exists($destinationRoot)) "Rejected $($case.Name) export pin created the destination tree."
        }

        $sourceWithoutBcd = Join-Path $runRoot 'esp-source-missing-bcd'
        [void](New-Item -ItemType Directory -Path (Join-Path $sourceWithoutBcd 'Microsoft\Boot') -Force)
        [System.IO.File]::WriteAllBytes((Join-Path $sourceWithoutBcd 'Microsoft\Boot\bootmgfw.efi'), [byte[]]@(11, 12, 13))
        $missingBcdDestination = Join-Path $runRoot 'esp-copy-missing-bcd'
        Assert-PlatformThrows {
            Copy-GpuEspTreeWithManifest `
                -SourceRoot $sourceWithoutBcd `
                -DestinationRoot $missingBcdDestination `
                -ActionDirectory $actionDirectory `
                -SystemBcdExport $validExportPin
        } 'Logical BCD backup accepted a source ESP without Microsoft\Boot\BCD.' 'BCD'
        Assert-PlatformTrue (-not [System.IO.Directory]::Exists($missingBcdDestination)) 'Missing source BCD created a partial destination tree.'
    }

    Invoke-PlatformTestCase 'System BCD backup uses exact bcdedit export arguments and verifies a non-overwriting recovery copy' {
        $actionDirectory = Join-Path $runRoot 'system-bcd-native-action'
        $recoveryDirectory = Join-Path $runRoot 'system-bcd-native-recovery'
        [void](New-Item -ItemType Directory -Path $actionDirectory -Force)
        [void](New-Item -ItemType Directory -Path $recoveryDirectory -Force)
        $expectedBytes = [byte[]]@(101, 102, 103, 104, 105, 106, 107)
        Set-GpuBcdExportFixtureScenario ([pscustomobject]@{
            Succeeded = $true
            ExitCode = 0
            OutputMode = 'Bytes'
            Bytes = $expectedBytes
        })

        $result = Export-GpuSystemBcdBackup -ActionDirectory $actionDirectory -RecoveryRunDirectory $recoveryDirectory
        $localPath = Join-Path $actionDirectory 'system-bcd-export.bcd'
        $externalPath = Join-Path $recoveryDirectory 'system-bcd-export.bcd'
        $calls = @(Get-GpuBcdExportFixtureNativeCalls)
        Assert-PlatformEqual 1 $calls.Count 'System BCD export invoked the native tool an unexpected number of times.'
        Assert-PlatformEqual 'fixture-bcdedit.exe' $calls[0].Program 'Unexpected native executable was used for BCD export.'
        Assert-PlatformEqual 'export-system-bcd' $calls[0].Label 'Native BCD export label differs.'
        Assert-PlatformEqual $actionDirectory $calls[0].ActionDirectory 'Native BCD export action directory differs.'
        Assert-PlatformEqual 2 $calls[0].Arguments.Count 'Native BCD export argument count differs.'
        Assert-PlatformEqual '/export' $calls[0].Arguments[0] 'Native BCD export did not use the logical /export operation.'
        Assert-PlatformEqual $localPath $calls[0].Arguments[1] 'Native BCD export target path differs.'
        Assert-PlatformEqual (Get-GpuFilePin -Path $localPath).Sha256 $result.LocalExport.Sha256 'Returned local BCD export hash differs.'
        Assert-PlatformEqual (Get-GpuFilePin -Path $externalPath).Sha256 $result.ExternalExport.Sha256 'Returned external BCD export hash differs.'
        Assert-PlatformEqual $result.LocalExport.Sha256 $result.ExternalExport.Sha256 'Local and external BCD export hashes differ.'
        Assert-PlatformEqual $result.LocalExport.Bytes $result.ExternalExport.Bytes 'Local and external BCD export lengths differ.'
        Assert-PlatformEqual 7 $result.ExternalExport.Bytes 'External BCD export length differs from the synthetic output.'
        Assert-PlatformEqual ([System.IO.Path]::GetFullPath((Join-Path $actionDirectory 'fixture-export-system-bcd.receipt.json'))) `
            ([System.IO.Path]::GetFullPath($result.ReceiptPath)) 'Native BCD export receipt path was not returned.'

        $secondActionDirectory = Join-Path $runRoot 'system-bcd-native-action-overwrite'
        [void](New-Item -ItemType Directory -Path $secondActionDirectory -Force)
        Set-GpuBcdExportFixtureScenario ([pscustomobject]@{
            Succeeded = $true
            ExitCode = 0
            OutputMode = 'Bytes'
            Bytes = [byte[]]@(201, 202, 203)
        })
        Assert-PlatformThrows {
            Export-GpuSystemBcdBackup -ActionDirectory $secondActionDirectory -RecoveryRunDirectory $recoveryDirectory
        } 'System BCD exporter overwrote an existing recovery export.' 'Refusing to overwrite'
        Assert-PlatformEqual 0 @(Get-GpuBcdExportFixtureNativeCalls).Count 'Exporter invoked bcdedit before rejecting an existing external destination.'
        Assert-PlatformEqual $result.ExternalExport.Sha256 (Get-GpuFilePin -Path $externalPath).Sha256 'Existing external BCD export changed after overwrite rejection.'
    }

    Invoke-PlatformTestCase 'System BCD backup rejects native failure, missing output, and empty output' {
        foreach ($case in @(
            [pscustomobject]@{ Name = 'native-failure'; Succeeded = $false; ExitCode = 5; OutputMode = 'Missing'; Pattern = 'failed with exit 5' }
            [pscustomobject]@{ Name = 'missing-output'; Succeeded = $true; ExitCode = 0; OutputMode = 'Missing'; Pattern = 'does not exist|not found|missing' }
            [pscustomobject]@{ Name = 'empty-output'; Succeeded = $true; ExitCode = 0; OutputMode = 'Empty'; Pattern = 'empty' }
        )) {
            $actionDirectory = Join-Path $runRoot ('system-bcd-reject-action-' + $case.Name)
            $recoveryDirectory = Join-Path $runRoot ('system-bcd-reject-recovery-' + $case.Name)
            [void](New-Item -ItemType Directory -Path $actionDirectory -Force)
            [void](New-Item -ItemType Directory -Path $recoveryDirectory -Force)
            Set-GpuBcdExportFixtureScenario ([pscustomobject]@{
                Succeeded = [bool]$case.Succeeded
                ExitCode = [int]$case.ExitCode
                OutputMode = [string]$case.OutputMode
                Bytes = [byte[]]@(111, 112, 113)
            })
            Assert-PlatformThrows {
                Export-GpuSystemBcdBackup -ActionDirectory $actionDirectory -RecoveryRunDirectory $recoveryDirectory
            } "System BCD export accepted $($case.Name)." $case.Pattern
            Assert-PlatformTrue (-not [System.IO.File]::Exists((Join-Path $recoveryDirectory 'system-bcd-export.bcd'))) "Rejected $($case.Name) created an external BCD export."
        }
    }

    Invoke-PlatformTestCase 'System BCD backup validator checks pinned external export, omission evidence, and manifest schema' {
        $recoveryDirectory = Join-Path $runRoot 'system-bcd-validation-recovery'
        [void](New-Item -ItemType Directory -Path $recoveryDirectory -Force)
        $externalPath = Join-Path $recoveryDirectory 'system-bcd-export.bcd'
        [System.IO.File]::WriteAllBytes($externalPath, [byte[]]@(121, 122, 123, 124))
        $externalPin = Get-GpuFilePin -Path $externalPath
        $expectedOmissions = @(
            [pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD'; Reason = 'SystemBcdLogicalExport' }
            [pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD.LOG'; Reason = 'VolatileSystemBcdLog' }
            [pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD.LOG1'; Reason = 'VolatileSystemBcdLog' }
            [pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD.LOG2'; Reason = 'VolatileSystemBcdLog' }
        )
        $description = [pscustomobject]@{
            Kind = 'BcdeditSystemStoreExport'
            RelativePath = 'system-bcd-export.bcd'
            Sha256 = $externalPin.Sha256
            Bytes = $externalPin.Bytes
        }
        $backup = [pscustomobject]@{ SystemBcdExport = $externalPin }
        $manifest = [pscustomobject]@{
            SchemaVersion = 2
            SystemBcdExport = $description
            OmittedFiles = $expectedOmissions
            Files = @([pscustomobject]@{ RelativePath = 'Microsoft\Boot\bootmgfw.efi'; Sha256 = ('A' * 64); Bytes = 1 })
        }
        Assert-GpuSystemBcdBackup -Backup $backup -Manifest $manifest -RecoveryRunDirectory $recoveryDirectory

        $badOmissionCases = @(
            [pscustomobject]@{ Name = 'unapproved path'; Entries = @($expectedOmissions[0], [pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD.LOG3'; Reason = 'VolatileSystemBcdLog' }) }
            [pscustomobject]@{ Name = 'wrong reason'; Entries = @($expectedOmissions[0], [pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD.LOG'; Reason = 'SystemBcdLogicalExport' }) }
            [pscustomobject]@{ Name = 'duplicate path'; Entries = @($expectedOmissions[0], $expectedOmissions[0]) }
            [pscustomobject]@{ Name = 'missing system BCD substitution'; Entries = @($expectedOmissions[1], $expectedOmissions[2], $expectedOmissions[3]) }
        )
        foreach ($badOmissions in $badOmissionCases) {
            $candidateManifest = [pscustomobject]@{
                SchemaVersion = 2
                SystemBcdExport = $description
                OmittedFiles = $badOmissions.Entries
                Files = @()
            }
            Assert-PlatformThrows {
                Assert-GpuSystemBcdBackup -Backup $backup -Manifest $candidateManifest -RecoveryRunDirectory $recoveryDirectory
            } "Validator accepted $($badOmissions.Name) omission evidence." 'omission|substitution'
        }

        $manifestWithRawBcd = [pscustomobject]@{
            SchemaVersion = 2
            SystemBcdExport = $description
            OmittedFiles = @($expectedOmissions[0])
            Files = @([pscustomobject]@{ RelativePath = 'Microsoft\Boot\BCD'; Sha256 = ('B' * 64); Bytes = 1 })
        }
        Assert-PlatformThrows {
            Assert-GpuSystemBcdBackup -Backup $backup -Manifest $manifestWithRawBcd -RecoveryRunDirectory $recoveryDirectory
        } 'Validator accepted a raw live BCD entry alongside the logical export.' 'incorrectly claims a raw copy'

        $unknownSchema = [pscustomobject]@{
            SchemaVersion = 3
            SystemBcdExport = $description
            OmittedFiles = $expectedOmissions
            Files = @()
        }
        Assert-PlatformThrows {
            Assert-GpuSystemBcdBackup -Backup $backup -Manifest $unknownSchema -RecoveryRunDirectory $recoveryDirectory
        } 'Validator accepted an unknown manifest schema.' 'Unsupported.*schema'

        $legacyManifest = [pscustomobject]@{ SchemaVersion = 1 }
        Assert-GpuSystemBcdBackup -Backup ([pscustomobject]@{}) -Manifest $legacyManifest -RecoveryRunDirectory $recoveryDirectory

        [System.IO.File]::WriteAllBytes($externalPath, [byte[]]@(125, 126, 127, 128))
        Assert-PlatformThrows {
            Assert-GpuSystemBcdBackup -Backup $backup -Manifest $manifest -RecoveryRunDirectory $recoveryDirectory
        } 'Validator accepted a tampered external system BCD export.' 'Pinned file has drifted'
        [System.IO.File]::Delete($externalPath)
        Assert-PlatformThrows {
            Assert-GpuSystemBcdBackup -Backup $backup -Manifest $manifest -RecoveryRunDirectory $recoveryDirectory
        } 'Validator accepted a missing external system BCD export.' 'does not exist|not found|missing'
    }

    Invoke-PlatformTestCase 'Final verification compares firmware with the ESP mounted and always releases it' {
        $baselinePath = Join-Path $runRoot 'final-verify-firmware-baseline.txt'
        $baselineText = "Windows Boot Manager`r`ndevice partition=S:`r`ndisplayorder {A} {B}`r`n"
        [System.IO.File]::WriteAllText($baselinePath, $baselineText)
        $baselinePin = Get-GpuFilePin -Path $baselinePath
        $probePath = Join-Path $runRoot 'final-verify-probe.exe'
        [System.IO.File]::WriteAllBytes($probePath, [byte[]]@(1, 2, 3, 4))
        $probePin = Get-GpuFilePin -Path $probePath
        $mountedEsp = [pscustomobject]@{
            Volume = '\\?\Volume{11111111-2222-3333-4444-555555555555}\'
            DiskNumber = 1
            PartitionGuid = '{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}'
        }
        $backup = [pscustomobject]@{
            FirmwareEnumeration = $baselinePin
            BootManagerSha256 = ('A' * 64)
            EspVolume = $mountedEsp.Volume
            EspDiskNumber = $mountedEsp.DiskNumber
            EspPartitionGuid = $mountedEsp.PartitionGuid
        }
        $state = [pscustomobject]@{
            Host = [pscustomobject]@{ LogicalCpus = 1 }
            EverArmIntent = $true
            DisarmedBootId = 'boot-before-reset'
            Tools = [pscustomobject]@{ Probe = $probePin }
            Backup = $backup
        }
        $current = [pscustomobject]@{ BootId = 'boot-after-reset'; SecureBoot = $true; LogicalCpus = 1 }
        $unmountedText = "Windows Boot Manager`r`ndevice partition=\Device\HarddiskVolume2`r`ndisplayorder {A} {B}`r`n"
        $scenarios = @(
            [pscustomobject]@{
                Name = 'healthy-mounted-comparison'
                FirmwareTextWhenMounted = $baselineText
                FirmwareTextWhenUnmounted = $unmountedText
                FirmwareNativeSucceeded = $true
                ExpectedSucceeded = $true
                ExpectedTextEqual = $true
                ExpectedStatus = 'FINAL_RESET_VERIFIED_GPU_UNATTESTED'
            }
            [pscustomobject]@{
                Name = 'changed-firmware-order'
                FirmwareTextWhenMounted = "Windows Boot Manager`r`ndevice partition=S:`r`ndisplayorder {B} {A}`r`n"
                FirmwareTextWhenUnmounted = $unmountedText
                FirmwareNativeSucceeded = $true
                ExpectedSucceeded = $false
                ExpectedTextEqual = $false
                ExpectedStatus = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
            }
            [pscustomobject]@{
                Name = 'native-enumeration-failure'
                FirmwareTextWhenMounted = $baselineText
                FirmwareTextWhenUnmounted = $unmountedText
                FirmwareNativeSucceeded = $false
                ExpectedSucceeded = $false
                ExpectedTextEqual = $null
                ExpectedStatus = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
            }
        )

        foreach ($scenarioCase in $scenarios) {
            $actionDirectory = Join-Path $runRoot ('final-verify-action-' + $scenarioCase.Name)
            [void](New-Item -ItemType Directory -Path $actionDirectory -Force)
            $scenario = [pscustomobject]@{
                Current = $current
                ProbePin = $probePin
                PreflightValid = $true
                MountedEsp = $mountedEsp
                FirmwareTextWhenMounted = [string]$scenarioCase.FirmwareTextWhenMounted
                FirmwareTextWhenUnmounted = [string]$scenarioCase.FirmwareTextWhenUnmounted
                FirmwareNativeSucceeded = [bool]$scenarioCase.FirmwareNativeSucceeded
            }
            Set-GpuFinalVerifyFixtureScenario $scenario
            $result = Invoke-GpuFinalVerify -State $state -ActionDirectory $actionDirectory
            $observations = Get-GpuFinalVerifyFixtureObservations

            Assert-PlatformEqual ([bool]$scenarioCase.ExpectedSucceeded) ([bool]$result.Succeeded) "Final verification success differs for $($scenarioCase.Name)."
            Assert-PlatformEqual ([string]$scenarioCase.ExpectedStatus) ([string]$result.Status) "Final verification status differs for $($scenarioCase.Name)."
            Assert-PlatformEqual 1 $observations.MountCount "Final verification mounted the ESP an unexpected number of times for $($scenarioCase.Name)."
            Assert-PlatformEqual 1 $observations.DismountCount "Final verification did not unmount exactly once for $($scenarioCase.Name)."
            Assert-PlatformTrue (-not [bool]$observations.Mounted) "Final verification left the ESP mounted after $($scenarioCase.Name)."
            Assert-PlatformEqual 1 $observations.FirmwareEnumerationMounted.Count "Final verification did not enumerate firmware exactly once for $($scenarioCase.Name)."
            Assert-PlatformTrue ([bool]$observations.FirmwareEnumerationMounted[0]) "Firmware enumeration ran after unmount for $($scenarioCase.Name)."
            if ($null -ne $scenarioCase.ExpectedTextEqual) {
                Assert-PlatformEqual ([bool]$scenarioCase.ExpectedTextEqual) ([bool]$result.FinalVerification.FirmwareEnumeration.TextEqual) `
                    "Firmware text comparison differs for $($scenarioCase.Name)."
            }
            if ($scenarioCase.Name -ceq 'healthy-mounted-comparison') {
                Assert-PlatformEqual 'SystemEspMountedAtS' ([string]$result.FinalVerification.FirmwareEnumeration.CaptureContext) `
                    'Firmware comparison omitted its mounted capture context.'
            }
        }
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
    if ($null -ne $bcdExportFixtureModule) {
        Remove-Module -ModuleInfo $bcdExportFixtureModule -Force -ErrorAction SilentlyContinue
    }
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
