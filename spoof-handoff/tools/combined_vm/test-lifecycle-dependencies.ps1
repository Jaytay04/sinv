[CmdletBinding()]
param(
    [string]$RepoRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-TemplateNode {
    param([System.Management.Automation.Language.Ast]$Ast, [scriptblock]$Predicate, [string]$Label)
    $found = @($Ast.FindAll($Predicate, $true))
    if ($found.Count -ne 1) { throw "Expected exactly one $Label in the lifecycle template; found $($found.Count)." }
    return $found[0]
}

$templatePath = Join-Path $PSScriptRoot 'guest-lifecycle.template.ps1'
$renderPath = Join-Path $PSScriptRoot 'render-lifecycle.ps1'
if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf) -or -not (Test-Path -LiteralPath $renderPath -PathType Leaf)) {
    throw 'Lifecycle source files are missing.'
}
$templateTokens = $null
$templateErrors = $null
$templateAst = [System.Management.Automation.Language.Parser]::ParseFile($templatePath, [ref]$templateTokens, [ref]$templateErrors)
if (@($templateErrors).Count -gt 0) { throw ('Lifecycle template has parse errors: ' + (($templateErrors | ForEach-Object { $_.Message }) -join '; ')) }
$renderTokens = $null
$renderErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile($renderPath, [ref]$renderTokens, [ref]$renderErrors)
if (@($renderErrors).Count -gt 0) { throw ('Lifecycle renderer has parse errors: ' + (($renderErrors | ForEach-Object { $_.Message }) -join '; ')) }
$writeRunFileFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Write-RunFile' } 'run-file writer function'
$getShaFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-Sha256' } 'Get-Sha256 function'
$runnerEnvelopeFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'New-RunnerResultEnvelope' } 'runner result envelope function'
$metadataFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'New-LifecycleMetadata' } 'lifecycle metadata function'
$gateArgumentsFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Assert-GuestGateArguments' } 'guest gate argument guard function'
$testSigningMatchFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Test-TestSigningStateMatches' } 'test-signing baseline comparison function'
$testSigningOffFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Test-TestSigningStateOff' } 'test-signing OFF-state function'
$testSigningRestoreFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-TestSigningRestoreArguments' } 'test-signing restore argument function'
$firmwareReaderFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Initialize-FirmwareReader' } 'firmware reader initialization function'
$firmwareReadStatusFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Format-FirmwareReadStatus' } 'firmware read status formatter function'
$secureBootOffFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Test-SecureBootOffSnapshot' } 'Secure Boot OFF snapshot validator function'
$secureBootStatusFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Format-SecureBootStatus' } 'Secure Boot status formatter function'
$firmwareSourceNode = Get-TemplateNode $firmwareReaderFunction { param($node) $node -is [System.Management.Automation.Language.StringConstantExpressionAst] -and $node.Value -match 'public static class CombinedVmFirmware' } 'firmware reader C# source block'
$pathBLibraryFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Initialize-PathBLibrary' } 'Initialize-PathBLibrary function'
$scopeFunction = Get-TemplateNode $templateAst { param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Initialize-LifecycleDependencyScope' } 'dependency-scope function'
$bootstrapAssignment = Get-TemplateNode $templateAst {
    param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
    $node.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
    $node.Left.VariablePath.UserPath -ceq 'script:DependencyBootstrap'
} 'dependency bootstrap assignment'
$bootstrapExpression = $bootstrapAssignment.Right.Find({ param($node) $node -is [System.Management.Automation.Language.ScriptBlockExpressionAst] }, $true)
if ($null -eq $bootstrapExpression) {
    throw 'Lifecycle dependency bootstrap is not a script block.'
}

$repo = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')
$artifactRoot = Join-Path $repo 'spoof-handoff\artifacts\combined-vm\dependency-test-work'
$reportRoot = Join-Path $repo 'spoof-handoff\reports\combined-9field-vm-20261018\lifecycle-tests'
$resolvedWorkRoot = [IO.Path]::GetFullPath($artifactRoot)
if (-not $resolvedWorkRoot.StartsWith((Join-Path $repo 'spoof-handoff\artifacts\') , [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Dependency test work directory escaped spoof-handoff artifacts.'
}
$workRoot = Join-Path $resolvedWorkRoot ([Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($workRoot)

$results = [Collections.Generic.List[object]]::new()
try {
    $paramBlockText = $templateAst.ParamBlock.Extent.Text
    if ([string]::IsNullOrWhiteSpace($paramBlockText)) { throw 'Lifecycle template has no entrypoint parameter block.' }
    $paramProbe = [scriptblock]::Create($paramBlockText + "`n[pscustomobject]@{ RunRoot = `$RunRoot; Action = `$Action; ExpectedProbeVerdict = `$ExpectedProbeVerdict }")
    $gateArgs = & $paramProbe -RunRoot 'C:\ProgramData\UEFILoaderRunner\runs\binder-smoke' -Action Probe -ExpectedProbeVerdict NOT_MET
    Assert-Test ($gateArgs.RunRoot -ceq 'C:\ProgramData\UEFILoaderRunner\runs\binder-smoke' -and $gateArgs.Action -ceq 'Probe' -and $gateArgs.ExpectedProbeVerdict -ceq 'NOT_MET') 'Lifecycle entrypoint rejected or misbound the fixed Windows guest gate arguments.'
    . ([scriptblock]::Create($gateArgumentsFunction.Extent.Text))
    Assert-GuestGateArguments -Action $gateArgs.Action -ExpectedProbeVerdict $gateArgs.ExpectedProbeVerdict
    $unexpectedActionRejected = $false
    try { Assert-GuestGateArguments -Action CaptureOriginal -ExpectedProbeVerdict NOT_MET }
    catch { $unexpectedActionRejected = $_.Exception.Message -match 'requires the fixed guest-gate arguments' }
    Assert-Test $unexpectedActionRejected 'Lifecycle guest gate guard accepted an unexpected Action argument.'
    $unexpectedVerdictRejected = $false
    try { Assert-GuestGateArguments -Action Probe -ExpectedProbeVerdict MET }
    catch { $unexpectedVerdictRejected = $_.Exception.Message -match 'requires the fixed guest-gate arguments' }
    Assert-Test $unexpectedVerdictRejected 'Lifecycle guest gate guard accepted an unexpected ExpectedProbeVerdict argument.'
    $results.Add([ordered]@{ check = 'fixed-guest-gate-parameter-contract'; passed = $true; accepted = [ordered]@{ action = $gateArgs.Action; expectedProbeVerdict = $gateArgs.ExpectedProbeVerdict }; rejected = @('unexpected Action','unexpected ExpectedProbeVerdict') })

    . ([scriptblock]::Create(($testSigningMatchFunction.Extent.Text + "`n" + $testSigningOffFunction.Extent.Text + "`n" + $testSigningRestoreFunction.Extent.Text)))
    $testSigningAbsent = [pscustomobject]@{ exists = $false; value = $null }
    $testSigningOff = [pscustomobject]@{ exists = $true; value = 'no' }
    $testSigningOn = [pscustomobject]@{ exists = $true; value = 'yes' }
    Assert-Test (Test-TestSigningStateOff $testSigningAbsent) 'Absent/default testsigning state was not recognized as OFF.'
    Assert-Test (Test-TestSigningStateOff $testSigningOff) 'Explicit testsigning No state was not recognized as OFF.'
    Assert-Test (-not (Test-TestSigningStateOff $testSigningOn)) 'Explicit testsigning Yes state was incorrectly recognized as OFF.'
    Assert-Test (Test-TestSigningStateMatches $testSigningAbsent $testSigningAbsent) 'Absent baseline failed its typed equality check.'
    Assert-Test (-not (Test-TestSigningStateMatches $testSigningAbsent $testSigningOff)) 'Absent baseline incorrectly matched explicit No.'
    $restoreAbsent = @(Get-TestSigningRestoreArguments $testSigningAbsent)
    $restoreOff = @(Get-TestSigningRestoreArguments $testSigningOff)
    $restoreOn = @(Get-TestSigningRestoreArguments $testSigningOn)
    Assert-Test (($restoreAbsent -join ' ') -ceq '/deletevalue {current} testsigning') 'Absent testsigning baseline did not select /deletevalue restoration.'
    Assert-Test (($restoreOff -join ' ') -ceq '/set {current} testsigning off') 'Explicit No baseline did not select explicit OFF restoration.'
    Assert-Test (($restoreOn -join ' ') -ceq '/set {current} testsigning on') 'Explicit Yes baseline did not select explicit ON restoration.'
    $results.Add([ordered]@{ check = 'typed-testsigning-baseline-and-restore'; passed = $true; cases = @([ordered]@{ baseline = 'absent'; restore = $restoreAbsent },[ordered]@{ baseline = 'no'; restore = $restoreOff },[ordered]@{ baseline = 'yes'; restore = $restoreOn }) })
    Add-Type -AssemblyName System.IO.Compression
    Assert-Test ([Enum]::IsDefined([IO.Compression.ZipArchiveMode], [IO.Compression.ZipArchiveMode]::Create)) 'System.IO.Compression.ZipArchiveMode did not load for lifecycle evidence packaging.'
    $results.Add([ordered]@{ check = 'compression-archive-api-loaded'; passed = $true; enumType = 'System.IO.Compression.ZipArchiveMode' })
    . ([scriptblock]::Create($firmwareReadStatusFunction.Extent.Text))
    $firmwareStatus = Format-FirmwareReadStatus ([pscustomobject]@{ win32Error = 1314; win32ErrorText = 'A required privilege is not held by the client.'; privilegeError = 0; privilegeRestoreError = 0; bytesHex = '' })
    Assert-Test ($firmwareStatus -match 'win32Error=1314' -and $firmwareStatus -match 'privilegeError=0' -and $firmwareStatus -match 'bytesHex=') 'Firmware read diagnostics omitted the exact error or returned bytes field.'
    . ([scriptblock]::Create(($secureBootOffFunction.Extent.Text + "`n" + $secureBootStatusFunction.Extent.Text)))
    $secureBootWithoutValue = [pscustomobject]@{ enabled = $false; variable = [pscustomobject]@{ supported = $true; win32Error = 0; win32ErrorText = $null; size = 1; bytesHex = '00'; bytesBase64 = 'AA==' } }
    Assert-Test (Test-SecureBootOffSnapshot $secureBootWithoutValue) 'Secure Boot OFF byte record with no optional value member was rejected.'
    $secureBootWrongBytes = [pscustomobject]@{ enabled = $false; variable = [pscustomobject]@{ supported = $true; win32Error = 0; win32ErrorText = $null; size = 1; bytesHex = '01'; bytesBase64 = 'AQ==' } }
    Assert-Test (-not (Test-SecureBootOffSnapshot $secureBootWrongBytes)) 'Secure Boot ON bytes were incorrectly accepted as OFF.'
    $secureBootFailure = [pscustomobject]@{ enabled = $false; variable = [pscustomobject]@{ supported = $false; win32Error = 1314; win32ErrorText = 'A required privilege is not held by the client.'; size = 0; bytesHex = $null; bytesBase64 = $null } }
    $secureBootFailureText = Format-SecureBootStatus $secureBootFailure
    Assert-Test ($secureBootFailureText -match 'win32Error=1314' -and $secureBootFailureText -match 'size=0') 'Secure Boot diagnostics omitted exact Win32 status or byte length.'
    $results.Add([ordered]@{ check = 'firmware-readback-byte-contract-without-value'; passed = $true; noOptionalValueMember = $true; failuresIncludeWin32Status = $true })
    if ('CombinedVmFirmware' -as [type]) { throw 'Run lifecycle tests in a fresh PowerShell process; CombinedVmFirmware is already compiled.' }
    Add-Type -TypeDefinition $firmwareSourceNode.Value -ErrorAction Stop
    Assert-Test ($null -ne ('CombinedVmFirmware' -as [type]) -and $null -ne ([CombinedVmFirmware].GetMethod('Read'))) 'Scoped firmware privilege reader did not compile into its expected read-only API.'
    $results.Add([ordered]@{ check = 'scoped-firmware-read-helper-compiles'; passed = $true; type = 'CombinedVmFirmware'; nativeReadInvoked = $false; tokenPrivilegeAdjusted = $false })

    $script:StateRoot = Join-Path $workRoot 'state'
    $script:StorageRead = Join-Path $workRoot 'storage-read.ps1'
    $script:StorageModel = Join-Path $workRoot 'storage-model.psm1'
    $script:PathBScript = Join-Path $workRoot 'path-b.ps1'
    $script:Config = [pscustomobject]@{ pathBMountLetter = 'Z'; assets = @() }
    $script:Assets = @{}
    if ('StorageHostReadNative' -as [type]) { throw 'Run dependency-scope tests in a fresh PowerShell process; StorageHostReadNative is already loaded.' }

    $mockFiles = [ordered]@{
        'inventory_loader.efi' = 'mock loader payload'
        'ib-install.exe' = 'mock one-shot tool'
        'efi_inventory.exe' = 'mock inventory host tool'
        'smbios-inventory.efi' = 'mock SMBIOS collector'
    }
    $configAssets = [Collections.Generic.List[object]]::new()
    foreach ($name in $mockFiles.Keys) {
        $path = Join-Path $workRoot $name
        [IO.File]::WriteAllText($path, [string]$mockFiles[$name], [Text.UTF8Encoding]::new($false))
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
        $script:Assets[$name] = $path
        $configAssets.Add([pscustomobject]@{ name = $name; sha256 = $hash })
    }
    $script:Config.assets = @($configAssets.ToArray())

    [IO.File]::WriteAllText($script:StorageRead, "# Deliberately does not define StorageHostReadNative on the first pass.`r`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($script:StorageModel, @'
function Get-StorageHostTestSigningElement { 'mock-storage-model' }
Export-ModuleMember -Function Get-StorageHostTestSigningElement
'@, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($script:PathBScript, @'
[CmdletBinding()]
param([string]$Mode, [string]$LoaderBuildDir, [string]$InventoryBuildDir, [string]$MountLetter, [switch]$LibraryOnly)
if (-not $LibraryOnly) { throw 'The test Path-B library must never run an action.' }
function Mount-Esp { throw 'Mount-Esp is a test stub and must not run.' }
function Dismount-Esp { throw 'Dismount-Esp is a test stub and must not run.' }
function Stage-ToRoot { throw 'Stage-ToRoot is a test stub and must not run.' }
function Invoke-Arm { throw 'Invoke-Arm is a test stub and must not run.' }
function Invoke-Collect { throw 'Invoke-Collect is a test stub and must not run.' }
function Remove-OwnedStage { throw 'Remove-OwnedStage is a test stub and must not run.' }
function Read-Receipt { throw 'Read-Receipt is a test stub and must not run.' }
function Get-MountedStageStatus { throw 'Get-MountedStageStatus is a test stub and must not run.' }
'@, [Text.UTF8Encoding]::new($false))

    $extractPath = Join-Path $workRoot 'dependency-bootstrap.extract.ps1'
    $extractText = @(
        $writeRunFileFunction.Extent.Text
        $runnerEnvelopeFunction.Extent.Text
        $metadataFunction.Extent.Text
        $getShaFunction.Extent.Text
        $pathBLibraryFunction.Extent.Text
        $scopeFunction.Extent.Text
        $bootstrapAssignment.Extent.Text
    ) -join "`r`n`r`n"
    [IO.File]::WriteAllText($extractPath, $extractText + "`r`n", [Text.UTF8Encoding]::new($false))
    $extractTokens = $null
    $extractErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($extractPath, [ref]$extractTokens, [ref]$extractErrors)
    Assert-Test (@($extractErrors).Count -eq 0) 'Extracted lifecycle dependency bootstrap did not parse.'
    . $extractPath

    $script:RunRootFull = $workRoot
    Write-RunFile -Name 'stderr-empty-test.txt' -Text ''
    $emptyStderrPath = Join-Path $workRoot 'stderr-empty-test.txt'
    Assert-Test ((Test-Path -LiteralPath $emptyStderrPath -PathType Leaf) -and (Get-Item -LiteralPath $emptyStderrPath).Length -eq 0) 'Empty standard-error evidence cannot be written to the mailbox.'
    $results.Add([ordered]@{ check = 'empty-stderr-is-writable'; passed = $true })

    $runnerPass = New-RunnerResultEnvelope -RunRoot 'C:\ProgramData\UEFILoaderRunner\runs\combined-run-001' -Status PASS -Message 'completed'
    Assert-Test ($runnerPass.SchemaVersion -eq 1 -and $runnerPass.RunId -ceq 'combined-run-001' -and $runnerPass.Status -ceq 'PASS' -and $runnerPass.Message -ceq 'completed') 'PASS runner result envelope does not satisfy the guest mailbox contract.'
    $runnerFail = New-RunnerResultEnvelope -RunRoot 'C:\ProgramData\UEFILoaderRunner\runs\combined-run-002' -Status FAIL -Message 'expected failure'
    Assert-Test ($runnerFail.SchemaVersion -eq 1 -and $runnerFail.RunId -ceq 'combined-run-002' -and $runnerFail.Status -ceq 'FAIL' -and $runnerFail.Message -ceq 'expected failure') 'FAIL runner result envelope does not satisfy the guest mailbox contract.'
    $fullResult = [ordered]@{ action = 'FinalVerify'; status = 'FINAL_VERIFY_PASS'; details = [ordered]@{ phase = 'restore' } }
    $fullJournal = [ordered]@{ schema = 'combined-vm-lifecycle-journal.v1'; actions = @('CaptureOriginal','FinalVerify') }
    $rawEvidence = [ordered]@{ encoding = 'base64-zip'; sha256 = ('A' * 64); contentBase64 = 'AA==' }
    $metadata = New-LifecycleMetadata -Action FinalVerify -LifecycleId 'scope-test' -OutcomeStatus FINAL_VERIFY_PASS -Result $fullResult -Journal $fullJournal -RawEvidence $rawEvidence
    Assert-Test ($metadata.schema -ceq 'combined-vm-lifecycle-metadata.v1' -and $metadata.result.status -ceq 'FINAL_VERIFY_PASS' -and $metadata.journal.actions.Count -eq 2 -and $metadata.rawEvidence.sha256 -ceq ('A' * 64)) 'Lifecycle metadata omitted the full result, journal, or embedded evidence.'
    $results.Add([ordered]@{ check = 'runner-envelope-and-full-metadata'; passed = $true; schemaVersion = $runnerPass.SchemaVersion; statuses = @($runnerPass.Status,$runnerFail.Status); recordJournalEvidencePresent = $true })

    $missingTypeRejected = $false
    try { . $script:DependencyBootstrap }
    catch { $missingTypeRejected = $_.Exception.Message -match 'StorageHostReadNative type was not compiled' }
    Assert-Test $missingTypeRejected 'Missing StorageHostReadNative type was not rejected by the lifecycle preflight.'
    $results.Add([ordered]@{ check = 'missing-native-type-fails-closed'; passed = $true })

    $firstPassCommands = @('Get-StorageHostTestSigningElement','Mount-Esp','Dismount-Esp','Stage-ToRoot','Invoke-Arm','Invoke-Collect','Remove-OwnedStage','Read-Receipt','Get-MountedStageStatus')
    foreach ($name in $firstPassCommands) { Assert-Test ($null -ne (Get-Command -Name $name -ErrorAction SilentlyContinue)) "Dependency '$name' did not remain visible in the invoking scope." }
    $results.Add([ordered]@{ check = 'module-and-path-b-functions-visible-in-caller'; passed = $true; commands = $firstPassCommands })

    [IO.File]::WriteAllText($script:StorageRead, @'
if (-not ('StorageHostReadNative' -as [type])) {
    Add-Type -TypeDefinition 'public static class StorageHostReadNative { public static int Probe() { return 1; } }'
}
'@, [Text.UTF8Encoding]::new($false))
    . $script:DependencyBootstrap
    Assert-Test ($null -ne ('StorageHostReadNative' -as [type])) 'StorageHostReadNative was not loaded into the lifecycle action scope.'
    foreach ($name in $firstPassCommands) { Assert-Test ($null -ne (Get-Command -Name $name -ErrorAction SilentlyContinue)) "Dependency '$name' disappeared after a successful bootstrap: no hardware/device/EFI command was invoked." }
    $results.Add([ordered]@{ check = 'bootstrap-success-visible-in-caller'; passed = $true; nativeType = 'StorageHostReadNative'; hardwareActionsInvoked = 0 })

    Remove-Item -LiteralPath Function:\Invoke-Arm -Force -ErrorAction Stop
    $missingCommandRejected = $false
    try { Initialize-LifecycleDependencyScope }
    catch { $missingCommandRejected = $_.Exception.Message -match 'Invoke-Arm' }
    Assert-Test $missingCommandRejected 'A missing Path-B action function was not rejected by the lifecycle preflight.'
    $results.Add([ordered]@{ check = 'missing-path-b-command-fails-closed'; passed = $true; missingCommand = 'Invoke-Arm' })

    $report = [ordered]@{
        schema = 'combined-vm-lifecycle-dependency-tests.v1'
        result = 'PASS'
        checks = @($results.ToArray())
        templatePath = $templatePath
        templateSha256 = (Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash.ToUpperInvariant()
        rendererPath = $renderPath
        rendererSha256 = (Get-FileHash -LiteralPath $renderPath -Algorithm SHA256).Hash.ToUpperInvariant()
        testPath = $PSCommandPath
        testSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToUpperInvariant()
        testRunUtc = [DateTime]::UtcNow.ToString('o')
    }
    [void][IO.Directory]::CreateDirectory($reportRoot)
    $reportPath = Join-Path $reportRoot ('dependency-scope.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N') + '.json')
    [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 12) + "`r`n", [Text.UTF8Encoding]::new($false))
    Write-Output 'PASS: lifecycle dependency bootstrap scope and fail-closed checks; no hardware, VM, EFI, BCD, or device operations were invoked.'
    Write-Output "REPORT: $reportPath"
    Write-Output "REPORT SHA256: $((Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToUpperInvariant())"
}
finally {
    $resolvedWorkRoot = [IO.Path]::GetFullPath($workRoot)
    if ($resolvedWorkRoot.StartsWith((Join-Path $repo 'spoof-handoff\artifacts\combined-vm\dependency-test-work') + '\', [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedWorkRoot -PathType Container)) {
        Remove-Item -LiteralPath $resolvedWorkRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
