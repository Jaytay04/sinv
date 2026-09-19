param(
    [Parameter(Mandatory=$true)][string]$ModelPath,
    [Parameter(Mandatory=$true)][string]$ModelHeader,
    [Parameter(Mandatory=$true)][string]$DriverMain,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$ValidatorPath,
    [Parameter(Mandatory=$true)][string]$ProbePath,
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake,
    [Parameter(Mandatory=$true)][string]$DriverCMake,
    [Parameter(Mandatory=$true)][string]$LoaderCMake
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
    foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Verify([string[]]$Text){
    $Model=$Text[0];$Header=$Text[1];$Driver=$Text[2];$Interface=$Text[3]
    $Validator=$Text[4];$Probe=$Text[5];$Gate=$Text[6];$Roadmap=$Text[7]
    $Doc=$Text[8];$Tests=$Text[9];$DriverBuild=$Text[10];$LoaderBuild=$Text[11]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 52 does not parse'
    Has $Header @('CONTROL_SESSION_REQUEST_FRAME_UNKNOWN',
      'CONTROL_SESSION_REQUEST_FRAME_ACCEPTED',
      'CONTROL_SESSION_REQUEST_FRAME_LENGTH_MISMATCH',
      'ControlSessionRequestFrameValidate(') 'request-frame header'
    Has $Model @('IOCTL_HYPERVISOR_LOAD','IOCTL_HYPERVISOR_UNLOAD',
      'IOCTL_HYPERVISOR_HOOK','IOCTL_HYPERVISOR_UNHOOK',
      'IOCTL_HYPERVISOR_STATUS','IOCTL_HYPERVISOR_PROBE',
      'IOCTL_HYPERVISOR_LAUNCH_PROBE',
      'IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY',
      'IOCTL_HYPERVISOR_EPT_STATUS_QUERY',
      'IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP',
      'IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP',
      'IOCTL_HYPERVISOR_CR3_STATUS_QUERY','IOCTL_HYPERVISOR_CR3_PROBE',
      'IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE',
      'IOCTL_HYPERVISOR_CPUID_PROBE','IOCTL_HYPERVISOR_MSR_READ_PROBE',
      'IOCTL_HYPERVISOR_TSC_PROBE','IOCTL_HYPERVISOR_INVLPG_PROBE',
      'IOCTL_HYPERVISOR_CET_VMCS_PROBE',
      'IOCTL_HYPERVISOR_VPID_STATUS_QUERY',
      'IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY',
      'IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY',
      'IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY',
      'IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE',
      'IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY',
      'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE',
      'IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED',
      'IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF',
      'IOCTL_HYPERVISOR_CAPABILITIES_QUERY',
      'expected_length = 0U','input_buffer_length == expected_length',
      'CONTROL_SESSION_REQUEST_FRAME_UNKNOWN') 'request-frame model'
    Has $Driver @('if (!file_context->ProtocolNegotiated)',
      'ControlSessionRequestFrameValidate(',
      'CONTROL_SESSION_REQUEST_FRAME_LENGTH_MISMATCH',
      'STATUS_INFO_LENGTH_MISMATCH','IoctlMainDispatch(') 'driver framing gate'
    A ($Driver.IndexOf('if (!file_context->ProtocolNegotiated)') -lt
       $Driver.IndexOf('ControlSessionRequestFrameValidate(')) `
      'request framing occurs before protocol authorization'
    A ($Driver.IndexOf('ControlSessionRequestFrameValidate(') -lt
       $Driver.LastIndexOf('IoctlMainDispatch(')) `
      'runtime dispatch occurs before request framing'
    Has $Interface @('0x800, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x816, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x817, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x818, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x819, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81A, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81B, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81C, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81D, METHOD_BUFFERED, FILE_ANY_ACCESS') 'public IOCTL inventory'
    Has $Validator @('buffer_length != sizeof(hypervisor_hook_request_t)',
      'buffer_length != sizeof(hypervisor_unhook_request_t)') `
      'handler exact-length defense'
    Has $Probe @('OversizedLegacyStatus=DENIED_%lu',
      'OversizedLoad=DENIED_%lu','ExactLegacyStatus=ACCEPTED_BYTES_%lu',
      'LegacyStatusSnapshot=STATE_%u_CORES_%u_HOOKS_%u_ERROR_0x%08X',
      'bytes_returned != sizeof(response)','ERROR_BAD_LENGTH',
      'ControlSessionProtocolClientNegotiate(') `
      'request-frame probe'
    Has $Gate @('gate52-control-session-request-framing',
      'control_session_request_frame_probe.exe',
      'OversizedLegacyStatus=DENIED_24','OversizedLoad=DENIED_24',
      'ExactLegacyStatus=ACCEPTED_BYTES_16',
      'Probe=CONTROL_SESSION_REQUEST_FRAMING',
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 52'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 52 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V14 Roadmap - Exact Operational Request Framing',
      'Gate 52','One run is sufficient','STATUS_INFO_LENGTH_MISMATCH') 'roadmap'
    Has $Doc @('Gate 52: exact operational request framing',
      'gate52-control-session-request-framing.ps1','Do not retry LOAD') `
      'documentation'
    Has $Tests @('test_control_session_request_frame',
      'TestControlSessionRequestFrameBoundary.ps1') 'tests CMake'
    Has $DriverBuild @('control_session_request_frame.c') 'driver CMake'
    Has $LoaderBuild @('control_session_request_frame_probe') 'loader CMake'
}
$Paths=@($ModelPath,$ModelHeader,$DriverMain,$InterfacePath,$ValidatorPath,
  $ProbePath,$GateScript,$RoadmapPath,$DocPath,$TestsCMake,$DriverCMake,
  $LoaderCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('input_buffer_length == expected_length',
     'input_buffer_length >= expected_length')},
 @{I=2;T=$Text[2].Replace('STATUS_INFO_LENGTH_MISMATCH','STATUS_SUCCESS')},
 @{I=4;T=$Text[4].Replace(
     'buffer_length != sizeof(hypervisor_hook_request_t)',
     'buffer_length < sizeof(hypervisor_hook_request_t)')},
 @{I=5;T=$Text[5].Replace('ERROR_BAD_LENGTH','ERROR_SUCCESS')},
 @{I=6;T=$Text[6].Replace('OversizedLoad=DENIED_24',
     'OversizedLoad=ACCEPTED')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'request-frame mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "request framing accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: negotiated operational requests require exact input framing.'
