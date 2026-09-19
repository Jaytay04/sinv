[CmdletBinding()]
param([string]$Stage='C:\V3H',[string]$EvidenceRoot='C:\V3H\evidence')
$ErrorActionPreference='Stop'
# Operator-only native INVLPG transparency diagnostic. Never run on a host.
$Driver=Join-Path $Stage 'hypervisor_driver.sys'
$DriverPdb=Join-Path $Stage 'hypervisor_driver.pdb'
$Loader=Join-Path $Stage 'hypervisor_loader.exe'
$LoaderPdb=Join-Path $Stage 'hypervisor_loader.pdb'
$GateScript=Join-Path $Stage 'gate31-invlpg-native-pass-through.ps1'
$Required=@($Driver,$DriverPdb,$Loader,$LoaderPdb,$GateScript)
$Missing=@($Required|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)})
if($Missing.Count-ne0){throw "Missing staged artifact(s): $($Missing -join ', ')"}
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=[Security.Principal.WindowsPrincipal]::new($Identity)
if(-not$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 throw 'Run this script from an elevated Windows PowerShell session in the test VM.'
}
if(-not('Gate31ProcessorGroups'-as[type])){
 Add-Type @'
using System.Runtime.InteropServices;
public static class Gate31ProcessorGroups {
 [DllImport("kernel32.dll")] public static extern ushort GetActiveProcessorGroupCount();
 [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}
$RunName='gate31-invlpg-native-pass-through-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
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
 $Options=@{FilePath=$Loader;ArgumentList=$Arguments;Wait=$true;PassThru=$true;NoNewWindow=$true;RedirectStandardOutput=$Stdout;RedirectStandardError=$Stderr}
 $Process=Start-Process @Options
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
 $GroupCount=[Gate31ProcessorGroups]::GetActiveProcessorGroupCount()
 $GroupSizes=@(for($Group=0;$Group-lt$GroupCount;$Group++){[pscustomobject]@{Group=$Group;ActiveProcessors=[Gate31ProcessorGroups]::GetActiveProcessorCount([uint16]$Group)}})
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
 Get-AuthenticodeSignature -LiteralPath $Driver|Select-Object Status,StatusMessage,@{Name='Signer';Expression={$_.SignerCertificate.Subject}}|Format-List
 $BeforeService=Save-ServiceState 'service-before'
 if($BeforeService.ExitCode-ne1060){throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."}
 $Load=Invoke-Loader 'load' 'load'
 $RuntimeProof="SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
 if($Load.ExitCode-ne0-or-not$Load.Stdout.Contains($RuntimeProof)-or-not$Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')){
  throw "LOAD was not verified (exit $($Load.ExitCode)). Stop; do not retry."
 }
 Save-ServiceState 'service-after-load'|Out-Null
 $ProbeFailure=$null
 $Probe=Invoke-Loader 'invlpg-probe' 'invlpg-probe'
 $PrimaryControls=[regex]::Match($Probe.Stdout,'PRIMARY_CONTROLS=0x([0-9A-Fa-f]{8})')
 $InvlpgCompletion=[regex]::Match($Probe.Stdout,'INVLPG_COMPLETION=0x([0-9A-Fa-f]{8})')
 if($Probe.ExitCode-ne0-or
    -not$Probe.Stdout.Contains('INVLPG Probe: status=0x00000000 stage=1')-or
    -not$Probe.Stdout.Contains('SUCCESS: Persistent native INVLPG execution and live VMCS transparency verified.')-or
    -not$PrimaryControls.Success-or-not$InvlpgCompletion.Success){
  $ProbeFailure="INVLPG command was not verified (exit $($Probe.ExitCode))."
 }else{
  $PrimaryControlsValue=[Convert]::ToUInt32($PrimaryControls.Groups[1].Value,16)
  $InvlpgCompletionValue=[Convert]::ToUInt32($InvlpgCompletion.Groups[1].Value,16)
  if(($PrimaryControlsValue -band 0x00000200)-ne0-or
     $InvlpgCompletionValue-ne0x494E564C){
   $ProbeFailure='Live VMCS INVLPG transparency contract failed.'
  }
 }
  $Unload=Invoke-Loader 'unload' 'unload'
 if($Unload.ExitCode-ne0-or-not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')){
  throw "UNLOAD was not verified (exit $($Unload.ExitCode)). Stop; do not retry."
 }
 $AfterService=Save-ServiceState 'service-after-unload'
 if($AfterService.ExitCode-ne1060){throw "Service removal was not confirmed (exit $($AfterService.ExitCode))."}
 if($ProbeFailure){throw "$ProbeFailure Runtime unload completed cleanly."}
 $FailureEvents=@(Get-GateFailureEvents)
 $FailureEvents|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|Export-Clixml (Join-Path $RunEvidence 'failure-events.xml')
 if($FailureEvents.Count-ne0){throw 'Crash or application-failure event detected.'}
 $Dump=Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue
 if($Dump-and$Dump.LastWriteTimeUtc-gt$GateStart.ToUniversalTime()){throw 'A new MEMORY.DMP was generated.'}
 @('Result=PASS','Probe=INVLPG_NATIVE_PASS_THROUGH','ContextResources=63',"PrimaryControls=0x$($PrimaryControls.Groups[1].Value.ToUpperInvariant())","InvlpgCompletion=0x$($InvlpgCompletion.Groups[1].Value.ToUpperInvariant())",'InvlpgExit=0','GuestRoundtrip=1','ServiceAfterUnload=ABSENT')|Set-Content $SummaryPath -Encoding utf8
 $Passed=$true
 Write-Host 'PASS: native INVLPG transparency and clean unload were verified.'
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
