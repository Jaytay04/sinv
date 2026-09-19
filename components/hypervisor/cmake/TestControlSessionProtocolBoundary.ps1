param(
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$ModelPath,
    [Parameter(Mandatory=$true)][string]$ModelHeader,
    [Parameter(Mandatory=$true)][string]$KmdfPath,
    [Parameter(Mandatory=$true)][string]$FileContext,
    [Parameter(Mandatory=$true)][string]$DriverMain,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$ClientPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
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
    $Abi=$Text[0];$Model=$Text[1];$Header=$Text[2];$Kmdf=$Text[3]
    $Context=$Text[4];$Driver=$Text[5];$Interface=$Text[6];$Client=$Text[7]
    $Loader=$Text[8];$Probe=$Text[9];$Gate=$Text[10];$Roadmap=$Text[11]
    $Doc=$Text[12];$Tests=$Text[13];$DriverBuild=$Text[14];$LoaderBuild=$Text[15]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 51 does not parse'
    Has $Abi @('HYPERVISOR_CONTROL_PROTOCOL_MAGIC',
      'HYPERVISOR_CONTROL_PROTOCOL_MAJOR UINT16_C(1)',
      'HYPERVISOR_CONTROL_PROTOCOL_MINOR UINT16_C(0)',
      'sizeof(HYPERVISOR_CONTROL_PROTOCOL_REQUEST) == 16',
      'sizeof(HYPERVISOR_CONTROL_PROTOCOL_RESPONSE) == 16') 'protocol ABI'
    Has $Header @('CONTROL_SESSION_PROTOCOL_MALFORMED',
      'CONTROL_SESSION_PROTOCOL_INCOMPATIBLE',
      'CONTROL_SESSION_PROTOCOL_ACCEPTED') 'protocol model header'
    Has $Model @('input_size != sizeof(*request)',
      'output_size != sizeof(*response)',
      'request->Magic != HYPERVISOR_CONTROL_PROTOCOL_MAGIC',
      'request->Major != HYPERVISOR_CONTROL_PROTOCOL_MAJOR',
      'request->Minor != HYPERVISOR_CONTROL_PROTOCOL_MINOR',
      '*response = negotiated') 'protocol model'
    Has $Kmdf @('STATUS_INFO_LENGTH_MISMATCH','STATUS_REVISION_MISMATCH',
      'WdfRequestRetrieveInputBuffer','WdfRequestRetrieveOutputBuffer',
      '*negotiated = TRUE') 'KMDF negotiation adapter'
    Has $Context @('BOOLEAN ProtocolNegotiated') 'file context'
    Has $Driver @('IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE',
      'ControlSessionProtocolNegotiateRequest(',
      '&file_context->ProtocolNegotiated',
      'if (!file_context->ProtocolNegotiated)',
      'WdfRequestComplete(Request, STATUS_REVISION_MISMATCH)',
      'IoctlMainDispatch(') 'driver protocol gate'
    A ($Driver.IndexOf('if (!file_context->ProtocolNegotiated)') -lt
       $Driver.LastIndexOf('IoctlMainDispatch(')) `
      'operational dispatch occurs before protocol authorization'
    Has $Interface @('0x816, METHOD_BUFFERED, FILE_ANY_ACCESS',
      '0x817, METHOD_BUFFERED, FILE_ANY_ACCESS') 'additive IOCTL ABI'
    Has $Client @('IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE',
      'ERROR_REVISION_MISMATCH','bytes_returned != sizeof(response)') `
      'protocol client'
    Has $Loader @('ControlSessionProtocolClientNegotiate(',
      'Protocol negotiated: %u.%u') 'loader negotiation'
    Has $Probe @('UnnegotiatedIoctl=DENIED_%lu',
      'WrongVersion=DENIED_%lu','ExactNegotiation=ACCEPTED_%u_%u',
      'PostNegotiationObservation=EXACT_ACTIVE') 'protocol probe'
    Has $Gate @('gate51-control-session-protocol',
      'control_session_protocol_probe.exe','UnnegotiatedIoctl=DENIED_1306',
      'WrongVersion=DENIED_1306','ExactNegotiation=ACCEPTED_1_0',
      'Probe=CONTROL_SESSION_PROTOCOL',"Invoke-Loader 'unload' 'unload'",
      'tar.exe -a -c -f') 'Gate 51'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 51 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V13 Roadmap - Control Protocol Negotiation',
      'Gate 51','One run is sufficient') 'roadmap'
    Has $Doc @('Gate 51: control-protocol negotiation',
      'gate51-control-session-protocol.ps1','Do not retry LOAD') `
      'documentation'
    Has $Tests @('test_control_session_protocol',
      'TestControlSessionProtocolBoundary.ps1') 'tests CMake'
    Has $DriverBuild @('control_session_protocol.c',
      'control_session_protocol_kmdf.c') 'driver CMake'
    Has $LoaderBuild @('control_session_protocol_client.c',
      'control_session_protocol_probe') 'loader CMake'
}
$Paths=@($AbiPath,$ModelPath,$ModelHeader,$KmdfPath,$FileContext,$DriverMain,
  $InterfacePath,$ClientPath,$LoaderPath,$ProbePath,$GateScript,$RoadmapPath,
  $DocPath,$TestsCMake,$DriverCMake,$LoaderCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=1;T=$Text[1].Replace(
     'request->Major != HYPERVISOR_CONTROL_PROTOCOL_MAJOR',
     'request->Major == HYPERVISOR_CONTROL_PROTOCOL_MAJOR')},
 @{I=3;T=$Text[3].Replace('STATUS_REVISION_MISMATCH','STATUS_SUCCESS')},
 @{I=5;T=$Text[5].Replace('if (!file_context->ProtocolNegotiated)',
     'if (file_context->ProtocolNegotiated)')},
 @{I=7;T=$Text[7].Replace('bytes_returned != sizeof(response)',
     'bytes_returned == sizeof(response)')},
 @{I=10;T=$Text[10].Replace('WrongVersion=DENIED_1306',
     'WrongVersion=ACCEPTED')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'protocol mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "control protocol accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: control sessions require exact protocol negotiation.'
