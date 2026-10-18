[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$rendererPath = Join-Path $PSScriptRoot 'render-lifecycle.ps1'
$tokens = $null
$parseErrors = $null
$rendererAst = [Management.Automation.Language.Parser]::ParseFile($rendererPath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -gt 0) {
    throw ('Lifecycle renderer has parse errors: ' + (($parseErrors | ForEach-Object { $_.Message }) -join '; '))
}

$functionNames = @('Get-FullPath', 'Assert-PathUnder', 'Assert-PathUnderRoots', 'Assert-ExactPath', 'Assert-NoReparsePointComponents')
$functions = [Collections.Generic.List[string]]::new()
foreach ($name in $functionNames) {
    $nodes = @($rendererAst.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name
    }, $true))
    if ($nodes.Count -ne 1) { throw "Expected exactly one renderer function '$name'; found $($nodes.Count)." }
    $functions.Add($nodes[0].Extent.Text)
}
. ([scriptblock]::Create(($functions -join [Environment]::NewLine)))

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$mainReports = Join-Path $mainRoot 'spoof-handoff\reports'
$mainArtifacts = Join-Path $mainRoot 'spoof-handoff\artifacts'
$worktreeReports = Join-Path $worktreeRoot 'spoof-handoff\reports'
$worktreeArtifactsF6 = Join-Path $worktreeRoot 'spoof-handoff\artifacts\f6'
$expectedOutput = Join-Path $worktreeReports 'f6\runtime\lifecycle'
$expectedShared = Join-Path $worktreeArtifactsF6 'shared-readonly'

$checks = [Collections.Generic.List[object]]::new()

$acceptedInputRoots = @($mainReports, $mainArtifacts, (Join-Path $worktreeReports 'f6'), $worktreeArtifactsF6)
$mainReportInput = Join-Path $mainReports 'combined-9field-vm-20261018\vmnet-baseline\inventory.json'
$worktreeReportInput = Join-Path $worktreeReports 'f6\runtime\capture.json'
$worktreeArtifactInput = Join-Path $worktreeArtifactsF6 'build\fixture-manifest.json'
Assert-Test ((Assert-PathUnderRoots $acceptedInputRoots $mainReportInput 'main report input') -ceq (Get-FullPath $mainReportInput)) 'A main-checkout reference input was rejected.'
Assert-Test ((Assert-PathUnderRoots $acceptedInputRoots $worktreeReportInput 'worktree report input') -ceq (Get-FullPath $worktreeReportInput)) 'A worktree f6 report input was rejected.'
Assert-Test ((Assert-PathUnderRoots $acceptedInputRoots $worktreeArtifactInput 'worktree artifact input') -ceq (Get-FullPath $worktreeArtifactInput)) 'A worktree f6 artifact input was rejected.'
$checks.Add([ordered]@{ check = 'authorized-reference-input-roots'; passed = $true; accepted = @('main reports', 'main artifacts', 'worktree f6 reports', 'worktree f6 artifacts') })

$outsideMain = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv-other\spoof-handoff\reports\capture.json'
$outsideWorktree = Join-Path $worktreeRoot 'spoof-handoff\reports\other-run\capture.json'
$traversal = Join-Path $worktreeReports 'f6\..\..\..\outside.json'
foreach ($candidate in @($outsideMain, $outsideWorktree, $traversal)) {
    $rejected = $false
    try { [void](Assert-PathUnderRoots $acceptedInputRoots $candidate 'candidate input') }
    catch { $rejected = $_.Exception.Message -match 'authorized input roots' }
    Assert-Test $rejected "An external or out-of-scope input path was accepted: $candidate"
}
$checks.Add([ordered]@{ check = 'external-and-traversal-inputs-rejected'; passed = $true; rejected = 3 })

Assert-Test ((Assert-ExactPath $expectedOutput $expectedOutput 'OutputDirectory') -ceq (Get-FullPath $expectedOutput)) 'The exact WT lifecycle output root was rejected.'
Assert-Test ((Assert-ExactPath $expectedShared $expectedShared 'SharedAssetsDirectory') -ceq (Get-FullPath $expectedShared)) 'The exact WT shared-readonly root was rejected.'
foreach ($candidate in @(
    (Join-Path $mainRoot 'spoof-handoff\reports\f6\runtime\lifecycle'),
    (Join-Path $worktreeReports 'f6\runtime\lifecycle-escape'),
    (Join-Path $worktreeReports 'f6\runtime'),
    (Join-Path $mainRoot 'spoof-handoff\artifacts\f6\shared-readonly')
)) {
    $rejected = $false
    try {
        if ($candidate -match 'shared-readonly') {
            [void](Assert-ExactPath $candidate $expectedShared 'SharedAssetsDirectory')
        } else {
            [void](Assert-ExactPath $candidate $expectedOutput 'OutputDirectory')
        }
    } catch { $rejected = $_.Exception.Message -match 'must be exactly' }
    Assert-Test $rejected "A non-assigned output or share root was accepted: $candidate"
}
$checks.Add([ordered]@{ check = 'exact-worktree-output-and-share-roots'; passed = $true; accepted = @($expectedOutput, $expectedShared); rejected = 4 })

$noReparseResult = Assert-NoReparsePointComponents $worktreeRoot (Join-Path $worktreeRoot 'spoof-handoff\reports\f6\runtime\not-yet-created') 'test output'
Assert-Test ($noReparseResult -ceq (Get-FullPath (Join-Path $worktreeRoot 'spoof-handoff\reports\f6\runtime\not-yet-created'))) 'Missing output suffix did not remain bounded under the worktree.'
$reparseEscapeRejected = $false
try { [void](Assert-NoReparsePointComponents $worktreeRoot (Join-Path $mainRoot 'spoof-handoff\reports\external') 'test output') }
catch { $reparseEscapeRejected = $_.Exception.Message -match 'must remain under' }
Assert-Test $reparseEscapeRejected 'The no-reparse path validator accepted a target outside the worktree.'
$checks.Add([ordered]@{ check = 'worktree-reparse-boundary'; passed = $true; createdFilesystemLinks = $false })

$reportRoot = Join-Path $worktreeReports 'f6\runtime\lifecycle-tests'
[void][IO.Directory]::CreateDirectory($reportRoot)
$report = [ordered]@{
    schema = 'combined-vm-lifecycle-render-boundary-tests.v1'
    result = 'PASS'
    checks = @($checks.ToArray())
    rendererPath = $rendererPath
    rendererSha256 = (Get-FileHash -LiteralPath $rendererPath -Algorithm SHA256).Hash.ToUpperInvariant()
    testPath = $PSCommandPath
    testSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runUtc = [DateTime]::UtcNow.ToString('o')
}
$reportPath = Join-Path $reportRoot ('render-boundaries.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N') + '.json')
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output 'PASS: lifecycle input, output, shared-asset, and reparse boundaries; no VM or host actions were invoked.'
Write-Output "REPORT: $reportPath"
Write-Output "REPORT SHA256: $((Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToUpperInvariant())"
