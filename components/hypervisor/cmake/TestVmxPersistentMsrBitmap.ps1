param(
    [Parameter(Mandatory=$true)][string]$PolicyHeader,
    [Parameter(Mandatory=$true)][string]$PolicySource,
    [Parameter(Mandatory=$true)][string]$PolicyTest,
    [Parameter(Mandatory=$true)][string]$ContextHeader,
    [Parameter(Mandatory=$true)][string]$ContextSource,
    [Parameter(Mandatory=$true)][string]$ContextTest,
    [Parameter(Mandatory=$true)][string]$ControlsHeader,
    [Parameter(Mandatory=$true)][string]$ControlsSource,
    [Parameter(Mandatory=$true)][string]$ControlsTest,
    [Parameter(Mandatory=$true)][string]$StateOpsSource,
    [Parameter(Mandatory=$true)][string]$KernelAdaptersSource,
    [Parameter(Mandatory=$true)][string]$VmcsSource,
    [Parameter(Mandatory=$true)][string]$VmcsTest,
    [Parameter(Mandatory=$true)][string]$LoaderHeader,
    [Parameter(Mandatory=$true)][string]$LoaderSource,
    [Parameter(Mandatory=$true)][string]$LoaderTest,
    [Parameter(Mandatory=$true)][string]$HypervisorCMake,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)

$ErrorActionPreference = 'Stop'
function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Require-Tokens([string]$Text, [string[]]$Tokens, [string]$Scope) {
    foreach ($Token in $Tokens) {
        Assert-True $Text.Contains($Token) "$Scope missing $Token"
    }
}
function Verify(
    [string]$PolicyH, [string]$PolicyC, [string]$PolicyT,
    [string]$ContextH, [string]$ContextC, [string]$ContextT,
    [string]$ControlsH, [string]$ControlsC, [string]$ControlsT,
    [string]$StateOpsC, [string]$KernelAdaptersC,
    [string]$VmcsC, [string]$VmcsT,
    [string]$LoaderH, [string]$LoaderC, [string]$LoaderT,
    [string]$HvCMake, [string]$TestCMake) {
    Require-Tokens $PolicyH @('VMX_MSR_BITMAP_SIZE 4096u','VmxMsrBitmapPolicyBuild(','VmxMsrBitmapPolicyAccessExits(') 'policy header'
    Require-Tokens $PolicyC @('(void)memset(bitmap, 0, bitmap_size)','ApplyPolicyRange(bitmap, 0, MSR_LOW_END)','ApplyPolicyRange(bitmap, MSR_HIGH_START, MSR_HIGH_END)','MsrLookupReadAction(msr)','MsrLookupWriteAction(msr)','return true;') 'policy source'
    Require-Tokens $PolicyT @('test_policy_owned_accesses_exit','test_native_addressable_msrs_pass_through','test_addressable_policy_and_routes_are_coherent','intercepted read has route','intercepted write has route','test_unaddressable_ranges_exit_by_architecture','deterministic image') 'policy tests'

    Require-Tokens $ContextH @('VMX_PERSISTENT_RESOURCE_MSR_BITMAP  (1u << 5)','void* MsrBitmap','uint64_t MsrBitmapPhysicalAddress') 'context header'
    Require-Tokens $ContextC @('AllocateContiguous(','VMX_MSR_BITMAP_SIZE','VmxMsrBitmapPolicyBuild(','VMX_PERSISTENT_RESOURCE_MSR_BITMAP','VmxPersistentBitmapPhysicalValid','staged->MsrBitmap = NULL') 'context source'
    Require-Tokens $ContextT @('bitmap PA alignment','bitmap PA width','destroy bitmap first','VMX_MSR_BITMAP_SIZE') 'context tests'

    Require-Tokens $ControlsH @('VMX_PRIMARY_TRANSPARENCY_REQUIRED','VmxEnableMsrBitmapControl(') 'controls header'
    Require-Tokens $ControlsC @('VmxEnableMsrBitmapControl(','staged.PrimaryControls |= CPU_BASED_USE_MSR_BITMAPS') 'controls source'
    Require-Tokens $ControlsT @('MSR bitmap enabled','Unavailable MSR bitmaps rejected','generic plan leaves MSR bitmap disabled') 'controls tests'
    Require-Tokens $StateOpsC @('VmxEnableMsrBitmapControl(&staged, primary1, &decorated)','staged = decorated') 'persistent state controls'
    Require-Tokens $KernelAdaptersC @('plan->PrimaryControls & CPU_BASED_USE_MSR_BITMAPS','return 0;') 'transient rejection'

    Require-Tokens $VmcsC @('VMX_PERSISTENT_RESOURCE_MSR_BITMAP','VMCS_CTRL_MSR_BITMAP_ADDR','context->MsrBitmapPhysicalAddress','control_count + bitmap_count + timer_count +','cr3_count + 74u') 'VMCS source'
    Require-Tokens $VmcsT @('bitmap PA','bitmap boundary','~CPU_BASED_USE_MSR_BITMAPS') 'VMCS tests'

    Require-Tokens $LoaderH @('LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL 63u') 'loader header'
    Require-Tokens $LoaderC @('(r->ContextOwnedResources & ~UINT32_C(63)) == 0u') 'loader source'
    Require-Tokens $LoaderT @('ContextOwnedResources|=64u') 'loader tests'

    Assert-True (([regex]::Matches($HvCMake, '(?m)^\s*vmx_msr_bitmap_policy\.c\s*$')).Count -eq 1) 'policy source must be registered once'
    Assert-True (([regex]::Matches($TestCMake, 'add_test\(NAME test_vmx_msr_bitmap_policy COMMAND')).Count -eq 1) 'policy unit test must be registered once'
    Assert-True (([regex]::Matches($TestCMake, 'test_vmx_persistent_msr_bitmap_contracts')).Count -eq 1) 'bitmap contract must be registered once'
}

$values = @{
    PolicyH = Get-Content -Raw $PolicyHeader; PolicyC = Get-Content -Raw $PolicySource; PolicyT = Get-Content -Raw $PolicyTest
    ContextH = Get-Content -Raw $ContextHeader; ContextC = Get-Content -Raw $ContextSource; ContextT = Get-Content -Raw $ContextTest
    ControlsH = Get-Content -Raw $ControlsHeader; ControlsC = Get-Content -Raw $ControlsSource; ControlsT = Get-Content -Raw $ControlsTest
    StateOpsC = Get-Content -Raw $StateOpsSource; KernelAdaptersC = Get-Content -Raw $KernelAdaptersSource
    VmcsC = Get-Content -Raw $VmcsSource; VmcsT = Get-Content -Raw $VmcsTest
    LoaderH = Get-Content -Raw $LoaderHeader; LoaderC = Get-Content -Raw $LoaderSource; LoaderT = Get-Content -Raw $LoaderTest
    HvCMake = Get-Content -Raw $HypervisorCMake; TestCMake = Get-Content -Raw $TestsCMake
}
Verify @values

$mutations = @(
    @('PolicyC', 'ApplyPolicyRange(bitmap, 0, MSR_LOW_END);', ''),
    @('PolicyC', '(void)memset(bitmap, 0, bitmap_size);', ''),
    @('ContextC', 'VmxMsrBitmapPolicyBuild(', 'VmxMsrBitmapPolicyBypass('),
    @('ControlsC', 'staged.PrimaryControls |= CPU_BASED_USE_MSR_BITMAPS;', ''),
    @('StateOpsC', 'VmxEnableMsrBitmapControl(&staged, primary1, &decorated)', 'true'),
    @('KernelAdaptersC', 'plan->PrimaryControls & CPU_BASED_USE_MSR_BITMAPS', 'false'),
    @('VmcsC', 'VMCS_CTRL_MSR_BITMAP_ADDR', 'VMCS_CTRL_TSC_OFFSET'),
    @('LoaderH', 'LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL 63u', 'LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL 31u')
)
foreach ($Mutation in $mutations) {
    $copy = @{} + $values
    $copy[$Mutation[0]] = $copy[$Mutation[0]].Replace($Mutation[1], $Mutation[2])
    Assert-True ($copy[$Mutation[0]] -ne $values[$Mutation[0]]) "vacuous mutation $($Mutation[0])"
    $rejected = $false
    try { Verify @copy } catch { $rejected = $true }
    Assert-True $rejected "accepted mutation $($Mutation[0])"
}

Write-Host 'PASS: selective MSR bitmap policy, ownership, VMCS wiring, controls, and public resource masks are mutation-protected.'
