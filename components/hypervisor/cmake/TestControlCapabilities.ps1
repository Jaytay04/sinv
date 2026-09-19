param(
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$PolicyPath,
    [Parameter(Mandatory=$true)][string]$RequestFramePath,
    [Parameter(Mandatory=$true)][string]$ResponseFramePath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$GatePath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMakePath
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
    foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Verify([string[]]$Text){
    $Abi=$Text[0];$Interface=$Text[1];$Policy=$Text[2]
    $RequestFrame=$Text[3];$ResponseFrame=$Text[4];$Ioctl=$Text[5]
    $Loader=$Text[6];$Gate=$Text[7];$Roadmap=$Text[8];$Doc=$Text[9]
    $Tests=$Text[10]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 59 does not parse'
    Has $Abi @('HYPERVISOR_CONTROL_CAPABILITIES_REQUEST_SIZE UINT32_C(16)',
      'HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE UINT32_C(32)',
      'HYPERVISOR_CONTROL_CAPABILITIES_ALL UINT64_C(0x00000000000007FF)',
      'HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT UINT32_C(30)',
      'sizeof(HYPERVISOR_CONTROL_CAPABILITIES_REQUEST) == 16',
      'sizeof(HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE) == 32') 'capability ABI'
    $Request=([regex]::Match($Abi,
      'typedef struct HYPERVISOR_CONTROL_CAPABILITIES_REQUEST[\s\S]*?} HYPERVISOR_CONTROL_CAPABILITIES_REQUEST;')).Value
    A ($Request -notmatch '(?i)pid|process|handle|pointer|address|cr3') `
      'capability request contains identity or address input'
    Has $Interface @('IOCTL_HYPERVISOR_CAPABILITIES_QUERY',
      '0x81D, METHOD_BUFFERED, FILE_ANY_ACCESS') 'capability IOCTL'
    Has $Policy @('InputLength != HYPERVISOR_CONTROL_CAPABILITIES_REQUEST_SIZE',
      'Request->Flags != HYPERVISOR_CONTROL_CAPABILITIES_FLAGS_NONE',
      'Request->Reserved != 0','memset(Response, 0, sizeof(*Response))',
      'Response->CapabilityBits = HYPERVISOR_CONTROL_CAPABILITIES_ALL',
      'HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT') 'capability policy'
    Has $RequestFrame @('IOCTL_HYPERVISOR_CAPABILITIES_QUERY',
      'sizeof(HYPERVISOR_CONTROL_CAPABILITIES_REQUEST)') 'request framing'
    Has $ResponseFrame @('IOCTL_HYPERVISOR_CAPABILITIES_QUERY',
      'HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE') 'response framing'
    Has $Ioctl @('IoctlDispatchCapabilitiesQuery(',
      'ControlCapabilitiesRequestValidate(&Query, InputBufferLength)',
      'ControlCapabilitiesResponseBuild(&Response, sizeof(Response))',
      'IOCTL_HYPERVISOR_CAPABILITIES_QUERY') 'driver publication'
    Has $Loader @('VerifyControlCapabilities(',
      'response.CapabilityBits != HYPERVISOR_CONTROL_CAPABILITIES_ALL',
      'response.OperationalIoctlCount !=',
      'SUCCESS: Exact control capability manifest verified.',
      'if (!VerifyControlCapabilities(g_device_handle, &protocol_error))') `
      'loader handshake'
    A ($Loader.IndexOf('ControlSessionProtocolClientNegotiate(') -lt
       $Loader.IndexOf('if (!VerifyControlCapabilities(g_device_handle, &protocol_error))')) `
      'loader verifies capabilities before protocol negotiation'
    A ($Loader.IndexOf('if (!VerifyControlCapabilities(g_device_handle, &protocol_error))') -lt
       $Loader.IndexOf('return TRUE;',
         $Loader.IndexOf('if (!VerifyControlCapabilities(g_device_handle, &protocol_error))'))) `
      'loader returns before capability verification'
    Has $Gate @('gate59-control-capability-manifest',
      'bitmap=0x00000000000003FF operational_ioctls=29',
      'ManifestSessions=LOAD/STATUS/UNLOAD',
      "Invoke-Loader 'load' 'load'", "Invoke-Loader 'unload' 'unload'",
      'Probe=CONTROL_CAPABILITY_MANIFEST','tar.exe -a -c -f') 'Gate 59'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 59 mutates boot, signing, SCM, or reboot state'
    Has $Roadmap @('Runtime V21 - Authenticated Control Capability Manifest',
      'Gate 59','one deterministic disposable-VM lifecycle') 'roadmap'
    Has $Doc @('Gate 59: authenticated control capability manifest',
      'gate59-control-capability-manifest.ps1') 'test documentation'
    Has $Tests @('test_control_capabilities_policy',
      'TestControlCapabilities.ps1') 'tests CMake'
}
$Paths=@($AbiPath,$InterfacePath,$PolicyPath,$RequestFramePath,
  $ResponseFramePath,$IoctlPath,$LoaderPath,$GatePath,$RoadmapPath,$DocPath,
  $TestsCMakePath)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=2;T=$Text[2].Replace('Request->Reserved != 0','Request->Reserved == 0')},
 @{I=0;T=$Text[0].Replace('UINT64_C(0x00000000000007FF)','UINT64_C(0x00000000000003FF)')},
 @{I=6;T=$Text[6].Replace(
   'response.CapabilityBits != HYPERVISOR_CONTROL_CAPABILITIES_ALL',
   'response.CapabilityBits == HYPERVISOR_CONTROL_CAPABILITIES_ALL')},
 @{I=7;T=$Text[7].Replace('ManifestSessions=LOAD/STATUS/UNLOAD',
   'ManifestSessions=LOAD')}
)
foreach($Mutation in $Mutations){
    A ($Mutation.T -cne $Text[$Mutation.I]) 'mutation did not change source'
    $Candidate=@($Text.Clone());$Candidate[$Mutation.I]=$Mutation.T
    $Rejected=$false
    try{Verify $Candidate}catch{$Rejected=$true}
    A $Rejected "mutation $($Mutation.I) was not rejected"
}
Write-Host 'PASS: authenticated control capability manifest contracts verified.'
