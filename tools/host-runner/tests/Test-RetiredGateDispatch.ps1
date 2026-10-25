[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$runnerPaths = @(
    'tools/host-runner/Invoke-UEFILoaderHostRunner.ps1',
    'components/uefi-loader/tools/host-runner/Invoke-UEFILoaderHostRunner.ps1'
)
$gateActions = [ordered]@{
    RunInstallerHostGate = 'tests\Invoke-InstallerHostGate.ps1'
    RunInstallerOneShotHostGate = 'tests\Invoke-InstallerOneShotHostGate.ps1'
    RunAuditHostGate = 'tests\Invoke-AuditHostGate.ps1'
    RunFixtureHostGate = 'tests\Invoke-FixtureHostGate.ps1'
}

$global:RetiredDispatchProbe = $null
function Invoke-HostBuild {
    $global:RetiredDispatchProbe.BuildCalls++
}
function Join-Path {
    param(
        [Parameter(Position = 0)] [string]$Path,
        [Parameter(Position = 1)] [string]$ChildPath
    )

    $gateAction = @($global:RetiredDispatchProbe.GateActions.Keys | Where-Object {
        $global:RetiredDispatchProbe.GateActions[$_] -ceq $ChildPath
    })
    if ($gateAction.Count -ne 1) {
        throw "Unexpected path in extracted dispatch: $ChildPath"
    }

    $global:RetiredDispatchProbe.JoinPathCalls++
    return { $global:RetiredDispatchProbe.GateCalls++ }
}

$checks = 0
foreach ($relativePath in $runnerPaths) {
    $runnerPath = [IO.Path]::GetFullPath([IO.Path]::Combine($repoRoot, $relativePath))
    $tokens = $null
    $parseErrors = $null
    $runnerAst = [System.Management.Automation.Language.Parser]::ParseFile(
        $runnerPath, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        throw "Runner parse failed for ${relativePath}: $($parseErrors[0].Message)"
    }

    $actionSwitch = @($runnerAst.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.SwitchStatementAst] -and
            $node.Condition.Extent.Text -eq '$Action'
    }, $true))
    if ($actionSwitch.Count -ne 1) {
        throw "Expected one action dispatcher in ${relativePath}; found $($actionSwitch.Count)."
    }

    foreach ($action in $gateActions.Keys) {
        $clause = @($actionSwitch[0].Clauses | Where-Object {
            $_.Item1.Extent.Text -ceq "'$action'"
        })
        if ($clause.Count -ne 1) {
            throw "Expected one $action dispatcher clause in ${relativePath}."
        }

        $global:RetiredDispatchProbe = @{
            BuildCalls = 0
            JoinPathCalls = 0
            GateCalls = 0
            GateActions = $gateActions
        }
        $SkipBuild = $false
        $UefiProjectRoot = 'project-root'
        $Config = [pscustomobject]@{
            ProjectRoot = 'project-root'
            StateRoot = 'state-root'
        }

        # Run only the AST-extracted action clause inside a switch so a future
        # `break` remains local. Join-Path and Invoke-HostBuild are inert mocks.
        $dispatchText = "switch ('dispatch') { 'dispatch' $($clause[0].Item2.Extent.Text) }"
        $dispatch = [ScriptBlock]::Create($dispatchText)
        $failure = $null
        try {
            & $dispatch
        }
        catch {
            $failure = $_.Exception.Message
        }

        if ([string]::IsNullOrWhiteSpace($failure) -or
            $failure -notmatch '(?i)retired|no longer have build targets') {
            throw "${relativePath} / ${action} did not refuse as retired. Error: $failure"
        }
        if ($global:RetiredDispatchProbe.BuildCalls -ne 0 -or
            $global:RetiredDispatchProbe.JoinPathCalls -ne 0 -or
            $global:RetiredDispatchProbe.GateCalls -ne 0) {
            throw "${relativePath} / ${action} reached build/gate dispatch: $($global:RetiredDispatchProbe | ConvertTo-Json -Compress)"
        }
        $checks++
    }
}

$global:RetiredDispatchProbe = $null
[pscustomobject]@{
    Status = 'PASS'
    Checks = $checks
    RunnerCopies = $runnerPaths.Count
    RetiredGateActions = $gateActions.Count
    ExecutedRunnerOrGateScripts = $false
}