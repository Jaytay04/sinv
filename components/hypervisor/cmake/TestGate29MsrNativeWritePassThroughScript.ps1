param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$LaunchAsm,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)

$ErrorActionPreference = 'Stop'
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Verify([string]$Source, [string]$Launch, [string]$Doc, [string]$CMake) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 29 script does not parse'
    foreach ($Token in @(
        'gate29-msr-native-write-pass-through.ps1','Gate29ProcessorGroups',
        'Invoke-Loader ''load'' ''load''',
        'Invoke-Loader ''msr-transparency-probe'' ''msr-transparency-probe''',
        'IA32_EFER_BEFORE=0x([0-9A-Fa-f]{16})',
        'IA32_EFER_AFTER=0x([0-9A-Fa-f]{16})',
        'Authenticated native IA32_EFER RDMSR/WRMSR/RDMSR completed without VM-exit.',
        '[Convert]::ToUInt64($NativeEferBefore.Groups[1].Value,16)',
        '$NativeEferBefore.Groups[1].Value-ne$NativeEferAfter.Groups[1].Value',
        '-band[uint64]0x500','Probe=MSR_NATIVE_WRITE_PASS_THROUGH',
        'NativeEferStable=1','Invoke-Loader ''unload'' ''unload''',
        '$AfterService.ExitCode-ne1060','Stop-Transcript',
        'tar.exe -a -c -f $ZipPath','if($Failure){throw $Failure}'
    )) {
        Assert-True ($Source.Contains($Token)) "Gate 29 missing token: $Token"
    }
    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
        'Gate 29 mutates signing, boot, SCM, or reboot state'
    $NativeSequence = '(?is)mov ecx, 0C0000080h\s+rdmsr\s+' +
        'mov dword ptr \[rsi \+ VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_BEFORE_OFFSET \+ 0\], eax\s+' +
        'mov dword ptr \[rsi \+ VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_BEFORE_OFFSET \+ 4\], edx\s+' +
        'wrmsr\s+mov ecx, 0C0000080h\s+rdmsr\s+' +
        'mov dword ptr \[rsi \+ VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_AFTER_OFFSET \+ 0\], eax\s+' +
        'mov dword ptr \[rsi \+ VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_AFTER_OFFSET \+ 4\], edx'
    Assert-True ($Launch -match $NativeSequence) `
        'Gate 29 assembly does not preserve the native EFER read/write/read sequence'
    foreach ($Token in @(
        'Gate 29: supervised native MSR write pass-through',
        'gate29-msr-native-write-pass-through.ps1','same-value IA32_EFER write',
        'Run Gate 29 only once'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 29 documentation missing: $Token"
    }
    foreach ($Token in @(
        'test_gate29_msr_native_write_pass_through_script_contracts',
        'TestGate29MsrNativeWritePassThroughScript.ps1',
        'gate29-msr-native-write-pass-through.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 29 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Launch = [IO.File]::ReadAllText((Resolve-Path $LaunchAsm))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))
Verify $Source $Launch $Doc $CMake

$Mutations = @(
    $Source.Replace('-band[uint64]0x500','-band[uint64]0x100'),
    $Source.Replace('$NativeEferBefore.Groups[1].Value-ne$NativeEferAfter.Groups[1].Value','$false'),
    $Source.Replace('Probe=MSR_NATIVE_WRITE_PASS_THROUGH','Probe=MSR_NATIVE_PASS_THROUGH'),
    $Source.Replace('$AfterService.ExitCode-ne1060','$false'),
    $Source.Replace('Stop-Transcript|Out-Null','$TranscriptStarted=$false'),
    $Source.Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath')
)
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 29 vacuous mutation'
    $Rejected = $false
    try { Verify $Mutation $Launch $Doc $CMake } catch { $Rejected = $true }
    Assert-True $Rejected 'Gate 29 accepted a safety mutation'
}

$NativeWritePattern = '(VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_BEFORE_OFFSET \+ 4\], edx\s+)wrmsr'
$LaunchMutation = [regex]::Replace(
    $Launch, $NativeWritePattern, '${1}nop',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase)
Assert-True ($LaunchMutation -ne $Launch) 'Gate 29 native-write mutation was vacuous'
$Rejected = $false
try { Verify $Source $LaunchMutation $Doc $CMake } catch { $Rejected = $true }
Assert-True $Rejected 'Gate 29 accepted removal of the native EFER write'

Write-Host 'PASS: Gate 29 preserves native IA32_EFER write-quadrant proof and clean lifecycle evidence.'
