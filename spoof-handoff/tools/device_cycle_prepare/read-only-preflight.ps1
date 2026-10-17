[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$worktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repo=[IO.Path]::GetFullPath((Join-Path $worktree '..\..\..'))
$report=[IO.Path]::GetFullPath($ReportDirectory)
$boundary=Join-Path $repo 'spoof-handoff\reports'
if(-not $report.StartsWith($boundary+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $report)){throw 'A new handoff report directory is required.'}
$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation required for read-only machine inventory.'}
[void][IO.Directory]::CreateDirectory($report)
$pointer=Get-Content -LiteralPath (Join-Path $repo 'spoof-handoff\artifacts\device-reboot-cycle-20261016\prepared.json') -Raw|ConvertFrom-Json
$script:cycleConfig=Get-Content -LiteralPath $pointer.configPath -Raw|ConvertFrom-Json
$script:cycleRunDirectory=$report
$statePath=Join-Path $script:cycleConfig.runDirectory 'state.json'
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
if($state.phase -cne 'Prepared'){throw 'Read-only preflight expects Prepared state.'}
$beforeState=(Get-FileHash -LiteralPath $statePath).Hash
Import-Module -Name (Join-Path $script:cycleConfig.payloadDirectory 'runtime.psm1') -Force -DisableNameChecking
$verified=Assert-DeviceCycleManifest -ManifestPath $script:cycleConfig.manifestPath -PayloadDirectory $script:cycleConfig.payloadDirectory -RepoRoot $repo -ExpectedManifestSha256 $pointer.manifestSha256
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:cycleConfig.payloadDirectory 'runner.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Prepared runner did not parse.'}
# Load only the production read-only functions, without executing its dispatcher.
$names=@('Write-CycleJsonNew','Write-CycleTextNew','New-CycleEvidencePath','ConvertTo-CycleArgument','Invoke-CycleProcess','Get-CycleSecureBoot','Get-CycleBcdExecutable','Get-CycleBcdEnumeration','Get-CycleCurrentBcdObject','Get-CycleBcdAll','Get-CycleDriverInventory','Get-CycleTrustMembership','Get-CycleAdapterBindings','Get-CycleNetCfgComponentState','Get-CycleSystemDriver','Get-CycleServiceRegistryState','Get-CycleMachineSnapshot','Test-CycleNoOwnedPreExistingState','Get-CycleLockedMap')
foreach($name in $names){
    $nodes=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$false))
    if($nodes.Count -ne 1){throw ('Read-only function is not unique: '+$name)}
    . ([scriptblock]::Create($nodes[0].Extent.Text))
}
$failure=$null;$snapshot=$null;$bcdBefore=$null;$bcdAfter=$null
try{
    $bcdBefore=Get-CycleBcdAll
    $snapshot=Get-CycleMachineSnapshot (Get-CycleLockedMap)
    Write-CycleJsonNew (Join-Path $report 'machine-snapshot.json') $snapshot|Out-Null
    Test-CycleNoOwnedPreExistingState $snapshot
}catch{$failure=$_.Exception.Message}
finally{$bcdAfter=Get-CycleBcdAll}
$stateUnchanged=((Get-FileHash -LiteralPath $statePath).Hash -ceq $beforeState)
$bcdUnchanged=([IO.File]::ReadAllText($bcdBefore.path) -ceq [IO.File]::ReadAllText($bcdAfter.path))
$result=[ordered]@{result=$(if(-not $failure -and $stateUnchanged -and $bcdUnchanged){'PASS'}else{'FAIL'});error=$failure;stateUnchanged=$stateUnchanged;bcdUnchanged=$bcdUnchanged;manifestPinsVerified=$verified.pins.Count;secureBoot=$(if($snapshot){$snapshot.secureBoot}else{$null});noEnableExecuted=$true;noRestartExecuted=$true;ready=$false}
Write-CycleJsonNew (Join-Path $report 'verdict.json') $result|Out-Null
if($result.result -cne 'PASS'){exit 1}
