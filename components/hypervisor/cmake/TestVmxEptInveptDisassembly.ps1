param(
    [Parameter(Mandatory = $true)][string]$LinkerPath,
    [Parameter(Mandatory = $true)][string]$ObjectPath
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $LinkerPath -PathType Leaf)) {
    throw "MSVC linker is unavailable: $LinkerPath"
}
if (-not (Test-Path -LiteralPath $ObjectPath -PathType Leaf)) {
    throw "INVEPT MASM object is unavailable: $ObjectPath"
}

$dump = & $LinkerPath /dump /disasm $ObjectPath 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "MSVC disassembly failed ($LASTEXITCODE): $($dump -join [Environment]::NewLine)"
}
$text = $dump -join [Environment]::NewLine
$start = $text.IndexOf('VmxEptInveptAsm:', [StringComparison]::Ordinal)
if ($start -lt 0) { throw 'VmxEptInveptAsm symbol is missing' }
$block = $text.Substring($start)

$instructions = @()
foreach ($line in ($block -split '\r?\n')) {
    if ($line -match '^\s{2}([0-9A-F]+):\s+([0-9A-F]{2}(?:\s+[0-9A-F]{2})*)\s{2,}(.+)$') {
        $instructions += [pscustomobject]@{
            Address = $Matches[1]
            Text = (($Matches[3] -replace '\s+', ' ').Trim())
        }
    }
}
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Find-Index([string]$Exact) {
    $found = @()
    for ($i = 0; $i -lt $instructions.Count; $i++) {
        if ($instructions[$i].Text -ceq $Exact) { $found += $i }
    }
    Assert-True ($found.Count -eq 1) "expected one '$Exact', found $($found.Count)"
    return $found[0]
}
function Branch-Index([int]$Index) {
    Assert-True ($instructions[$Index].Text -cmatch '^jne ([0-9A-F]+)$') "unexpected branch: $($instructions[$Index].Text)"
    $target = $Matches[1]
    for ($i = 0; $i -lt $instructions.Count; $i++) {
        if ($instructions[$i].Address -ceq $target) { return $i }
    }
    throw "branch target is missing: $target"
}

Assert-True ($instructions.Count -eq 13) "INVEPT wrapper instruction count changed: $($instructions.Count)"
$expected = @(
    'invept rcx,oword ptr [rdx]',
    'pushfq',
    'pop rax',
    'test eax,1',
    $null,
    'test eax,40h',
    $null,
    'mov eax,1',
    'ret',
    'mov eax,2',
    'ret',
    'mov eax,3',
    'ret'
)
for ($i = 0; $i -lt $expected.Count; $i++) {
    if ($null -ne $expected[$i]) {
        Assert-True ($instructions[$i].Text -ceq $expected[$i]) "instruction $i changed: $($instructions[$i].Text)"
    }
}
Assert-True ((Find-Index 'invept rcx,oword ptr [rdx]') -eq 0) 'INVEPT must be first'
Assert-True ((Branch-Index 4) -eq 11) 'CF must branch to FAIL_INVALID'
Assert-True ((Branch-Index 6) -eq 9) 'ZF must branch to FAIL_VALID'

$original = $instructions
$instructions = @(
    [pscustomobject]@{ Address = '0000'; Text = 'jne 0000' },
    [pscustomobject]@{ Address = '0001'; Text = 'ret' }
)
$rejected = $false
try {
    $target = Branch-Index 0
    Assert-True ($target -eq 1) 'wrong-target mutation accepted'
} catch {
    $rejected = $true
}
$instructions = $original
Assert-True $rejected 'branch mutation verifier is vacuous'

Write-Host 'PASS: emitted INVEPT wrapper disassembly'
