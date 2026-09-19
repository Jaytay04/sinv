param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)

$ErrorActionPreference = 'Stop'
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Verify([string]$Source, [string]$Doc, [string]$CMake) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 27 script does not parse'
    foreach ($Token in @(
        'gate27-msr-bitmap-residency.ps1','Gate27ProcessorGroups','[uint16]$Group',
        '$GroupCount-ne2','$TotalProcessors-ne4','$Recovery.DebugInfoType-ne2',
        '$Recovery.AutoReboot','-not$Recovery.WriteDebugInfo','Start-Transcript',
        '$BeforeService.ExitCode-ne1060','Invoke-Loader ''load'' ''load''',
        'SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status.',
        'INFO: Exact EPT READY status is verified.',
        'Invoke-Loader ''msr-transparency-probe'' ''msr-transparency-probe''',
        'MSR Read Probe: status=0x00000000 stage=2',
        'INFO: Authenticated non-root RDMSR guest roundtrip completed.',
        'INFO: Authenticated non-root WRMSR discard roundtrip completed.',
        'Invoke-Loader ''unload'' ''unload''','$AfterService.ExitCode-ne1060',
        'Microsoft-Windows-Kernel-Power','$_.Id-eq41',
        'Microsoft-Windows-WHEA-Logger','Application Error','$_.Id-eq1000',
        'C:\Windows\MEMORY.DMP','$FailureEvents.Count-ne0',
        'Result=PASS','Probe=MSR_BITMAP_RESIDENCY','ContextResources=63',
        'FeatureControl=1','VmxBasic=0','UserCet=0','WriteDiscard=1',
        'ServiceAfterUnload=ABSENT','Stop-Transcript',
        'tar.exe -a -c -f $ZipPath','if($Failure){throw $Failure}'
    )) {
        Assert-True ($Source.Contains($Token)) "Gate 27 missing token: $Token"
    }
    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 27 mutates signing, boot, SCM, or reboot state'
    foreach ($Token in @(
        'Gate 27: supervised selective MSR-bitmap residency',
        'gate27-msr-bitmap-residency.ps1','ContextResources=63',
        'Do not set software breakpoints','Run Gate 27 only once'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 27 documentation missing: $Token"
    }
    foreach ($Token in @(
        'test_gate27_msr_bitmap_residency_script_contracts',
        'TestGate27MsrBitmapResidencyScript.ps1',
        'gate27-msr-bitmap-residency.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 27 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))
Verify $Source $Doc $CMake

$Mutations = @(
    $Source.Replace('$GroupCount-ne2','$false'),
    $Source.Replace('$Recovery.DebugInfoType-ne2','$false'),
    $Source.Replace('$BeforeService.ExitCode-ne1060','$false'),
    $Source.Replace("Invoke-Loader 'msr-transparency-probe' 'msr-transparency-probe'","Invoke-Loader 'status' 'status'"),
    $Source.Replace('$AfterService.ExitCode-ne1060','$false'),
    $Source.Replace('$_.Id-eq41','$_.Id-eq566'),
    $Source.Replace('$FailureEvents.Count-ne0','$false'),
    $Source.Replace('Probe=MSR_BITMAP_RESIDENCY','Probe=MSR_READ_WRITE_TRANSPARENCY'),
    $Source.Replace('ContextResources=63','ContextResources=31'),
    $Source.Replace('Stop-Transcript|Out-Null','$TranscriptStarted=$false'),
    $Source.Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath')
)
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 27 vacuous mutation'
    $Rejected = $false
    try { Verify $Mutation $Doc $CMake } catch { $Rejected = $true }
    Assert-True $Rejected 'Gate 27 accepted a safety mutation'
}

Write-Host 'PASS: Gate 27 preserves one-cycle topology, dump, exact MSR-bitmap residency, unload, service, transcript, and archive gates.'
