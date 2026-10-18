[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$mailboxPath = Join-Path $PSScriptRoot '..\device_vm\invoke-mailbox.ps1'
$mailboxPath = [IO.Path]::GetFullPath($mailboxPath)
$handoffRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$worktreeRoot = Join-Path $mainRoot '.codex-build\worktrees\smbios-boot-serial-probe'
$runtimeRoot = Join-Path (Join-Path $handoffRoot 'reports') 'f6\runtime'
[void][IO.Directory]::CreateDirectory($runtimeRoot)
$reportPath = Join-Path $runtimeRoot ('mailbox-boundaries.' + [Guid]::NewGuid().ToString('N') + '.json')
$records = [Collections.Generic.List[object]]::new()

function Get-ProductionFunction([Management.Automation.Language.ScriptBlockAst]$Ast, [string]$Name) {
    $definitions = @($Ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name
    }, $true))
    if ($definitions.Count -ne 1) { throw ('Expected one production function ' + $Name) }
    return [scriptblock]::Create($definitions[0].Extent.Text)
}

function Get-WrapperStringAssignment([Management.Automation.Language.ScriptBlockAst]$Ast, [string]$Name) {
    $assignments = @($Ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
            $node.Left.VariablePath.UserPath -ceq $Name
    }, $true))
    if ($assignments.Count -ne 1) { throw ('Expected one wrapper source pin ' + $Name) }
    $constants = @($assignments[0].Right.FindAll({
        param($node)
        $node -is [Management.Automation.Language.StringConstantExpressionAst]
    }, $true))
    if ($constants.Count -ne 1) { throw ('Expected one literal wrapper source pin ' + $Name) }
    return [string]$constants[0].Value
}

function Assert-Pass([string]$Name, [scriptblock]$Body) {
    & $Body
    $records.Add([ordered]@{ name = $Name; pass = $true })
}

function Assert-Reject([string]$Name, [scriptblock]$Body, [string]$ExpectedMessage) {
    $message = $null
    try { & $Body } catch { $message = $_.Exception.Message }
    if ([string]::IsNullOrWhiteSpace($message) -or $message -notmatch $ExpectedMessage) {
        throw ('Mailbox boundary rejection failed: ' + $Name + '; actual=' + $message)
    }
    $records.Add([ordered]@{ name = $Name; pass = $true; error = $message })
}

$tokens = $null
$parseErrors = $null
$mailboxAst = [Management.Automation.Language.Parser]::ParseFile($mailboxPath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) {
    throw ('Mailbox syntax check failed: ' + ($parseErrors.Message -join '; '))
}
if ($mailboxAst.ParamBlock.Extent.Text -notmatch "ValidateSet\('Storage', 'Nic', 'CombinedFixture'\)") {
    throw 'CombinedFixture is missing from the mailbox TestProfile allowlist.'
}
$records.Add([ordered]@{ name = 'mailbox-script-parses-and-allows-combinedfixture-profile'; pass = $true })

$wrapperPath = Join-Path $PSScriptRoot 'invoke-mailbox.ps1'
$wrapperTokens = $null
$wrapperParseErrors = $null
$wrapperAst = [Management.Automation.Language.Parser]::ParseFile($wrapperPath, [ref]$wrapperTokens, [ref]$wrapperParseErrors)
if (@($wrapperParseErrors).Count -gt 0) {
    throw ('Combined mailbox wrapper syntax check failed: ' + ($wrapperParseErrors.Message -join '; '))
}
$mailboxSource = ([IO.File]::ReadAllText($mailboxPath)).Replace("`r`n", "`n")
foreach ($pinName in @('oldNamespace', 'oldRoot', 'oldFolderGuard')) {
    $pinnedText = Get-WrapperStringAssignment $wrapperAst $pinName
    if ($pinName -ceq 'oldRoot') { $pinnedText = $pinnedText.TrimEnd("`r", "`n") }
    if ([regex]::Matches($mailboxSource, [regex]::Escape($pinnedText)).Count -ne 1) {
        throw ('Combined wrapper source pin is missing or ambiguous: ' + $pinName)
    }
}
$records.Add([ordered]@{ name = 'combined-wrapper-source-pins-remain-compatible'; pass = $true })

. (Get-ProductionFunction $mailboxAst 'ConvertFrom-CombinedFixtureVmxUuid')
. (Get-ProductionFunction $mailboxAst 'Get-CombinedFixtureVmxUuid')
. (Get-ProductionFunction $mailboxAst 'Assert-CombinedFixtureTarget')
. (Get-ProductionFunction $mailboxAst 'Get-FullPath')
. (Get-ProductionFunction $mailboxAst 'Test-PathWithin')

$expectedVmx = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx'
$expectedOutput = Join-Path $worktreeRoot 'spoof-handoff\reports\f6\runtime\mailbox'
$validVmxText = @'
displayName = "Combined-9Field-Win11-Disposable"
uuid.bios = "56 4d 11 f0 8a cf 36 8a-27 72 aa e2 fa 24 64 d1"
uuid.location = "56 4d 11 f0 8a cf 36 8a-27 72 aa e2 fa 24 64 d1"
'@

Assert-Pass 'combinedfixture-accepts-exact-authorized-vmx-output-and-uuid' {
    $result = Assert-CombinedFixtureTarget -VmxPath $expectedVmx `
        -OutputDirectory $expectedOutput -VmxText $validVmxText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1'
    if ($result.VmxUuid -cne 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1') {
        throw ('Unexpected normalized VMX UUID: ' + $result.VmxUuid)
    }
}
Assert-Reject 'combinedfixture-rejects-other-main-vm-path' {
    $otherVmx = Join-Path $mainRoot 'spoof-handoff\artifacts\device-vm-20261017\vm-other\other.vmx'
    Assert-CombinedFixtureTarget -VmxPath $otherVmx `
        -OutputDirectory $expectedOutput -VmxText $validVmxText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'exact authorized disposable VMX path'
Assert-Reject 'combinedfixture-rejects-other-clone-under-main-artifacts' {
    $otherVmx = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\OtherClone\OtherClone.vmx'
    Assert-CombinedFixtureTarget -VmxPath $otherVmx `
        -OutputDirectory $expectedOutput -VmxText $validVmxText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'exact authorized disposable VMX path'
Assert-Reject 'combinedfixture-rejects-main-checkout-output' {
    $mainOutput = Join-Path $mainRoot 'spoof-handoff\reports\f6\runtime\mailbox'
    Assert-CombinedFixtureTarget -VmxPath $expectedVmx `
        -OutputDirectory $mainOutput -VmxText $validVmxText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'exactly worktree spoof-handoff'
Assert-Reject 'combinedfixture-rejects-worktree-output-sibling' {
    $siblingOutput = Join-Path $worktreeRoot 'spoof-handoff\reports\f6\runtime\mailbox-other'
    Assert-CombinedFixtureTarget -VmxPath $expectedVmx `
        -OutputDirectory $siblingOutput -VmxText $validVmxText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'exactly worktree spoof-handoff'

$wrongUuidText = $validVmxText.Replace('fa 24 64 d1', 'fa 24 64 d2')
Assert-Reject 'combinedfixture-rejects-wrong-vm-uuid' {
    Assert-CombinedFixtureTarget -VmxPath $expectedVmx `
        -OutputDirectory $expectedOutput -VmxText $wrongUuidText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'UUID differs from the authorized disposable clone'
$mismatchedLocationText = $validVmxText.Replace(
    'uuid.location = "56 4d 11 f0 8a cf 36 8a-27 72 aa e2 fa 24 64 d1"',
    'uuid.location = "56 4d 11 f0 8a cf 36 8a-27 72 aa e2 fa 24 64 d2"')
Assert-Reject 'combinedfixture-rejects-bios-location-uuid-mismatch' {
    Assert-CombinedFixtureTarget -VmxPath $expectedVmx `
        -OutputDirectory $expectedOutput -VmxText $mismatchedLocationText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'BIOS and location UUID values differ'
$duplicateUuidText = $validVmxText + "`n" + 'uuid.bios = "56 4d 11 f0 8a cf 36 8a-27 72 aa e2 fa 24 64 d1"'
Assert-Reject 'combinedfixture-rejects-duplicate-uuid-property' {
    Assert-CombinedFixtureTarget -VmxPath $expectedVmx `
        -OutputDirectory $expectedOutput -VmxText $duplicateUuidText `
        -ExpectedVmxPath $expectedVmx -ExpectedOutputDirectory $expectedOutput `
        -ExpectedUuid 'F0114D56-CF8A-8A36-2772-AAE2FA2464D1' | Out-Null
} 'exactly one uuid.bios and uuid.location'
Assert-Reject 'combinedfixture-rejects-malformed-uuid-bytes' {
    ConvertFrom-CombinedFixtureVmxUuid '56 4d 11 f0-8a cf 36 8a-27 72 aa e2 fa 24 64 d1' | Out-Null
} '16-byte VMware form'

$mainInput = Join-Path $mainRoot 'spoof-handoff\tools\combined_vm\guest-capture.template.ps1'
$worktreeInput = Join-Path $worktreeRoot 'spoof-handoff\tools\combined_vm\guest-active.template.ps1'
$externalInput = 'C:\Users\jayli\Downloads\external-guest.ps1'
Assert-Pass 'mailbox-allows-input-from-main-repository' {
    if (-not (Test-PathWithin $mainRoot $mainInput)) { throw 'Main repository input was not accepted.' }
}
Assert-Pass 'mailbox-allows-input-from-authorized-worktree' {
    if (-not (Test-PathWithin $mainRoot $worktreeInput)) { throw 'Authorized worktree input was not accepted.' }
}
Assert-Pass 'mailbox-rejects-input-outside-repository' {
    if (Test-PathWithin $mainRoot $externalInput) { throw 'External input unexpectedly passed the repository boundary.' }
}

$report = [ordered]@{
    schema = 'combined-vm-mailbox-boundary-tests.v1'
    status = 'PASS'
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    tests = @($records.ToArray())
    productionSource = [ordered]@{
        path = $mailboxPath
        sha256 = (Get-FileHash -LiteralPath $mailboxPath -Algorithm SHA256).Hash
    }
    wrapperSource = [ordered]@{
        path = $wrapperPath
        sha256 = (Get-FileHash -LiteralPath $wrapperPath -Algorithm SHA256).Hash
    }
    allowedInputRoots = @($mainRoot, $worktreeRoot)
    vmrunInvoked = $false
    credentialsLoaded = $false
}
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
[pscustomobject]@{
    status = 'PASS'
    tests = $records.Count
    reportPath = $reportPath
    reportSha256 = (Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash
} | ConvertTo-Json
