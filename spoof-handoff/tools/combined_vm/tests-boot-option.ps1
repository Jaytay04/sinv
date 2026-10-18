[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'boot-option.psm1') -Force
$checks = [Collections.Generic.List[object]]::new()

function Assert-Equal($Expected, $Actual, [string]$Name) {
    if ($Expected -is [byte[]] -and $Actual -is [byte[]]) {
        $equal = [Convert]::ToBase64String($Expected) -ceq [Convert]::ToBase64String($Actual)
    } else { $equal = [string]$Expected -ceq [string]$Actual }
    if (-not $equal) { throw "$Name mismatch: expected=$Expected actual=$Actual" }
    $script:checks.Add([ordered]@{ name = $Name; expected = $Expected; actual = $Actual; pass = $true })
}

function Assert-Throws([scriptblock]$Action, [string]$Name) {
    $threw = $false
    try { & $Action } catch { $threw = $true }
    if (-not $threw) { throw "$Name did not reject malformed data." }
    $script:checks.Add([ordered]@{ name = $Name; pass = $true })
}

$partitionGuid = [Guid]'23374794-afa5-4795-a1a1-a03c6e4ef276'
$efiPath = '\EFI\HypersinvInventory\fixture-correction\smbios2-ep-prelude.efi'
$bytes = New-EfiBootLoadOptionBytes -Description 'Hypersinv SMBIOS fixture prelude' -PartitionNumber 1 -PartitionStartLba 2048 -PartitionSizeLba 262144 -PartitionGuid $partitionGuid -FilePath $efiPath
$parsed = Read-EfiBootLoadOptionBytes -Bytes $bytes
Assert-Equal 1 $parsed.attributes 'active load option attributes'
Assert-Equal 'Hypersinv SMBIOS fixture prelude' $parsed.description 'description round trip'
Assert-Equal 1 $parsed.hardDrive.partitionNumber 'partition number round trip'
Assert-Equal 2048 $parsed.hardDrive.partitionStartLba 'partition start round trip'
Assert-Equal 262144 $parsed.hardDrive.partitionSizeLba 'partition size round trip'
Assert-Equal $partitionGuid.ToString('D') $parsed.hardDrive.partitionGuid 'GPT GUID round trip'
Assert-Equal 2 $parsed.hardDrive.mbrType 'GPT MBR-type marker'
Assert-Equal 2 $parsed.hardDrive.signatureType 'GPT signature-type marker'
Assert-Equal $efiPath $parsed.filePath 'EFI path round trip'

$extendedAttributes = [uint32]2147483649
$extendedBytes = New-EfiBootLoadOptionBytes -Description 'Attribute round trip' -Attributes $extendedAttributes -PartitionNumber 1 -PartitionStartLba 2048 -PartitionSizeLba 262144 -PartitionGuid $partitionGuid -FilePath $efiPath
$extendedParsed = Read-EfiBootLoadOptionBytes -Bytes $extendedBytes
Assert-Equal $extendedAttributes $extendedParsed.attributes 'full 32-bit load option attributes round trip'
Assert-Equal $true $extendedParsed.active 'active bit remains independently decoded'

$bootNext = ConvertFrom-EfiBootNextBytes -Bytes ([byte[]](6, 0))
Assert-Equal 6 $bootNext 'BootNext little-endian decode'
Assert-Equal 'Boot0006' (ConvertTo-EfiBootVariableName -Number $bootNext) 'Boot variable name format'
$bootOrder = ConvertFrom-EfiBootOrderBytes -Bytes ([byte[]](5,0,0,0,2,0))
Assert-Equal '5,0,2' ($bootOrder -join ',') 'BootOrder little-endian decode'
Assert-Throws { ConvertFrom-EfiBootOrderBytes -Bytes ([byte[]](5,0,5,0)) } 'duplicate BootOrder entry guard'
Assert-Throws { Assert-EfiBootNextPointsTo -Bytes ([byte[]](7,0)) -ExpectedNumber 6 } 'unrelated BootNext target guard'

$ownedRecord = [pscustomobject]@{ supported = $true; attributes = [uint32]7; bytesBase64 = [Convert]::ToBase64String($bytes) }
if (-not (Test-EfiVariableRecordEquals -Actual $ownedRecord -ExpectedBytes $bytes -ExpectedAttributes 7)) { throw 'Exact owned firmware value check rejected matching bytes and attributes.' }
$checks.Add([ordered]@{ name = 'exact owned Boot#### bytes and attributes'; pass = $true })
$wrongRecord = [pscustomobject]@{ supported = $true; attributes = [uint32]7; bytesBase64 = [Convert]::ToBase64String([byte[]](1,2,3)) }
if (Test-EfiVariableRecordEquals -Actual $wrongRecord -ExpectedBytes $bytes -ExpectedAttributes 7) { throw 'Exact ownership check accepted different bytes.' }
$checks.Add([ordered]@{ name = 'owned firmware value rejects different bytes'; pass = $true })
$wrongAttributes = [pscustomobject]@{ supported = $true; attributes = [uint32]6; bytesBase64 = [Convert]::ToBase64String($bytes) }
if (Test-EfiVariableRecordEquals -Actual $wrongAttributes -ExpectedBytes $bytes -ExpectedAttributes 7) { throw 'Exact ownership check accepted different attributes.' }
$checks.Add([ordered]@{ name = 'owned firmware value rejects different attributes'; pass = $true })

Assert-Throws { ConvertFrom-EfiBootNextBytes -Bytes ([byte[]](6)) } 'BootNext length guard'
Assert-Throws { ConvertFrom-EfiBootOrderBytes -Bytes ([byte[]](5,0,0)) } 'BootOrder length guard'
Assert-Throws { Read-EfiBootLoadOptionBytes -Bytes ([byte[]](1,2,3)) } 'truncated EFI_LOAD_OPTION guard'
Assert-Throws { New-EfiBootLoadOptionBytes -Description 'bad' -PartitionNumber 1 -PartitionStartLba 0 -PartitionSizeLba 1 -PartitionGuid $partitionGuid -FilePath 'EFI\\bad.efi' } 'absolute EFI path guard'

$missingEnd = [byte[]]::new($bytes.Length - 4)
[Array]::Copy($bytes, $missingEnd, $missingEnd.Length)
$newPathLength = [uint16]([BitConverter]::ToUInt16($missingEnd, 4) - 4)
[Array]::Copy([BitConverter]::GetBytes($newPathLength), 0, $missingEnd, 4, 2)
Assert-Throws { Read-EfiBootLoadOptionBytes -Bytes $missingEnd } 'missing terminal end node guard'

$trailingNode = [byte[]]::new($bytes.Length + 4)
[Array]::Copy($bytes, $trailingNode, $bytes.Length)
$trailingNode[$bytes.Length] = 0x7f
$trailingNode[$bytes.Length + 1] = 0xff
$trailingNode[$bytes.Length + 2] = 4
$trailingNode[$bytes.Length + 3] = 0
$withTrailingLength = [uint16]([BitConverter]::ToUInt16($trailingNode, 4) + 4)
[Array]::Copy([BitConverter]::GetBytes($withTrailingLength), 0, $trailingNode, 4, 2)
Assert-Throws { Read-EfiBootLoadOptionBytes -Bytes $trailingNode } 'trailing device path node guard'

$duplicateFileNode = [byte[]]::new($bytes.Length + ($parsed.filePath.Length + 1) * 2 + 4)
$fileNodeOffset = $bytes.Length - 4 - (($parsed.filePath.Length + 1) * 2 + 4)
[Array]::Copy($bytes, 0, $duplicateFileNode, 0, $fileNodeOffset)
$fileNodeLength = $bytes.Length - 4 - $fileNodeOffset
[Array]::Copy($bytes, $fileNodeOffset, $duplicateFileNode, $fileNodeOffset, $fileNodeLength)
[Array]::Copy($bytes, $fileNodeOffset, $duplicateFileNode, $fileNodeOffset + $fileNodeLength, $fileNodeLength)
[Array]::Copy($bytes, $bytes.Length - 4, $duplicateFileNode, $duplicateFileNode.Length - 4, 4)
$duplicatePathLength = [uint16]([BitConverter]::ToUInt16($duplicateFileNode, 4) + $fileNodeLength)
[Array]::Copy([BitConverter]::GetBytes($duplicatePathLength), 0, $duplicateFileNode, 4, 2)
Assert-Throws { Read-EfiBootLoadOptionBytes -Bytes $duplicateFileNode } 'duplicate EFI file path node guard'

$report = [ordered]@{ schema = 'combined-vm-efi-load-option-tests.v1'; status = 'PASS'; checkCount = $checks.Count; checks = @($checks.ToArray()) }
$report | ConvertTo-Json -Depth 8
