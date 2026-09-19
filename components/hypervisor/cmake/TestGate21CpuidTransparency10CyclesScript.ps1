param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$Gate20Path,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Ordered(
    [string]$Source,
    [string[]]$Tokens,
    [string]$Name
) {
    $Last = -1
    foreach ($Token in $Tokens) {
        $At = $Source.IndexOf(
            $Token, $Last + 1, [StringComparison]::Ordinal)
        Assert-True ($At -gt $Last) "$Name order changed at: $Token"
        $Last = $At
    }
}

function Verify([string]$Source, [string]$Doc, [string]$CMake) {
    $Tokens = $null
    $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Source, [ref]$Tokens, [ref]$Errors)
    Assert-True ($Errors.Count -eq 0) 'Gate 21 script does not parse'

    foreach ($Token in @(
        '$TotalCycles = 10',
        '$Gate20Script = Join-Path $Stage ''gate20-cpuid-transparency.ps1''',
        'Gate21ProcessorGroups',
        '[uint16]$Group',
        '$GroupCount -ne 2',
        '$TotalProcessors -ne 4',
        '$Recovery.DebugInfoType -ne 2',
        '$Recovery.AutoReboot',
        '-not $Recovery.WriteDebugInfo',
        'Start-Transcript',
        '$Cycle -le $TotalCycles',
        'Save-WrapperServiceState $CycleRoot ''service-before-wrapper''',
        '$BeforeService.ExitCode -ne 1060',
        'Start-Process @StartArguments',
        '$Process.ExitCode -ne 0',
        '$InnerSummaries.Count -ne 1',
        '''Result=PASS''',
        '''Probe=CPUID_TRANSPARENCY''',
        '''Leaf1Hypervisor=0''',
        '''HypervisorLeaf=0/0/0/0''',
        '''Leaf7Cet=0/0''',
        '''ServiceAfterUnload=ABSENT''',
        '$ProbeOutputs.Count -ne 1',
        '''CPUID Probe: status=0x00000000 stage=2''',
        '''SUCCESS: Persistent CPUID transparency verified.''',
        '''INFO: Authenticated non-root CPUID guest roundtrip completed.''',
        'Save-WrapperServiceState $CycleRoot ''service-after-wrapper''',
        '$AfterService.ExitCode -ne 1060',
        'Get-CycleFailureEvents $CycleStart',
        '''Microsoft-Windows-Kernel-Power''',
        '$_.Id -eq 41',
        '''Microsoft-Windows-WHEA-Logger''',
        '$_.Level -le 2',
        '''Application Error''',
        '$_.Id -eq 1000',
        '''(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b''',
        '$FailureEvents.Count -ne 0',
        'C:\Windows\MEMORY.DMP',
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        'PassedCycles=$PassedCycles',
        'Stop-Transcript',
        'tar.exe -a -c -f $ZipPath',
        'throw $Failure'
    )) {
        Assert-True ($Source.Contains($Token)) "Gate 21 missing token: $Token"
    }

    Assert-Ordered $Source @(
        '$BeforeService.ExitCode -ne 1060',
        'Start-Process @StartArguments',
        '$Process.ExitCode -ne 0',
        '$InnerSummaries.Count -ne 1',
        '''Result=PASS''',
        '''Probe=CPUID_TRANSPARENCY''',
        '''Leaf1Hypervisor=0''',
        '''HypervisorLeaf=0/0/0/0''',
        '''Leaf7Cet=0/0''',
        '''ServiceAfterUnload=ABSENT''',
        '$ProbeOutputs.Count -ne 1',
        '''CPUID Probe: status=0x00000000 stage=2''',
        '''INFO: Authenticated non-root CPUID guest roundtrip completed.''',
        '$AfterService.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        '$PassedCycles++'
    ) 'Gate 21 cycle'

    Assert-True (
        ([regex]::Matches(
            $Source, '(?i)hypervisor_loader\\\.exe')).Count -eq 1
    ) 'Gate 21 invokes the loader directly or lost its crash-event scope'

    Assert-True ($Source -notmatch
        '(?i)hypervisor_loader(?!\\\.exe)') 'Gate wrapper contains an unscoped direct loader reference'

    Assert-True ($Source -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'Gate 21 bypasses Gate 20 or mutates signing, boot, SCM, or reboot state'

    foreach ($Token in @(
        'Gate 21: 10-cycle persistent CPUID transparency',
        'gate21-cpuid-transparency-10cycles.ps1',
        'PassedCycles=10',
        'accepted Gate 20',
        'do not retry LOAD'
    )) {
        Assert-True ($Doc.Contains($Token)) "Gate 21 documentation missing: $Token"
    }

    foreach ($Token in @(
        'test_gate21_cpuid_transparency_10cycles_script_contracts',
        'TestGate21CpuidTransparency10CyclesScript.ps1',
        'gate21-cpuid-transparency-10cycles.ps1'
    )) {
        Assert-True ($CMake.Contains($Token)) "Gate 21 CMake missing: $Token"
    }
}

$Source = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Gate20 = [IO.File]::ReadAllText((Resolve-Path $Gate20Path))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))
$CMake = [IO.File]::ReadAllText((Resolve-Path $TestsCMake))

foreach ($Token in @(
    '''Result=PASS''',
    '''Probe=CPUID_TRANSPARENCY''',
    '''Leaf1Hypervisor=0''',
    '''HypervisorLeaf=0/0/0/0''',
    '''Leaf7Cet=0/0''',
    '''ServiceAfterUnload=ABSENT''',
    '''SUCCESS: Persistent CPUID transparency verified.''',
    '''INFO: Authenticated non-root CPUID guest roundtrip completed.''',
    'exit 1'
)) {
    Assert-True ($Gate20.Contains($Token)) "Gate 20 no longer proves: $Token"
}

Verify $Source $Doc $CMake

$Mutations = @(
    $Source.Replace('$TotalCycles = 10', '$TotalCycles = 1'),
    $Source.Replace(
        '$Gate20Script = Join-Path $Stage ''gate20-cpuid-transparency.ps1''',
        '$Gate20Script = Join-Path $Stage ''gate14-cr3-probe-diagnostic.ps1'''),
    $Source.Replace('$GroupCount -ne 2', '$false'),
    $Source.Replace('$Recovery.DebugInfoType -ne 2', '$false'),
    $Source.Replace('$BeforeService.ExitCode -ne 1060', '$false'),
    $Source.Replace('$Process.ExitCode -ne 0', '$false'),
    $Source.Replace('$InnerSummaries.Count -ne 1', '$false'),
    $Source.Replace('''Result=PASS''', '''Result=FAIL'''),
    $Source.Replace('''Probe=CPUID_TRANSPARENCY''', '''Probe=CPUID_NATIVE'''),
    $Source.Replace('''Leaf1Hypervisor=0''', '''Leaf1Hypervisor=1'''),
    $Source.Replace('''HypervisorLeaf=0/0/0/0''', '''HypervisorLeaf=1/0/0/0'''),
    $Source.Replace('''Leaf7Cet=0/0''', '''Leaf7Cet=1/0'''),
    $Source.Replace('''ServiceAfterUnload=ABSENT''', '''ServiceAfterUnload=PRESENT'''),
    $Source.Replace('$ProbeOutputs.Count -ne 1', '$false'),
    $Source.Replace('''CPUID Probe: status=0x00000000 stage=2''', '''CPUID Probe: status=0x00000000 stage=0'''),
    $Source.Replace('$AfterService.ExitCode -ne 1060', '$false'),
    $Source.Replace('$_.Id -eq 41', '$_.Id -eq 566'),
    $Source.Replace('$_.Id -eq 1000', '$_.Id -eq 1001'),
    $Source.Replace('$FailureEvents.Count -ne 0', '$false'),
    $Source.Replace(
        '$Dump.LastWriteTimeUtc -gt $CycleStart.ToUniversalTime()',
        '$false'),
    $Source.Replace(
        'Stop-Transcript | Out-Null',
        '$TranscriptStarted = $false'),
    $Source.Replace(
        'tar.exe -a -c -f $ZipPath',
        'tar.exe -tf $ZipPath')
)

foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Source) 'Gate 21 vacuous mutation'
    $Rejected = $false
    try {
        Verify $Mutation $Doc $CMake
    }
    catch {
        $Rejected = $true
    }
    Assert-True $Rejected 'Gate 21 accepted a safety mutation'
}

Write-Host 'PASS: Gate 21 repeats the exact Gate 20 CPUID transparency transaction 10 times with fresh-state, exact-result, topology, crash, service, transcript, and archive gates.'
