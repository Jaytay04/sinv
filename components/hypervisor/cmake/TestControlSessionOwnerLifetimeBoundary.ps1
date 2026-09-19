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
    $Doc=$Text[8];$Tests=$Text[9];$LoaderBuild=$Text[10]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 50 does not parse'
    Has $Header @('uintptr_t owner_process_identity',
      'uintptr_t requestor_process_identity','requestor_is_user_mode') `
      'owner header'
    Has $Model @('owner_process_identity == (uintptr_t)0',
      'requestor_process_identity == (uintptr_t)0',
      'owner_process_identity != requestor_process_identity') 'owner model'
    Has $Context @('PEPROCESS OwnerProcess','ControlSessionGetFileContext') `
      'file context'
    Has $Driver @('IoGetRequestorProcess(','DriverEvtFileContextCleanup',
      'file_attributes.EvtCleanupCallback = DriverEvtFileContextCleanup',
      'ObReferenceObject(requestor_process)',
      'file_context->OwnerProcess = requestor_process',
      'ObDereferenceObject(file_context->OwnerProcess)',
      'file_context->OwnerProcess = NULL',
      '(uintptr_t)file_context->OwnerProcess',
      '(uintptr_t)requestor_process',
      'WdfRequestGetRequestorMode(Request) == UserMode') 'driver lifetime'
    A (-not $Driver.Contains('IoGetRequestorProcessId(')) `
      'driver still authorizes with numeric requestor PID'
    A (-not $Driver.Contains('OwnerProcessId')) `
      'file context still stores a numeric owner PID'
    $Ioctls=@([regex]::Matches($Interface,
      '(?m)^#define\s+IOCTL_HYPERVISOR_[A-Z0-9_]+\s+.*CTL_CODE\([^\r\n]+\r?$') |
      ForEach-Object {$_.Value})
    A ($Ioctls.Count -eq 31) "expected 30 operational IOCTLs plus negotiation; found $($Ioctls.Count)"
    foreach($Definition in $Ioctls){
        A ($Definition.Contains('METHOD_BUFFERED, FILE_ANY_ACCESS')) `
          "IOCTL ABI changed: $Definition"
    }
    Has $Probe @('SetHandleInformation(','HANDLE_FLAG_INHERIT',
      'WaitForSingleObject(owner_process, OWNER_EXIT_TIMEOUT_MS)',
      'WriteResultFile(','CloseHandle(device);','CreateProcessW(',
      'OwnerBeforeExit=ADMITTED','OwnerExited=CONFIRMED',
      'PostExitChildIoctl=DENIED_5','ERROR_ACCESS_DENIED') `
      'owner-lifetime probe'
    A ($Probe -match
       'CloseHandle\(device\);\s*CloseHandle\(owner_process\);\s*if \(!WriteResultFile\(result_path, result\)\)') `
      'child must close the final inherited device handle before result publication'
    Has $Gate @('gate50-control-session-owner-lifetime',
      'control_session_owner_lifetime_probe.exe',
      'OwnerBeforeExit=ADMITTED','OwnerExited=CONFIRMED',
      'PostExitChildIoctl=DENIED_5',
      'Probe=CONTROL_SESSION_OWNER_LIFETIME',
      'ObservationAfterFinalClose=EXACT_ACTIVE',
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 50'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 50 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V12 Roadmap - Stable Control-Session Owner Identity',
      '| V12A Stable owner identity | Complete',
      'Gate 50 - owner exit and final inherited-handle close',
      'One run is sufficient') 'roadmap'
    Has $Doc @('Gate 50: stable owner identity after opener exit',
      'gate50-control-session-owner-lifetime.ps1','Do not retry LOAD') `
      'documentation'
    Has $Tests @('test_control_session_owner_lifetime_boundary_contracts',
      'TestControlSessionOwnerLifetimeBoundary.ps1') 'test CMake'
    Has $LoaderBuild @('control_session_owner_lifetime_probe') 'loader CMake'
}
$Paths=@($OwnerModel,$OwnerHeader,$FileContext,$DriverMain,$InterfacePath,
  $ProbeSource,$GateScript,$RoadmapPath,$DocPath,$TestsCMake,$LoaderCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=3;T=$Text[3].Replace('ObReferenceObject(requestor_process)',
     'UNREFERENCED_PARAMETER(requestor_process)')},
 @{I=3;T=$Text[3].Replace(
     'file_attributes.EvtCleanupCallback = DriverEvtFileContextCleanup',
     'file_attributes.EvtCleanupCallback = NULL')},
 @{I=3;T=$Text[3].Replace('ObDereferenceObject(file_context->OwnerProcess)',
     'UNREFERENCED_PARAMETER(file_context->OwnerProcess)')},
 @{I=5;T=$Text[5].Replace(
     'WaitForSingleObject(owner_process, OWNER_EXIT_TIMEOUT_MS)',
     'WaitForSingleObject(owner_process, 0U)')},
 @{I=5;T=$Text[5].Replace('PostExitChildIoctl=DENIED_5',
     'PostExitChildIoctl=ADMITTED')},
 @{I=6;T=$Text[6].Replace('Probe=CONTROL_SESSION_OWNER_LIFETIME',
     'Probe=CONTROL_SESSION_OWNER')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) `
      'owner-lifetime mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "control-session owner lifetime accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: control sessions retain stable owner identity through final close.'
