[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
Import-Module (Join-Path $ToolRoot 'GpuFirmwareComparison.psm1') -Force

$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()
$script:WindowsId = '9dea862c-5cdd-4e70-acc1-f32b344d4795'
$script:UsbAId = '6cc7730c-c9c3-11f1-847b-806e6f6e6963'
$script:UsbBId = '6cc7730d-c9c3-11f1-847b-806e6f6e6963'
$script:DefaultGenericIds = [ordered]@{
    'CD/DVD' = 'd81f168e-cd6f-11f1-8b27-806e6f6e6963'
    'Removable' = 'd81f168f-cd6f-11f1-8b27-806e6f6e6963'
    'Network' = 'd81f1690-cd6f-11f1-8b27-806e6f6e6963'
}
$script:GenericDescriptions = [ordered]@{
    'CD/DVD' = 'UEFI:CD/DVD Drive'
    'Removable' = 'UEFI:Removable Device'
    'Network' = 'UEFI:Network Device'
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-Throws {
    param([scriptblock]$Operation, [string]$Message)
    $observed = $null
    try { $null = & $Operation } catch { $observed = [string]$_.Exception.Message }
    Assert-True ($null -ne $observed) "$Message (operation did not reject the input)"
}

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try { & $Body; $script:PassedCases++; Write-Output "PASS: $Name" }
    catch { $script:FailedCases.Add("$Name`: $($_.Exception.Message)"); Write-Output "FAIL: $Name`: $($_.Exception.Message)" }
}

function Add-FirmwareBlock {
    param([Collections.Generic.List[string]]$Lines, [string]$Title, [string[]]$Body)
    if ($Lines.Count -gt 0) { $Lines.Add('') }
    $Lines.Add($Title)
    $Lines.Add('-' * $Title.Length)
    foreach ($line in $Body) { $Lines.Add([string]$line) }
}

function New-FirmwareFixture {
    param(
        [string[]]$GenericTypes = @('CD/DVD', 'Removable', 'Network'),
        [Collections.IDictionary]$GenericIds = $script:DefaultGenericIds,
        [string[]]$GenericReferenceTypes = $null,
        [string[]]$UsbReferenceOrder = @('A', 'B'),
        [string[]]$UsbBlockOrder = @('A', 'B'),
        [string[]]$GenericBlockOrder = @('CD/DVD', 'Removable', 'Network'),
        [string[]]$RawReferenceIds = $null,
        [switch]$GenericExtraField,
        [switch]$GenericDescriptionSuffix,
        [string]$DuplicateGenericType = '',
        [switch]$ChangedUsbPath,
        [switch]$ChangedUsbDescription,
        [switch]$ChangedWindowsPath,
        [switch]$AddUnknownEntry,
        [switch]$LocalizeManager
    )

    if ($null -eq $GenericReferenceTypes) { $GenericReferenceTypes = @($GenericTypes) }
    $genericGuids = [Collections.Generic.List[string]]::new()
    foreach ($type in $GenericReferenceTypes) { $genericGuids.Add([string]$GenericIds[$type]) }
    if ($null -eq $RawReferenceIds) {
        $orderedReferences = [Collections.Generic.List[string]]::new()
        $orderedReferences.Add($script:WindowsId)
        foreach ($usb in $UsbReferenceOrder) {
            if ($usb -eq 'A') { $orderedReferences.Add($script:UsbAId) }
            elseif ($usb -eq 'B') { $orderedReferences.Add($script:UsbBId) }
            else { throw "Unknown USB test identifier '$usb'." }
        }
        foreach ($guid in $genericGuids) { $orderedReferences.Add($guid) }
        $RawReferenceIds = $orderedReferences.ToArray()
    }

    $lines = [Collections.Generic.List[string]]::new()
    $managerTitle = if ($LocalizeManager) { 'Administrador de arranque de firmware' } else { 'Firmware Boot Manager' }
    $managerBody = [Collections.Generic.List[string]]::new()
    $managerBody.Add('identifier              {a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}')
    $managerBody.Add(('displayorder            {' + [string]$RawReferenceIds[0] + '}'))
    for ($i = 1; $i -lt $RawReferenceIds.Count; $i++) { $managerBody.Add(('                        {' + [string]$RawReferenceIds[$i] + '}')) }
    $managerBody.Add('timeout                 0')
    Add-FirmwareBlock $lines $managerTitle $managerBody.ToArray()

    $windowsPath = if ($ChangedWindowsPath) { '\EFI\Microsoft\Boot\changed.efi' } else { '\EFI\Microsoft\Boot\bootmgfw.efi' }
    Add-FirmwareBlock $lines 'Windows Boot Manager' @(
        ('identifier              {' + $script:WindowsId + '}')
        'device                  partition=S:'
        ('path                    ' + $windowsPath)
        'description             Windows Boot Manager'
        'locale                  en-US'
        'inherit                 {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}'
        'default                 {71152aeb-45e9-11ed-8140-a26d57e4c014}'
        'resumeobject            {71152aea-45e9-11ed-8140-a26d57e4c014}'
        'displayorder            {71152aeb-45e9-11ed-8140-a26d57e4c014}'
        'toolsdisplayorder       {b2721d73-1db4-4c62-bf78-c548a880142d}'
        'timeout                 30'
    )

    $usbAPath = if ($ChangedUsbPath) { 'device                  partition=F:' } else { 'device                  partition=D:' }
    $usbBDescription = if ($ChangedUsbDescription) { 'description             UEFI: VendorCo ProductCode 2.01, Partition 1' } else { 'description             UEFI: VendorCo ProductCode 2.00, Partition 1' }
    $usbBodies = @{
        A = @(
            ('identifier              {' + $script:UsbAId + '}')
            $usbAPath
            'description             UEFI: Memorex USB Flash Drive PMAP, Partition 1'
        )
        B = @(
            ('identifier              {' + $script:UsbBId + '}')
            'device                  partition=E:'
            $usbBDescription
        )
    }
    foreach ($usb in $UsbBlockOrder) {
        if (-not $usbBodies.ContainsKey($usb)) { throw "Unknown USB block '$usb'." }
        Add-FirmwareBlock $lines 'Firmware Application (101fffff)' $usbBodies[$usb]
    }

    foreach ($type in $GenericBlockOrder) {
        if ($GenericTypes -notcontains $type) { continue }
        $description = [string]$script:GenericDescriptions[$type]
        if ($GenericDescriptionSuffix -and $type -eq 'CD/DVD') { $description += ', device=partition=Z:' }
        $body = [Collections.Generic.List[string]]::new()
        $body.Add(('identifier              {' + [string]$GenericIds[$type] + '}'))
        $body.Add(('description             ' + $description))
        if ($GenericExtraField -and $type -eq 'CD/DVD') { $body.Add('device                  partition=Z:') }
        Add-FirmwareBlock $lines 'Firmware Application (101fffff)' $body.ToArray()
    }
    if (-not [string]::IsNullOrEmpty($DuplicateGenericType)) {
        if (-not $script:GenericDescriptions.Contains($DuplicateGenericType)) { throw "Unknown duplicate generic type '$DuplicateGenericType'." }
        Add-FirmwareBlock $lines 'Firmware Application (101fffff)' @(
            'identifier              {dddddddd-dddd-4ddd-8ddd-dddddddddddd}'
            ('description             ' + [string]$script:GenericDescriptions[$DuplicateGenericType])
        )
    }
    if ($AddUnknownEntry) {
        Add-FirmwareBlock $lines 'Firmware Application (101fffff)' @(
            'identifier              {22222222-3333-4444-5555-666666666666}'
            'description             UEFI: Vendor Unknown Device'
        )
    }
    $lines.Insert(0, '')
    return [string]::Join("`n", $lines.ToArray())
}

$baseline = New-FirmwareFixture

Invoke-TestCase 'exact text and generic inventory parse' {
    $parsed = ConvertFrom-GpuFirmwareText -Text $baseline
    Assert-True ($parsed.SchemaVersion -eq 1) 'Unexpected schema version.'
    Assert-True ($parsed.StableEntryCount -eq 4) 'Expected firmware manager, Windows manager, and two USB entries.'
    Assert-True (@($parsed.GenericEntries).Count -eq 3) 'All three generic entries should parse.'
    Assert-True (@($parsed.GenericEntries | Where-Object { -not $_.Referenced }).Count -eq 0) 'All generic fixture entries should be referenced.'
}

Invoke-TestCase 'line endings and final blank lines normalize' {
    $variant = $baseline.Replace("`n", "`r`n") + "`r`n`r`n"
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True $result.Equivalent 'Equivalent fixture was rejected.'
    Assert-True (-not $result.TextEqual) 'TextEqual must describe the original ordinal strings.'
}

Invoke-TestCase 'generic GUID replacement is equivalent and reported' {
    $ids = [ordered]@{
        'CD/DVD' = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
        'Removable' = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'
        'Network' = 'cccccccc-cccc-4ccc-8ccc-cccccccccccc'
    }
    $variant = New-FirmwareFixture -GenericIds $ids
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True $result.Equivalent 'Replacing only generic identifiers should be equivalent.'
    Assert-True (-not $result.TextEqual) 'Raw text should report the changed generic GUIDs.'
    Assert-True ($result.BaselineStableSha256 -ceq $result.CurrentStableSha256) 'Stable hashes differ after generic GUID replacement.'
    Assert-True (@($result.GenericChanges | Where-Object { $_.Change -ne 'IdentifierReplaced' }).Count -eq 0) 'Generic GUID replacements were not reported by type.'
}

Invoke-TestCase 'generic removal is equivalent' {
    $variant = New-FirmwareFixture -GenericTypes @()
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True $result.Equivalent 'Removing generic placeholder entries should be equivalent.'
    Assert-True (@($result.CurrentGenericEntries).Count -eq 0) 'Current generic entries should be empty.'
    Assert-True (@($result.GenericChanges | Where-Object { $_.Change -ne 'Removed' }).Count -eq 0) 'Generic removals were not reported.'
}

Invoke-TestCase 'generic addition is equivalent' {
    $without = New-FirmwareFixture -GenericTypes @()
    $withOne = New-FirmwareFixture -GenericTypes @('CD/DVD')
    $result = Compare-GpuFirmwareText -BaselineText $without -CurrentText $withOne
    Assert-True $result.Equivalent 'Adding one approved generic placeholder should be equivalent.'
    Assert-True ($result.GenericChanges[0].Change -eq 'Added') 'The generic addition was not reported.'
}

Invoke-TestCase 'every generic block must have one displayorder reference' {
    $variant = New-FirmwareFixture -GenericTypes @('CD/DVD', 'Removable') -GenericReferenceTypes @('CD/DVD')
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'An unreferenced generic block must reject.'
}

Invoke-TestCase 'duplicate generic placeholder types are rejected' {
    $variant = New-FirmwareFixture -DuplicateGenericType 'CD/DVD'
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Duplicate generic placeholder types must reject.'
}

Invoke-TestCase 'stable USB device change is rejected' {
    $variant = New-FirmwareFixture -ChangedUsbPath
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True (-not $result.Equivalent) 'A physical USB device path change must not be normalized.'
}

Invoke-TestCase 'stable USB description change is rejected' {
    $variant = New-FirmwareFixture -ChangedUsbDescription
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True (-not $result.Equivalent) 'A physical USB description change must not be normalized.'
}

Invoke-TestCase 'Windows Boot Manager path change is rejected' {
    $variant = New-FirmwareFixture -ChangedWindowsPath
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True (-not $result.Equivalent) 'Windows Boot Manager path change must not be normalized.'
}

Invoke-TestCase 'stable displayorder change is rejected' {
    $variant = New-FirmwareFixture -UsbReferenceOrder @('B', 'A')
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True (-not $result.Equivalent) 'Stable firmware displayorder changes must not be normalized.'
}

Invoke-TestCase 'stable firmware block order change is rejected' {
    $variant = New-FirmwareFixture -UsbBlockOrder @('B', 'A')
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True (-not $result.Equivalent) 'Stable firmware block order changes must not be normalized.'
}

Invoke-TestCase 'generic block order changes are equivalent' {
    $variant = New-FirmwareFixture -GenericBlockOrder @('Network', 'Removable', 'CD/DVD')
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True $result.Equivalent 'Moving approved generic blocks should not change stable firmware content.'
}

Invoke-TestCase 'unknown additional firmware entry remains stable content' {
    $variant = New-FirmwareFixture -AddUnknownEntry
    $result = Compare-GpuFirmwareText -BaselineText $baseline -CurrentText $variant
    Assert-True (-not $result.Equivalent) 'An unknown new firmware entry must not be ignored.'
}

Invoke-TestCase 'generic placeholder with extra field is rejected' {
    $variant = New-FirmwareFixture -GenericExtraField
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Extra fields on a generic placeholder must reject.'
}

Invoke-TestCase 'generic placeholder with device data in description is rejected' {
    $variant = New-FirmwareFixture -GenericDescriptionSuffix
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Device data attached to a generic description must reject.'
}

Invoke-TestCase 'duplicate identifiers are rejected' {
    $variant = $baseline.Replace('{6cc7730d-c9c3-11f1-847b-806e6f6e6963}', '{6cc7730c-c9c3-11f1-847b-806e6f6e6963}')
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Duplicate firmware identifiers must reject.'
}

Invoke-TestCase 'unresolved displayorder references are rejected' {
    $variant = $baseline.Replace("                        {6cc7730d-c9c3-11f1-847b-806e6f6e6963}", "                        {11111111-2222-4333-8444-555555555555}")
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Unresolved firmware displayorder references must reject.'
}

Invoke-TestCase 'duplicate displayorder references are rejected' {
    $variant = $baseline.Replace("                        {6cc7730d-c9c3-11f1-847b-806e6f6e6963}", "                        {6cc7730c-c9c3-11f1-847b-806e6f6e6963}")
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Duplicate firmware displayorder references must reject.'
}

Invoke-TestCase 'generic references before stable entries are rejected' {
    $references = @($script:DefaultGenericIds['CD/DVD'], $script:WindowsId, $script:UsbAId, $script:UsbBId)
    $variant = New-FirmwareFixture -RawReferenceIds $references
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Generic references before Windows Boot Manager must reject.'
}

Invoke-TestCase 'Windows Boot Manager must be first retained entry' {
    $references = @($script:UsbAId, $script:WindowsId, $script:UsbBId, $script:DefaultGenericIds['CD/DVD'], $script:DefaultGenericIds['Removable'], $script:DefaultGenericIds['Network'])
    $variant = New-FirmwareFixture -RawReferenceIds $references
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'A physical entry before Windows Boot Manager must reject.'
}

Invoke-TestCase 'generic references interleaved with stable entries are rejected' {
    $references = @($script:WindowsId, $script:UsbAId, $script:DefaultGenericIds['CD/DVD'], $script:UsbBId)
    $variant = New-FirmwareFixture -RawReferenceIds $references
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Generic references between stable entries must reject.'
}

Invoke-TestCase 'generic references must follow their fixed relative order' {
    $variant = New-FirmwareFixture -GenericReferenceTypes @('Network', 'CD/DVD')
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Generic reference order changes must reject.'
}

Invoke-TestCase 'localized manager output is rejected' {
    $variant = New-FirmwareFixture -LocalizeManager
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Localized firmware output must reject.'
}

Invoke-TestCase 'malformed displayorder continuation is rejected' {
    $variant = $baseline.Replace("                        {6cc7730c-c9c3-11f1-847b-806e6f6e6963}", '                        not-a-guid')
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Malformed displayorder continuation must reject.'
}

Invoke-TestCase 'lone carriage return is rejected' {
    $variant = $baseline.Replace("`n", "`r")
    Assert-Throws { ConvertFrom-GpuFirmwareText -Text $variant } 'Lone carriage returns must reject.'
}

if ($script:FailedCases.Count -gt 0) {
    throw "Firmware comparison tests failed ($($script:FailedCases.Count)/$($script:Assertions) assertions): $([string]::Join(' | ', $script:FailedCases.ToArray()))"
}
[pscustomobject]@{
    Status = 'PASS'
    TestCases = $script:PassedCases
    Assertions = $script:Assertions
    FilesystemChangesPerformed = $false
    NativeCommandsExecuted = $false
} | ConvertTo-Json -Depth 4
