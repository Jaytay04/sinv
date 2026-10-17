$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-SnapshotCondition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-SnapshotThrows([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { & $Action } catch { $threw = $true }
    if (-not $threw) { throw $Message }
}

function New-SnapshotBytes([int[]]$Numbers) {
    $bytes = [Collections.Generic.List[byte]]::new()
    foreach ($number in $Numbers) {
        $bytes.AddRange([BitConverter]::GetBytes([uint16]$number))
    }
    return ,([byte[]]$bytes.ToArray())
}

function New-SnapshotLoadOption(
    [string]$Description = 'Fixture entry',
    [uint16]$FilePathLength = 4,
    [switch]$OmitDescriptionTerminator
) {
    $bytes = [Collections.Generic.List[byte]]::new()
    $bytes.AddRange([BitConverter]::GetBytes([uint32]1))
    $bytes.AddRange([BitConverter]::GetBytes($FilePathLength))
    $descriptionBytes = if ($OmitDescriptionTerminator) {
        [Text.Encoding]::Unicode.GetBytes($Description)
    } else {
        [Text.Encoding]::Unicode.GetBytes($Description + [char]0)
    }
    $bytes.AddRange([byte[]]$descriptionBytes)
    if (-not $OmitDescriptionTerminator) {
        $bytes.AddRange([byte[]](0, 0, 0, 0))
    }
    return ,([byte[]]$bytes.ToArray())
}

function New-SnapshotRecord(
    [string]$Name,
    [bool]$Present,
    [int]$Win32Error = 0,
    [byte[]]$Bytes = $null
) {
    $encoded = if ($null -ne $Bytes) { [Convert]::ToBase64String($Bytes) } else { $null }
    return [pscustomobject]@{
        name = $Name
        present = $Present
        win32Error = $Win32Error
        base64 = $encoded
    }
}

function Reset-SnapshotMocks([object[]]$Records) {
    $script:SnapshotMockRecords = @{}
    $script:SnapshotMockReads = @{}
    $script:SnapshotMockAudit = [Collections.Generic.List[string]]::new()
    foreach ($record in $Records) { $script:SnapshotMockRecords[$record.name] = $record }
}

function Read-VariableRecord([string]$Name, [string]$Guid) {
    if ($Guid -cne '{fixture-guid}') { throw 'Helper passed an unexpected firmware GUID.' }
    if (-not $script:SnapshotMockRecords.ContainsKey($Name)) {
        throw ('Unexpected mocked variable read: ' + $Name)
    }
    if (-not $script:SnapshotMockReads.ContainsKey($Name)) { $script:SnapshotMockReads[$Name] = 0 }
    $script:SnapshotMockReads[$Name]++
    return $script:SnapshotMockRecords[$Name]
}

function Write-Audit([string]$Message) {
    $script:SnapshotMockAudit.Add($Message)
}

function Get-SnapshotSelectionRecords(
    [int[]]$OrderNumbers,
    [object]$NextRecord = $null,
    [object]$CurrentRecord = $null
) {
    $order = New-SnapshotRecord 'BootOrder' $true 0 (New-SnapshotBytes $OrderNumbers)
    if ($null -eq $NextRecord) { $NextRecord = New-SnapshotRecord 'BootNext' $false 203 }
    if ($null -eq $CurrentRecord) { $CurrentRecord = New-SnapshotRecord 'BootCurrent' $false 203 }
    return @($order, $NextRecord, $CurrentRecord)
}

function Invoke-SnapshotCase([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:SnapshotPassed++
    Write-Host ('PASS ' + $Name)
}

$helperPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'snapshot-reference.ps1'
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw ('Snapshot helper not found: ' + $helperPath)
}
$tokens = $null
$parseErrors = $null
$helperAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $helperPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw ('Snapshot helper parse failed: ' + $parseErrors[0].Message) }
$definitions = @($helperAst.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -CEQ 'Read-ReadyBootEntries'
}, $true))
if ($definitions.Count -ne 1) { throw 'Expected exactly one Read-ReadyBootEntries definition.' }
Invoke-Expression $definitions[0].Extent.Text

$script:SnapshotPassed = 0
$script:SnapshotGuid = '{fixture-guid}'

Invoke-SnapshotCase 'BootCurrent-only missing 203 is preserved and audited' {
    $selection = Get-SnapshotSelectionRecords @(1) $null (New-SnapshotRecord 'BootCurrent' $true 0 (New-SnapshotBytes @(2)))
    Reset-SnapshotMocks (@($selection) + @(
        (New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption 'Order entry')),
        (New-SnapshotRecord 'Boot0002' $false 203)
    ))
    $entries = @(Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid)
    Assert-SnapshotCondition ($entries.Count -eq 2) 'Expected the ordered entry and absent current entry.'
    $missing = @($entries | Where-Object { $_.variable -ceq 'Boot0002' })
    Assert-SnapshotCondition ($missing.Count -eq 1) 'Absent BootCurrent record was not returned.'
    Assert-SnapshotCondition (-not $missing[0].present -and $missing[0].win32Error -eq 203) 'Absent BootCurrent record has the wrong presence evidence.'
    Assert-SnapshotCondition ($missing[0].referencedBy -contains 'BootCurrent') 'Absent record lost its BootCurrent reference.'
    Assert-SnapshotCondition ($null -eq $missing[0].description -and -not $missing[0].active -and $null -eq $missing[0].unicodeData) 'Absent record contains invented boot-option data.'
    Assert-SnapshotCondition ($script:SnapshotMockAudit.Count -eq 1 -and $script:SnapshotMockAudit[0] -like '*Boot0002*203*BootCurrent*') 'Absent BootCurrent record was not explicitly audited.'
}

Invoke-SnapshotCase 'BootOrder target missing 203 is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1)
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $false 203))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Missing required BootOrder target was accepted.'
}

Invoke-SnapshotCase 'BootNext target missing 203 is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1) (New-SnapshotRecord 'BootNext' $true 0 (New-SnapshotBytes @(2)))
    Reset-SnapshotMocks (@($selection) + @(
        (New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption 'Required order entry')),
        (New-SnapshotRecord 'Boot0002' $false 203)
    ))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Missing required BootNext target was accepted.'
}

Invoke-SnapshotCase 'BootCurrent access denied is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1) $null (New-SnapshotRecord 'BootCurrent' $false 5)
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption)))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'BootCurrent access denied was treated as absence.'
}

Invoke-SnapshotCase 'BootCurrent-only target access denied is rejected' {
    $selection = Get-SnapshotSelectionRecords @(0) $null (New-SnapshotRecord 'BootCurrent' $true 0 (New-SnapshotBytes @(1)))
    Reset-SnapshotMocks (@($selection) + @(
        (New-SnapshotRecord 'Boot0000' $true 0 (New-SnapshotLoadOption 'Required order entry')),
        (New-SnapshotRecord 'Boot0001' $false 5)
    ))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'BootCurrent-only target access denied was treated as historical absence.'
    Assert-SnapshotCondition ($script:SnapshotMockAudit.Count -eq 0) 'Access-denied current target was incorrectly audited as ordinary absence.'
}

Invoke-SnapshotCase 'Empty and odd-length BootOrder values are rejected' {
    foreach ($orderBytes in @([byte[]]@(), [byte[]]@(1))) {
        $order = New-SnapshotRecord 'BootOrder' $true 0 $orderBytes
        $next = New-SnapshotRecord 'BootNext' $false 203
        $current = New-SnapshotRecord 'BootCurrent' $false 203
        Reset-SnapshotMocks @($order, $next, $current)
        Assert-SnapshotThrows { Read-ReadyBootEntries $order $next $current $script:SnapshotGuid | Out-Null } 'Malformed BootOrder was accepted.'
    }
}

Invoke-SnapshotCase 'Malformed present BootCurrent selection is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1) $null (New-SnapshotRecord 'BootCurrent' $true 0 ([byte[]]@(1)))
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption)))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Malformed BootCurrent selection was accepted.'
}

Invoke-SnapshotCase 'A missing target shared by BootOrder and BootCurrent remains required and is read once' {
    $selection = Get-SnapshotSelectionRecords @(1) $null (New-SnapshotRecord 'BootCurrent' $true 0 (New-SnapshotBytes @(1)))
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $false 203))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Shared required/current missing target was accepted.'
    Assert-SnapshotCondition ($script:SnapshotMockReads['Boot0001'] -eq 1) 'Duplicate target was not deduplicated before its firmware read.'
}

Invoke-SnapshotCase 'Valid two-entry selection is parsed once per target with merged references' {
    $selection = Get-SnapshotSelectionRecords @(1, 2) `
        (New-SnapshotRecord 'BootNext' $true 0 (New-SnapshotBytes @(2))) `
        (New-SnapshotRecord 'BootCurrent' $true 0 (New-SnapshotBytes @(1)))
    Reset-SnapshotMocks (@($selection) + @(
        (New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption 'First entry')),
        (New-SnapshotRecord 'Boot0002' $true 0 (New-SnapshotLoadOption 'Second entry'))
    ))
    $entries = @(Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid)
    Assert-SnapshotCondition ($entries.Count -eq 2) 'Expected exactly two unique boot entries.'
    Assert-SnapshotCondition ($script:SnapshotMockReads['Boot0001'] -eq 1 -and $script:SnapshotMockReads['Boot0002'] -eq 1) 'Unique boot entries were not each read exactly once.'
    $first = @($entries | Where-Object { $_.variable -ceq 'Boot0001' })[0]
    $second = @($entries | Where-Object { $_.variable -ceq 'Boot0002' })[0]
    Assert-SnapshotCondition ($first.description -ceq 'First entry' -and $first.active) 'First EFI_LOAD_OPTION description or active bit was parsed incorrectly.'
    Assert-SnapshotCondition ($first.referencedBy -contains 'BootOrder' -and $first.referencedBy -contains 'BootCurrent') 'First entry references were not merged.'
    Assert-SnapshotCondition ($second.description -ceq 'Second entry' -and $second.referencedBy -contains 'BootNext' -and $second.referencedBy -contains 'BootOrder') 'Second EFI_LOAD_OPTION or its references were parsed incorrectly.'
}

Invoke-SnapshotCase 'Present boot entry with nonzero read error is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1)
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $true 5 (New-SnapshotLoadOption)))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Present boot entry with a failed read status was accepted.'
}

Invoke-SnapshotCase 'Truncated EFI_LOAD_OPTION header is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1)
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $true 0 ([byte[]]@(1, 0, 0, 0, 0, 0, 0))))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Truncated boot-entry header was accepted.'
}

Invoke-SnapshotCase 'Truncated EFI_LOAD_OPTION device path is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1)
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption 'Short path' 400)))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Boot entry with an out-of-bounds device path length was accepted.'
}

Invoke-SnapshotCase 'EFI_LOAD_OPTION without a description terminator is rejected' {
    $selection = Get-SnapshotSelectionRecords @(1)
    Reset-SnapshotMocks (@($selection) + @(New-SnapshotRecord 'Boot0001' $true 0 (New-SnapshotLoadOption 'No terminator' 0 -OmitDescriptionTerminator)))
    Assert-SnapshotThrows { Read-ReadyBootEntries $selection[0] $selection[1] $selection[2] $script:SnapshotGuid | Out-Null } 'Boot entry without a terminated description was accepted.'
}

# Exercise the real imported receipt reader with the handoff-local reference.
$referenceRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..\..'))
$referenceBoundary=Join-Path $referenceRepo 'spoof-handoff'
$referenceReceipt=Join-Path $referenceBoundary 'artifacts\smbios-seven-probe\inventory\shim-chain-stage.json'
Invoke-SnapshotCase 'Explicit reference receipt binds the real reader without changing original source paths' {
    $bound=& {
        . $helperPath -RepoRoot $referenceRepo -OutputBoundary $referenceBoundary -ReferenceReceiptPath $referenceReceipt -LibraryOnly
        $receipt=Read-ShimReceipt
        [pscustomobject]@{path=$script:ShimReceiptPath;hash=$receipt.bootManagerSha256;inventory=$script:InventoryBuild}
    }
    Assert-SnapshotCondition ($bound.path -ceq $referenceReceipt) 'The imported reader selected a different receipt.'
    Assert-SnapshotCondition ($bound.inventory -ceq (Join-Path $referenceRepo '.codex-build\efi_inventory')) 'The reference changed the original source inventory.'
    Assert-SnapshotCondition ($bound.hash -cmatch '^[A-F0-9]{64}$') 'The actual package receipt was not parsed.'
}
Invoke-SnapshotCase 'Missing reference receipt is rejected before any firmware action' {
    Assert-SnapshotThrows { . $helperPath -RepoRoot $referenceRepo -OutputBoundary $referenceBoundary -ReferenceReceiptPath (Join-Path $referenceBoundary 'missing-reference.json') -LibraryOnly } 'Missing receipt accepted.'
}
Invoke-SnapshotCase 'Reference receipt outside the handoff package is rejected' {
    Assert-SnapshotThrows { . $helperPath -RepoRoot $referenceRepo -OutputBoundary $referenceBoundary -ReferenceReceiptPath (Join-Path $referenceRepo 'components\hypervisor\hypersinv.cer') -LibraryOnly } 'Outside receipt accepted.'
}

Write-Host ('PASS total: ' + $script:SnapshotPassed)
