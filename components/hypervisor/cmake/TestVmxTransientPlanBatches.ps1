[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourcePath,
    [Parameter(Mandatory=$true)][string]$HeaderPath,
    [Parameter(Mandatory=$true)][string]$TestPath
)
$ErrorActionPreference = 'Stop'
function A([bool]$Ok,[string]$Why){if(-not$Ok){throw $Why}}
function O([string]$Text,[string[]]$Tokens){
 $At=0;foreach($Token in $Tokens){$Next=$Text.IndexOf($Token,$At,[StringComparison]::Ordinal);if($Next-lt0){return $false};$At=$Next+$Token.Length};return $true
}
function V([string]$Source){
 O $Source @(
  'expected_plan_count = plan.Count + 36u',
  'platform.PlanSegments(&plan, &proc)',
  'if (plan.Count != expected_plan_count) goto teardown',
  'expected_plan_count += 16u',
  'platform.PlanMachine(&plan, &proc)',
  'if (plan.Count != expected_plan_count) goto teardown',
  'stage = VMX_STAGE_GUEST_PLAN',
  'expected_plan_count += 22u',
  'platform.PlanHost(&plan, &proc, ctx)',
  'if (plan.Count != expected_plan_count) goto teardown',
  'stage = VMX_STAGE_HOST_PLAN',
  'VmxApplyVmcsPlan(&plan')
}
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$t=Get-Content -Raw -LiteralPath $TestPath
A (V $s) 'Transient planner batch contract is incomplete or misordered.'
foreach($x in @('exact 36-field guest-segment','16-field guest-machine','22-field host-state','without any VMWRITE')){A($h.Contains($x)) "Header missing $x"}
foreach($x in @('t_incomplete_planners_stop_before_vmwrite','segment failure no vmwrite','machine failure no vmwrite','host failure no vmwrite','segment failure teardown','machine failure teardown','host failure teardown')){A($t.Contains($x)) "Test missing $x"}
$m=@(
 $s.Replace('plan.Count + 36u','plan.Count + 35u'),
 $s.Replace('expected_plan_count += 16u','expected_plan_count += 15u'),
 $s.Replace('expected_plan_count += 22u','expected_plan_count += 21u'),
 $s.Replace('if (plan.Count != expected_plan_count) goto teardown;','if (false) goto teardown;')
)
foreach($x in $m){A($x-ne$s)'Mutation did not apply';A(-not(V $x))'Planner batch mutation survived'}
Write-Host 'PASS: transient VMCS planners require exact complete batches before VMWRITE.'
