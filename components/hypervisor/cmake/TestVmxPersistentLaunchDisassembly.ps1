param(
    [Parameter(Mandatory = $true)] [string]$LinkerPath,
    [Parameter(Mandatory = $true)] [string]$DumpbinPath,
    [Parameter(Mandatory = $true)] [string]$LaunchObjectPath,
    [Parameter(Mandatory = $true)] [string]$ExitObjectPath
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Invoke-Tool {
    param([string]$Tool, [string[]]$Arguments, [string]$Purpose)
    if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) {
        throw "$Purpose tool is unavailable: $Tool"
    }
    $output = & $Tool @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "$Purpose failed ($LASTEXITCODE): $($output -join [Environment]::NewLine)"
    }
    return ($output -join "`n")
}

function Get-SymbolNames {
    param([string]$Symbols, [string]$ObjectName)
    $names = @()
    foreach ($line in ($Symbols -split "`n")) {
        if ($line -match '\bUNDEF\b') {
            throw "$ObjectName has an unresolved symbol: $line"
        }
        if ($line -match 'External\s+\|\s*(\S+)\s*$') {
            $names += $Matches[1]
        }
    }
    return @($names | Sort-Object -Unique)
}

function Assert-SymbolSet {
    param([string[]]$Actual, [string[]]$Expected, [string]$ObjectName)
    $a = @($Actual | Sort-Object)
    $e = @($Expected | Sort-Object)
    Assert-True (($a -join '|') -ceq ($e -join '|')) `
        "$ObjectName PUBLIC symbol set changed (actual: $($a -join ', '); expected: $($e -join ', '))"
}

function Get-SymbolBlock {
    param([string]$Disassembly, [string]$Name, [string[]]$NextPublicNames = @())
    $matches = [regex]::Matches($Disassembly, "(?m)^$([regex]::Escape($Name)):\r?$")
    Assert-True ($matches.Count -eq 1) "Disassembly symbol region is missing or duplicated: $Name"
    $start = $matches[0].Index + $matches[0].Length
    $tail = $Disassembly.Substring($start)
    if ($NextPublicNames.Count -eq 0) { return $tail }
    $escaped = @($NextPublicNames | ForEach-Object { [regex]::Escape($_) })
    $pattern = '(?m)^(' + ($escaped -join '|') + '):\r?$'
    $next = [regex]::Matches($tail, $pattern)
    Assert-True ($next.Count -eq 1) "PUBLIC boundary is missing or duplicated after $Name"
    return $tail.Substring(0, $next[0].Index)
}
function Get-Instructions {
    param([string]$Text)
    $items = New-Object 'System.Collections.Generic.List[object]'
    $current = $null
    foreach ($line in ($Text -split "`n")) {
        if ($line -match '^\s{2}([0-9A-Fa-f]+):\s+([0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2})*)\s{2,}(.+)$') {
            if ($null -ne $current) { [void]$items.Add($current) }
            $current = [pscustomobject]@{
                Address = $Matches[1].ToUpperInvariant()
                Text = (($Matches[3] -replace '\s+', ' ').Trim())
            }
            continue
        }
        if ($null -ne $current) {
            $continuation = $line.Trim()
            if ($continuation -match '^\[.+\]$') {
                $current.Text = "$($current.Text) $($continuation -replace '\s+', ' ')"
            }
        }
    }
    if ($null -ne $current) { [void]$items.Add($current) }
    return $items.ToArray()
}

function Get-LabelMap {
    param([string]$Disassembly)
    $map = @{}
    $pending = @()
    foreach ($line in ($Disassembly -split "`n")) {
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_?$@]*):\s*$') {
            $pending += $Matches[1]
            continue
        }
        if ($line -match '^\s{2}([0-9A-Fa-f]+):\s+') {
            $address = $Matches[1].ToUpperInvariant()
            foreach ($label in $pending) {
                Assert-True (-not $map.ContainsKey($label)) "Disassembly label is duplicated: $label"
                $map[$label] = $address
            }
            $pending = @()
        }
    }
    return $map
}
function Find-Exact {
    param($Instructions, [string]$Text, [string]$Contract)
    $hits = @()
    for ($i = 0; $i -lt $Instructions.Count; $i++) {
        if ($Instructions[$i].Text -ceq $Text) { $hits += $i }
    }
    Assert-True ($hits.Count -eq 1) "$Contract (expected one '$Text', found $($hits.Count))"
    return [int]$hits[0]
}

function Find-All {
    param($Instructions, [string]$Text)
    $hits = @()
    for ($i = 0; $i -lt $Instructions.Count; $i++) {
        if ($Instructions[$i].Text -ceq $Text) { $hits += $i }
    }
    return @($hits)
}

function Find-Like {
    param($Instructions, [string]$Pattern, [string]$Contract)
    $hits = @()
    for ($i = 0; $i -lt $Instructions.Count; $i++) {
        if ($Instructions[$i].Text -cmatch $Pattern) { $hits += $i }
    }
    Assert-True ($hits.Count -eq 1) "$Contract (expected one match, found $($hits.Count))"
    return [int]$hits[0]
}

function Find-Address {
    param($Instructions, [string]$Address, [string]$Contract)
    for ($i = 0; $i -lt $Instructions.Count; $i++) {
        if ($Instructions[$i].Address -ceq $Address.ToUpperInvariant()) { return [int]$i }
    }
    throw "$Contract (target address $Address is missing)"
}

function Get-BranchTarget {
    param($Instruction, [string]$Mnemonic, [string]$Contract, [hashtable]$LabelMap)
    $escapedMnemonic = [regex]::Escape($Mnemonic)
    if ($Instruction.Text -cmatch "^$escapedMnemonic ([0-9A-Fa-f]+)$") {
        return $Matches[1].ToUpperInvariant()
    }
    if ($Instruction.Text -cmatch "^$escapedMnemonic ([A-Za-z_][A-Za-z0-9_?$@]*)$") {
        $label = $Matches[1]
        Assert-True ($null -ne $LabelMap -and $LabelMap.ContainsKey($label)) "$Contract target label is missing: $label"
        return $LabelMap[$label]
    }
    throw "$Contract (unexpected '$($Instruction.Text)')"
}
function Assert-GprFrame {
    param($Instructions, [int]$Start, [string]$Contract)
    $stack = @('', '+8', '+10h', '+18h', '+20h', '+28h', '+30h', '+38h', '+40h', '+48h', '+50h', '+58h', '+60h', '+68h', '+70h', '+78h')
    $context = @('10h', '18h', '20h', '28h', '30h', '38h', '40h', '48h',
                 '50h', '58h', '60h', '68h', '70h', '78h',
                 '0000000000000080h', '0000000000000088h')
    for ($slot = 0; $slot -lt 16; $slot++) {
        $source = "mov rax,qword ptr [rsp$($stack[$slot])]"
        $store = "mov qword ptr [r11+$($context[$slot])],rax"
        Assert-True (($Start + $slot * 2 + 1) -lt $Instructions.Count) "$Contract truncated at GPR slot $slot"
        Assert-True ($Instructions[$Start + $slot * 2].Text -ceq $source) "$Contract source slot $slot changed"
        Assert-True ($Instructions[$Start + $slot * 2 + 1].Text -ceq $store) "$Contract destination slot $slot changed"
    }
}

$syntheticDisassembly = @'
SyntheticPublic:
  0000000000000000: 90              nop
V3dInitialGate:
  0000000000000001: 90              nop
NextPublic:
  0000000000000002: 90              nop
'@
$syntheticBlock = Get-SymbolBlock $syntheticDisassembly 'SyntheticPublic' @('NextPublic')
Assert-True ($syntheticBlock -match 'V3dInitialGate:') 'Internal labels must remain inside a PUBLIC symbol block'
$missingBoundaryRejected = $false
try { [void](Get-SymbolBlock $syntheticDisassembly 'SyntheticPublic' @('MissingPublic')) } catch { $missingBoundaryRejected = $true }
Assert-True $missingBoundaryRejected 'Missing PUBLIC boundary must be rejected'
$duplicateBoundary = $syntheticDisassembly -replace 'NextPublic:', ('NextPublic:' + [Environment]::NewLine + 'NextPublic:')
$duplicateBoundaryRejected = $false
try { [void](Get-SymbolBlock $duplicateBoundary 'SyntheticPublic' @('NextPublic')) } catch { $duplicateBoundaryRejected = $true }
Assert-True $duplicateBoundaryRejected 'Duplicate PUBLIC boundary must be rejected'
$syntheticLabels = Get-LabelMap $syntheticDisassembly
$symbolicBranch = [pscustomobject]@{ Address = '0000000000000003'; Text = 'jne V3dInitialGate' }
Assert-True ((Get-BranchTarget $symbolicBranch 'jne' 'Synthetic symbolic branch' $syntheticLabels) -ceq '0000000000000001') 'Symbolic branch target was not resolved'
$launchSymbols = Invoke-Tool $LinkerPath @('/dump', '/symbols', $LaunchObjectPath) 'Launch symbol dump'
$exitSymbols = Invoke-Tool $LinkerPath @('/dump', '/symbols', $ExitObjectPath) 'Exit symbol dump'
Assert-SymbolSet (Get-SymbolNames $launchSymbols 'launch object') `
    @('VmxV3dPersistentLaunch', 'VmxV3dPersistentGuestContinuation') 'launch object'
Assert-SymbolSet (Get-SymbolNames $exitSymbols 'exit object') `
    @('VmxV3dPersistentVmExitEntry', 'VmxV3dPersistentShutdownNative') 'exit object'
Assert-True (($launchSymbols + $exitSymbols) -notmatch '(?i)VMX_V2|V2_|CPU0') 'V2/CPU0 symbols are forbidden'

$launchDisassembly = Invoke-Tool $DumpbinPath @('/nologo', '/disasm:bytes', $LaunchObjectPath) 'Launch disassembly'
$exitDisassembly = Invoke-Tool $DumpbinPath @('/nologo', '/disasm:bytes', $ExitObjectPath) 'Exit disassembly'
$launchLabels = Get-LabelMap $launchDisassembly
$exitLabels = Get-LabelMap $exitDisassembly
$launchAll = @(Get-Instructions $launchDisassembly)
$exitAll = @(Get-Instructions $exitDisassembly)
$launch = @(Get-Instructions (Get-SymbolBlock $launchDisassembly 'VmxV3dPersistentLaunch' @('VmxV3dPersistentGuestContinuation')))
$guest = @(Get-Instructions (Get-SymbolBlock $launchDisassembly 'VmxV3dPersistentGuestContinuation'))
$vmexit = @(Get-Instructions (Get-SymbolBlock $exitDisassembly 'VmxV3dPersistentVmExitEntry' @('VmxV3dPersistentShutdownNative')))
$shutdown = @(Get-Instructions (Get-SymbolBlock $exitDisassembly 'VmxV3dPersistentShutdownNative'))


function Assert-MnemonicCount {
    param($Instructions, [string]$Mnemonic, [int]$Expected)
    $hits = @($Instructions | Where-Object { $_.Text -cmatch "^$Mnemonic(?:\s|$)" })
    Assert-True ($hits.Count -eq $Expected) "Unexpected $Mnemonic count ($($hits.Count), expected $Expected)"
}
function Assert-Count {
    param($Instructions, [string]$Text, [int]$Expected)
    $hits = @(Find-All $Instructions $Text)
    Assert-True ($hits.Count -eq $Expected) "Unexpected $Text count ($($hits.Count), expected $Expected)"
}
Assert-MnemonicCount $launchAll 'vmlaunch' 1
Assert-MnemonicCount $launchAll 'vmresume' 1
Assert-MnemonicCount $launchAll 'vmcall' 9
Assert-MnemonicCount $launchAll 'cpuid' 3
Assert-MnemonicCount $launchAll 'rdmsr' 5
Assert-MnemonicCount $launchAll 'wrmsr' 2
Assert-MnemonicCount $launchAll 'rdtsc' 1
Assert-MnemonicCount $launchAll 'invlpg' 1
Assert-MnemonicCount $launchAll 'hlt' 1
Assert-MnemonicCount $launchAll 'vmclear' 0
Assert-MnemonicCount $launchAll 'vmxoff' 0
Assert-MnemonicCount $launchAll 'call' 0
Assert-MnemonicCount $exitAll 'vmlaunch' 0
Assert-MnemonicCount $exitAll 'vmresume' 1
Assert-MnemonicCount $exitAll 'vmcall' 0
Assert-MnemonicCount $exitAll 'vmclear' 1
Assert-MnemonicCount $exitAll 'vmxoff' 1
Assert-MnemonicCount $exitAll 'call' 1
Assert-Count $launchAll 'call rax' 0
Assert-Count $exitAll 'call rax' 1
Assert-Count $launchAll 'vmwrite rcx,rax' 4

Assert-True (@(Find-All $launchAll 'vmwrite rax,rcx').Count -eq 0) 'Reversed VMWRITE operands are forbidden'
Assert-True ((Find-Exact $exitAll 'vmclear qword ptr [r11+0000000000000118h]' 'VMCLEAR VMCS PA operand') -lt (Find-Exact $exitAll 'vmxoff' 'VMXOFF order')) 'VMCLEAR must precede VMXOFF'
Assert-True (@(Find-All $guest 'vmcall').Count -eq 9) 'Guest continuation must emit READY, write-complete, CPUID-complete, MSR-read-complete, TSC-complete, INVLPG-complete, CET-VMCS-complete, HLT-complete, and SHUTDOWN VMCALLs'
Assert-True (@($guest | Where-Object { $_.Text -ceq 'cpuid' }).Count -eq 3) 'Guest continuation must execute exactly three CPUID instructions'
Assert-True (@($guest | Where-Object { $_.Text -ceq 'rdmsr' }).Count -eq 5) 'Guest continuation must execute exactly five RDMSR instructions'
Assert-True (@($guest | Where-Object { $_.Text -ceq 'wrmsr' }).Count -eq 2) 'Guest continuation must execute exactly two WRMSR instructions'
Assert-True (@($guest | Where-Object { $_.Text -ceq 'rdtsc' }).Count -eq 1) 'Guest continuation must execute exactly one RDTSC instruction'
Assert-True (@($guest | Where-Object { $_.Text -cmatch '^invlpg\s' }).Count -eq 1) 'Guest continuation must execute exactly one INVLPG instruction'
Assert-True (@($guest | Where-Object { $_.Text -ceq 'hlt' }).Count -eq 1) 'Guest continuation must execute exactly one HLT instruction'
$guestCr3Read = Find-Like $guest '^mov rax,cr3$' 'Guest CR3 read'
$guestCr3Write = Find-Like $guest '^mov cr3,rax$' 'Guest CR3 self-write'
$guestTarget = Find-Like $guest '^mov rax,qword ptr \[r15\+0*2F0h\]$' 'Stable diagnostic target load'
$guestTargetContracts = @()
for ($i = 0; $i -lt $guest.Count; $i++) {
    if ($guest[$i].Text -cmatch '^cmp qword ptr \[r15\+0*2F8h\],rcx$') {
        $guestTargetContracts += $i
    }
}
Assert-True ($guestTargetContracts.Count -eq 7) 'Write, CPUID, MSR-read, TSC, INVLPG, CET-VMCS, and HLT diagnostics must each authenticate the target contract'
$guestTargetContract = [int]$guestTargetContracts[0]
$guestTargetMode = Find-Exact $guest 'mov edx,dword ptr [r15+0000000000000398h]' 'Generic target-mode load'
$guestTargetModeContract = Find-Exact $guest 'cmp dword ptr [r15+000000000000039Ch],ecx' 'Generic target-mode complement gate'
Assert-True (@(Find-All $guest 'cmp dword ptr [r15+0000000000000398h],1').Count -eq 7) `
    'Generic and six typed routes must admit owned payload mode explicitly'
Assert-True (@(Find-All $guest 'cmp dword ptr [r15+0000000000000398h],2').Count -eq 1) `
    'Only the generic EPT-capable route may admit direct-page mode'
Assert-True (@(Find-All $guest 'cmp dword ptr [r15+000000000000039Ch],0FFFFFFFEh').Count -eq 6) `
    'All six typed routes must authenticate the owned-mode complement'
$guestWrite = Find-Like $guest '^mov byte ptr \[rax\],5Ah$' 'Guest diagnostic write'
Assert-True ($guestCr3Read -lt $guestCr3Write -and $guestCr3Write -lt $guestTarget -and $guestTarget -lt $guestTargetContract -and $guestTargetContract -lt $guestTargetMode -and $guestTargetMode -lt $guestTargetModeContract -and $guestTargetModeContract -lt $guestWrite) 'Guest CR3 self-write must precede contracted diagnostic target and mode use'
Assert-True (@(Find-All $guest 'mov rax,qword ptr [r15+10h]').Count -eq 0) 'VM-exit GPR RAX storage must not supply the diagnostic target'
$guestDiagnosticVmcall = @(Find-All $guest 'vmcall')[1]
Assert-True ($guestWrite -lt $guestDiagnosticVmcall) 'Guest write must precede diagnostic-complete VMCALL'
$guestRetryIndex = $guestDiagnosticVmcall + 1
Assert-True ($guestRetryIndex -lt $guest.Count -and $guest[$guestRetryIndex].Text -cmatch '^jmp\s') `
    'Guest diagnostic-complete VMCALL must branch back to the diagnostic gate'
$guestRetryTargetAddress = Get-BranchTarget $guest[$guestRetryIndex] 'jmp' 'Guest diagnostic retry' $launchLabels
$guestRetryTarget = Find-Address $guest $guestRetryTargetAddress 'Guest diagnostic retry target'
$guestReadyVmcall = @(Find-All $guest 'vmcall')[0]
Assert-True ($guestReadyVmcall -lt $guestRetryTarget -and $guestRetryTarget -lt $guestCr3Read -and $guestRetryTarget -lt $guestDiagnosticVmcall) `
    'Guest diagnostic retry must return to the state gate before the CR3 diagnostic body'
Assert-True (@(Find-All $exitAll 'cmp eax,2').Count -eq 2) 'Resume action and diagnostic-complete state gates are required'
Assert-True (@(Find-All $exitAll 'cmp eax,5').Count -eq 1) 'Resident-resume action gate is required'
Assert-True (@(Find-All $exitAll 'cmp eax,0Dh').Count -eq 1) 'Armed-state upper bound is required'
Assert-True (@(Find-All $exitAll 'cmp eax,0Eh').Count -eq 1) 'Complete-state upper bound is required'
Assert-True (@(Find-All $exitAll 'test eax,1').Count -eq 2) 'Armed/complete parity gates are required'

$publication = @(Find-All $launch 'mov qword ptr [rax],r11')
$vmwrites = @(Find-All $launch 'vmwrite rcx,rax')
Assert-True ($publication.Count -eq 2 -and $vmwrites.Count -eq 4) 'HostRsp publication or VM writes are missing'
Assert-True ($publication[0] -lt $vmwrites[0] -and $publication[1] -lt $vmwrites[2]) `
    'HostRsp context publication must precede each VM entry path'
$resumeCapture0 = Find-Like $launch '^mov qword ptr \[r11\+0*200h\],rax$' 'Resume CR0 capture'
$resumeCapture4 = Find-Like $launch '^mov qword ptr \[r11\+0*208h\],rax$' 'Resume CR4 capture'
$resumeIndex = Find-Exact $launch 'vmresume' 'VMRESUME site'
Assert-True ($resumeCapture0 -lt $resumeCapture4 -and $resumeCapture4 -lt $resumeIndex) 'Resume CR capture must precede VMRESUME'
Assert-True ((Find-All $launch 'and eax,0FFFFFFCFh').Count -eq 1) 'Resume failure must clear both ownership bits'

$pushes = @('push r15','push r14','push r13','push r12','push r11','push r10','push r9','push r8','push rdi','push rsi','push rbp','push rsp','push rbx','push rdx','push rcx','push rax')
for ($i = 0; $i -lt $pushes.Count; $i++) { Assert-True ($vmexit[$i].Text -ceq $pushes[$i]) "VM-exit push slot $i changed" }
$gprStart = Find-Exact $vmexit 'mov rax,qword ptr [rsp]' 'GPR frame start'
Assert-GprFrame $vmexit $gprStart '16-slot guest GPR frame'
$mutated = @($vmexit | ForEach-Object { [pscustomobject]@{ Address = $_.Address; Text = $_.Text } })
$mutated[$gprStart].Text = 'mov rax,qword ptr [rsp+8h]'
$mutationRejected = $false
try { Assert-GprFrame $mutated $gprStart 'GPR mutation' } catch { $mutationRejected = $true }
Assert-True $mutationRejected 'GPR verifier must reject a mutated stack slot'
$badCount = @($exitAll | Where-Object { $_.Text -notmatch '^vmclear(?:\s|$)' })
$countRejected = $false
try { Assert-MnemonicCount $badCount 'vmclear' 1 } catch { $countRejected = $true }
Assert-True $countRejected 'Count mutation must be rejected'
$badOrder = @($exitAll | ForEach-Object { [pscustomobject]@{ Address = $_.Address; Text = $_.Text } })
$badClear = Find-Exact $badOrder 'vmclear qword ptr [r11+0000000000000118h]' 'Order mutation VMCLEAR'
$badOff = Find-Exact $badOrder 'vmxoff' 'Order mutation VMXOFF'
$tmp = $badOrder[$badClear]; $badOrder[$badClear] = $badOrder[$badOff]; $badOrder[$badOff] = $tmp
$orderRejected = $false
try { Assert-True ((Find-Exact $badOrder 'vmclear qword ptr [r11+0000000000000118h]' 'Order mutation VMCLEAR') -lt (Find-Exact $badOrder 'vmxoff' 'Order mutation VMXOFF')) 'Order mutation accepted' } catch { $orderRejected = $true }
Assert-True $orderRejected 'Order mutation must be rejected'

$contextSlots = @(Find-All $vmexit 'mov r11,qword ptr [rsp+0000000000000080h]')
Assert-True ($contextSlots.Count -eq 2) 'Authoritative host context slot must be captured and reloaded exactly once'
$contextSlot = $contextSlots[0]
$contextReload = $contextSlots[1]
$guestR15Loads = @(Find-All $vmexit 'mov rax,qword ptr [rsp+78h]')
Assert-True ($guestR15Loads.Count -eq 2) 'Guest R15 must be loaded for authentication and the GPR copy'
$guestR15 = $guestR15Loads[0]
$guestR15Copy = $guestR15Loads[1]
Assert-True ($guestR15Copy -eq ($gprStart + 30)) 'GPR frame must contain the R15 copy at slot 15'
$contextCompare = Find-Exact $vmexit 'cmp rax,r11' 'Context authentication compare'
$contextAlign = Find-Exact $vmexit 'test r11,0Fh' 'Context alignment gate'
$magic = Find-Exact $vmexit 'cmp qword ptr [r11],rax' 'Header magic gate'
$size = Find-Exact $vmexit 'cmp dword ptr [r11+8],3F0h' 'Header size gate'
$version = Find-Exact $vmexit 'cmp dword ptr [r11+0Ch],4' 'Header version gate'
$stackAlign = Find-Exact $vmexit 'test rsp,0Fh' 'Callback stack alignment gate'
$firstWrite = Find-Exact $vmexit 'mov qword ptr [r11+10h],rax' 'First context write'
Assert-True ($contextSlot -lt $guestR15 -and $guestR15 -lt $contextCompare -and $contextCompare -lt $contextAlign -and $contextAlign -lt $magic -and $magic -lt $size -and $size -lt $version -and $version -lt $stackAlign -and $stackAlign -lt $firstWrite) 'Context/header gates must precede writes'
$sub = Find-Exact $vmexit 'sub rsp,28h' 'Callback shadow/alignment reservation'
$call = Find-Exact $vmexit 'call rax' 'Typed dispatcher call'
$add = Find-Exact $vmexit 'add rsp,28h' 'Callback stack cleanup'
Assert-True ($sub -lt $call -and $call -lt $add -and $add -lt $contextReload) 'Dispatcher call stack reservation or context reload changed'
Assert-True (@($vmexit | Where-Object { $_.Text -in @('vmclear','vmxoff') }).Count -eq 0) 'READY restore region must not contain teardown instructions'
[void](Find-Like $vmexit '^cmp dword ptr \[r11\+0*220h\],2$' 'READY protocol-step gate')
[void](Find-Like $vmexit '^cmp dword ptr \[r11\+0*2C4h\],1$' 'READY observed gate')
[void](Find-Like $vmexit '^test dword ptr \[r11\+0*228h\],2$' 'READY validation gate')

$gate = Find-Like $exitAll '^mov rax,qword ptr \[r11\+0*118h\]$' 'Shared VMCS PA gate'
$gateNulls = @((Find-All $exitAll 'test rax,rax') | Where-Object { $_ -gt $gate })
Assert-True ($gateNulls.Count -eq 1) 'Shared PA gate must have one post-load null check'
$gateNull = $gateNulls[0]
$gateAlign = Find-Exact $exitAll 'test ax,0FFFh' 'VMCS PA alignment gate'
$vmclear = Find-Exact $exitAll 'vmclear qword ptr [r11+0000000000000118h]' 'Single VMCLEAR site'
Assert-True ($gate -lt $gateNull -and $gateNull -lt $gateAlign -and $gateAlign -lt $vmclear) 'PA gate must precede VMCLEAR'
$gateAddress = $exitAll[$gate].Address
$gateJumps = @($exitAll | Where-Object { $_.Text -ceq "jmp $gateAddress" })
Assert-True ($gateJumps.Count -ge 3) 'Authenticated and fail-closed teardown paths must converge on the shared PA gate'
$vmxoff = Find-Exact $exitAll 'vmxoff' 'Single VMXOFF site'
$vmxoffSuccess = $vmxoff + 3
Assert-True ($exitAll[$vmxoffSuccess].Text -cmatch '^mov dword ptr \[r11\+0*264h\],1$') 'VMXOFF success publication changed'
$cr0 = Find-Exact $exitAll 'mov cr0,rax' 'CR0 restore'
$cr4 = Find-Exact $exitAll 'mov cr4,rax' 'CR4 restore'
Assert-True ($vmxoff -lt $cr0 -and $cr0 -lt $cr4) 'CR writes must follow VMXOFF success'
$vmxoffInvalidTarget = Find-Address $exitAll (Get-BranchTarget $exitAll[$vmxoff + 1] 'jb' 'VMXOFF fail-invalid branch' $exitLabels) 'VMXOFF fail-invalid target'
$vmxoffValidTarget = Find-Address $exitAll (Get-BranchTarget $exitAll[$vmxoff + 2] 'je' 'VMXOFF fail-valid branch' $exitLabels) 'VMXOFF fail-valid target'
Assert-True ($vmxoffInvalidTarget -gt $cr4 -and $vmxoffValidTarget -gt $cr4) 'VMXOFF failure paths must not write CR0/CR4'
$cr0Formula = @('^mov rax,qword ptr \[r11\+0*200h\]$','^mov rcx,qword ptr \[r11\+0*1F0h\]$','^mov rdx,qword ptr \[r11\+0*210h\]$','^xor rdx,rcx$','^mov r8,rdx$','^not rdx$','^and rax,rdx$','^and rcx,r8$','^or rax,rcx$','^mov cr0,rax$','^mov qword ptr \[r11\+0*210h\],rax$')
$cr4Formula = @('^mov rax,qword ptr \[r11\+0*208h\]$', '^mov rcx,qword ptr \[r11\+0*1F8h\]$', '^mov rdx,qword ptr \[r11\+0*218h\]$', '^xor rdx,rcx$', '^mov r8,rdx$', '^not rdx$', '^and rax,rdx$', '^and rcx,r8$', '^or rax,rcx$', '^mov cr4,rax$', '^mov qword ptr \[r11\+0*218h\],rax$')
foreach ($formula in @($cr0Formula, $cr4Formula)) {
    $start = Find-Like $exitAll $formula[0] 'CR restoration formula'
    for ($i = 0; $i -lt $formula.Count; $i++) { Assert-True ($exitAll[$start + $i].Text -cmatch $formula[$i]) "CR restoration formula changed at step $i" }
}
$vmclearSuccessCheck = Find-Like $exitAll '^cmp dword ptr \[r11\+0*258h\],1$' 'VMCLEAR success check'
$vmxoffSuccessCheck = Find-Like $exitAll '^cmp dword ptr \[r11\+0*264h\],1$' 'VMXOFF success check'
$proven = Find-Exact $exitAll 'or eax,4' 'ShutdownProven publication'
Assert-True ($vmclearSuccessCheck -lt $vmxoffSuccessCheck -and $vmxoffSuccessCheck -lt $proven) 'ShutdownProven requires both teardown successes'

Write-Output 'Verified V3D4d persistent launch/exit symbols, VMX/RDMSR/WRMSR/RDTSC counts/order, host-context/GPR gates, PA convergence, CR ownership, and proof ordering.'
