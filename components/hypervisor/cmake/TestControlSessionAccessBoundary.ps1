param(
    [Parameter(Mandatory=$true)][string]$AccessModel,
    [Parameter(Mandatory=$true)][string]$AccessHeader,
    [Parameter(Mandatory=$true)][string]$DriverMain,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake,
    [Parameter(Mandatory=$true)][string]$DriverCMake
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
    foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Verify([string[]]$Text){
    $Model=$Text[0];$Header=$Text[1];$Driver=$Text[2];$Interface=$Text[3]
    $Gate=$Text[4];$Roadmap=$Text[5];$Doc=$Text[6];$Tests=$Text[7]
    $DriverBuild=$Text[8]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 47 does not parse'
    Has $Header @('CONTROL_SESSION_FILE_READ_DATA',
      'CONTROL_SESSION_FILE_WRITE_DATA','CONTROL_SESSION_GENERIC_ALL',
      'CONTROL_SESSION_GENERIC_READ','CONTROL_SESSION_GENERIC_WRITE',
      'ControlSessionAccessAuthorize') 'access header'
    Has $Model @('read_access','write_access',
      'CONTROL_SESSION_ACCESS_DENIED','CONTROL_SESSION_ACCESS_ALLOWED') `
      'access model'
    Has $Driver @('WDF_REQUEST_PARAMETERS_INIT(&request_parameters)',
      'WdfRequestGetParameters(Request, &request_parameters)',
      'request_parameters.Type != WdfRequestTypeCreate',
      'request_parameters.Parameters.Create.SecurityContext == NULL',
      'SecurityContext->DesiredAccess',
      'ControlSessionAccessAuthorize((uint32_t)desired_access)',
      'WdfRequestComplete(Request, STATUS_ACCESS_DENIED)') 'KMDF integration'
    $Ioctls=@([regex]::Matches($Interface,
      '(?m)^#define\s+IOCTL_HYPERVISOR_[A-Z0-9_]+\s+.*CTL_CODE\([^\r\n]+\r?$') |
      ForEach-Object {$_.Value})
    A ($Ioctls.Count -eq 31) "expected 30 operational IOCTLs plus negotiation; found $($Ioctls.Count)"
    foreach($Definition in $Ioctls){
        A ($Definition.Contains('METHOD_BUFFERED, FILE_ANY_ACCESS')) `
          "IOCTL ABI changed: $Definition"
    }
    Has $Gate @('gate47-control-session-access-matrix',
      'Gate47DeviceAccess','ZeroAccess=DENIED_5','ReadOnly=DENIED_5',
      'WriteOnly=DENIED_5','ReadWrite=ADMITTED',
      'Probe=CONTROL_SESSION_ACCESS_MATRIX',
      "Invoke-Loader 'observation-after-release' 'observation-status'",
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 47'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 47 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V9 Roadmap - Explicit Control-Session Access',
      '| V9A Pure access decision and KMDF integration | Complete',
      'Gate 47 - control-session access matrix','One run is sufficient') `
      'roadmap'
    Has $Doc @('Gate 47: explicit control-session access matrix',
      'gate47-control-session-access-matrix.ps1','Do not retry LOAD') `
      'documentation'
    Has $Tests @('test_control_session_access',
      'test_control_session_access_boundary_contracts',
      'TestControlSessionAccessBoundary.ps1') 'test CMake'
    Has $DriverBuild @('control_session_access.c') 'driver CMake'
}
$Paths=@($AccessModel,$AccessHeader,$DriverMain,$InterfacePath,$GateScript,
  $RoadmapPath,$DocPath,$TestsCMake,$DriverCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=2;T=$Text[2].Replace('WdfRequestComplete(Request, STATUS_ACCESS_DENIED)',
     'WdfRequestComplete(Request, STATUS_SUCCESS)')},
 @{I=3;T=$Text[3].Replace('METHOD_BUFFERED, FILE_ANY_ACCESS',
     'METHOD_BUFFERED, FILE_READ_ACCESS')},
 @{I=4;T=$Text[4].Replace('ReadOnly=DENIED_5','ReadOnly=ADMITTED')},
 @{I=4;T=$Text[4].Replace('ReadWrite=ADMITTED','ReadWrite=DENIED_5')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'access-boundary mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "control-session boundary accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: control sessions require read/write rights without IOCTL ABI drift.'
