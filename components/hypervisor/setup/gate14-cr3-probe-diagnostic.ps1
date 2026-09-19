[CmdletBinding()]
param([string]$Stage='C:\V3H',[string]$EvidenceRoot='C:\V3H\evidence')
$ErrorActionPreference='Stop'
# Operator-only one-shot CR3-load VM-exit diagnostic. Never run on a host.
$Driver=Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb=Join-Path $Stage 'hypervisor_driver.pdb'
$Loader=Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb=Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript=Join-Path $Stage 'gate14-cr3-probe-diagnostic.ps1'
$Required=@($Driver,$DriverPdb,$Loader,$LoaderPdb,$GateScript)
$Missing=@($Required|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)})
if($Missing.Count-ne0){throw "Missing staged artifact(s): $($Missing -join ', ')"}
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=[Security.Principal.WindowsPrincipal]::new($Identity)
if(-not$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 throw 'Run this script from an elevated Windows PowerShell session in the test VM.'
}
$RunName='gate14-cr3-probe-diagnostic-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence=Join-Path $EvidenceRoot $RunName
$TranscriptPath=Join-Path $RunEvidence 'console.log'
$ZipPath="$RunEvidence.zip"
$GateStart=Get-Date
$TranscriptStarted=$false
$Passed=$false
function Save-ServiceState([string]$Name){
 $Path=Join-Path $RunEvidence "$Name.txt"
 $Output=& sc.exe query hypersinv 2>&1;$Code=$LASTEXITCODE
 $Output|Out-File -LiteralPath $Path -Encoding utf8;$Output|Write-Host
 [pscustomobject]@{ExitCode=$Code;Output=($Output-join[Environment]::NewLine)}
}
function Invoke-Loader([string]$Name,[string]$Arguments){
 $Stdout=Join-Path $RunEvidence "$Name-stdout.txt"
 $Stderr=Join-Path $RunEvidence "$Name-stderr.txt"
 $StartArguments=@{
  FilePath=$Loader
  ArgumentList=$Arguments
  Wait=$true
  PassThru=$true
  NoNewWindow=$true
  RedirectStandardOutput=$Stdout
  RedirectStandardError=$Stderr
 }
 $Process=Start-Process @StartArguments
 $OutText=Get-Content -Raw -LiteralPath $Stdout
 $ErrText=Get-Content -Raw -LiteralPath $Stderr
 if($OutText){$OutText|Write-Host};if($ErrText){$ErrText|Write-Host}
 [pscustomobject]@{ExitCode=$Process.ExitCode;Stdout=$OutText;Stderr=$ErrText}
}
function Save-FailureEvidence{
 try{& sc.exe query hypersinv 2>&1|Out-File (Join-Path $RunEvidence 'service-on-exit.txt') -Encoding utf8}catch{}
 try{& sc.exe qc hypersinv 2>&1|Out-File (Join-Path $RunEvidence 'service-config-on-exit.txt') -Encoding utf8}catch{}
 try{
  Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$GateStart} -ErrorAction Stop|
   Where-Object{$_.ProviderName-in@('Service Control Manager','Microsoft-Windows-WER-SystemErrorReporting','Microsoft-Windows-Kernel-Power')}|
   Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|
   Export-Clixml -LiteralPath (Join-Path $RunEvidence 'system-events.xml')
 }catch{$_|Out-File (Join-Path $RunEvidence 'system-events-error.txt')}
 try{
  Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$GateStart} -ErrorAction Stop|
   Where-Object{$_.ProviderName-in@('Application Error','Windows Error Reporting')}|
   Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|
   Export-Clixml -LiteralPath (Join-Path $RunEvidence 'application-events.xml')
 }catch{$_|Out-File (Join-Path $RunEvidence 'application-events-error.txt')}
}
New-Item -ItemType Directory -Force -Path $RunEvidence|Out-Null
try{
 Start-Transcript -LiteralPath $TranscriptPath -Force|Out-Null
 $TranscriptStarted=$true
 "GateStart=$($GateStart.ToString('o'))";"Stage=$Stage";"RunEvidence=$RunEvidence"
 Write-Host;Write-Host '=== BASELINE ==='
 Get-ComputerInfo|Select-Object WindowsProductName,WindowsVersion,OsBuildNumber|Format-List
 Get-CimInstance Win32_ComputerSystem|
  Select-Object Manufacturer,Model,NumberOfLogicalProcessors|Format-List
 Get-CimInstance Win32_Processor|
  Select-Object Name,NumberOfCores,NumberOfLogicalProcessors|Format-List
 bcdedit.exe /enum '{current}'
 Get-Item -LiteralPath $Required|Select-Object FullName,Length,LastWriteTime|Format-List
 Get-FileHash -LiteralPath $Required -Algorithm SHA256|Format-List Path,Hash
 Get-AuthenticodeSignature -LiteralPath $Driver|
  Select-Object Status,StatusMessage,@{Name='Signer';Expression={$_.SignerCertificate.Subject}}|Format-List
 $BeforeService=Save-ServiceState 'service-before'
 if($BeforeService.ExitCode-ne1060){
  throw "Service was present before LOAD (sc.exe exit $($BeforeService.ExitCode))."
 }

 Write-Host;Write-Host '=== LOAD ==='
 $Load=Invoke-Loader 'load' 'load'
 $Processors=[int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
 $RuntimeProof="SUCCESS: Hypervisor loaded on $Processors cores and verified by exact V3 status."
 if($Load.ExitCode-ne0-or-not$Load.Stdout.Contains($RuntimeProof)-or
    -not$Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')){
  throw "LOAD did not prove canonical Runtime LOADED and EPT READY (exit $($Load.ExitCode)). Stop; do not retry."
 }
 Save-ServiceState 'service-after-load'|Out-Null

 $ProbeFailure=$null
 Write-Host;Write-Host '=== CR3 STATUS BEFORE PROBE ==='
 $Before=Invoke-Loader 'cr3-before' 'cr3-status'
 if($Before.ExitCode-ne0){$ProbeFailure="Pre-probe CR3 status exited $($Before.ExitCode)."}

 if(-not$ProbeFailure){
  Write-Host;Write-Host '=== ONE-SHOT CR3 PROBE ==='
  $Probe=Invoke-Loader 'cr3-probe' 'cr3-probe'
  $ProbeLine=[regex]::Match($Probe.Stdout,
   'CR3 Probe: status=0x([0-9A-Fa-f]{8}) stage=(\d+) cpu=(\d+):(\d+) before=(\d+) after=(\d+) delta=(\d+)')
  if($Probe.ExitCode-ne0-or-not$ProbeLine.Success-or
     -not$Probe.Stdout.Contains('SUCCESS: One-shot CR3-load VM-exit probe verified.')){
   $ProbeFailure="CR3 probe was not verified (exit $($Probe.ExitCode))."
  }else{
   $BeforeTransitions=[uint64]$ProbeLine.Groups[5].Value
   $AfterTransitions=[uint64]$ProbeLine.Groups[6].Value
   $Delta=[uint64]$ProbeLine.Groups[7].Value
   if($ProbeLine.Groups[1].Value-ne'00000000'-or
      [int]$ProbeLine.Groups[2].Value-ne4-or$AfterTransitions-le$BeforeTransitions-or
      $Delta-ne($AfterTransitions-$BeforeTransitions)){
    $ProbeFailure='CR3 probe response failed exact stage/count validation.'
   }
  }
 }

 if(-not$ProbeFailure){
  Write-Host;Write-Host '=== CR3 STATUS AFTER PROBE ==='
  $After=Invoke-Loader 'cr3-after' 'cr3-status'
  $AfterHeader=[regex]::Match($After.Stdout,
   'CR3 Status: class=OBSERVED .* transitions=(\d+)')
  if($After.ExitCode-ne0-or-not$AfterHeader.Success-or
     [uint64]$AfterHeader.Groups[1].Value-lt$AfterTransitions){
   $ProbeFailure='Post-probe CR3 status did not authenticate the observed transition.'
  }
 }

 Write-Host;Write-Host '=== UNLOAD ==='
 $Unload=Invoke-Loader 'unload' 'unload'
 if($Unload.ExitCode-ne0-or
    -not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')){
  throw "UNLOAD did not prove canonical detached cleanup (exit $($Unload.ExitCode)). Stop; do not retry."
 }
 $AfterService=Save-ServiceState 'service-after-unload'
 if($AfterService.ExitCode-ne1060){
  throw "Service removal was not confirmed (sc.exe exit $($AfterService.ExitCode))."
 }
 if($ProbeFailure){throw "$ProbeFailure Runtime unload completed cleanly."}

 @(
  'Result=PASS'
  'ProbeStage=COMPLETE'
  "BeforeTransitions=$BeforeTransitions"
  "AfterTransitions=$AfterTransitions"
  "Delta=$Delta"
 )|Out-File -LiteralPath (Join-Path $RunEvidence 'summary.txt') -Encoding utf8
 $Passed=$true
 Write-Host 'PASS: one-shot CR3-load VM-exit and clean unload were verified.'
}catch{
 $FailureText='Result=FAIL'+[Environment]::NewLine+'Error='+$_.Exception.Message
 $FailureText|Out-File -LiteralPath (Join-Path $RunEvidence 'summary.txt') -Encoding utf8
 Write-Error $_
}finally{
 Save-FailureEvidence
 if($TranscriptStarted){Stop-Transcript|Out-Null}
 if(Test-Path -LiteralPath $ZipPath){Remove-Item -LiteralPath $ZipPath -Force}
 & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
 if($LASTEXITCODE-ne0){Write-Warning "Evidence archive failed: $ZipPath"}
 else{Write-Host "Evidence: $ZipPath"}
}
if(-not$Passed){exit 1}