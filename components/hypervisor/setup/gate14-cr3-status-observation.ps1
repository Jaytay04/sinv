[CmdletBinding()]
param([string]$Stage='C:\V3H',[string]$EvidenceRoot='C:\V3H\evidence')
$ErrorActionPreference='Stop'
# Operator-only single supervised CR3 routing observation gate. Never run on a host.
$Driver=Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb=Join-Path $Stage 'hypervisor_driver.pdb'
$Loader=Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb=Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript=Join-Path $Stage 'gate14-cr3-status-observation.ps1'
$Required=@($Driver,$DriverPdb,$Loader,$LoaderPdb,$GateScript)
$Missing=@($Required|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)})
if($Missing.Count-ne0){throw "Missing staged artifact(s): $($Missing -join ', ')"}
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=[Security.Principal.WindowsPrincipal]::new($Identity)
if(-not$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this script from an elevated Windows PowerShell session in the test VM.'}
$RunName='gate14-cr3-status-observation-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence=Join-Path $EvidenceRoot $RunName
$TranscriptPath=Join-Path $RunEvidence 'console.log'
$ZipPath="$RunEvidence.zip"
$GateStart=Get-Date
$TranscriptStarted=$false
$Passed=$false
function Save-ServiceState([string]$Name){
 $Path=Join-Path $RunEvidence "$Name.txt";$Output=& sc.exe query hypersinv 2>&1;$Code=$LASTEXITCODE
 $Output|Out-File -LiteralPath $Path -Encoding utf8;$Output|Write-Host
 [pscustomobject]@{ExitCode=$Code;Output=($Output-join[Environment]::NewLine)}
}
function Invoke-Loader([string]$Name,[string]$Arguments){
 $Stdout=Join-Path $RunEvidence "$Name-stdout.txt";$Stderr=Join-Path $RunEvidence "$Name-stderr.txt"
 $Process=Start-Process -FilePath $Loader -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
 $OutText=Get-Content -Raw -LiteralPath $Stdout;$ErrText=Get-Content -Raw -LiteralPath $Stderr
 if($OutText){$OutText|Write-Host};if($ErrText){$ErrText|Write-Host}
 [pscustomobject]@{ExitCode=$Process.ExitCode;Stdout=$OutText;Stderr=$ErrText}
}
function Save-FailureEvidence{
 try{& sc.exe query hypersinv 2>&1|Out-File (Join-Path $RunEvidence 'service-on-exit.txt') -Encoding utf8}catch{}
 try{& sc.exe qc hypersinv 2>&1|Out-File (Join-Path $RunEvidence 'service-config-on-exit.txt') -Encoding utf8}catch{}
 try{Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$GateStart} -ErrorAction Stop|Where-Object{$_.ProviderName-in@('Service Control Manager','Microsoft-Windows-WER-SystemErrorReporting','Microsoft-Windows-Kernel-Power')}|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|Export-Clixml -LiteralPath (Join-Path $RunEvidence 'system-events.xml')}catch{$_|Out-File (Join-Path $RunEvidence 'system-events-error.txt')}
 try{Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$GateStart} -ErrorAction Stop|Where-Object{$_.ProviderName-in@('Application Error','Windows Error Reporting')}|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|Export-Clixml -LiteralPath (Join-Path $RunEvidence 'application-events.xml')}catch{$_|Out-File (Join-Path $RunEvidence 'application-events-error.txt')}
}
New-Item -ItemType Directory -Force -Path $RunEvidence|Out-Null
try{
 Start-Transcript -LiteralPath $TranscriptPath -Force|Out-Null;$TranscriptStarted=$true
 "GateStart=$($GateStart.ToString('o'))";"Stage=$Stage";"RunEvidence=$RunEvidence"
 Write-Host "`n=== BASELINE ==="
 Get-ComputerInfo|Select-Object WindowsProductName,WindowsVersion,OsBuildNumber|Format-List
 Get-CimInstance Win32_ComputerSystem|Select-Object Manufacturer,Model,NumberOfLogicalProcessors|Format-List
 Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors|Format-List
 try{Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard|Format-List *}catch{Write-Warning "DeviceGuard query unavailable: $($_.Exception.Message)"}
 bcdedit.exe /enum '{current}'
 Get-Item -LiteralPath $Required|Select-Object FullName,Length,LastWriteTime|Format-List
 Get-FileHash -LiteralPath $Required -Algorithm SHA256|Format-List Path,Hash
 Get-AuthenticodeSignature -LiteralPath $Driver|Select-Object Status,StatusMessage,@{Name='Signer';Expression={$_.SignerCertificate.Subject}}|Format-List
 if(-not('Gate14ProcessorGroups'-as[type])){
  $GroupSource=@('using System.Runtime.InteropServices;','public static class Gate14ProcessorGroups {','[DllImport("kernel32.dll")] public static extern ushort GetActiveProcessorGroupCount();','[DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort groupNumber);','}')
  Add-Type ($GroupSource-join[Environment]::NewLine)
 }
 $GroupCount=[Gate14ProcessorGroups]::GetActiveProcessorGroupCount()
 $GroupSizes=for($Group=0;$Group-lt$GroupCount;++$Group){[pscustomobject]@{Group=$Group;ActiveProcessors=[Gate14ProcessorGroups]::GetActiveProcessorCount([uint16]$Group)}}
 $GroupSizes|Format-Table -AutoSize
 $Processors=[int](($GroupSizes|Measure-Object ActiveProcessors -Sum).Sum)
 if($GroupCount-ne2-or$Processors-ne4-or@($GroupSizes|Where-Object ActiveProcessors -ne 2).Count-ne0){throw "Gate 14 requires exactly two processor groups with two processors each; groups=$GroupCount processors=$Processors."}
 $Before=Save-ServiceState 'service-before';if($Before.ExitCode-ne1060){throw "Service was present before LOAD (sc.exe exit $($Before.ExitCode))."}
 Write-Host "`n=== LOAD ==="
 $Load=Invoke-Loader 'load' 'load'
 $RuntimeProof="SUCCESS: Hypervisor loaded on $Processors cores and verified by exact V3 status.";$EptProof='INFO: Exact EPT READY status is verified.'
 if($Load.ExitCode-ne0-or-not$Load.Stdout.Contains($RuntimeProof)-or-not$Load.Stdout.Contains($EptProof)){throw "LOAD did not prove canonical Runtime LOADED and EPT READY (exit $($Load.ExitCode)). Stop; do not retry."}
 Save-ServiceState 'service-after-load'|Out-Null
 Write-Host "`n=== REDACTED CR3 STATUS OBSERVATION ==="
 $Cr3Observed=$false;$Cr3Failure=$null
 for($Attempt=1;$Attempt-le40;++$Attempt){
  try{$Cr3=Invoke-Loader ("cr3-status-{0:D2}"-f$Attempt) 'cr3-status'}catch{$Cr3Failure="CR3 status attempt $Attempt failed: $($_.Exception.Message)";break}
  if($Cr3.ExitCode-ne0){$Cr3Failure="CR3 status attempt $Attempt exited $($Cr3.ExitCode).";break}
  if($Cr3.Stdout-match'(?i)\b(root|normalized_root|guest_cr3|system_cr3)='){$Cr3Failure='CR3 status output exposed a forbidden root field.';break}
  $Header=[regex]::Match($Cr3.Stdout,'CR3 Status: class=(OBSERVED|INACTIVE) owner_generation=(\d+) target_generation=(\d+) contexts=(\d+) targets=(\d+) pa_bits=(\d+) pcide=(\d+) residents=(\d+) transitions=(\d+)')
  $CpuLines=[regex]::Matches($Cr3.Stdout,'(?m)^CR3 CPU: index=(\d+) group=(\d+) number=(\d+) resident=(\d+) matched=(\d+) pcid=(\d+) transitions=(\d+)\r?$')
  if(-not$Header.Success){$Cr3Failure="CR3 status attempt $Attempt lacked the exact header.";break}
  $Contexts=[int]$Header.Groups[4].Value;$Targets=[int]$Header.Groups[5].Value;$PaBits=[int]$Header.Groups[6].Value;$Pcide=[int]$Header.Groups[7].Value;$Residents=[int]$Header.Groups[8].Value;$Transitions=[uint64]$Header.Groups[9].Value
  if($Contexts-ne$Processors-or$Targets-lt1-or$Targets-gt2-or$PaBits-lt36-or$PaBits-gt52-or$Pcide-notin0,1-or$CpuLines.Count-ne$Contexts){$Cr3Failure="CR3 status attempt $Attempt failed topology/header bounds.";break}
  $Seen=@{};[uint64]$TransitionSum=0;$ResidentSum=0
  for($Index=0;$Index-lt$CpuLines.Count;++$Index){
   $Line=$CpuLines[$Index];$SystemIndex=[int]$Line.Groups[1].Value;$Group=[int]$Line.Groups[2].Value;$Number=[int]$Line.Groups[3].Value;$Resident=[int]$Line.Groups[4].Value;$Key="$Group`:$Number"
   if($SystemIndex-ne$Index-or$Seen.ContainsKey($Key)-or$Group-ge$GroupCount-or$Number-ge$GroupSizes[$Group].ActiveProcessors-or$Resident-notin0,1){$Cr3Failure="CR3 status attempt $Attempt failed CPU identity/state validation.";break}
   $Seen[$Key]=$true;$ResidentSum+=$Resident;$TransitionSum+=[uint64]$Line.Groups[7].Value
  }
  if($Cr3Failure){break}
  if($ResidentSum-ne$Residents-or$TransitionSum-ne$Transitions){$Cr3Failure="CR3 status attempt $Attempt failed aggregate validation.";break}
  if($Header.Groups[1].Value-eq'OBSERVED'-and$Transitions-gt0){$Cr3Observed=$true;break}
  Start-Sleep -Milliseconds 250
 }
 if(-not$Cr3Observed-and-not$Cr3Failure){$Cr3Failure='No authenticated CR3 transition was observed within 10 seconds.'}
 Write-Host "`n=== UNLOAD ==="
 $Unload=Invoke-Loader 'unload' 'unload'
 if($Unload.ExitCode-ne0-or-not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')){throw "UNLOAD did not prove canonical Runtime/EPT detached cleanup (exit $($Unload.ExitCode)). Stop; do not retry."}
 $After=Save-ServiceState 'service-after-unload';if($After.ExitCode-ne1060){throw "Service removal was not confirmed (sc.exe exit $($After.ExitCode))."}
 if($Cr3Failure){throw "$Cr3Failure Runtime unload completed cleanly."}
 $CrashDumps=@(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'CrashDumps') -Filter 'hypervisor_loader*.dmp' -ErrorAction SilentlyContinue|Where-Object LastWriteTime -ge $GateStart)
 $MemoryDump=Get-Item -LiteralPath "$env:SystemRoot\MEMORY.DMP" -ErrorAction SilentlyContinue
 if($CrashDumps.Count-ne0-or($MemoryDump-and$MemoryDump.LastWriteTime-ge$GateStart)){throw 'A new loader or kernel crash dump was detected.'}
 @('Result=PASS','Cr3Status=OBSERVED','Cycles=1')|Out-File -LiteralPath (Join-Path $RunEvidence 'summary.txt') -Encoding utf8
 $Passed=$true;Write-Host 'PASS: Gate 14 observed authenticated redacted CR3 routing and clean unload.'
}catch{
 "Result=FAIL`nError=$($_.Exception.Message)"|Out-File -LiteralPath (Join-Path $RunEvidence 'summary.txt') -Encoding utf8
 Write-Error $_
}finally{
 Save-FailureEvidence
 if($TranscriptStarted){Stop-Transcript|Out-Null}
 if(Test-Path -LiteralPath $ZipPath){Remove-Item -LiteralPath $ZipPath -Force}
 & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
 if($LASTEXITCODE-ne0){Write-Warning "Evidence archive failed: $ZipPath"}else{Write-Host "Evidence: $ZipPath"}
}
if(-not$Passed){exit 1}