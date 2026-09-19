[CmdletBinding()]
param(
 [string]$Stage='C:\V3H',
 [string]$EvidenceRoot='C:\V3H\evidence')
$ErrorActionPreference='Stop'
# Release-qualification gate for the read-only operator status queue. Never run on a host.
$Workers=4;$Rounds=25;$BatchTimeoutSeconds=120
$Driver=Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb=Join-Path $Stage 'hypervisor_driver.pdb'
$Loader=Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb=Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript=Join-Path $Stage 'gate34-status-queue-qualification.ps1'
$Required=@($Driver,$DriverPdb,$Loader,$LoaderPdb,$GateScript)
$Missing=@($Required|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)})
if($Missing.Count-ne0){throw "Missing staged artifact(s): $($Missing -join ', ')"}
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=[Security.Principal.WindowsPrincipal]::new($Identity)
if(-not$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 throw 'Run this script from an elevated Windows PowerShell session in the test VM.'
}
if(-not('Gate34ProcessorGroups'-as[type])){
 Add-Type @'
using System.Runtime.InteropServices;
public static class Gate34ProcessorGroups {
 [DllImport("kernel32.dll")] public static extern ushort GetActiveProcessorGroupCount();
 [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}
$RunName='gate34-status-queue-qualification-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence=Join-Path $EvidenceRoot $RunName
$SummaryPath=Join-Path $RunEvidence 'summary.txt'
$TranscriptPath=Join-Path $RunEvidence 'console.log'
$ResultsPath=Join-Path $RunEvidence 'status-results.csv'
$ZipPath="$RunEvidence.zip"
$GateStart=Get-Date;$TranscriptStarted=$false;$Passed=$false;$Failure=$null
function Save-ServiceState([string]$Name){
 $Output=& sc.exe query hypersinv 2>&1;$Code=$LASTEXITCODE
 $Output|Out-File (Join-Path $RunEvidence "$Name.txt") -Encoding utf8
 $Output|Write-Host
 [pscustomobject]@{ExitCode=$Code;Output=($Output-join[Environment]::NewLine)}
}
function Invoke-Loader([string]$Name,[string]$Arguments){
 $Stdout=Join-Path $RunEvidence "$Name-stdout.txt"
 $Stderr=Join-Path $RunEvidence "$Name-stderr.txt"
 $Process=Start-Process -FilePath $Loader -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
 $OutText=Get-Content -Raw -LiteralPath $Stdout
 $ErrText=Get-Content -Raw -LiteralPath $Stderr
 if($OutText){$OutText|Write-Host};if($ErrText){$ErrText|Write-Host}
 [pscustomobject]@{ExitCode=$Process.ExitCode;Stdout=$OutText;Stderr=$ErrText}
}
function Assert-AuthoritativeStatus([string]$Text,[string]$Label){
 foreach($Line in @(
  '=== AUTHORITATIVE STATUS ===',
  '  Persistent Runtime: LOADED',
  '  EPT Runtime:        READY',
  '  Combined Readiness: READY',
  '  CR3 Observation:    INACTIVE',
  '  Live Gate Evidence: EXTERNAL (not encoded by runtime status)')){
  if(-not$Text.Contains($Line)){throw "$Label omitted: $Line"}
 }
 if($Text.Contains('VMX root active')-or$Text.Contains('Live Gate Evidence: VERIFIED')){
  throw "$Label overclaimed live VMX or gate verification."
 }
}
function Invoke-StatusBatch([int]$Round,[int]$WorkerCount){
 $Entries=@();$Completed=$false
 try{
  for($Worker=1;$Worker-le$WorkerCount;$Worker++){
   $Label=('status-r{0:D2}-w{1:D2}'-f$Round,$Worker)
   $Stdout=Join-Path $RunEvidence "$Label-stdout.txt"
   $Stderr=Join-Path $RunEvidence "$Label-stderr.txt"
   $StartInfo=[Diagnostics.ProcessStartInfo]::new()
   $StartInfo.FileName=$Loader
   $StartInfo.Arguments='status'
   $StartInfo.UseShellExecute=$false
   $StartInfo.CreateNoWindow=$true
   $StartInfo.RedirectStandardOutput=$true
   $StartInfo.RedirectStandardError=$true
   $Process=[Diagnostics.Process]::new()
   $Process.StartInfo=$StartInfo
   if(-not$Process.Start()){throw "$Label did not start."}
   $Entries+= [pscustomobject]@{Round=$Round;Worker=$Worker;Label=$Label;Process=$Process;Stdout=$Stdout;Stderr=$Stderr}
  }
  $Deadline=[DateTime]::UtcNow.AddSeconds($BatchTimeoutSeconds)
  $TimedOut=$false
  foreach($Entry in $Entries){
   $Remaining=[int][Math]::Max(0,($Deadline-[DateTime]::UtcNow).TotalMilliseconds)
   if($Remaining-eq0-or-not$Entry.Process.WaitForExit($Remaining)){$TimedOut=$true;break}
  }
  if($TimedOut){throw "Round $Round exceeded the $BatchTimeoutSeconds-second batch watchdog."}
  $BatchResults=@()
  foreach($Entry in $Entries){
   $OutText=$Entry.Process.StandardOutput.ReadToEnd()
   $ErrText=$Entry.Process.StandardError.ReadToEnd()
   $Entry.Process.WaitForExit()
   $OutText|Set-Content -LiteralPath $Entry.Stdout -Encoding utf8
   $ErrText|Set-Content -LiteralPath $Entry.Stderr -Encoding utf8
   $ExitCode=[int]$Entry.Process.ExitCode
   if($ExitCode-ne0){throw "$($Entry.Label) exited $ExitCode."}
   if(-not[string]::IsNullOrWhiteSpace($ErrText)){throw "$($Entry.Label) wrote unexpected stderr."}
   Assert-AuthoritativeStatus $OutText $Entry.Label
   $DurationMs=[Math]::Round(($Entry.Process.ExitTime.ToUniversalTime()-$Entry.Process.StartTime.ToUniversalTime()).TotalMilliseconds,3)
   $BatchResults+= [pscustomobject]@{Round=$Entry.Round;Worker=$Entry.Worker;ExitCode=$ExitCode;DurationMs=$DurationMs;PersistentRuntime='LOADED';EptRuntime='READY';CombinedReadiness='READY';Cr3Observation='INACTIVE';LiveGateEvidence='EXTERNAL'}
  }
  $Completed=$true
  $BatchResults
 }finally{
  if(-not$Completed){
   foreach($Entry in $Entries){
    if(-not$Entry.Process.HasExited){
     try{$Entry.Process.Kill();$Entry.Process.WaitForExit()}catch{}
    }
    if(-not(Test-Path -LiteralPath $Entry.Stdout)){
     try{$Entry.Process.StandardOutput.ReadToEnd()|Set-Content -LiteralPath $Entry.Stdout -Encoding utf8}catch{}
    }
    if(-not(Test-Path -LiteralPath $Entry.Stderr)){
     try{$Entry.Process.StandardError.ReadToEnd()|Set-Content -LiteralPath $Entry.Stderr -Encoding utf8}catch{}
    }
   }
  }
 }
}
function Get-GateFailureEvents{
 $System=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$GateStart} -ErrorAction SilentlyContinue|Where-Object{
  ($_.ProviderName-eq'Microsoft-Windows-Kernel-Power'-and$_.Id-eq41-and$_.Level-le2)-or
  ($_.ProviderName-eq'Microsoft-Windows-WHEA-Logger'-and$_.Level-le2)})
 $Application=@(Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$GateStart} -ErrorAction SilentlyContinue|Where-Object{
  $_.ProviderName-eq'Application Error'-and$_.Id-eq1000-and$_.Level-le2-and
  $_.Message-match'(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b'})
 @($System+$Application)
}
New-Item -ItemType Directory -Force -Path $RunEvidence|Out-Null
try{
 Start-Transcript -LiteralPath $TranscriptPath -Force|Out-Null;$TranscriptStarted=$true
 "GateStart=$($GateStart.ToString('o'))";"Stage=$Stage";"RunEvidence=$RunEvidence"
 "Workers=$Workers";"Rounds=$Rounds";"BatchTimeoutSeconds=$BatchTimeoutSeconds"
 Get-ComputerInfo|Select-Object WindowsProductName,WindowsVersion,OsBuildNumber|Format-List
 Get-CimInstance Win32_ComputerSystem|Select-Object Manufacturer,Model,NumberOfLogicalProcessors|Format-List
 $GroupCount=[Gate34ProcessorGroups]::GetActiveProcessorGroupCount()
 $GroupSizes=@(for($Group=0;$Group-lt$GroupCount;$Group++){[pscustomobject]@{Group=$Group;ActiveProcessors=[Gate34ProcessorGroups]::GetActiveProcessorCount([uint16]$Group)}})
 $GroupSizes|Format-Table -AutoSize
 $TotalProcessors=($GroupSizes|Measure-Object ActiveProcessors -Sum).Sum
 if($GroupCount-ne2-or$TotalProcessors-ne4-or@($GroupSizes|Where-Object ActiveProcessors-ne2).Count-ne0){
  throw "Expected 2 groups with 2 processors each; groups=$GroupCount total=$TotalProcessors."
 }
 $Recovery=Get-CimInstance Win32_OSRecoveryConfiguration
 $Recovery|Select-Object DebugInfoType,AutoReboot,WriteDebugInfo,DebugFilePath,OverwriteExistingDebugFile|Format-List
 if($Recovery.DebugInfoType-ne2-or$Recovery.AutoReboot-or-not$Recovery.WriteDebugInfo){throw 'Crash-dump configuration is incorrect.'}
 bcdedit.exe /enum '{current}'
 Get-Item -LiteralPath $Required|Select-Object FullName,Length,LastWriteTime|Format-List
 Get-FileHash -LiteralPath $Required -Algorithm SHA256|Format-List Path,Hash
 Get-AuthenticodeSignature -LiteralPath $Driver|Select-Object Status,StatusMessage,@{Name='Signer';Expression={$_.SignerCertificate.Subject}}|Format-List
 $BeforeService=Save-ServiceState 'service-before'
 if($BeforeService.ExitCode-ne1060){throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."}
 $Load=Invoke-Loader 'load' 'load'
 $RuntimeProof="SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
 if($Load.ExitCode-ne0-or-not$Load.Stdout.Contains($RuntimeProof)-or-not$Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')){
  throw "LOAD was not verified (exit $($Load.ExitCode)). Stop; do not retry."
 }
 $AfterLoadService=Save-ServiceState 'service-after-load'
 if($AfterLoadService.ExitCode-ne0-or-not$AfterLoadService.Output.Contains('STATE              : 4  RUNNING')){throw 'Driver service was not running after LOAD.'}
 $StatusFailure=$null;$Results=@();$Wall=[Diagnostics.Stopwatch]::StartNew()
 try{
  for($Round=1;$Round-le$Rounds;$Round++){
   $Results+=@(Invoke-StatusBatch $Round $Workers)
   Write-Host ("Completed status round {0}/{1}."-f$Round,$Rounds)
  }
 }catch{$StatusFailure=$_.Exception.Message}
 $Wall.Stop()
 if($Results.Count-gt0){$Results|Export-Csv -LiteralPath $ResultsPath -NoTypeInformation -Encoding utf8}
 $Unload=Invoke-Loader 'unload' 'unload'
 if($Unload.ExitCode-ne0-or-not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')){throw "UNLOAD was not verified (exit $($Unload.ExitCode)). Stop; do not retry."}
 $AfterService=Save-ServiceState 'service-after-unload'
 if($AfterService.ExitCode-ne1060){throw "Service removal was not confirmed (exit $($AfterService.ExitCode))."}
 if($StatusFailure){throw "$StatusFailure Runtime unload completed cleanly."}
 $ExpectedQueries=$Workers*$Rounds
 if($Results.Count-ne$ExpectedQueries){throw "Expected $ExpectedQueries successful status queries, got $($Results.Count)."}
 $Durations=@($Results.DurationMs|Sort-Object)
 $P50Index=[Math]::Max(0,[Math]::Ceiling($Durations.Count*0.50)-1)
 $P95Index=[Math]::Max(0,[Math]::Ceiling($Durations.Count*0.95)-1)
 $P50=[double]$Durations[$P50Index];$P95=[double]$Durations[$P95Index]
 $Maximum=[double]($Durations|Measure-Object -Maximum).Maximum
 $Average=[double]($Durations|Measure-Object -Average).Average
 $Throughput=if($Wall.Elapsed.TotalSeconds-gt0){$ExpectedQueries/$Wall.Elapsed.TotalSeconds}else{0}
 $FailureEvents=@(Get-GateFailureEvents)
 $FailureEvents|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|Export-Clixml (Join-Path $RunEvidence 'failure-events.xml')
 if($FailureEvents.Count-ne0){throw 'Crash or application-failure event detected.'}
 $Dump=Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue
 if($Dump-and$Dump.LastWriteTimeUtc-gt$GateStart.ToUniversalTime()){throw 'A new MEMORY.DMP was generated.'}
 @(
  'Result=PASS','Probe=STATUS_QUEUE_QUALIFICATION',"Workers=$Workers","Rounds=$Rounds","TotalQueries=$ExpectedQueries","PassedQueries=$($Results.Count)",
  'PersistentRuntime=LOADED','EptRuntime=READY','CombinedReadiness=READY','Cr3Observation=INACTIVE','LiveGateEvidence=EXTERNAL',
  ('AverageMs={0:F3}'-f$Average),('P50Ms={0:F3}'-f$P50),('P95Ms={0:F3}'-f$P95),('MaxMs={0:F3}'-f$Maximum),
  ('WallTimeMs={0:F3}'-f$Wall.Elapsed.TotalMilliseconds),('ThroughputPerSecond={0:F3}'-f$Throughput),'ServiceAfterUnload=ABSENT')|Set-Content $SummaryPath -Encoding utf8
 $Passed=$true
 Write-Host 'PASS: 100 authoritative status queries, bounded queue completion, and clean unload were verified.'
}catch{
 $Failure=$_;@('Result=FAIL',"Error=$($_.Exception.Message)")|Set-Content $SummaryPath -Encoding utf8
 Write-Host $_.Exception.Message -ForegroundColor Red
}finally{
 try{& sc.exe query hypersinv 2>&1|Out-File (Join-Path $RunEvidence 'service-on-exit.txt') -Encoding utf8}catch{}
 try{& sc.exe qc hypersinv 2>&1|Out-File (Join-Path $RunEvidence 'service-config-on-exit.txt') -Encoding utf8}catch{}
 if($TranscriptStarted){Stop-Transcript|Out-Null}
 if(Test-Path $ZipPath){Remove-Item $ZipPath -Force}
 & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
 if($LASTEXITCODE-ne0){Write-Warning "Evidence archive failed: $ZipPath"}else{Write-Host "Evidence: $ZipPath"}
}
if($Failure){throw $Failure}
if(-not$Passed){exit 1}
