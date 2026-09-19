param(
 [Parameter(Mandatory=$true)][string]$ModelSource,
 [Parameter(Mandatory=$true)][string]$ModelHeader,
 [Parameter(Mandatory=$true)][string]$ExecutorSource,
 [Parameter(Mandatory=$true)][string]$ExecutorHeader,
 [Parameter(Mandatory=$true)][string]$NativeSource,
 [Parameter(Mandatory=$true)][string]$NativeHeader,
 [Parameter(Mandatory=$true)][string]$AsmPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$DriverCMake,
 [Parameter(Mandatory=$true)][string]$HypervisorCMake,
 [Parameter(Mandatory=$true)][string]$TestsCMake,
 [Parameter(Mandatory=$true)][string]$ControlsPath)
$ErrorActionPreference='Stop'
function A([bool]$C,[string]$M){if(-not$C){throw $M}}
function Verify($Model,$Header,$Executor,$ExecutorH,$Native,$NativeH,$Asm,$Test,
 $Driver,$Hv,$Tests,$Controls){
 foreach($Token in @('VMX_VPID_INVALIDATION_SINGLE_CONTEXT UINT32_C(1)',
  'VMX_VPID_INVALIDATION_ALL_CONTEXTS UINT32_C(2)',
  'Request->Vpid == 0 || Request->LinearAddress != 0',
  'Staged.VpidAndReserved = Request->Vpid','VmxVpidValidateCapabilities')){
  A ($Model.Contains($Token)-or$Header.Contains($Token)) "VPID descriptor missing: $Token"}
 foreach($Token in @('RootOwned == 0','VMX_VPID_EXECUTION_WRONG_IRQL',
  'Result == VMX_VPID_INSTRUCTION_SUCCESS','VMX_VPID_EXECUTION_INSTRUCTION_FAILED')){
  A ($Executor.Contains($Token)-or$ExecutorH.Contains($Token)) "VPID executor missing: $Token"}
 foreach($Token in @('KeGetCurrentProcessorNumberEx','KeGetCurrentIrql() != DISPATCH_LEVEL',
  'Snapshot.RootOwned != 1','VmxVpidInvvpidAsm')){A $Native.Contains($Token) "native adapter missing: $Token"}
 A $NativeH.Contains('#include <ntddk.h>') 'native adapter is not WDK-only'
 A ($Model-notmatch'(?i)ntddk|wdm\.h|__vmx|invvpid|vmread|vmwrite|IOCTL') 'descriptor model is not pure'
 A ($Executor-notmatch'(?i)ntddk|wdm\.h|__vmx|invvpid|vmread|vmwrite|IOCTL') 'executor is not pure'
 foreach($Token in @('invvpid rcx, oword ptr [rdx]','test eax, 1','test eax, 40h',
  'mov eax, 1','mov eax, 2','mov eax, 3')){A $Asm.Contains($Token) "INVVPID ASM missing: $Token"}
 foreach($Token in @('zero VPID rejected','unsupported request type rejected',
  'root proof required','IRQL bounded','fail-valid mapped exactly',
  'not-attempted cannot manufacture success')){A $Test.Contains($Token) "VPID test missing: $Token"}
 foreach($Token in @('vmx_vpid_native_adapter.c','vmx_vpid_invvpid.asm')){A $Driver.Contains($Token) "driver CMake missing: $Token"}
 foreach($Token in @('vmx_vpid_invalidation.c','vmx_vpid_executor.c')){A $Hv.Contains($Token) "hypervisor CMake missing: $Token"}
 foreach($Token in @('test_vmx_vpid_invalidation','test_vmx_vpid_invalidation_contracts')){A $Tests.Contains($Token) "test CMake missing: $Token"}
 A ($Controls-notmatch'SECONDARY_ENABLE_VPID|VMCS_CTRL_VPID|VmxVpid') 'V4C enabled VPID controls'
}
$Text=@($ModelSource,$ModelHeader,$ExecutorSource,$ExecutorHeader,$NativeSource,
 $NativeHeader,$AsmPath,$TestPath,$DriverCMake,$HypervisorCMake,$TestsCMake,
 $ControlsPath|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
$Mutations=@(
 @{Index=0;Text=$Text[0].Replace('Request->Vpid == 0 || Request->LinearAddress != 0','Request->LinearAddress != 0')},
 @{Index=2;Text=$Text[2].Replace('Result == VMX_VPID_INSTRUCTION_SUCCESS','Result != VMX_VPID_INSTRUCTION_SUCCESS')},
 @{Index=4;Text=$Text[4].Replace('Snapshot.RootOwned != 1','Snapshot.RootOwned == 1')},
 @{Index=6;Text=$Text[6].Replace('test eax, 40h','test eax, 20h')})
foreach($Mutation in $Mutations){A ($Mutation.Text-ne$Text[$Mutation.Index]) 'vacuous mutation';$Copy=$Text.Clone();$Copy[$Mutation.Index]=$Mutation.Text;$Rejected=$false;try{Verify @Copy}catch{$Rejected=$true};A $Rejected "accepted mutation $($Mutation.Index)"}
Write-Host 'PASS: INVVPID descriptor, checked execution, native adapter, and exact result mapping are mutation-backed and unwired.'
