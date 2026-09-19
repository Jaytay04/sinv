param(
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$PolicyPath,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$OwnerHeaderPath,
    [Parameter(Mandatory=$true)][string]$FileContextPath,
    [Parameter(Mandatory=$true)][string]$DriverMainPath,
    [Parameter(Mandatory=$true)][string]$RequestFramePath,
    [Parameter(Mandatory=$true)][string]$ResponseFramePath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$GatePath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath
)
$ErrorActionPreference = 'Stop'
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
    foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Ordered([string]$Text,[string[]]$Tokens,[string]$Name){
    $Previous=-1
    foreach($Token in $Tokens){
        $Current=$Text.IndexOf($Token,$Previous+1,[StringComparison]::Ordinal)
        A ($Current -gt $Previous) "$Name order changed at: $Token"
        $Previous=$Current
    }
}
function Verify([string[]]$Text){
    $Abi=$Text[0];$Interface=$Text[1];$Policy=$Text[2];$Owner=$Text[3]
    $OwnerHeader=$Text[4];$FileContext=$Text[5];$Driver=$Text[6]
    $RequestFrame=$Text[7];$ResponseFrame=$Text[8];$Ioctl=$Text[9]
    $Loader=$Text[10];$Gate=$Text[11];$Roadmap=$Text[12];$Doc=$Text[13]

    Has $Abi @('HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST_SIZE',
      'HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST',
      'sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST) == 16') `
      'configure-self ABI'
    $SelfRequest=[regex]::Match($Abi,
      'typedef struct HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST[\s\S]*?} HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST;').Value
    A ($SelfRequest -and $SelfRequest -notmatch 'ProcessId|ProcessStartKey|TargetGeneration') `
      'configure-self request carries a PID'
    Has $Interface @('IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF',
      '0x81C, METHOD_BUFFERED, FILE_ANY_ACCESS') 'configure-self IOCTL'
    Has $Policy @('VmxProcessTargetConfigureSelfRequestValidate(',
      'HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST_SIZE',
      'Request->Flags, Request->Reserved, InputLength') 'configure-self policy'
    Has $FileContext @('PEPROCESS OwnerProcess;') 'referenced file owner'
    Has $OwnerHeader @('VmxProcessTargetOwnerConfigureReferenced(') `
      'referenced target owner surface'
    Ordered $Owner @('VmxProcessTargetOwnerConfigureReferenced(',
      'PsGetProcessId(Process)', 'ObReferenceObject(Process);',
      'ConfigureOwnedProcess(') 'independent target reference'
    Has $Owner @('Process, TargetGeneration, &TargetSet',
      'Owner->Process = Process;', 'ObDereferenceObject(Process);') `
      'shared capture and release'
    Ordered $Driver @('file_context->OwnerProcess = requestor_process;',
      'if (!file_context->ProtocolNegotiated)',
      'IoctlMainDispatch(') 'authenticated session boundary'
    Has $RequestFrame @('IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF',
      'sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST)') `
      'configure-self request frame'
    Has $ResponseFrame @('IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF',
      'HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE') `
      'configure-self response frame'
    Ordered $Ioctl @('static NTSTATUS IoctlDispatchProcessTargetConfigureSelf(',
      'VmxProcessTargetConfigureSelfRequestValidate(',
      'WdfRequestGetFileObject(Request)',
      'FileContext->OwnerProcess == NULL',
      'Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED',
      'Context->ProcessTargetGeneration == UINT64_MAX',
      'VmxProcessTargetOwnerConfigureReferenced(',
      'Context->ProcessTargetGeneration = TargetGeneration;',
      'VmxProcessTargetOwnerQuery(') 'configure-self dispatch'
    Has $Loader @('load-target-session-owner',
      'HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST request',
      'IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF',
      'Configuring current session owner as process target (no PID input).',
      'response.ProcessId != current_process_id',
      'SUCCESS: Current control-session owner configured as target.') `
      'loader session-owned transaction'
    Has $Gate @('gate58-session-owned-process-target',
      "-ArgumentList 'load-target-session-owner'",
      'Configuring current session owner as process target (no PID input).',
      "Contains('Configuring current process target: pid=')",
      'ConfigurationSource=SESSION_OWNER_NO_PID',
      'SelfBoundary=1/0/1','ExitState=EXPIRED','Result=PASS') 'Gate 58'
    Has $Roadmap @('Session-Owned Process Target Configuration',
      'takes a separate target-lifetime reference',
      'No PID or process handle',
      'Gate 58 is one deterministic disposable-VM run') 'Runtime V20 roadmap'
    Has $Doc @('Gate 58: session-owned process target',
      'gate58-session-owned-process-target.ps1',
      'load-target-session-owner') 'operator documentation'
    $Errors=$null
    [Management.Automation.Language.Parser]::ParseInput(
      $Gate,[ref]$null,[ref]$Errors)|Out-Null
    A ($Errors.Count -eq 0) ('Gate 58 parse errors: '+($Errors.Message -join '; '))
}
$Paths=@($AbiPath,$InterfacePath,$PolicyPath,$OwnerPath,$OwnerHeaderPath,
  $FileContextPath,$DriverMainPath,$RequestFramePath,$ResponseFramePath,
  $IoctlPath,$LoaderPath,$GatePath,$RoadmapPath,$DocPath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
  @{I=2;T=$Text[2].Replace('Request->Flags, Request->Reserved, InputLength',
      'Request->Flags, 0, InputLength')},
  @{I=3;T=$Text[3].Replace('ObReferenceObject(Process);',
      '/* target reference omitted */')},
  @{I=9;T=$Text[9].Replace('FileContext->OwnerProcess == NULL','FALSE')},
  @{I=9;T=$Text[9].Replace('VmxProcessTargetOwnerConfigureReferenced(',
      'VmxProcessTargetOwnerConfigure(')},
  @{I=10;T=$Text[10].Replace('(no PID input).','(PID input).')},
  @{I=11;T=$Text[11].Replace('ConfigurationSource=SESSION_OWNER_NO_PID',
      'ConfigurationSource=CALLER_PID')}
)
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'configure-self mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "configure-self verifier accepted mutation $($Mutation.I)"
}
Write-Host 'PASS: session-owned process-target configuration and Gate 58 contracts verified.'
