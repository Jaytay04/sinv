param(
    [Parameter(Mandatory=$true)][string]$ModelPath,
    [Parameter(Mandatory=$true)][string]$ModelHeader,
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$ValidatorPath,
    [Parameter(Mandatory=$true)][string]$RuntimePath,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$TestPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Has([string]$Text,[string[]]$Tokens,[string]$Name){
    foreach($Token in $Tokens){A ($Text.Contains($Token)) "$Name missing: $Token"}
}
function Verify([string[]]$Text){
    $Model=$Text[0];$Header=$Text[1];$Abi=$Text[2];$Interface=$Text[3]
    $Validator=$Text[4];$Runtime=$Text[5];$Owner=$Text[6];$Ioctl=$Text[7]
    $Loader=$Text[8];$Gate=$Text[9];$Test=$Text[10];$Roadmap=$Text[11]
    $Doc=$Text[12];$CMake=$Text[13]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 45 does not parse'
    Has $Header @('VmxObservationStatusCompose','HYPERVISOR_OBSERVATION_STATUS_RESPONSE') 'model header'
    Has $Model @('TelemetryValid','DispatchValid','AddCounter',
      'telemetry->SnapshotSequence != dispatch->TelemetrySequence',
      'telemetry->OwnerGeneration != dispatch->OwnerGeneration',
      'memset(&staged, 0') 'model'
    Has $Abi @('HYPERVISOR_OBSERVATION_STATUS_VERSION',
      'HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE UINT32_C(240)',
      'Telemetry','Dispatch','sizeof(HYPERVISOR_OBSERVATION_STATUS_RESPONSE) == 240') 'ABI'
    A ($Abi -notmatch '\b(GuestRip|Qualification|GuestCr3|DiagnosticTarget|RawExitReason)\b') `
      'observation ABI exposes a forbidden raw field'
    Has $Interface @('IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY','0x816',
      'HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)',
      'IoctlValidateObservationStatusQueryRequest') 'interface'
    Has $Validator @('IoctlValidateObservationStatusQueryRequest',
      'HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_LENGTH',
      'HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_FLAGS') 'validator'
    Has $Runtime @('VmxPersistentRuntimeQueryObservationStatus',
      'RuntimeAcquire(r)','first=*sequence','VmxExitTelemetrySnapshot',
      'view.DispatchSequence=last','VmxObservationStatusCompose') 'runtime'
    Has $Owner @('VmxPersistentDriverOwnerQueryObservationStatus',
      'OwnerEnter(o)','VmxPersistentRuntimeQueryObservationStatus') 'owner'
    Has $Ioctl @('IOCTL Dispatch: OBSERVATION_STATUS_QUERY (0x816)',
      'IoctlValidateObservationStatusQueryRequest',
      'VmxPersistentDriverOwnerQueryObservationStatus',
      'IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY') 'IOCTL'
    Has $Loader @('hypervisor_loader.exe observation-status',
      'Observation status query %u timed out','ValidateObservationStatus',
      'OBSERVATION CPU: index=%u','L"observation-status"') 'loader'
    Has $Gate @('gate45-observation-bundle-cpuid-coherence',
      "Invoke-Loader 'observation-before' 'observation-status'",
      "Invoke-Loader 'observation-after' 'observation-status'",
      'telemetry_sequence=10 dispatch_sequence=10',
      'Probe=OBSERVATION_BUNDLE_CPUID_COHERENCE',
      'ObservationBundle=COHERENT',"Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'gate'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 45 mutates signing, boot, SCM, or reboot state'
    Has $Test @('RejectIdentityAndSequenceMismatch','RejectMalformedNestedRecords',
      'TestRequestAbi','UINT32_C(0x80002058)',
      'VmxObservationStatusCompose','SnapshotSequence == out.Dispatch.TelemetrySequence') 'unit test'
    Has $Roadmap @('Runtime V7 Roadmap - Authenticated Coherent Observation Bundles',
      '| V7A Pure bundle model | Complete','| V7B Runtime query path | Complete',
      'Gate 45 - observation-bundle CPUID coherence','One run is sufficient') 'roadmap'
    Has $Doc @('Gate 45: coherent observation-bundle CPUID coherence',
      'gate45-observation-bundle-cpuid-coherence.ps1','Do not retry LOAD') 'documentation'
    Has $CMake @('test_vmx_observation_status','TestVmxObservationStatusBoundary.ps1') 'CMake'
}
$Paths=@($ModelPath,$ModelHeader,$AbiPath,$InterfacePath,$ValidatorPath,$RuntimePath,
 $OwnerPath,$IoctlPath,$LoaderPath,$GateScript,$TestPath,$RoadmapPath,$DocPath,$TestsCMake)
$Text=@($Paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('telemetry->SnapshotSequence != dispatch->TelemetrySequence','0 == 1')},
 @{I=0;T=$Text[0].Replace('telemetry->OwnerGeneration != dispatch->OwnerGeneration','0 == 1')},
 @{I=3;T=$Text[3].Replace('0x816','0x815')},
 @{I=5;T=$Text[5].Replace('first=*sequence','first=0')},
 @{I=7;T=$Text[7].Replace('VmxPersistentDriverOwnerQueryObservationStatus','VmxPersistentDriverOwnerQueryDispatchStatus')},
 @{I=8;T=$Text[8].Replace('Observation status query %u timed out','Observation query has no timeout')},
 @{I=9;T=$Text[9].Replace('telemetry_sequence=10 dispatch_sequence=10','telemetry_sequence=10 dispatch_sequence=12')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'observation mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "observation boundary accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: Runtime V7 observation bundles are fixed-width, redacted, owner-serialized, and sequence-coherent.'
