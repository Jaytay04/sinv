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
    Assert-True ($Errors.Count -eq 0) 'Gate 28 script does not parse'
    foreach ($Token in @(
        'gate28-msr-native-pass-through.ps1','Gate28ProcessorGroups',
        'Invoke-Loader ''load'' ''load''',
        'Invoke-Loader ''msr-transparency-probe'' ''msr-transparency-probe''',
        'IA32_EFER=0x([0-9A-Fa-f]{16})',
        'Authenticated native IA32_EFER RDMSR completed without VM-exit.',
        '[Convert]::ToUInt64($NativeEfer.Groups[1].Value,16)',
        '-band[uint64]0x500','Probe=MSR_NATIVE_PASS_THROUGH',
        'NativeEferLongMode=1','Invoke-Loader ''unload'' ''unload''',
        '$AfterService.ExitCode-ne1060','Stop-Transcript',
        'tar.exe -a -c -f $ZipPath','if($Failure){throw $Failure}'
    )) {
        Assert-True ($Source.Contains($Token)) "Gate 28 missing token: $Token"
    }
    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
        'Gate 28 mutates signing, boot, SCM, or reboot state'
    foreach ($Token in @(
        'Gate 28: supervised native MSR pass-through',
        'gate28-msr-native-pass-through.ps1','IA32_EFER',
        'Run Gate 28 only once'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 28 documentation missing: $Token"
    }
    foreach ($Token in @(
        'test_gate28_msr_native_pass_through_script_contracts',
        'TestGate28MsrNativePassThroughScript.ps1',
        'gate28-msr-native-pass-through.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 28 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))
Verify $Source $Doc $CMake

$Mutations = @(
    $Source.Replace('-band[uint64]0x500','-band[uint64]0x100'),
    $Source.Replace('Probe=MSR_NATIVE_PASS_THROUGH','Probe=MSR_BITMAP_RESIDENCY'),
    $Source.Replace('$AfterService.ExitCode-ne1060','$false'),
    $Source.Replace('Stop-Transcript|Out-Null','$TranscriptStarted=$false'),
    $Source.Replace('tar.exe -a -c -f $ZipPath','tar.exe -tf $ZipPath')
)
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 28 vacuous mutation'
    $Rejected = $false
    try { Verify $Mutation $Doc $CMake } catch { $Rejected = $true }
    Assert-True $Rejected 'Gate 28 accepted a safety mutation'
}

Write-Host 'PASS: Gate 28 preserves the native IA32_EFER no-exit proof and clean lifecycle evidence.'
