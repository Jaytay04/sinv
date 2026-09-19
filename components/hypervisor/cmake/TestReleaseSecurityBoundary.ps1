param(
 [Parameter(Mandatory=$true)][string]$DriverMain,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$LoaderMain,
 [Parameter(Mandatory=$true)][string]$GateScript,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function Assert-T([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Driver,[string]$Interface,[string]$Loader,[string]$Gate,[string]$Doc,[string]$CMake){
 foreach($Token in @(
  'RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)")',
  'FILE_DEVICE_SECURE_OPEN',
  'WdfIoQueueDispatchSequential',
  'attributes.ExecutionLevel = WdfExecutionLevelPassive')){
  Assert-T ($Driver.Contains($Token)) "Driver security boundary missing: $Token"
 }
 Assert-T (-not$Driver.Contains('WdfIoQueueDispatchParallel')) 'Driver queue is no longer sequential'
 $Ioctls=@([regex]::Matches($Interface,'(?m)^#define\s+IOCTL_HYPERVISOR_[A-Z0-9_]+\s+.*CTL_CODE\([^\r\n]+$')|ForEach-Object{$_.Value})
 Assert-T ($Ioctls.Count-ge18) "Expected at least 18 IOCTL definitions, got $($Ioctls.Count)"
 foreach($Definition in $Ioctls){
  Assert-T ($Definition.Contains('METHOD_BUFFERED')) "IOCTL is not METHOD_BUFFERED: $Definition"
  Assert-T ($Definition.Contains('FILE_ANY_ACCESS')) "IOCTL access ABI changed unexpectedly: $Definition"
  Assert-T (-not($Definition.Contains('METHOD_NEITHER')-or$Definition.Contains('METHOD_IN_DIRECT')-or$Definition.Contains('METHOD_OUT_DIRECT'))) "Unsafe/direct IOCTL method found: $Definition"
 }
 foreach($Token in @(
  'static BOOL QueryPersistentStatus(',
  'static BOOL QueryEptStatus(',
  'static BOOL QueryCr3Status(',
  'const DWORD timeout_ms = 30000u;',
  '(void)CancelSynchronousIo(thread);',
  'WaitForSingleObject(thread, 5000u)',
  'static int CmdStatus(void)',
  'Live Gate Evidence: EXTERNAL (not encoded by runtime status)')){
  Assert-T ($Loader.Contains($Token)) "Loader status safety boundary missing: $Token"
 }
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput($Gate,[ref]$Tokens,[ref]$Errors)
 Assert-T ($Errors.Count-eq0) 'Gate 34 script does not parse'
 foreach($Token in @(
  'gate34-status-queue-qualification.ps1','Gate34ProcessorGroups',
  '$Workers=4;$Rounds=25;$BatchTimeoutSeconds=120',
  'Invoke-StatusBatch $Round $Workers','$StartInfo=[Diagnostics.ProcessStartInfo]::new()',
  '$StartInfo.FileName=$Loader','$StartInfo.Arguments=''status''',
  '$StartInfo.UseShellExecute=$false','$StartInfo.CreateNoWindow=$true',
  '$StartInfo.RedirectStandardOutput=$true','$StartInfo.RedirectStandardError=$true',
  '$Process=[Diagnostics.Process]::new()','if(-not$Process.Start())',
  '$OutText=$Entry.Process.StandardOutput.ReadToEnd()','$ExitCode=[int]$Entry.Process.ExitCode',
  '$Deadline=[DateTime]::UtcNow.AddSeconds($BatchTimeoutSeconds)',
  '$Entry.Process.Kill();$Entry.Process.WaitForExit()',
  'Persistent Runtime: LOADED','EPT Runtime:        READY','Combined Readiness: READY',
  'CR3 Observation:    INACTIVE','Live Gate Evidence: EXTERNAL (not encoded by runtime status)',
  'Live Gate Evidence: VERIFIED','VMX root active',
  'Export-Csv -LiteralPath $ResultsPath','Probe=STATUS_QUEUE_QUALIFICATION',
  'TotalQueries=$ExpectedQueries','PassedQueries=$($Results.Count)',
  'P50Ms=','P95Ms=','MaxMs=','ThroughputPerSecond=',
  'Invoke-Loader ''unload'' ''unload''','$AfterService.ExitCode-ne1060',
  'Stop-Transcript','tar.exe -a -c -f $ZipPath')){
  Assert-T ($Gate.Contains($Token)) "Gate 34 missing token: $Token"
 }
 Assert-T ($Gate-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 34 mutates signing, boot, SCM, or reboot state'
 Assert-T (-not$Gate.Contains("Start-Process -FilePath `$Loader -ArgumentList 'status'")) 'Gate 34 uses PowerShell 5.1 Start-Process for concurrent exit-code capture'
 foreach($Token in @(
  'Gate 34: status-queue release qualification',
  'gate34-status-queue-qualification.ps1',
  'producing 100 authoritative status',
  'characterization, not a production benchmark',
  'administrator/System-only device ACL')){
  Assert-T ($Doc.Contains($Token)) "Release-qualification documentation missing: $Token"
 }
 foreach($Token in @(
  'test_gate34_release_security_and_queue_contracts','TestReleaseSecurityBoundary.ps1',
  'gate34-status-queue-qualification.ps1')){
  Assert-T ($CMake.Contains($Token)) "Release-qualification CMake missing: $Token"
 }
}
$Text=@($DriverMain,$InterfacePath,$LoaderMain,$GateScript,$DocPath,$TestsCMake|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify @Text
$Mutations=@(
 @{Index=0;Text=$Text[0].Replace('D:P(A;;GA;;;SY)(A;;GA;;;BA)','D:P(A;;GA;;;WD)')},
 @{Index=0;Text=$Text[0].Replace('FILE_DEVICE_SECURE_OPEN','FILE_DEVICE_ALLOW_APPCONTAINER_TRAVERSAL')},
 @{Index=0;Text=$Text[0].Replace('WdfIoQueueDispatchSequential','WdfIoQueueDispatchParallel')},
 @{Index=0;Text=$Text[0].Replace('WdfExecutionLevelPassive','WdfExecutionLevelDispatch')},
 @{Index=1;Text=$Text[1].Replace('METHOD_BUFFERED, FILE_ANY_ACCESS','METHOD_NEITHER, FILE_ANY_ACCESS')},
 @{Index=2;Text=$Text[2].Replace('const DWORD timeout_ms = 30000u;','const DWORD timeout_ms = INFINITE;')},
 @{Index=2;Text=$Text[2].Replace('(void)CancelSynchronousIo(thread);','(void)thread;')},
 @{Index=3;Text=$Text[3].Replace('$Workers=4;$Rounds=25;$BatchTimeoutSeconds=120','$Workers=100;$Rounds=1;$BatchTimeoutSeconds=120')},
 @{Index=3;Text=$Text[3].Replace('$StartInfo.UseShellExecute=$false','$StartInfo.UseShellExecute=$true')},
 @{Index=3;Text=$Text[3].Replace('$ExitCode=[int]$Entry.Process.ExitCode','$ExitCode=0')},
 @{Index=3;Text=$Text[3].Replace('$Deadline=[DateTime]::UtcNow.AddSeconds($BatchTimeoutSeconds)','$Deadline=[DateTime]::MaxValue')},
 @{Index=3;Text=$Text[3].Replace('$AfterService.ExitCode-ne1060','$false')})
foreach($Mutation in $Mutations){
 Assert-T ($Mutation.Text-ne$Text[$Mutation.Index]) 'Release security mutation was vacuous'
 $Copy=$Text.Clone();$Copy[$Mutation.Index]=$Mutation.Text;$Rejected=$false
 try{Verify @Copy}catch{$Rejected=$true}
 Assert-T $Rejected "Release security boundary accepted mutation index $($Mutation.Index)"
}
Write-Host 'PASS: release security boundaries preserve administrator/System access, buffered IOCTLs, sequential passive dispatch, bounded status queries, and Gate 34 evidence.'
