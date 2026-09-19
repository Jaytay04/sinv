[CmdletBinding()]
param([string]$Stage='C:\V3H',[string]$EvidenceRoot='C:\V3H\evidence')
$ErrorActionPreference='Stop'
$Driver=Join-Path $Stage 'hypervisor_driver.sys'
$Loader=Join-Path $Stage 'hypervisor_loader.exe'
$GateScript=Join-Path $Stage 'gate36-vpid-activation.ps1'
$Required=@($Driver,(Join-Path $Stage 'hypervisor_driver.pdb'),$Loader,
 (Join-Path $Stage 'hypervisor_loader.pdb'),$GateScript)
$Missing=@($Required|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)})
if($Missing.Count-ne0){throw "Missing staged artifact(s): $($Missing -join ', ')"}
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=[Security.Principal.WindowsPrincipal]::new($Identity)
if(-not$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run from elevated Windows PowerShell in the disposable test VM.'}
if(-not('Gate36ProcessorGroups'-as[type])){
 Add-Type @'
using System.Runtime.InteropServices;
public static class Gate36ProcessorGroups {
 [DllImport("kernel32.dll")] public static extern ushort GetActiveProcessorGroupCount();
 [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}
$RunName='gate36-vpid-activation-'+(Get-Date -Format 'yyyyMMdd-HHmmss')
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
 [pscustomobject]@{ExitCode=[int]$Process.ExitCode;Stdout=$OutText;Stderr=$ErrText}
}
function Get-GateFailureEvents{
 $System=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$GateStart} -ErrorAction SilentlyContinue|Where-Object{
  ($_.ProviderName-eq'Microsoft-Windows-Kernel-Power'-and$_.Id-eq41-and$_.Level-le2)-or
  ($_.ProviderName-eq'Microsoft-Windows-WHEA-Logger'-and$_.Level-le2)})
 $Application=@(Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$GateStart} -ErrorAction SilentlyContinue|Where-Object{
  $_.ProviderName-eq'Application Error'-and$_.Id-eq1000-and$_.Level-le2-and$_.Message-match'(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b'})
 @($System+$Application)
}
New-Item -ItemType Directory -Force -Path $RunEvidence|Out-Null
try{
 Start-Transcript -LiteralPath $TranscriptPath -Force|Out-Null;$TranscriptStarted=$true
 "GateStart=$($GateStart.ToString('o'))";"Stage=$Stage";"RunEvidence=$RunEvidence"
 $Signature=Get-AuthenticodeSignature -LiteralPath $Driver
 $Signature|Select-Object Status,StatusMessage,@{Name='Signer';Expression={$_.SignerCertificate.Subject}}|Format-List
 if($null-eq$Signature.SignerCertificate-or$Signature.SignerCertificate.Subject-ne'CN=hypersinv'-or
  ($Signature.Status-ne[Management.Automation.SignatureStatus]::Valid-and$Signature.Status-ne[Management.Automation.SignatureStatus]::UnknownError)){
  throw 'Driver is not signed by the expected CN=hypersinv test certificate.'
 }
 Get-ComputerInfo|Select-Object WindowsProductName,WindowsVersion,OsBuildNumber|Format-List
 Get-CimInstance Win32_ComputerSystem|Select-Object Manufacturer,Model,NumberOfLogicalProcessors|Format-List
 $GroupCount=[Gate36ProcessorGroups]::GetActiveProcessorGroupCount()
 $GroupSizes=@(for($Group=0;$Group-lt$GroupCount;$Group++){[pscustomobject]@{Group=$Group;ActiveProcessors=[Gate36ProcessorGroups]::GetActiveProcessorCount([uint16]$Group)}})
 $GroupSizes|Format-Table -AutoSize
 $TotalProcessors=($GroupSizes|Measure-Object ActiveProcessors -Sum).Sum
 if($GroupCount-ne2-or$TotalProcessors-ne4-or@($GroupSizes|Where-Object ActiveProcessors-ne2).Count-ne0){throw "Expected 2 groups with 2 processors each; groups=$GroupCount total=$TotalProcessors."}
 $Recovery=Get-CimInstance Win32_OSRecoveryConfiguration
 $Recovery|Select-Object DebugInfoType,AutoReboot,WriteDebugInfo,DebugFilePath,OverwriteExistingDebugFile|Format-List
 if($Recovery.DebugInfoType-ne2-or$Recovery.AutoReboot-or-not$Recovery.WriteDebugInfo){throw 'Crash-dump configuration is incorrect.'}
 bcdedit.exe /enum '{current}'
 Get-Item -LiteralPath $Required|Select-Object FullName,Length,LastWriteTime|Format-List
 Get-FileHash -LiteralPath $Required -Algorithm SHA256|Format-List Path,Hash
 $Before=Save-ServiceState 'service-before'
 if($Before.ExitCode-ne1060){throw "Service was present before LOAD (exit $($Before.ExitCode))."}
 $Load=Invoke-Loader 'load' 'load'
 $LoadProof="SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
 if($Load.ExitCode-ne0-or-not$Load.Stdout.Contains($LoadProof)-or-not$Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')){throw "LOAD was not verified (exit $($Load.ExitCode)). STOP; do not retry or unload manually."}
 $AfterLoad=Save-ServiceState 'service-after-load'
 if($AfterLoad.ExitCode-ne0-or-not$AfterLoad.Output.Contains('STATE              : 4  RUNNING')){throw 'Driver service was not running after LOAD. STOP; do not unload manually.'}
 $Vpid=Invoke-Loader 'vpid-status' 'vpid-status'
 if($Vpid.ExitCode-eq2-and$Vpid.Stderr.Contains('VPID activation is unsupported by the current VMX control surface.')){
  $Unload=Invoke-Loader 'unload' 'unsupported-unload'
  if($Unload.ExitCode-ne0-or-not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')){throw 'ENVIRONMENT_UNSUPPORTED and canonical cleanup failed. STOP; do not retry.'}
  $AfterUnsupported=Save-ServiceState 'service-after-unsupported-unload'
  if($AfterUnsupported.ExitCode-ne1060){throw 'ENVIRONMENT_UNSUPPORTED and service removal was not confirmed. STOP; do not retry.'}
  throw 'ENVIRONMENT_UNSUPPORTED: the VM exposes INVVPID but not the VMCS VPID control surface. Use a VPID-capable test environment.'
 }
 $CpuLines=@($Vpid.Stdout-split"`r?`n"|Where-Object{$_-like'VPID CPU: *'})
 if($Vpid.ExitCode-ne0-or-not$Vpid.Stdout.Contains('SUCCESS: Exact ACTIVE VPID state verified on 4 processors.')-or$CpuLines.Count-ne4){throw 'Exact four-processor ACTIVE VPID proof failed. STOP; preserve the VM and WinDbg state.'}
 $Vpids=@($CpuLines|ForEach-Object{if($_-notmatch'index=(\d+).*vpid=(\d+).*observed=(\d+)'){throw "Malformed VPID record: $_"};[uint32]$Matches[2]})
 if(@($Vpids|Where-Object{$_-eq0}).Count-ne0-or@($Vpids|Select-Object -Unique).Count-ne4){throw 'VPID zero or alias detected. STOP; preserve the VM and WinDbg state.'}
 $Unload=Invoke-Loader 'unload' 'unload'
 if($Unload.ExitCode-ne0-or-not$Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')){throw "UNLOAD was not verified (exit $($Unload.ExitCode)). STOP; do not retry."}
 $AfterUnload=Save-ServiceState 'service-after-unload'
 if($AfterUnload.ExitCode-ne1060){throw "Service removal was not confirmed (exit $($AfterUnload.ExitCode))."}
 $FailureEvents=@(Get-GateFailureEvents)
 $FailureEvents|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message|Export-Clixml (Join-Path $RunEvidence 'failure-events.xml')
 if($FailureEvents.Count-ne0){throw 'Crash or application-failure event detected.'}
 $Dump=Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue
 if($Dump-and$Dump.LastWriteTimeUtc-gt$GateStart.ToUniversalTime()){throw 'A new MEMORY.DMP was generated.'}
 @('Result=PASS','Probe=VPID_ACTIVATION','Processors=4','Groups=2','Vpids=1,2,3,4','LiveVmcsVpid=VERIFIED','InvvpidBeforeActivation=VERIFIED_BY_RUNTIME','InvvpidBeforeReuse=VERIFIED_BY_UNLOAD','ServiceAfterUnload=ABSENT')|Set-Content $SummaryPath -Encoding utf8
 $Passed=$true
 Write-Host 'PASS: VPID activation, live VMCS identity, invalidation lifecycle, canonical unload, and service absence were verified.'
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
