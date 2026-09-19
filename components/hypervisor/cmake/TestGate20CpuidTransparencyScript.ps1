param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Verify([string]$Source,[string]$Loader,[string]$Doc,[string]$CMake){
 $Tokens=$null;$Errors=$null
 [void][Management.Automation.Language.Parser]::ParseInput($Source,[ref]$Tokens,[ref]$Errors)
 A ($Errors.Count-eq0) 'Gate 20 script does not parse'
 foreach($Token in @(
  'gate20-cpuid-transparency.ps1','Gate20ProcessorGroups','[uint16]$Group',
  '$GroupCount-ne2','$TotalProcessors-ne4','$Recovery.DebugInfoType-ne2',
  '$Recovery.AutoReboot','-not$Recovery.WriteDebugInfo','Start-Transcript',
  '$BeforeService.ExitCode-ne1060','Invoke-Loader ''load'' ''load''',
  'SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status.',
  'INFO: Exact EPT READY status is verified.','Invoke-Loader ''cpuid-probe'' ''cpuid-probe''',
  'SUCCESS: Persistent CPUID transparency verified.',
  'INFO: Authenticated non-root CPUID guest roundtrip completed.',
  'CPUID leaf=0x00000001',
  'CPUID leaf=0x400000FE','CPUID leaf=0x00000007','0x80000000',
  '0x00100000','Invoke-Loader ''unload'' ''unload''',
  '$AfterService.ExitCode-ne1060','Microsoft-Windows-Kernel-Power',
  '$_.Id-eq41','Microsoft-Windows-WHEA-Logger','Application Error',
  '$_.Id-eq1000','C:\Windows\MEMORY.DMP','$FailureEvents.Count-ne0',
  'Result=PASS','Probe=CPUID_TRANSPARENCY','Leaf1Hypervisor=0',
  'HypervisorLeaf=0/0/0/0','Leaf7Cet=0/0','Stop-Transcript',
  'tar.exe -a -c -f $ZipPath','if($Failure){throw $Failure}'
 )){A ($Source.Contains($Token)) "Gate 20 missing token: $Token"}
 A ($Source -notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 20 mutates signing, boot, SCM, or reboot state'
 foreach($Token in @(
  'cpuid-probe','SUCCESS: Persistent CPUID transparency verified.',
  'INFO: Authenticated non-root CPUID guest roundtrip completed.',
  'HYPERVISOR_CPUID_PROBE_REQUEST','IOCTL_HYPERVISOR_CPUID_PROBE',
  'HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE',
  'LoaderCpuidProbeClassify(&leaf1, &hypervisor, &leaf7)'
 )){A ($Loader.Contains($Token)) "Loader CPUID command missing: $Token"}
 A (-not $Loader.Contains('ReadCpuidRegisters')) 'Loader CPUID probe must execute through the authenticated guest boundary'
 foreach($Token in @(
  'Gate 20: persistent CPUID transparency','gate20-cpuid-transparency.ps1',
  'Leaf1Hypervisor=0','HypervisorLeaf=0/0/0/0','Leaf7Cet=0/0',
  'Do not set software breakpoints'
 )){A ($Doc.Contains($Token)) "Gate 20 documentation missing: $Token"}
 foreach($Token in @(
  'test_gate20_cpuid_transparency_script_contracts',
  'TestGate20CpuidTransparencyScript.ps1','gate20-cpuid-transparency.ps1'
 )){A ($CMake.Contains($Token)) "Gate 20 CMake missing: $Token"}
}
$Source=[IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Loader=[IO.File]::ReadAllText((Resolve-Path $LoaderPath))
$Doc=[IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake=[IO.File]::ReadAllText((Resolve-Path $TestsCMake))
Verify $Source $Loader $Doc $CMake
$Mutations=@(
 $Source.Replace('$GroupCount-ne2','$false'),
 $Source.Replace('$Recovery.DebugInfoType-ne2','$false'),
 $Source.Replace('$BeforeService.ExitCode-ne1060','$false'),
 $Source.Replace('Invoke-Loader ''cpuid-probe'' ''cpuid-probe''','Invoke-Loader ''status'' ''status'''),
 $Source.Replace('0x80000000','0x00000000'),
 $Source.Replace('0x00100000','0x00000000'),
 $Source.Replace('$AfterService.ExitCode-ne1060','$false'),
 $Source.Replace('$_.Id-eq41','$_.Id-eq566'),
 $Source.Replace('$FailureEvents.Count-ne0','$false'),
 $Source.Replace('Result=PASS','Result=FAIL'),
 $Source.Replace('Stop-Transcript|Out-Null','$TranscriptStarted=$false'),
 $Source.Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath')
)
foreach($Mutation in $Mutations){
 A ($Mutation-ne$Source) 'Gate 20 vacuous mutation'
 $Rejected=$false
 try{Verify $Mutation $Loader $Doc $CMake}catch{$Rejected=$true}
 A $Rejected 'Gate 20 accepted a safety mutation'
}
Write-Host 'PASS: Gate 20 preserves topology, crash, exact CPUID, unload, service, transcript, and archive gates.'
