param(
 [Parameter(Mandatory=$true)][string]$LaunchAsm,
 [Parameter(Mandatory=$true)][string]$PlanHeader,
 [Parameter(Mandatory=$true)][string]$PlanSource,
 [Parameter(Mandatory=$true)][string]$PlanTest,
 [Parameter(Mandatory=$true)][string]$DispatchHeader,
 [Parameter(Mandatory=$true)][string]$DispatchSource,
 [Parameter(Mandatory=$true)][string]$DispatchTest,
 [Parameter(Mandatory=$true)][string]$ControlsSource,
 [Parameter(Mandatory=$true)][string]$VmcsPlanSource,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$GateScript,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function Read([string]$Path){
 if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "Missing contract input: $Path"}
 Get-Content -Raw -LiteralPath $Path
}
function Require([string]$Text,[string[]]$Tokens,[string]$Label){
 foreach($Token in $Tokens){if(-not$Text.Contains($Token)){throw "$Label missing token: $Token"}}
}
function Assert-Launch([string]$Text){
 if(([regex]::Matches($Text,'(?im)^\s*wrmsr\s*$')).Count-ne2){
  throw 'guest assembly must contain exactly two WRMSR instructions'
 }
 $sequence='(?is)mov ecx, 06A0h\s+mov eax, 1\s+xor edx, edx\s+wrmsr\s+mov ecx, 06A0h\s+rdmsr'
 if($Text-notmatch$sequence){throw 'WRMSR discard probe must precede the accepted IA32_U_CET RDMSR observation'}
}
function Assert-DiscardPolicy([string]$Text){
 Require $Text @('MsrLookupWriteAction','MSR_ACTION_DISCARD_WRITE','VMX_MSR_WRITE_ROUTE_UNSUPPORTED') 'MSR write route plan'
 if($Text.Contains('__writemsr')){throw 'pure MSR write route plan must not execute a physical WRMSR'}
}
$launch=Read $LaunchAsm;$planH=Read $PlanHeader;$planC=Read $PlanSource
$planT=Read $PlanTest;$dispatchH=Read $DispatchHeader;$dispatchC=Read $DispatchSource
$dispatchT=Read $DispatchTest;$controls=Read $ControlsSource;$vmcs=Read $VmcsPlanSource
$driver=Read $DriverPath;$loader=Read $LoaderPath;$gate=Read $GateScript
$hyperCmake=Read $HypervisorCMake;$tests=Read $TestsCMake

Assert-Launch $launch
Require $planH @('VMX_MSR_WRITE_INSTRUCTION_LENGTH','VMX_MSR_WRITE_ROUTE_UNSUPPORTED','VmxMsrWriteRoutePlanBuild') 'MSR write plan header'
Assert-DiscardPolicy $planC
Require $planT @('IA32_U_CET','IA32_EFER','TEST_VALUE','unsupported write zeroes output') 'MSR write plan tests'
Require $dispatchH @('VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE','VMX_V3D_DISPATCH_FAIL_MSR_WRITE_ROUTE') 'dispatcher header'
Require $dispatchC @('DispatchMsrWrite','VmxMsrWriteRoutePlanBuild','(uint64_t)(uint32_t)guest_gprs->Rax','(uint64_t)(uint32_t)guest_gprs->Rdx << 32') 'dispatcher source'
if($dispatchC.Contains('__writemsr')){throw 'persistent dispatcher must not execute a physical WRMSR'}
Require $dispatchT @('TestMsrWriteRoute','IA32_EFER','MSR write preserves every guest GPR','VMX_V3D_DISPATCH_FAIL_MSR_WRITE_ROUTE') 'dispatcher tests'
Require $controls @('FORBID_PRIMARY VMX_PRIMARY_TRANSPARENCY_FORBIDDEN','VmxEnableMsrBitmapControl','staged.PrimaryControls |= CPU_BASED_USE_MSR_BITMAPS','plan->MissingRequired = r.value & FORBID_PRIMARY') 'control plan'
Require $vmcs @('VMX_PRIMARY_TRANSPARENCY_FORBIDDEN) == 0','VMX_PRIMARY_TRANSPARENCY_REQUIRED','VMCS_CTRL_MSR_BITMAP_ADDR','context->MsrBitmapPhysicalAddress') 'VMCS plan'
Require $driver @('MSR_TRANSPARENCY_PROBE boundary') 'driver boundary'
Require $loader @('msr-transparency-probe','INFO: Authenticated non-root WRMSR discard roundtrip completed.') 'loader command'
Require $gate @('gate26-msr-transparency','msr-transparency-probe','Probe=MSR_READ_WRITE_TRANSPARENCY','WriteDiscard=1','ServiceAfterUnload=ABSENT') 'Gate26 script'
Require $hyperCmake @('vmx_msr_write_route_plan.c') 'hypervisor CMake'
Require $tests @('test_vmx_msr_write_route_plan','test_vmx_persistent_msr_write_boundary_contracts') 'CTest registration'

$launchMutation=$launch-replace '(?im)^\s*wrmsr\s*$','    nop'
$rejected=$false
try{Assert-Launch $launchMutation}catch{$rejected=$true}
if(-not$rejected){throw 'WRMSR assembly mutation was not rejected'}
$policyMutation=$planC.Replace('MSR_ACTION_DISCARD_WRITE','MSR_ACTION_NONE')
$rejected=$false
try{Assert-DiscardPolicy $policyMutation}catch{$rejected=$true}
if(-not$rejected){throw 'discard-policy mutation was not rejected'}

Write-Host 'PASS: persistent WRMSR discard transparency boundary contracts verified.'
