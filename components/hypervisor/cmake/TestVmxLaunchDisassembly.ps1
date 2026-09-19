param(
    [Parameter(Mandatory = $true)]
    [string]$LinkerPath,

    [Parameter(Mandatory = $true)]
    [string]$ObjectPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $LinkerPath -PathType Leaf)) {
    throw "MSVC linker is unavailable: $LinkerPath"
}
if (-not (Test-Path -LiteralPath $ObjectPath -PathType Leaf)) {
    throw "MASM object is unavailable: $ObjectPath"
}

$dump = & $LinkerPath /dump /disasm $ObjectPath 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "MSVC disassembly failed ($LASTEXITCODE): $($dump -join [Environment]::NewLine)"
}
$disassembly = $dump -join "`n"

function Get-SymbolBlock {
    param([string]$Name)

    $token = "${Name}:"
    $start = $disassembly.IndexOf($token, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "Disassembly symbol is missing: $Name"
    }

    $tail = $disassembly.Substring($start + $token.Length)
    $next = [regex]::Match($tail, '(?m)^\w+:\r?$')
    if ($next.Success) {
        return $tail.Substring(0, $next.Index)
    }
    return $tail
}

function Get-Instructions {
    param([string]$Block)

    $instructions = @()
    foreach ($line in ($Block -split "`n")) {
        if ($line -match '^\s{2}([0-9A-F]+):\s+([0-9A-F]{2}(?:\s+[0-9A-F]{2})*)\s{2,}(.+)$') {
            $instructions += [pscustomobject]@{
                Address = $Matches[1]
                Text = (($Matches[3] -replace '\s+', ' ').Trim())
            }
        }
    }
    return $instructions
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Exact {
    param($Instructions, [int]$Index, [string]$Expected, [string]$Contract)
    Assert-True ($Index -lt $Instructions.Count) "$Contract (missing instruction $Index)"
    Assert-True ($Instructions[$Index].Text -ceq $Expected) `
        "$Contract (expected '$Expected', got '$($Instructions[$Index].Text)')"
}

function Assert-Pattern {
    param($Instructions, [int]$Index, [string]$Pattern, [string]$Contract)
    Assert-True ($Index -lt $Instructions.Count) "$Contract (missing instruction $Index)"
    Assert-True ($Instructions[$Index].Text -cmatch $Pattern) `
        "$Contract (unexpected '$($Instructions[$Index].Text)')"
}

function Find-UniqueInstruction {
    param($Instructions, [string]$Text, [string]$Contract)

    $matches = @()
    for ($index = 0; $index -lt $Instructions.Count; $index++) {
        if ($Instructions[$index].Text -ceq $Text) {
            $matches += $index
        }
    }
    Assert-True ($matches.Count -eq 1) `
        "$Contract (expected one '$Text', found $($matches.Count))"
    return $matches[0]
}

function Find-Instructions {
    param($Instructions, [string]$Text)

    $matches = @()
    for ($index = 0; $index -lt $Instructions.Count; $index++) {
        if ($Instructions[$index].Text -ceq $Text) {
            $matches += $index
        }
    }
    return $matches
}

function Find-InstructionByAddress {
    param($Instructions, [string]$Address, [string]$Contract)

    for ($index = 0; $index -lt $Instructions.Count; $index++) {
        if ($Instructions[$index].Address -ceq $Address) {
            return $index
        }
    }
    throw "$Contract (target address $Address is missing)"
}

function Get-BranchTarget {
    param($Instruction, [string]$Mnemonic, [string]$Contract)

    if ($Instruction.Text -cnotmatch "^${Mnemonic} ([0-9A-F]+)$") {
        throw "$Contract (unexpected '$($Instruction.Text)')"
    }
    return $Matches[1]
}

function Assert-BranchDestination {
    param($Instructions, [int]$Index, [string]$Mnemonic,
          [int]$ExpectedIndex, [string]$Contract)

    $target = Get-BranchTarget $Instructions[$Index] $Mnemonic $Contract
    $actualIndex = Find-InstructionByAddress $Instructions $target $Contract
    Assert-True ($actualIndex -eq $ExpectedIndex) "$Contract (expected target $($Instructions[$ExpectedIndex].Address), got $target)"
}

$mutatedBranch = @(
    [pscustomobject]@{ Address = '0000'; Text = 'jmp 0000' },
    [pscustomobject]@{ Address = '0001'; Text = 'nop' }
)
$mutationRejected = $false
try {
    Assert-BranchDestination $mutatedBranch 0 'jmp' 1 'Wrong-target mutation'
} catch {
    $mutationRejected = $_.Exception.Message.StartsWith('Wrong-target mutation')
}
Assert-True $mutationRejected 'Branch verifier must reject a mutated destination'

$allInstructions = @(Get-Instructions $disassembly)
Assert-True (@($allInstructions | Where-Object Text -CEQ 'vmresume').Count -eq 1) 'Built MASM object must contain exactly one VMRESUME'
Assert-True (@($allInstructions | Where-Object Text -CEQ 'vmxoff').Count -eq 0) 'Built MASM object must contain no VMXOFF; C owns final teardown'
$exitActionSentinelStores = @($allInstructions | Where-Object { $_.Text -cmatch '^mov dword ptr \[[^]]+11Ch\],3$' })
Assert-True ($exitActionSentinelStores.Count -eq 0) 'ExitAction must never receive an internal sentinel value'
$launch = @(Get-Instructions (Get-SymbolBlock 'VmxAsmLaunch'))
Assert-True ($launch.Count -ge 11) 'VmxAsmLaunch disassembly is truncated'
$launchPrefix = @(
    'pushfq',
    'pop r10',
    'test rcx,rcx'
)
for ($index = 0; $index -lt $launchPrefix.Count; $index++) {
    Assert-Exact $launch $index $launchPrefix[$index] `
        'VmxAsmLaunch must capture RFLAGS before validation'
}
Assert-Pattern $launch 3 '^je [0-9A-F]+$' 'Null validation must branch after RFLAGS capture'
Assert-Exact $launch 4 'mov rax,564D584C41554E43h' 'Magic validation setup changed'
Assert-Exact $launch 5 'cmp qword ptr [rcx],rax' 'Magic validation comparison changed'
Assert-Pattern $launch 6 '^jne [0-9A-F]+$' 'Magic validation must reject before context writes'
Assert-Exact $launch 7 'mov eax,160h' 'Context size validation setup changed'
Assert-Exact $launch 8 'cmp dword ptr [rcx+8],eax' 'Context size validation comparison changed'
Assert-Pattern $launch 9 '^jne [0-9A-F]+$' 'Context size validation must reject before writes'
Assert-Exact $launch 10 'mov eax,2' 'ABI version validation setup changed'
Assert-Exact $launch 11 'cmp dword ptr [rcx+0Ch],eax' 'ABI version validation comparison changed'
Assert-Pattern $launch 12 '^jne [0-9A-F]+$' 'ABI version validation must reject before writes'
Assert-Exact $launch 13 'mov r11,rcx' 'Validated context must become the private pointer'
Assert-Exact $launch 14 'mov qword ptr [r11+0A0h],rsp' 'Native RSP capture changed'
Assert-Exact $launch 15 'mov qword ptr [r11+0B8h],rsp' 'Guest RSP capture changed'
Assert-Exact $launch 16 'mov qword ptr [r11+0A8h],r10' `
    'Stored RFLAGS must be the entry snapshot'
[void](Find-UniqueInstruction $launch 'pushfq' 'RFLAGS capture must not be duplicated')
[void](Find-UniqueInstruction $launch 'pop r10' 'RFLAGS snapshot register must be unique')
[void](Find-UniqueInstruction $launch 'mov qword ptr [r11+0A8h],r10' `
    'Pre-validation RFLAGS store must be unique')

$vmwriteIndices = @(Find-Instructions $launch 'vmwrite rcx,rax')
Assert-True ($vmwriteIndices.Count -eq 2) 'VmxAsmLaunch must contain exactly two dynamic guest-state VMWRITEs'
Assert-True (@(Find-Instructions $launch 'vmwrite rax,rcx').Count -eq 0) `
    'Dynamic guest-state VMWRITE operand order must not be reversed'
$rspWriteIndex = $vmwriteIndices[0]
$rflagsWriteIndex = $vmwriteIndices[1]
$sgdtIndex = Find-UniqueInstruction $launch 'sgdt tbyte ptr [r11+140h]' 'GDTR capture must be unique'
$sidtIndex = Find-UniqueInstruction $launch 'sidt tbyte ptr [r11+150h]' 'IDTR capture must be unique'
Assert-True ($sgdtIndex -gt 16 -and $sgdtIndex -lt $sidtIndex -and $sidtIndex -lt $rspWriteIndex) `
    'Descriptor-table capture must follow native snapshot and precede VMWRITE'
$vmlaunchIndex = Find-UniqueInstruction $launch 'vmlaunch' 'VMLAUNCH site must be unique'

$rspWriteSequence = @(
    'mov ecx,681Ch',
    'mov rax,qword ptr [r11+0B8h]',
    'vmwrite rcx,rax',
    '^jb [0-9A-F]+$',
    '^je [0-9A-F]+$',
    'mov dword ptr [r11+128h],1'
)
$rflagsWriteSequence = @(
    'mov ecx,6820h',
    'mov rax,qword ptr [r11+0A8h]',
    'vmwrite rcx,rax',
    '^jb [0-9A-F]+$',
    '^je [0-9A-F]+$',
    'mov dword ptr [r11+134h],1',
    'vmlaunch'
)
$writeSequences = @(
    [pscustomobject]@{ Expected = $rspWriteSequence; Start = $rspWriteIndex - 2 },
    [pscustomobject]@{ Expected = $rflagsWriteSequence; Start = $rflagsWriteIndex - 2 }
)
foreach ($sequence in $writeSequences) {
    $expectedSequence = $sequence.Expected
    $start = $sequence.Start
    for ($offset = 0; $offset -lt $expectedSequence.Count; $offset++) {
        $expected = $expectedSequence[$offset]
        if ($expected.StartsWith('^')) {
            Assert-Pattern $launch ($start + $offset) $expected 'Dynamic guest-state VMWRITE control flow changed'
        } else {
            Assert-Exact $launch ($start + $offset) $expected 'Dynamic guest-state VMWRITE sequence changed'
        }
    }
}
Assert-True ($rspWriteIndex -lt $rflagsWriteIndex -and $rflagsWriteIndex -lt $vmlaunchIndex) 'Guest RSP and RFLAGS must be the final ordered VMCS writes before VMLAUNCH'

$writeOutcomeResets = @(
    'mov dword ptr [r11+128h],0', 'mov dword ptr [r11+12Ch],0',
    'mov dword ptr [r11+130h],0', 'mov dword ptr [r11+134h],0',
    'mov dword ptr [r11+138h],0', 'mov dword ptr [r11+13Ch],0'
)
foreach ($reset in $writeOutcomeResets) {
    $resetIndex = Find-UniqueInstruction $launch $reset 'Guest-state diagnostic reset changed'
    Assert-True ($resetIndex -lt $rspWriteIndex) 'Guest-state diagnostics must be invalidated before VMWRITE'
}

$writeFailures = @(
    [pscustomobject]@{
        Name = 'Guest RSP'; InvalidBranch = $rspWriteIndex + 1
        ValidBranch = $rspWriteIndex + 2; OutcomeOffset = '128h'
        ErrorOffset = '12Ch'; ErrorValidOffset = '130h'
    },
    [pscustomobject]@{
        Name = 'Guest RFLAGS'; InvalidBranch = $rflagsWriteIndex + 1
        ValidBranch = $rflagsWriteIndex + 2; OutcomeOffset = '134h'
        ErrorOffset = '138h'; ErrorValidOffset = '13Ch'
    }
)
$writeFailureIndices = @()
$launchRestoreIndex = Find-UniqueInstruction $launch 'lgdt tbyte ptr [r11+140h]' 'Launch restore path changed'
Assert-Exact $launch ($launchRestoreIndex + 1) 'lidt tbyte ptr [r11+150h]' `
    'Failure IDTR restore ordering changed'
Assert-Exact $launch ($launchRestoreIndex + 2) 'mov rsp,qword ptr [r11+0A0h]' `
    'Failure native RSP restore ordering changed'
Assert-Exact $launch ($launchRestoreIndex + 3) 'push qword ptr [r11+0A8h]' `
    'Failure RFLAGS restore ordering changed'
Assert-Exact $launch ($launchRestoreIndex + 4) 'popfq' `
    'Failure flags restore ordering changed'
foreach ($failure in $writeFailures) {
    $invalidTarget = Get-BranchTarget $launch[$failure.InvalidBranch] 'jb' "$($failure.Name) VMfailInvalid branch changed"
    $validTarget = Get-BranchTarget $launch[$failure.ValidBranch] 'je' "$($failure.Name) VMfailValid branch changed"
    $invalidIndex = Find-InstructionByAddress $launch $invalidTarget "$($failure.Name) VMfailInvalid path changed"
    $validIndex = Find-InstructionByAddress $launch $validTarget "$($failure.Name) VMfailValid path changed"
    $writeFailureIndices += $invalidIndex, $validIndex
    Assert-True ($invalidIndex -ne $validIndex -and
                 $invalidIndex -gt $vmlaunchIndex -and
                 $validIndex -gt $vmlaunchIndex) "$($failure.Name) failures must bypass VMLAUNCH"

    Assert-Exact $launch $invalidIndex 'mov r10d,3' "$($failure.Name) VMfailInvalid return changed"
    Assert-Exact $launch ($invalidIndex + 1) "mov dword ptr [r11+$($failure.OutcomeOffset)],3" "$($failure.Name) VMfailInvalid outcome changed"
    Assert-BranchDestination $launch ($invalidIndex + 2) 'jmp' $launchRestoreIndex "$($failure.Name) VMfailInvalid restore branch changed"

    Assert-Exact $launch $validIndex 'mov r10d,2' "$($failure.Name) VMfailValid return changed"
    Assert-Exact $launch ($validIndex + 1) "mov dword ptr [r11+$($failure.OutcomeOffset)],2" "$($failure.Name) VMfailValid outcome changed"
    Assert-Exact $launch ($validIndex + 2) 'mov ecx,4400h' "$($failure.Name) VM-instruction error encoding changed"
    Assert-Exact $launch ($validIndex + 3) 'vmread rax,rcx' "$($failure.Name) VMfailValid must read VM-instruction error"
    Assert-BranchDestination $launch ($validIndex + 4) 'jb' ($validIndex + 8) "$($failure.Name) diagnostic VMfailInvalid target changed"
    Assert-BranchDestination $launch ($validIndex + 5) 'je' ($validIndex + 8) "$($failure.Name) diagnostic VMfailValid target changed"
    Assert-Exact $launch ($validIndex + 6) "mov dword ptr [r11+$($failure.ErrorOffset)],eax" "$($failure.Name) error store changed"
    Assert-Exact $launch ($validIndex + 7) "mov dword ptr [r11+$($failure.ErrorValidOffset)],1" "$($failure.Name) error validity changed"
    Assert-BranchDestination $launch ($validIndex + 8) 'jmp' $launchRestoreIndex "$($failure.Name) diagnostic exit changed"
}
Assert-True (@(Find-Instructions $launch 'vmread rax,rcx').Count -eq 2) 'Only guest-state VMfailValid paths may read VM-instruction error'

$launchOutcomeReset = Find-UniqueInstruction $launch 'mov dword ptr [r11+110h],0' 'Launch outcome reset changed'
Assert-True ($launchOutcomeReset -lt $rspWriteIndex) 'Launch outcome must start not-attempted'
$launchOutcomeWrites = @(Find-Instructions $launch 'mov dword ptr [r11+110h],r10d')
Assert-True ($launchOutcomeWrites.Count -eq 2) 'Only VMLAUNCH failures may replace the launch outcome'
$firstWriteFailureIndex = ($writeFailureIndices | Measure-Object -Minimum).Minimum
foreach ($outcomeIndex in $launchOutcomeWrites) {
    Assert-True ($outcomeIndex -gt $vmlaunchIndex -and $outcomeIndex -lt $firstWriteFailureIndex) 'A VMWRITE failure must leave VMLAUNCH not-attempted'
}

$vmexit = @(Get-Instructions (Get-SymbolBlock 'VmxVmExitEntry'))
$resumeIndex = Find-UniqueInstruction $vmexit 'vmresume' 'VMRESUME site must be unique'
$resumeMarkerCheck = Find-UniqueInstruction $vmexit 'cmp dword ptr [r11+114h],4' `
    'Resume success guard must test only the pending outcome'
Assert-True ($resumeMarkerCheck -lt $resumeIndex) `
    'Resume success must not be inferred before VMRESUME'
Assert-Pattern $vmexit ($resumeMarkerCheck + 1) '^jne [0-9A-F]+$' `
    'Resume success guard branch changed'
$resumePendingStore = Find-UniqueInstruction $vmexit 'mov dword ptr [r11+114h],4' `
    'VMRESUME pending outcome store must be unique'
Assert-True ($resumePendingStore -lt $resumeIndex) `
    'VMRESUME pending outcome must precede the instruction'
$resumeMarkerStore = Find-UniqueInstruction $vmexit 'mov dword ptr [r11+114h],1' `
    'Resume success store must be unique'
$dispatcherIndex = Find-UniqueInstruction $vmexit 'mov rax,qword ptr [r11+0C8h]' `
    'Exit dispatcher load changed'
Assert-True ($resumeMarkerStore -gt $resumeMarkerCheck -and $resumeMarkerStore -lt $dispatcherIndex) `
    'Resume success must be stored before dispatch only after a later VM exit'
$resetErrorIndex = Find-UniqueInstruction $vmexit 'mov dword ptr [r11+120h],0' `
    'Resume error must be reset before VMRESUME'
$resetValidIndex = Find-UniqueInstruction $vmexit 'mov dword ptr [r11+124h],0' `
    'Resume error validity must be reset before VMRESUME'
Assert-True ($resetErrorIndex -lt $resumeIndex -and $resetValidIndex -lt $resumeIndex) `
    'Resume diagnostics must be invalidated before VMRESUME'

$resumeSequence = @(
    'vmresume',
    '^jb [0-9A-F]+$',
    '^je [0-9A-F]+$',
    'mov r10,qword ptr [rsp]',
    'mov dword ptr [r10+114h],2',
    'mov ecx,4400h',
    'vmread rax,rcx',
    '^jb [0-9A-F]+$',
    '^je [0-9A-F]+$',
    'mov dword ptr [r10+120h],eax',
    'mov dword ptr [r10+124h],1',
    'mov rcx,r10',
    'jmp VmxAsmOfflineToNative',
    'mov rcx,r10',
    'jmp VmxAsmOfflineToNative',
    'mov rcx,qword ptr [rsp]',
    'mov dword ptr [rcx+114h],3',
    'jmp VmxAsmOfflineToNative'
)
for ($offset = 0; $offset -lt $resumeSequence.Count; $offset++) {
    $expected = $resumeSequence[$offset]
    if ($expected.StartsWith('^')) {
        Assert-Pattern $vmexit ($resumeIndex + $offset) $expected `
            'VMRESUME diagnostic control flow changed'
    } else {
        Assert-Exact $vmexit ($resumeIndex + $offset) $expected `
            'VMRESUME diagnostic instruction order changed'
    }
}

$invalidTarget = Get-BranchTarget $vmexit[$resumeIndex + 1] 'jb' 'VMfailInvalid branch changed'
$validTarget = Get-BranchTarget $vmexit[$resumeIndex + 2] 'je' 'VMfailValid branch changed'
$readFailureCarryTarget = Get-BranchTarget $vmexit[$resumeIndex + 7] 'jb' `
    'VMREAD fail-invalid branch changed'
$readFailureZeroTarget = Get-BranchTarget $vmexit[$resumeIndex + 8] 'je' `
    'VMREAD fail-valid branch changed'
Assert-True ($validTarget -ceq $vmexit[$resumeIndex + 3].Address) `
    'VMfailValid must enter the VMREAD diagnostic path'
Assert-True ($invalidTarget -ceq $vmexit[$resumeIndex + 15].Address) `
    'VMfailInvalid must bypass VMREAD and error-valid stores'
Assert-True ($readFailureCarryTarget -ceq $vmexit[$resumeIndex + 13].Address -and `
             $readFailureZeroTarget -ceq $vmexit[$resumeIndex + 13].Address) `
    'A failed diagnostic VMREAD must keep error validity clear'

Assert-True (@($vmexit | Where-Object Text -CEQ 'vmread rax,rcx').Count -eq 2) `
    'VmxVmExitEntry must contain only guest-RSP and fail-valid-error VMREAD sites'

$offline = @(Get-Instructions (Get-SymbolBlock 'VmxAsmOfflineToNative'))
$offlineExpected = @(
    'mov r11,rcx',
    'mov dword ptr [r11+110h],1',
    'lgdt tbyte ptr [r11+140h]',
    'lidt tbyte ptr [r11+150h]',
    'mov r10,qword ptr [r11+0B0h]',
    'mov rsp,qword ptr [r11+0A0h]',
    'push qword ptr [r11+0A8h]',
    'popfq'
)
Assert-True ($offline.Count -ge $offlineExpected.Count) `
    'VmxAsmOfflineToNative disassembly is truncated'
for ($index = 0; $index -lt $offlineExpected.Count; $index++) {
    Assert-Exact $offline $index $offlineExpected[$index] `
        'Offline/native restore instruction order changed'
}
$launchSuccessIndex = Find-UniqueInstruction $offline 'mov dword ptr [r11+110h],1' `
    'Launch success must be recorded in the offline return path'
Assert-True (@($offline | Where-Object Text -CEQ 'mov dword ptr [r11+118h],1').Count -eq 0) `
    'MASM must not record VMXOFF success'
Assert-True (@(Find-Instructions $launch 'mov dword ptr [r11+110h],1').Count -eq 0) `
    'VMLAUNCH failure paths must not record launch success'
[void](Find-UniqueInstruction $offline 'mov rsp,qword ptr [r11+0A0h]' `
    'Native RSP restore must be unique')
[void](Find-UniqueInstruction $offline 'popfq' 'Native RFLAGS restore must be unique')

Write-Output 'Verified built MASM executable order for launch, VMRESUME diagnostics, and native restore.'
