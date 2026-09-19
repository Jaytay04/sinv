param(
    [Parameter(Mandatory=$true)][string]$DriverMain,
    [Parameter(Mandatory=$true)][string]$GateScript,
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
    $Driver=$Text[0];$Gate=$Text[1];$Roadmap=$Text[2];$Doc=$Text[3];$CMake=$Text[4]
    $Tokens=$null;$Errors=$null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate,[ref]$Tokens,[ref]$Errors)
    A ($Errors.Count -eq 0) 'Gate 46 does not parse'
    Has $Driver @('D:P(A;;GA;;;SY)(A;;GA;;;BA)',
      'WdfControlDeviceInitAllocate','FILE_DEVICE_SECURE_OPEN',
      'WdfDeviceInitSetExclusive(device_init, TRUE)',
      'WdfIoQueueDispatchSequential','WdfExecutionLevelPassive') 'driver access boundary'
    A ($Driver -notmatch '\(A;;GA;;;(WD|AN|AU)\)') `
      'device SDDL admits a broad principal'
    Has $Gate @('gate46-exclusive-control-session',
      'Gate46DeviceAccess','FILE_SHARE_READ | FILE_SHARE_WRITE',
      'FirstOpen=SUCCESS','SecondOpen=DENIED',
      'SecondOpenWin32Error=5',"Invoke-Loader 'observation-after-release' 'observation-status'",
      'Probe=EXCLUSIVE_CONTROL_SESSION','ExclusiveSession=VERIFIED',
      "Invoke-Loader 'unload' 'unload'",'tar.exe -a -c -f') 'Gate 46'
    A ($Gate -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
      'Gate 46 mutates signing, boot, SCM, or reboot state'
    Has $Roadmap @('Runtime V8 Roadmap - Exclusive Privileged Control Sessions',
      '| V8A Exclusive device boundary | Complete',
      'Gate 46 - exclusive control-session contention','One run is sufficient') 'roadmap'
    Has $Doc @('Gate 46: exclusive privileged control session',
      'gate46-exclusive-control-session.ps1','Do not retry LOAD') 'documentation'
    Has $CMake @('test_exclusive_control_device_boundary_contracts',
      'TestExclusiveControlDeviceBoundary.ps1') 'CMake'
}
$Paths=@($DriverMain,$GateScript,$RoadmapPath,$DocPath,$TestsCMake)
$Text=@($Paths|%{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $Text
$Mutations=@(
 @{I=0;T=$Text[0].Replace('WdfDeviceInitSetExclusive(device_init, TRUE)','WdfDeviceInitSetExclusive(device_init, FALSE)')},
 @{I=0;T=$Text[0].Replace('D:P(A;;GA;;;SY)(A;;GA;;;BA)','D:P(A;;GA;;;WD)')},
 @{I=1;T=$Text[1].Replace('SecondOpen=DENIED','SecondOpen=SUCCESS')},
 @{I=1;T=$Text[1].Replace('SecondOpenWin32Error=5','SecondOpenWin32Error=0')})
foreach($Mutation in $Mutations){
    A ($Mutation.T -ne $Text[$Mutation.I]) 'exclusive-device mutation was vacuous'
    $Copy=$Text.Clone();$Copy[$Mutation.I]=$Mutation.T;$Rejected=$false
    try{Verify $Copy}catch{$Rejected=$true}
    A $Rejected "exclusive-device boundary accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: control-device access is administrator/System-only, secure-open, exclusive, sequential, and PASSIVE.'
