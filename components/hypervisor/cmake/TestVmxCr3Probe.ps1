param(
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$LaunchPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$AbiTestPath,
 [Parameter(Mandatory=$true)][string]$ScriptPath)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
$Header=[IO.File]::ReadAllText((Resolve-Path $HeaderPath))
$Validator=[IO.File]::ReadAllText((Resolve-Path $ValidatorPath))
$Driver=[IO.File]::ReadAllText((Resolve-Path $DriverPath))
$Launch=[IO.File]::ReadAllText((Resolve-Path $LaunchPath))
$Loader=[IO.File]::ReadAllText((Resolve-Path $LoaderPath))
$AbiTest=[IO.File]::ReadAllText((Resolve-Path $AbiTestPath))
$Script=[IO.File]::ReadAllText((Resolve-Path $ScriptPath))
A ($Header.Contains('0x80C, METHOD_BUFFERED')-and
   $Header.Contains('HYPERVISOR_CR3_PROBE_RESPONSE_SIZE UINT32_C(48)')) 'fixed probe ABI missing'
foreach($Token in @(
 'IoctlValidateCr3ProbeRequest(',
 'input_length != sizeof(*request)',
 'request->Version != HYPERVISOR_CR3_PROBE_VERSION',
 'request->Size != HYPERVISOR_CR3_PROBE_REQUEST_SIZE',
 'request->Flags != HYPERVISOR_CR3_PROBE_FLAGS_NONE',
 'request->Reserved != 0')){
 A ($Validator.Contains($Token)) "validator token missing: $Token"
}
foreach($Token in @(
 'IoctlDispatchCr3Probe(',
 'IoctlCaptureCr3Transitions(',
 'IoControlCode == IOCTL_HYPERVISOR_CR3_PROBE',
 'VmxPersistentDriverOwnerDiagnosticWrite(',
 'owner, 0, &diagnostic_byte)',
 'CR3_PROBE boundary status=%ld byte_match=%lu stage=%lu',
 'response.AfterTransitions <= response.BeforeTransitions',
 'HYPERVISOR_CR3_PROBE_STAGE_COMPLETE')){
 A ($Driver.Contains($Token)) "driver token missing: $Token"
}
A ($Driver.IndexOf('VmxPersistentDriverOwnerDiagnosticWrite(') -lt
   $Driver.IndexOf('response.AfterTransitions <= response.BeforeTransitions')) 'probe ordering'
$ProbeStart=$Driver.IndexOf('IoctlDispatchCr3Probe(')
$ProbeEnd=$Driver.IndexOf('/*===========================================================================',$ProbeStart+1)
A ($ProbeStart-ge0-and$ProbeEnd-gt$ProbeStart) 'probe function bounds'
$ProbeBody=$Driver.Substring($ProbeStart,$ProbeEnd-$ProbeStart)
A (-not$ProbeBody.Contains('VmxGetDriverState()')) 'probe must use persistent owner state, not legacy VMX state'
A (-not$ProbeBody.Contains('__readcr3()')-and-not$ProbeBody.Contains('__writecr3(')) 'resident root-mode IOCTL path must not be mistaken for guest execution'
A ($ProbeBody-notmatch 'CR3_PROBE[^\r\n]*(?:0x%|%p|%I64)') 'probe diagnostics must not expose pointer or CR3 values'
$Cr3Read=$Launch.IndexOf('mov rax, cr3')
$Cr3Write=$Launch.IndexOf('mov cr3, rax')
$CompletionWrite=$Launch.IndexOf('mov byte ptr [rax], VMX_V3D_DIAGNOSTIC_WRITE_VALUE')
A ($Cr3Read-ge0-and$Cr3Write-gt$Cr3Read-and$CompletionWrite-gt$Cr3Write) 'guest CR3 self-write must precede diagnostic completion write'
A ($Driver-notmatch'(?i)printf[^\r\n]*cr3|KdPrint[^\r\n]*cr3') 'driver must not print CR3'
foreach($Token in @(
 'hypervisor_loader.exe cr3-probe',
 'CmdCr3Probe(',
 'IOCTL_HYPERVISOR_CR3_PROBE',
 'SUCCESS: One-shot CR3-load VM-exit probe verified.')){
 A ($Loader.Contains($Token)) "loader token missing: $Token"
}
A ($Loader-notmatch'(?i)(root|guest_cr3|system_cr3)=') 'loader must not expose roots'
A ($AbiTest.Contains('0x80002030')-and$AbiTest.Contains('sizeof(response) == 48')) 'ABI test pins missing'
$ParseErrors=$null
[Management.Automation.Language.Parser]::ParseFile(
 (Resolve-Path $ScriptPath),[ref]$null,[ref]$ParseErrors)|Out-Null
A ($ParseErrors.Count-eq0) ('script parse errors: '+($ParseErrors.Message-join'; '))
foreach($Token in @(
 "'cr3-before' 'cr3-status'",
 "'cr3-probe' 'cr3-probe'",
 "'cr3-after' 'cr3-status'",
 "'unload' 'unload'",
 'Runtime unload completed cleanly.',
 'tar.exe -a -c -f')){
 A ($Script.Contains($Token)) "script token missing: $Token"
}
A ($Script.IndexOf("'cr3-probe' 'cr3-probe'") -lt
   $Script.IndexOf("'unload' 'unload'")) 'probe must precede unload'
Write-Output 'Verified fixed redacted CR3 probe ABI, one-shot kernel path, loader client, and evidence script.'