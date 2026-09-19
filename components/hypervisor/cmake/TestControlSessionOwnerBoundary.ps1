param(
    [Parameter(Mandatory=$true)][string]$OwnerModel,
    [Parameter(Mandatory=$true)][string]$OwnerHeader,
    [Parameter(Mandatory=$true)][string]$FileContext,
    [Parameter(Mandatory=$true)][string]$DriverMain,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$ProbeSource,
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
    $Model=$Text[0];$Header=$Text[1];$Context=$Text[2];$Driver=$Text[3]
    $Interface=$Text[4];$Probe=$Text[5];$Gate=$Text[6];$Roadmap=$Text[7]
    $Doc=$Text[8];$Tests=$Text[9];$DriverBuild=$Text[10];$LoaderBuild=$Text[11]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 49 does not parse'
    Has $Header @('CONTROL_SESSION_OWNER_DENIED','CONTROL_SESSION_OWNER_ALLOWED',
      'ControlSessionOwnerAuthorize','requestor_is_user_mode') 'owner header'
    Has $Model @('!requestor_is_user_mode',
      'owner_process_identity == (uintptr_t)0',
      'requestor_process_identity == (uintptr_t)0',
      'owner_process_identity != requestor_process_identity') `
      'owner model'
    Has $Context @('CONTROL_SESSION_FILE_CONTEXT','OwnerProcess',
      'ControlSessionGetFileContext') 'file context'
    Has $Driver @('WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(',
      'CONTROL_SESSION_FILE_CONTEXT','IoGetRequestorProcess(',
      'WdfRequestGetRequestorMode(Request) == UserMode',
      'ObReferenceObject(requestor_process)',
      'file_context->OwnerProcess = requestor_process',
      'ObDereferenceObject(file_context->OwnerProcess)',
      'WdfRequestGetFileObject(Request)',
      'ControlSessionOwnerAuthorize(',
      'WdfRequestComplete(Request, STATUS_ACCESS_DENIED)',
      'WdfDeviceInitSetExclusive(device_init, TRUE)',
      'ControlSessionAccessAuthorize((uint32_t)desired_access)',
      'ControlSessionNamespaceAuthorize((uint16_t)file_name->Length)') `
      'KMDF integration'
    $Ioctls=@([regex]::Matches($Interface,
      '(?m)^#define\s+IOCTL_HYPERVISOR_[A-Z0-9_]+\s+.*CTL_CODE\([^\r\n]+\r?$') |
      ForEach-Object {$_.Value})
    A ($Ioctls.Count -eq 31) "expected 30 operational IOCTLs plus negotiation; found $($Ioctls.Count)"
    foreach($Definition in $Ioctls){
        A ($Definition.Contains('METHOD_BUFFERED, FILE_ANY_ACCESS')) `
          "IOCTL ABI changed: $Definition"
    }
    Has $Probe @('security.bInheritHandle = TRUE',
      'CreateFileW(','CreateProcessW(','TRUE,','DeviceIoControl(',
      'ERROR_ACCESS_DENIED','ERROR_INVALID_DATA',
      'response.Telemetry.OwnerGeneration != UINT64_C(1)',
      'response.Dispatch.DispatchSequence != UINT32_C(2)',
      'ParentBefore=ADMITTED','ChildIoctl=DENIED_5',
      'ParentAfter=ADMITTED') 'owner probe'
    Has $Gate @('gate49-control-session-owner','control_session_owner_probe.exe',
      'ParentBefore=ADMITTED','ChildIoctl=DENIED_5','ParentAfter=ADMITTED',
      'Probe=CONTROL_SESSION_OWNER',
      "Invoke-Loader 'observation-after-release' 'observation-status'",
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 49'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 49 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V11 Roadmap - Process-Bound Control Sessions',
      '| V11A Pure owner decision and KMDF integration | Complete',
      'Gate 49 - inherited-handle process boundary','One run is sufficient') `
      'roadmap'
    Has $Doc @('Gate 49: process-bound control session',
      'gate49-control-session-owner.ps1','Do not retry LOAD') 'documentation'
    Has $Tests @('test_control_session_owner',
      'test_control_session_owner_boundary_contracts',
      'TestControlSessionOwnerBoundary.ps1') 'test CMake'
    Has $DriverBuild @('control_session_owner.c') 'driver CMake'
    Has $LoaderBuild @('control_session_owner_probe') 'loader CMake'
}
$Paths=@($OwnerModel,$OwnerHeader,$FileContext,$DriverMain,$InterfacePath,
  $ProbeSource,$GateScript,$RoadmapPath,$DocPath,$TestsCMake,$DriverCMake,
  $LoaderCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace(
     'owner_process_identity != requestor_process_identity',
     'owner_process_identity == requestor_process_identity')},
 @{I=3;T=$Text[3].Replace('WdfRequestGetFileObject(Request)','WDF_NO_HANDLE')},
 @{I=4;T=$Text[4].Replace('METHOD_BUFFERED, FILE_ANY_ACCESS',
     'METHOD_BUFFERED, FILE_READ_ACCESS')},
 @{I=5;T=$Text[5].Replace('ERROR_ACCESS_DENIED','ERROR_SUCCESS')},
 @{I=6;T=$Text[6].Replace('ChildIoctl=DENIED_5','ChildIoctl=ADMITTED')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'owner-boundary mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "control-session owner boundary accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: control sessions bind IOCTL authorization to the opener process.'
