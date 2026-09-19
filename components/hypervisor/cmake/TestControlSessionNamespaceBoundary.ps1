param(
    [Parameter(Mandatory=$true)][string]$NamespaceModel,
    [Parameter(Mandatory=$true)][string]$NamespaceHeader,
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
    A ($Errors.Count -eq 0) 'Gate 48 does not parse'
    Has $Header @('CONTROL_SESSION_NAMESPACE_DENIED',
      'CONTROL_SESSION_NAMESPACE_ALLOWED','ControlSessionNamespaceAuthorize') `
      'namespace header'
    Has $Model @('file_name_length_bytes == 0U',
      'CONTROL_SESSION_NAMESPACE_ALLOWED','CONTROL_SESSION_NAMESPACE_DENIED') `
      'namespace model'
    Has $Driver @('WdfDeviceInitSetExclusive(device_init, TRUE)',
      'WdfFileObjectGetFileName(FileObject)',
      'ControlSessionNamespaceAuthorize((uint16_t)file_name->Length)',
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
    Has $Gate @('gate48-control-session-namespace','Gate48DeviceAccess',
      '\\.\hypersinv\unexpected','NamedOpen=DENIED_5',
      'CanonicalOpen=ADMITTED','Probe=CONTROL_SESSION_NAMESPACE',
      "Invoke-Loader 'observation-after-release' 'observation-status'",
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 48'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 48 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V10 Roadmap - Root-Only Control Namespace',
      '| V10A Pure namespace decision and KMDF integration | Complete',
      'Gate 48 - control-session namespace boundary','One run is sufficient') `
      'roadmap'
    Has $Doc @('Gate 48: root-only control-device namespace',
      'gate48-control-session-namespace.ps1','Do not retry LOAD') `
      'documentation'
    Has $Tests @('test_control_session_namespace',
      'test_control_session_namespace_boundary_contracts',
      'TestControlSessionNamespaceBoundary.ps1') 'test CMake'
    Has $DriverBuild @('control_session_namespace.c') 'driver CMake'
}
$Paths=@($NamespaceModel,$NamespaceHeader,$DriverMain,$InterfacePath,
  $GateScript,$RoadmapPath,$DocPath,$TestsCMake,$DriverCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('file_name_length_bytes == 0U',
     'file_name_length_bytes != 0U')},
 @{I=2;T=$Text[2].Replace('WdfFileObjectGetFileName(FileObject)',
     'WDF_NO_HANDLE')},
 @{I=3;T=$Text[3].Replace('METHOD_BUFFERED, FILE_ANY_ACCESS',
     'METHOD_BUFFERED, FILE_READ_ACCESS')},
 @{I=4;T=$Text[4].Replace('NamedOpen=DENIED_5','NamedOpen=ADMITTED')},
 @{I=4;T=$Text[4].Replace('CanonicalOpen=ADMITTED','CanonicalOpen=DENIED_5')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'namespace-boundary mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "control-session namespace accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: control sessions are restricted to the canonical root namespace.'
