[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Original','Active','Restored')][string]$Phase,
    [Parameter(Mandatory=$true)][string]$GuestInventoryPath,
    [Parameter(Mandatory=$true)][string]$FixtureMapPath,
    [Parameter(Mandatory=$true)][string]$SmbiosReaderPath,
    [switch]$UseSharedAssets,
    [string]$OutputDirectory='C:\Users\jayli\OneDrive\Documents\Projects\hsinv\spoof-handoff\reports\combined-9field-vm-20261018\runtime'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
$mainRoot='C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$reportRoot=Join-Path $mainRoot 'spoof-handoff\reports'
$expectedOutput=Join-Path $reportRoot 'combined-9field-vm-20261018\runtime'
$handoff=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $handoff 'tools\combined_host\model.psm1') -Force
function Assert-RenderPath([string]$Path,[string]$Boundary) {
    $full=[IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith(([IO.Path]::GetFullPath($Boundary).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)) { throw ('VM render input escapes its allowed root: '+$full) }
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw ('Missing VM render input: '+$full) }
    return $full
}
if (-not [IO.Path]::GetFullPath($OutputDirectory).Equals($expectedOutput,[StringComparison]::OrdinalIgnoreCase)) { throw 'OutputDirectory must be the combined VM runtime evidence directory.' }
$inventoryPath=Assert-RenderPath $GuestInventoryPath $reportRoot
$mapPath=Assert-RenderPath $FixtureMapPath $reportRoot
$readerPath=Assert-RenderPath $SmbiosReaderPath (Join-Path $mainRoot 'spoof-handoff\artifacts')
$inventory=Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
if ($inventory.schema -cne 'combined-vm-original-inventory.v1' -or [string]$inventory.machine.manufacturer -cne 'VMware, Inc.' -or [string]$inventory.machine.model -cne 'VMware20,1' -or [string]$inventory.machine.uuidWmi -cne [string]$inventory.machine.uuidSmbios) { throw 'Fresh agreeing VMware original inventory is required.' }
if ([string]$inventory.disk.pnpDeviceId -notmatch '^SCSI\\DISK&VEN_NVME&PROD_VMWARE_VIRTUAL_N\\' -or [string]$inventory.disk.nativeSerial -cne [string]$inventory.disk.wmiSerial) { throw 'Inventory must identify the current VMware NVMe original through both readers.' }
$map=New-CombinedBindingMap (Get-CombinedMapRows $mapPath)
$canonical=New-CombinedBindingMap (Get-CombinedMapRows (Join-Path $mainRoot 'spoof-handoff\reports\client-originals.tsv'))
foreach ($binding in @($map.bindings | Where-Object category -CNE 'NIC')) {
    $expected=@($canonical.bindings | Where-Object member -CEQ $binding.member)
    if ($expected.Count -ne 1 -or [string]$binding.replacement -cne [string]$expected[0].replacement) { throw ('VM replacement differs from canonical value: '+$binding.member) }
    if ($binding.category -ceq 'SMBIOS') {
        $field=$inventory.smbiosCapture.fields.($binding.field)
        if ([string]$binding.original -cne [string]$field.observedNative -or [string]$binding.original -cne [string]$field.observedWmi -or $field.nativeMatchesWmi -ne $true) { throw ('VM SMBIOS original lacks fresh two-reader provenance: '+$binding.field) }
    } elseif ([string]$binding.original -cne [string]$inventory.disk.nativeSerial) { throw 'VM disk original differs from fresh inventory.' }
}
$nicBindings=@($map.bindings | Where-Object category -CEQ 'NIC')
if ((@($nicBindings.replacement | Sort-Object) -join '|') -cne (@($canonical.bindings | Where-Object category -CEQ 'NIC' | ForEach-Object replacement | Sort-Object) -join '|')) { throw 'VM NIC replacement set differs from locked values.' }
if (@($nicBindings | Where-Object pnpDeviceId -Match '^ROOT\\NET\\[0-9]{4}$').Count -ne 2 -or @($nicBindings | Where-Object pnpDeviceId -Match '^PCI\\VEN_15AD&DEV_07B0&').Count -ne 2) { throw 'VM fixture must contain two actual VMnet and two vmxnet3 adapters.' }
foreach ($binding in $nicBindings) {
    # The adapter prefix is its real guest IfIndex, not a portable host slot.
    # This clone retains the four captured original MACs as binding provenance.
    $originalMac=ConvertTo-CombinedComparableValue NIC mac $binding.original
    $canonicalNic=@($canonical.bindings | Where-Object { $_.category -ceq 'NIC' -and (ConvertTo-CombinedComparableValue NIC mac $_.original) -ceq $originalMac })
    if ($canonicalNic.Count -ne 1 -or [string]$binding.replacement -cne [string]$canonicalNic[0].replacement) { throw ('VM replacement differs for original MAC binding: '+$binding.original) }
    $nativeRows=@($inventory.nic.getNetAdapter | Where-Object PnPDeviceID -CEQ $binding.pnpDeviceId)
    $wmiRows=@($inventory.nic.wmiAdapters | Where-Object PNPDeviceID -CEQ $binding.pnpDeviceId)
    $wanted=ConvertTo-CombinedComparableValue NIC mac $binding.original
    if ($nativeRows.Count -ne 1 -or $wmiRows.Count -ne 1 -or (ConvertTo-CombinedComparableValue NIC mac $nativeRows[0].MacAddress) -cne $wanted -or (ConvertTo-CombinedComparableValue NIC mac $wmiRows[0].MACAddress) -cne $wanted) { throw ('VM NIC original lacks exact native/WMI provenance: '+$binding.pnpDeviceId) }
}
$bundle=Join-Path $reportRoot 'nic-vmnet-compatibility-20261018\production-address-functions.ps1'
$parityPath=Join-Path $reportRoot 'nic-vmnet-compatibility-20261018\tested-function-parity.json'
$hostSource=Join-Path $handoff 'tools\nic_vm\host-address-roundtrip.ps1'
$bundleHash=(Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash
$parity=Get-Content -LiteralPath $parityPath -Raw | ConvertFrom-Json
if ($bundleHash -cne 'E7B1B841372B890C0D1638E6CCB11D41B5F257A64FBF491B3FEF811B29A212D8' -or $parity.status -cne 'PASS' -or [string]$parity.bundleSha256 -cne $bundleHash -or [string]$parity.currentSourceSha256 -cne (Get-FileHash -LiteralPath $hostSource -Algorithm SHA256).Hash) { throw 'Tested NIC helper bundle or production parity pin differs.' }
$inputs=[ordered]@{
    'model.psm1'=(Join-Path $handoff 'tools\combined_host\model.psm1')
    'readback.ps1'=(Join-Path $handoff 'tools\combined_host\readback.ps1')
    'nic-read.ps1'=(Join-Path $handoff 'tools\nic_vm_probe\read-host.ps1')
    'storage-read.ps1'=(Join-Path $handoff 'tools\storage_host\read.ps1')
    'address-functions.ps1'=$bundle
    'locked.tsv'=$mapPath
    'smbios_windows_readback.exe'=$readerPath
    'nic_native_read.exe'=(Join-Path $mainRoot 'spoof-handoff\artifacts\nic-reader-gap-20261017\native-build-vs18\Release\nic_native_read.exe')
}
$assets=[Collections.Generic.List[object]]::new()
$pins=[Collections.Generic.List[object]]::new()
$assetSetRoot=$null
$assetSetId=$null
if ($UseSharedAssets) {
    $setText=@($inputs.Keys | ForEach-Object { $_+':'+(Get-FileHash -LiteralPath $inputs[$_] -Algorithm SHA256).Hash }) -join "`n"
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $assetSetId=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($setText)))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $assetSetRoot=Join-Path (Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\shared-readonly') $assetSetId
    [void][IO.Directory]::CreateDirectory($assetSetRoot)
}
foreach ($name in $inputs.Keys) {
    $path=Assert-RenderPath $inputs[$name] $mainRoot
    $bytes=[IO.File]::ReadAllBytes($path)
    try {
        $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($UseSharedAssets) {
            $destination=Join-Path $assetSetRoot $name
            if (Test-Path -LiteralPath $destination) {
                if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $hash) { throw ('Existing shared VM asset differs: '+$destination) }
            } else { [IO.File]::WriteAllBytes($destination,$bytes) }
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $hash) { throw ('Shared VM asset copy hash mismatch: '+$destination) }
            $assets.Add([ordered]@{name=$name;sha256=$hash;sourcePath=('\\vmware-host\Shared Folders\CombinedFixture\'+$assetSetId+'\'+$name)})
        } else { $assets.Add([ordered]@{name=$name;sha256=$hash;base64=[Convert]::ToBase64String($bytes)}) }
        $pins.Add([ordered]@{name=$name;path=$path;sha256=$hash;bytes=$bytes.Length})
    } finally { [Array]::Clear($bytes,0,$bytes.Length) }
}
$profile=[ordered]@{schema='combined-vm-phase-profile.v1';originalUuid=[string]$inventory.machine.uuidWmi;diskPnp=[string]$inventory.disk.pnpDeviceId;nicPnp=@($nicBindings.pnpDeviceId);mapSha256=(Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash}
$identityColumn=if($Phase -ceq 'Active'){'replacement'}else{'original'}
$profile.expectedIdentity=[ordered]@{
    manufacturer=[string]@($map.bindings | Where-Object member -CEQ 'FakeSMBIOSManufacturer')[0].$identityColumn
    model=[string]@($map.bindings | Where-Object member -CEQ 'FakeSMBIOSProduct')[0].$identityColumn
    uuid=[string]@($map.bindings | Where-Object member -CEQ 'FakeSMBIOSUUID')[0].$identityColumn
}
$templatePath=Join-Path $PSScriptRoot 'guest-active.template.ps1'
$rendered=[IO.File]::ReadAllText($templatePath)
foreach ($token in @('__PHASE__','__PROFILE_BASE64__','__ASSETS_BASE64__')) {
    $count=([regex]::Matches($rendered,[regex]::Escape($token))).Count
    $expectedCount=if($token -ceq '__PHASE__'){2}else{1}
    if ($count -ne $expectedCount) { throw ('Template placeholder count differs: '+$token+' actual='+$count) }
}
$profileBase64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($profile | ConvertTo-Json -Depth 6 -Compress)))
$assetBase64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject @($assets.ToArray()) -Depth 5 -Compress)))
$rendered=$rendered.Replace('__PHASE__',$Phase).Replace('__PROFILE_BASE64__',$profileBase64).Replace('__ASSETS_BASE64__',$assetBase64)
if ($rendered -match '__[A-Z0-9_]+__') { throw 'Rendered VM script contains an unresolved placeholder.' }
$tokens=$null;$parseErrors=$null
[void][Management.Automation.Language.Parser]::ParseInput($rendered,[ref]$tokens,[ref]$parseErrors)
if (@($parseErrors).Count -gt 0) { throw ('Rendered VM script syntax error: '+($parseErrors.Message -join '; ')) }
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$path=Join-Path $OutputDirectory ('guest-'+$Phase.ToLowerInvariant()+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'.'+[Guid]::NewGuid().ToString('N')+'.ps1')
[IO.File]::WriteAllText($path,$rendered,[Text.UTF8Encoding]::new($false))
$receiptPath=[IO.Path]::ChangeExtension($path,'.render.json')
$receipt=[ordered]@{schema='combined-vm-phase-render.v1';phase=$Phase;renderedUtc=[DateTime]::UtcNow.ToString('o');template=[ordered]@{path=$templatePath;sha256=(Get-FileHash -LiteralPath $templatePath -Algorithm SHA256).Hash};inventory=[ordered]@{path=$inventoryPath;sha256=(Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash};fixtureMap=[ordered]@{path=$mapPath;sha256=$profile.mapSha256};assetPins=@($pins.ToArray());sharedAssets=[ordered]@{enabled=[bool]$UseSharedAssets;hostRoot=$assetSetRoot;setId=$assetSetId;requiredShareName='CombinedFixture';guestWriteAccessPermitted=$false};renderedScriptPath=$path;renderedScriptSha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;guestMutationPerformed=$false;hostMutationPerformed=$false}
[IO.File]::WriteAllText($receiptPath,($receipt | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
[pscustomobject]@{scriptPath=$path;sha256=$receipt.renderedScriptSha256;receiptPath=$receiptPath;phase=$Phase} | ConvertTo-Json
