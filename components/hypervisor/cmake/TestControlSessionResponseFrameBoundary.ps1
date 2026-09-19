param(
    [Parameter(Mandatory=$true)][string]$ModelPath,
    [Parameter(Mandatory=$true)][string]$ModelHeader,
    [Parameter(Mandatory=$true)][string]$DriverMain,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$UnitTestPath,
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
    $Unit=$Text[4];$Probe=$Text[5];$Gate=$Text[6];$Roadmap=$Text[7]
    $Doc=$Text[8];$Tests=$Text[9];$DriverBuild=$Text[10];$LoaderBuild=$Text[11]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 54 does not parse'
    Has $Header @('CONTROL_SESSION_RESPONSE_FRAME_UNKNOWN',
      'CONTROL_SESSION_RESPONSE_FRAME_ACCEPTED',
      'CONTROL_SESSION_RESPONSE_FRAME_LENGTH_MISMATCH',
      'CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC',
      'ControlSessionResponseFrameValidate(') 'response-frame header'
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
      'output_buffer_length == expected_length') 'response-frame model'
    A (([regex]::Matches($Model,
      'return CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC;')).Count -eq 1) `
      'dynamic response classification is not one shared two-case branch'
    A ($Model.IndexOf('IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY') -lt
       $Model.IndexOf('return CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC;')) `
      'persistent aggregate response is not dynamic'
    A ($Model.IndexOf('IOCTL_HYPERVISOR_CR3_STATUS_QUERY') -lt
       $Model.IndexOf('return CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC;')) `
      'CR3 aggregate response is not dynamic'
    Has $Driver @('ControlSessionRequestFrameValidate(',
      'ControlSessionResponseFrameValidate(',
      'CONTROL_SESSION_RESPONSE_FRAME_LENGTH_MISMATCH',
      'STATUS_INFO_LENGTH_MISMATCH','IoctlMainDispatch(') 'driver framing gate'
    A ($Driver.IndexOf('ControlSessionRequestFrameValidate(') -lt
       $Driver.IndexOf('ControlSessionResponseFrameValidate(')) `
      'response framing occurs before request framing'
    A ($Driver.IndexOf('ControlSessionResponseFrameValidate(') -lt
       $Driver.LastIndexOf('IoctlMainDispatch(')) `
      'runtime dispatch occurs before response framing'
    A ($Driver -match 'STATUS_INFO_LENGTH_MISMATCH,\s*0\)') `
      'response mismatch does not complete with zero information'
    Has $Interface @('0x800, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x816, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x817, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x818, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x819, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81A, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81B, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81C, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81D, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x81E, METHOD_BUFFERED, FILE_ANY_ACCESS') 'public IOCTL inventory'
    Has $Unit @('fixed-response IOCTL inventory','28U',
      'short operational response length','overlong operational response length',
      'persistent aggregate output remains topology-dependent',
      'CR3 aggregate output remains topology-dependent') 'response-frame unit test'
    Has $Probe @('UndersizedLegacyStatus=DENIED_%lu',
      'OversizedLegacyStatus=DENIED_%lu',
      'ExactLegacyStatus=ACCEPTED_BYTES_%lu',
      'UndersizedObservation=DENIED_%lu',
      'OversizedObservation=DENIED_%lu',
      'ExactObservation=ACCEPTED_BYTES_%lu','ERROR_BAD_LENGTH',
      'ControlSessionProtocolClientNegotiate(') 'response-frame probe'
    Has $Gate @('gate54-control-session-response-framing',
      'control_session_response_frame_probe.exe',
      'UndersizedLegacyStatus=DENIED_24',
      'OversizedLegacyStatus=DENIED_24',
      'ExactLegacyStatus=ACCEPTED_BYTES_16',
      'UndersizedObservation=DENIED_24',
      'OversizedObservation=DENIED_24',
      'ExactObservation=ACCEPTED_BYTES_240',
      'Probe=CONTROL_SESSION_RESPONSE_FRAMING',
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 54'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 54 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V16 - Exact Fixed-Response Framing',
      'Gate 54','one Gate 54 session','STATUS_INFO_LENGTH_MISMATCH') 'roadmap'
    Has $Doc @('Gate 54: exact fixed-response framing',
      'gate54-control-session-response-framing.ps1','Do not retry LOAD') `
      'documentation'
    Has $Tests @('test_control_session_response_frame',
      'TestControlSessionResponseFrameBoundary.ps1') 'tests CMake'
    Has $DriverBuild @('control_session_response_frame.c') 'driver CMake'
    Has $LoaderBuild @('control_session_response_frame_probe') 'loader CMake'
}
$Paths=@($ModelPath,$ModelHeader,$DriverMain,$InterfacePath,$UnitTestPath,
  $ProbePath,$GateScript,$RoadmapPath,$DocPath,$TestsCMake,$DriverCMake,
  $LoaderCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('output_buffer_length == expected_length',
     'output_buffer_length >= expected_length')},
 @{I=0;T=$Text[0].Replace(
     'return CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC;',
     'return CONTROL_SESSION_RESPONSE_FRAME_ACCEPTED;')},
 @{I=2;T=$Text[2].Replace('STATUS_INFO_LENGTH_MISMATCH','STATUS_SUCCESS')},
 @{I=2;T=$Text[2].Replace('ControlSessionResponseFrameValidate(',
     'ControlSessionResponseFrameBypass(')},
 @{I=5;T=$Text[5].Replace('ERROR_BAD_LENGTH','ERROR_SUCCESS')},
 @{I=6;T=$Text[6].Replace('OversizedObservation=DENIED_24',
     'OversizedObservation=ACCEPTED')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'response-frame mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "response framing accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: fixed operational responses require exact output framing.'
