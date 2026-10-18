[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$templatePath = Join-Path $PSScriptRoot 'guest-fixture-correction.template.ps1'
$templateAst = [Management.Automation.Language.Parser]::ParseFile($templatePath, [ref]$null, [ref]$null)
$functionNames = @('Test-Checksum', 'Measure-Smbios2Table', 'Assert-EpsAndTable')
$functionSources = foreach ($name in $functionNames) {
    $function = @($templateAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name }, $true))
    if ($function.Count -ne 1) { throw "Expected one $name function in the guest template." }
    $function[0].Extent.Text
}
Invoke-Expression ($functionSources -join [Environment]::NewLine)

$checks = [Collections.Generic.List[object]]::new()
function Assert-True([bool]$Condition, [string]$Name, $Actual = $null) {
    if (-not $Condition) { throw "$Name failed: actual=$Actual" }
    $script:checks.Add([ordered]@{ name = $Name; pass = $true; actual = $Actual })
}

function Add-Structure([Collections.Generic.List[byte]]$Buffer, [byte]$Type, [int]$FormattedLength, [string]$StringValue = '') {
    [void]$Buffer.Add($Type)
    [void]$Buffer.Add([byte]$FormattedLength)
    for ($index = 2; $index -lt $FormattedLength; $index++) { [void]$Buffer.Add([byte]0) }
    foreach ($character in $StringValue.ToCharArray()) { [void]$Buffer.Add([byte][char]$character) }
    [void]$Buffer.Add([byte]0)
    [void]$Buffer.Add([byte]0)
}

function New-SmbiosTable {
    $bytes = [Collections.Generic.List[byte]]::new()
    Add-Structure $bytes 1 24 ('X' * 86)
    Add-Structure $bytes 2 8 ''
    Add-Structure $bytes 127 4 ''
    return ,$bytes.ToArray()
}

function New-Eps2([int]$TableBytes, [int]$StructureCount, [int]$MaxStructureBytes) {
    $bytes = [byte[]]::new(31)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('_SM_'), 0, $bytes, 0, 4)
    $bytes[5] = 31
    $bytes[6] = 2
    $bytes[7] = 8
    [BitConverter]::GetBytes([uint16]$MaxStructureBytes).CopyTo($bytes, 8)
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('_DMI_'), 0, $bytes, 16, 5)
    [BitConverter]::GetBytes([uint16]$TableBytes).CopyTo($bytes, 22)
    [BitConverter]::GetBytes([uint16]$StructureCount).CopyTo($bytes, 28)
    $bytes[21] = [byte]((256 - ((Test-Sum $bytes 16 15) -band 0xff)) -band 0xff)
    $bytes[4] = [byte]((256 - ((Test-Sum $bytes 0 31) -band 0xff)) -band 0xff)
    return ,$bytes
}

function Test-Sum([byte[]]$Bytes, [int]$Start, [int]$Length) {
    $sum = 0
    for ($index = $Start; $index -lt ($Start + $Length); $index++) { $sum += [int]$Bytes[$index] }
    return $sum
}

$table = New-SmbiosTable
$walk = Measure-Smbios2Table $table
Assert-True ($walk.structureCount -eq 3) 'multi-structure count' $walk.structureCount
Assert-True ($walk.maximumStructureBytes -eq 112) 'maximum complete structure size' $walk.maximumStructureBytes
Assert-True ($walk.tableBytes -gt $walk.maximumStructureBytes) 'table is larger than largest structure' "$($walk.tableBytes)>$($walk.maximumStructureBytes)"

$epsBefore = New-Eps2 $table.Length $walk.structureCount $walk.maximumStructureBytes
$epsAfter = [byte[]]$epsBefore.Clone()
$report = @{
    smbios2_table_bytes = [string]$table.Length
    smbios2_structure_count_before = [string]$walk.structureCount
    smbios2_structure_count_after = [string]$walk.structureCount
    smbios2_max_structure_before = [string]$walk.maximumStructureBytes
    smbios2_max_structure_measured = [string]$walk.maximumStructureBytes
    smbios2_max_structure_after = [string]$walk.maximumStructureBytes
}
$records = @(
    [pscustomobject]@{ name = 'eps2-before.bin'; content = $epsBefore },
    [pscustomobject]@{ name = 'eps2-after.bin'; content = $epsAfter },
    [pscustomobject]@{ name = 'eps3-before.bin'; content = [byte[]]@() },
    [pscustomobject]@{ name = 'eps3-after.bin'; content = [byte[]]@() },
    [pscustomobject]@{ name = 'table-before.bin'; content = $table },
    [pscustomobject]@{ name = 'table-after.bin'; content = $table }
)
Assert-EpsAndTable $records $report
Assert-True $true 'EPS maximum is compared with largest structure, not whole table' '112 versus 128 table bytes'

$withoutEnd = [byte[]]$table[0..($table.Length - 7)]
$failedMissingEnd = $false
try { [void](Measure-Smbios2Table $withoutEnd) } catch { $failedMissingEnd = $_.Exception.Message -like '*Type 127*' }
Assert-True $failedMissingEnd 'reject table without final Type 127' 'rejected'

$missingStringTerminator = [byte[]]$table.Clone()
$missingStringTerminator[110] = 1
$missingStringTerminator[111] = 1
$failedMalformed = $false
try { [void](Measure-Smbios2Table $missingStringTerminator) } catch { $failedMalformed = $true }
Assert-True $failedMalformed 'reject malformed structure terminator' 'rejected'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$reportRoot = Join-Path $repoRoot 'spoof-handoff\reports\f6\runtime'
[void][IO.Directory]::CreateDirectory($reportRoot)
$reportPath = Join-Path $reportRoot ('smbios2-table-tests.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N') + '.json')
$reportObject = [ordered]@{
    schema = 'combined-vm-smbios2-table-tests.v1'
    status = 'PASS'
    checks = @($checks)
}
[IO.File]::WriteAllText($reportPath, ($reportObject | ConvertTo-Json -Depth 12) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output ($reportObject | ConvertTo-Json -Depth 12 -Compress)
