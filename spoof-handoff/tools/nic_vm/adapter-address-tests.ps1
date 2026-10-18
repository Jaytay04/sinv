[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -ne 5) {
    throw ('This pure NIC address regression requires Windows PowerShell 5.x; actual=' + $PSVersionTable.PSVersion)
}

$functionPath = Join-Path $PSScriptRoot 'adapter-address.ps1'
$tokens = $null
$parseErrors = $null
$sourceAst = [Management.Automation.Language.Parser]::ParseFile($functionPath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) {
    throw ('Adapter address source parse failed: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; '))
}
. $functionPath

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$reportRoot = Join-Path $repositoryRoot 'spoof-handoff\reports\nic-reader-gap-20261017'
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
$reportPath = Join-Path $reportRoot ('adapter-address-unit.' + $runId + '.json')
$script:Assertions = [Collections.Generic.List[object]]::new()

function Add-Assertion {
    param([string]$Name, [bool]$Passed, [string]$Details)
    $script:Assertions.Add([ordered]@{ name = $Name; status = if ($Passed) { 'PASS' } else { 'FAIL' }; details = $Details })
    if (-not $Passed) { throw ('Assertion failed: ' + $Name + '; ' + $Details) }
}

function Assert-Equal {
    param([object]$Expected, [object]$Actual, [string]$Name)
    $expectedJson = ConvertTo-Json -InputObject $Expected -Compress -Depth 12
    $actualJson = ConvertTo-Json -InputObject $Actual -Compress -Depth 12
    Add-Assertion $Name ([string]::Equals($expectedJson, $actualJson, [StringComparison]::Ordinal)) ('expected=' + $expectedJson + '; actual=' + $actualJson)
}

function Assert-True {
    param([bool]$Condition, [string]$Name, [string]$Details)
    Add-Assertion $Name $Condition $Details
}

function Get-FunctionAst {
    param([object]$Ast, [string]$Name)
    $functions = @($Ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name }, $true))
    if ($functions.Count -ne 1) { throw ('Expected exactly one function AST for ' + $Name + '; actual=' + $functions.Count) }
    return $functions[0]
}

function Get-RequiredProperty {
    param([object]$Value, [string]$Name)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property) { throw ('Required test input property missing: ' + $Name) }
    return $property.Value
}

$result = [ordered]@{
    schema = 'nic-adapter-address-pure-tests.v1'
    runId = $runId
    status = 'FAIL'
    syntheticOnly = $true
    liveAdapterMutationExecuted = $false
    elevationRequired = $false
    assertions = @()
}

try {
    $registryLookupAst = Get-FunctionAst $sourceAst 'Get-NicAddressRegistryClassEntry'
    $automaticMatchesReferences = @($registryLookupAst.FindAll({
        param($node)
        $node -is [Management.Automation.Language.VariableExpressionAst] -and
        [string]::Equals($node.VariablePath.UserPath, 'Matches', [StringComparison]::OrdinalIgnoreCase)
    }, $true))
    Assert-Equal 0 $automaticMatchesReferences.Count 'registry class lookup avoids automatic PowerShell $Matches variable'
    Assert-True ($registryLookupAst.Extent.Text.Contains('-notmatch')) 'registry class lookup retains child-key format validation' 'the test guards the exact function that scans registry child names with a regex'

    $originalsPath = Join-Path $repositoryRoot 'spoof-handoff\reports\client-originals.tsv'
    if (-not (Test-Path -LiteralPath $originalsPath -PathType Leaf)) { throw ('Authoritative locked-value map is absent: ' + $originalsPath) }
    $macRows = @(Import-Csv -LiteralPath $originalsPath -Delimiter "`t" | Where-Object { $_.member -ceq 'FakeMacAddress' })
    if ($macRows.Count -ne 4) { throw ('Expected four authoritative NIC value pairs; actual=' + $macRows.Count) }

    $profile = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $macRows.Count; $index++) {
        $row = $macRows[$index]
        $profile.Add([ordered]@{
            pnpDeviceId = ('ROOT\VMWARE\NIC-REGRESSION-{0:D2}' -f $index)
            interfaceGuid = ('00000000-0000-4000-8000-{0:D12}' -f ($index + 1))
            original = [string]$row.original
            replacement = [string]$row.replacement
        })
    }
    $validated = @(Assert-NicAddressProfileBindings $profile.ToArray())
    Assert-Equal 4 $validated.Count 'profile accepts exactly the four locked NIC mappings'
    for ($index = 0; $index -lt 4; $index++) {
        Assert-Equal ([string]$macRows[$index].original) ([string]$validated[$index].original) ('locked original preserved ' + $index)
        Assert-Equal ([string]$macRows[$index].replacement) ([string]$validated[$index].replacement) ('locked replacement preserved ' + $index)
        Assert-True (Test-NicAddressUnicastMac $validated[$index].replacement) ('valid unicast replacement ' + $index) ([string]$validated[$index].replacement)
    }
    Assert-True (-not (Test-NicAddressUnicastMac '01:90:10:20:30:40')) 'multicast MAC rejected' 'the group bit is set'
    Assert-True (-not (Test-NicAddressUnicastMac '00:00:00:00:00:00')) 'all-zero MAC rejected' 'the address has no nonzero byte'

    $registryCases = @(
        [ordered]@{ name = 'absent'; present = $false; kind = $null; value = $null; expectedEncoding = 'none' },
        [ordered]@{ name = 'string'; present = $true; kind = 'String'; value = '02:90:10:20:30:40'; expectedEncoding = 'string' },
        [ordered]@{ name = 'expand string'; present = $true; kind = 'ExpandString'; value = '%SystemRoot%\device'; expectedEncoding = 'expand-string' },
        [ordered]@{ name = 'multi string'; present = $true; kind = 'MultiString'; value = [string[]]@('first', 'second'); expectedEncoding = 'multi-string' },
        [ordered]@{ name = 'DWord'; present = $true; kind = 'DWord'; value = [int]305419896; expectedEncoding = 'dword-le-hex' },
        [ordered]@{ name = 'QWord'; present = $true; kind = 'QWord'; value = [long]1234567890123456789; expectedEncoding = 'qword-le-hex' },
        [ordered]@{ name = 'binary'; present = $true; kind = 'Binary'; value = [byte[]]@(0, 1, 127, 255); expectedEncoding = 'base64' },
        [ordered]@{ name = 'none'; present = $true; kind = 'None'; value = [byte[]]@(10, 20, 30); expectedEncoding = 'base64' }
    )
    foreach ($case in $registryCases) {
        $snapshot = New-NicAddressRegistryValueSnapshot ([bool]$case.present) $case.kind $case.value
        Assert-Equal ([string]$case.expectedEncoding) ([string]$snapshot.encoding) ('registry encoding ' + $case.name)
        $json = ConvertTo-Json -InputObject $snapshot -Compress -Depth 12
        $roundTrip = ConvertFrom-Json -InputObject $json
        Assert-True (Test-NicAddressRegistryValueSnapshotEqual $snapshot $roundTrip) ('registry snapshot JSON round trip ' + $case.name) ('kind=' + [string]$case.kind + '; json=' + $json)
        $decoded = ConvertFrom-NicAddressRegistryValueSnapshot $roundTrip
        if ($case.present) {
            if ($case.kind -ceq 'Binary' -or $case.kind -ceq 'None') {
                Assert-Equal ([Convert]::ToBase64String([byte[]]$case.value)) ([Convert]::ToBase64String([byte[]]$decoded)) ('registry raw bytes restored ' + $case.name)
            }
            elseif ($case.kind -ceq 'MultiString') {
                Assert-Equal ([string[]]$case.value) ([string[]]$decoded) ('registry multi-string restored ' + $case.name)
            }
            else { Assert-Equal $case.value $decoded ('registry typed value restored ' + $case.name) }
        }
        else { Assert-True ($null -eq $decoded) 'absent registry value decodes as absent' 'the value is not synthesized' }
    }

    $baselineRegistry = New-NicAddressRegistryValueSnapshot $true 'String' ([string]$validated[0].original)
    $syntheticSnapshot = [ordered]@{
        pnpDeviceId = [string]$validated[0].pnpDeviceId
        interfaceGuid = [string]$validated[0].interfaceGuid
        original = [string]$validated[0].original
        replacement = [string]$validated[0].replacement
        networkAddress = $baselineRegistry
        adminState = [ordered]@{ value = 'Up'; enabled = $true; source = 'synthetic fixture' }
    }
    $applyPlan = New-NicAddressApplyPlan $syntheticSnapshot
    Assert-Equal ([string]$syntheticSnapshot.original) ([string]$applyPlan.expectedOriginal) 'apply plan expects captured original'
    Assert-Equal ([string]$syntheticSnapshot.replacement) ([string]$applyPlan.replacement) 'apply plan uses only locked replacement'
    $restorePlan = New-NicAddressRestorePlan $syntheticSnapshot
    Assert-Equal 'set' ([string]$restorePlan.action) 'present registry baseline plans exact set'
    Assert-Equal 'String' ([string]$restorePlan.kind) 'restore plan preserves registry type'
    Assert-Equal ([string]$syntheticSnapshot.original) ([string]$restorePlan.rawValue) 'restore plan preserves original registry value'

    $syntheticPolicy = [ordered]@{
        visibleMac = [string]$syntheticSnapshot.original
        registry = $baselineRegistry
        adminEnabled = [bool]$syntheticSnapshot.adminState.enabled
    }
    if ((ConvertTo-NicAddressMacKey $syntheticPolicy.visibleMac) -cne (ConvertTo-NicAddressMacKey $applyPlan.expectedOriginal)) {
        throw 'Synthetic apply precondition did not match the original address.'
    }
    $syntheticPolicy.visibleMac = [string]$applyPlan.replacement
    $syntheticPolicy.registry = New-NicAddressRegistryValueSnapshot $true 'String' $syntheticPolicy.visibleMac
    Assert-Equal ([string]$syntheticSnapshot.replacement) ([string]$syntheticPolicy.visibleMac) 'synthetic policy reads replacement after apply plan'

    $restoredRegistryValue = ConvertFrom-NicAddressRegistryValueSnapshot $syntheticSnapshot.networkAddress
    $syntheticPolicy.registry = New-NicAddressRegistryValueSnapshot $true 'String' $restoredRegistryValue
    $syntheticPolicy.visibleMac = [string]$syntheticSnapshot.original
    $syntheticPolicy.adminEnabled = [bool]$syntheticSnapshot.adminState.enabled
    Assert-Equal ([string]$syntheticSnapshot.original) ([string]$syntheticPolicy.visibleMac) 'synthetic policy reads original after restore plan'
    Assert-True (Test-NicAddressRegistryValueSnapshotEqual $syntheticSnapshot.networkAddress $syntheticPolicy.registry) 'synthetic policy restores exact registry value' 'present/type/value match the pre-test descriptor'
    Assert-Equal ([bool]$syntheticSnapshot.adminState.enabled) ([bool]$syntheticPolicy.adminEnabled) 'synthetic policy restores administrative state'

    $absentSnapshot = [ordered]@{ networkAddress = New-NicAddressRegistryValueSnapshot $false $null $null }
    Assert-Equal 'delete' ([string](New-NicAddressRestorePlan $absentSnapshot).action) 'absent registry baseline plans exact delete'
    $intent = New-NicAddressIntent 'apply' $syntheticSnapshot ([string]$syntheticSnapshot.replacement)
    Assert-Equal ([string]$syntheticSnapshot.pnpDeviceId) ([string]$intent.pnpDeviceId) 'journal intent binds exact PnP identity'
    Assert-Equal ([string]$syntheticSnapshot.replacement) ([string]$intent.targetMac) 'journal intent binds exact replacement'

    $result.status = 'PASS'
    $result.assertions = $script:Assertions.ToArray()
    $result.assertionCount = $script:Assertions.Count
    $result.lockedValuesSource = 'spoof-handoff\reports\client-originals.tsv'
    $result.syntheticRoundTrip = [ordered]@{
        original = [string]$syntheticSnapshot.original
        replacement = [string]$syntheticSnapshot.replacement
        restored = [string]$syntheticPolicy.visibleMac
        registryRestored = [bool](Test-NicAddressRegistryValueSnapshotEqual $syntheticSnapshot.networkAddress $syntheticPolicy.registry)
        adminStateRestored = [bool]$syntheticPolicy.adminEnabled
    }
}
catch {
    $result.status = 'FAIL'
    $result.failure = $_.Exception.ToString()
    $result.assertions = $script:Assertions.ToArray()
}

if (-not [IO.Directory]::Exists($reportRoot)) { [void][IO.Directory]::CreateDirectory($reportRoot) }
[IO.File]::WriteAllText($reportPath, (ConvertTo-Json -InputObject $result -Depth 16), [Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 16
if ($result.status -cne 'PASS') { throw ('NIC adapter address pure regression failed. See ' + $reportPath) }
Write-Output ('EVIDENCE ' + $reportPath)
