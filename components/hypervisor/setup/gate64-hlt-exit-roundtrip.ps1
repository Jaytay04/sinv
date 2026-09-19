[CmdletBinding()]
param([string]$Stage='C:\V3H',[string]$EvidenceRoot='C:\V3H\evidence')
$ErrorActionPreference='Stop'
# Operator-only authenticated HLT VM-exit roundtrip lifecycle gate. Never run on a host.
$Driver=Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb=Join-Path $Stage 'hypervisor_driver.pdb'
$Loader=Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb=Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript=Join-Path $Stage 'gate64-hlt-exit-roundtrip.ps1'
$Required=@($Driver,$DriverPdb,$Loader,$LoaderPdb,$GateScript)
$Missing=@($Required|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)})
if($Missing.Count-ne0){throw "Missing staged artifact(s): $($Missing -join ', ')"}
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=[Security.Principal.WindowsPrincipal]::new($Identity)
if(-not$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 throw 'Run this script from an elevated Windows PowerShell session in the test VM.'
}
if(-not('Gate64ProcessorGroups'-as[type])){
 Add-Type @'
using System.Runtime.InteropServices;
public static class Gate64ProcessorGroups {
 [DllImport("kernel32.dll")] public static extern ushort GetActiveProcessorGroupCount();
 [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}
$RunName='gate64-hlt-exit-roundtrip-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence=Join-Path $EvidenceRoot $RunName
$SummaryPath=Join-Path $RunEvidence 'summary.txt'
$TranscriptPath=Join-Path $RunEvidence 'console.log'
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
 Get-ComputerInfo|Select-Object WindowsProductName,WindowsVersion,OsBuildNumber|Format-List
 Get-CimInstance Win32_ComputerSystem|Select-Object Manufacturer,Model,NumberOfLogicalProcessors|Format-List
 $GroupCount=[Gate64ProcessorGroups]::GetActiveProcessorGroupCount()
 $GroupSizes=@(for($Group=0;$Group-lt$GroupCount;$Group++){[pscustomobject]@{Group=$Group;ActiveProcessors=[Gate64ProcessorGroups]::GetActiveProcessorCount([uint16]$Group)}})
 $GroupSizes|Format-Table -AutoSize
 $TotalProcessors=($GroupSizes|Measure-Object ActiveProcessors -Sum).Sum
 if($GroupCount-ne2-or$TotalProcessors-ne4-or@($GroupSizes|Where-Object ActiveProcessors-ne2).Count-ne0){
  throw "Expected 2 groups with 2 processors each; groups=$GroupCount total=$TotalProcessors."
 }
 $Recovery=Get-CimInstance Win32_OSRecoveryConfiguration
 $Recovery|Select-Object DebugInfoType,AutoReboot,WriteDebugInfo,DebugFilePath,OverwriteExistingDebugFile|Format-List
 if($Recovery.DebugInfoType-ne2-or$Recovery.AutoReboot-or-not$Recovery.WriteDebugInfo){
  throw 'Crash-dump configuration is incorrect.'
 }
 bcdedit.exe /enum '{current}'
 Get-Item -LiteralPath $Required|Select-Object FullName,Length,LastWriteTime|Format-List
 Get-FileHash -LiteralPath $Required -Algorithm SHA256|Format-List Path,Hash
 $Signature=Get-AuthenticodeSignature -LiteralPath $Driver
 $Signature|Select-Object Status,StatusMessage,@{Name='Signer';Expression={$_.SignerCertificate.Subject}}|Format-List
 if($Signature.Status-ne[Management.Automation.SignatureStatus]::Valid-or
    $Signature.SignerCertificate.Subject-ne'CN=hypersinv'){
  throw 'Driver signature is not valid for CN=hypersinv in this VM.'
 }
 $BeforeService=Save-ServiceState 'service-before'
 if($BeforeService.ExitCode-ne1060){throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."}
 $Load=Invoke-Loader 'load' 'load'
 $RuntimeProof="SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
 $CapabilityLine='Control capabilities: version=1 bitmap=0x00000000000007FF operational_ioctls=30'
 $CapabilityProof='SUCCESS: Exact control capability manifest verified.'
 $SubstrateLine='VMX substrate: VMWARE_NESTED'
 $SubstrateProof='SUCCESS: VMX substrate admitted before SCM mutation.'
 if($Load.ExitCode-ne0-or-not$Load.Stdout.Contains($RuntimeProof)-or
    -not$Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')-or
    -not$Load.Stdout.Contains($SubstrateLine)-or
    -not$Load.Stdout.Contains($SubstrateProof)-or
    -not$Load.Stdout.Contains($CapabilityLine)-or
    -not$Load.Stdout.Contains($CapabilityProof)){
  throw "LOAD was not verified (exit $($Load.ExitCode)). Stop; do not retry."
 }
 $AfterLoadService=Save-ServiceState 'service-after-load'
 if($AfterLoadService.ExitCode-ne0-or-not$AfterLoadService.Output.Contains('STATE              : 4  RUNNING')){
  throw 'Driver service was not running after LOAD.'
 }
 $Hlt=Invoke-Loader 'hlt-probe' 'hlt-probe'
 $HltProof='SUCCESS: Persistent HLT VM-exit and exact primary-control restoration verified.'
 foreach($Line in @(
  'HLT Probe: status=0x00000000 stage=1',
  'HLT_EXIT_REASON=12',
  'HLT_EXIT_LENGTH=1',
  'HLT_COMPLETION=0x484C5431',
  $HltProof)){
  if($Hlt.ExitCode-ne0-or-not$Hlt.Stdout.Contains($Line)){
   throw "HLT probe was not verified (exit $($Hlt.ExitCode)); missing: $Line"
  }
 }
 $BeforeMatch=[regex]::Match($Hlt.Stdout,'(?m)^PRIMARY_CONTROLS_BEFORE=0x([0-9A-Fa-f]{8})\r?$')
 $ArmedMatch=[regex]::Match($Hlt.Stdout,'(?m)^PRIMARY_CONTROLS_ARMED=0x([0-9A-Fa-f]{8})\r?$')
 $AfterMatch=[regex]::Match($Hlt.Stdout,'(?m)^PRIMARY_CONTROLS_AFTER=0x([0-9A-Fa-f]{8})\r?$')
 if((-not $BeforeMatch.Success)-or(-not $ArmedMatch.Success)-or(-not $AfterMatch.Success)){
  throw 'HLT probe omitted exact primary-control observations.'
 }
 $Before=[Convert]::ToUInt32($BeforeMatch.Groups[1].Value,16)
 $Armed=[Convert]::ToUInt32($ArmedMatch.Groups[1].Value,16)
 $After=[Convert]::ToUInt32($AfterMatch.Groups[1].Value,16)
 if((($Before -band 0x80)-ne 0)-or
    ($Armed-ne($Before -bor 0x80))-or$After-ne$Before){
  throw ('HLT primary-control transaction was not exact: before=0x{0:X8} armed=0x{1:X8} after=0x{2:X8}' -f $Before,$Armed,$After)
 }
 $StatusFailure=$null
 $Status=Invoke-Loader 'authoritative-status' 'status'
 $ExpectedStatus=@(
  '=== AUTHORITATIVE STATUS ===',
  '  Persistent Runtime: LOADED',
  '  EPT Runtime:        READY',
  '  Combined Readiness: READY',
  '  CR3 Observation:    INACTIVE',
  '  Live Gate Evidence: EXTERNAL (not encoded by runtime status)')
 if($Status.ExitCode-ne0){
  $StatusFailure="Authoritative status exited $($Status.ExitCode)."
 }else{
  if(-not$Status.Stdout.Contains($CapabilityLine)-or
     -not$Status.Stdout.Contains($CapabilityProof)){
   $StatusFailure='Status session did not verify the exact capability manifest.'
  }
  foreach($Line in $ExpectedStatus){
   if(-not$Status.Stdout.Contains($Line)){$StatusFailure="Authoritative status omitted: $Line";break}
  }
  if(-not$StatusFailure-and($Status.Stdout.Contains('VMX root active')-or
      $Status.Stdout.Contains('Live Gate Evidence: VERIFIED'))){
   $StatusFailure='Authoritative status overclaimed live VMX or gate verification.'
  }
 }
 $Unload=Invoke-Loader 'unload' 'unload'
 if($Unload.ExitCode-ne0-or-not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')-or
    -not$Unload.Stdout.Contains($CapabilityLine)-or
    -not$Unload.Stdout.Contains($CapabilityProof)){
  throw "UNLOAD was not verified (exit $($Unload.ExitCode)). Stop; do not retry."
 }
 $AfterService=Save-ServiceState 'service-after-unload'
 if($AfterService.ExitCode-ne1060){throw "Service removal was not confirmed (exit $($AfterService.ExitCode))."}
 foreach($Result in @($Load,$Hlt,$Status,$Unload)){
  if(-not[string]::IsNullOrWhiteSpace($Result.Stderr)){
   throw 'A loader operation emitted stderr. Preserve evidence and do not retry.'
  }
 }
 if($StatusFailure){throw "$StatusFailure Runtime unload completed cleanly."}
 $FailureEvents=@(Get-GateFailureEvents)
 $FailureEvents|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|Export-Clixml (Join-Path $RunEvidence 'failure-events.xml')
 if($FailureEvents.Count-ne0){throw 'Crash or application-failure event detected.'}
 $Dump=Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue
 if($Dump-and$Dump.LastWriteTimeUtc-gt$GateStart.ToUniversalTime()){throw 'A new MEMORY.DMP was generated.'}
 @('Result=PASS','Probe=HLT_EXIT_ROUNDTRIP','Substrate=VMWARE_NESTED','LoaderAdmission=BEFORE_SCM','KernelEvidence=EXTERNAL_KD','ReadinessProcessors=4','CapabilityVersion=1','CapabilityBits=0x00000000000007FF','OperationalIoctls=30','HltExitReason=12','HltExitLength=1',('PrimaryControlsBefore=0x{0:X8}'-f$Before),('PrimaryControlsArmed=0x{0:X8}'-f$Armed),('PrimaryControlsAfter=0x{0:X8}'-f$After),'ControlRestored=TRUE','PersistentRuntime=LOADED','EptRuntime=READY','CombinedReadiness=READY','StatusExit=0','ServiceAfterUnload=ABSENT')|Set-Content $SummaryPath -Encoding utf8
 $Passed=$true
 Write-Host 'PASS: authenticated HLT VM-exit roundtrip completed; qualify the closed KD HLT markers.'
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
