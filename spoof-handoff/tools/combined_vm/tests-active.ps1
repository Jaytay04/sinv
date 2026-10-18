[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GuestInventoryPath,
    [Parameter(Mandatory=$true)][string]$FixtureMapPath,
    [Parameter(Mandatory=$true)][string]$SmbiosReaderPath,
    [switch]$UseSharedAssets
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$repo='C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$reportRoot=Join-Path $repo 'spoof-handoff\reports\combined-9field-vm-20261018'
$testRoot=Join-Path $reportRoot ('phase-tests.'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($testRoot)
$records=[Collections.Generic.List[object]]::new()
$renderer=Join-Path $PSScriptRoot 'render-active.ps1'
function Invoke-RenderTest([string]$Name,[string]$Inventory,[string]$Map,[string]$ExpectedError) {
    $priorPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        $renderArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$renderer,'-Phase','Original','-GuestInventoryPath',$Inventory,'-FixtureMapPath',$Map,'-SmbiosReaderPath',$SmbiosReaderPath)
        if($UseSharedAssets){$renderArguments+='-UseSharedAssets'}
        $lines=@(& (Join-Path $PSHOME 'powershell.exe') @renderArguments 2>&1 | ForEach-Object{[string]$_})
        $exitCode=$LASTEXITCODE
    } finally { $ErrorActionPreference=$priorPreference }
    $text=$lines -join [Environment]::NewLine
    [IO.File]::WriteAllText((Join-Path $testRoot ($Name+'.txt')),$text,[Text.UTF8Encoding]::new($false))
    $pass=if([string]::IsNullOrWhiteSpace($ExpectedError)){$exitCode -eq 0}else{$exitCode -ne 0 -and $text -match $ExpectedError}
    $records.Add([ordered]@{name=$Name;pass=$pass;exitCode=$exitCode;expectedError=$ExpectedError})
    if (-not $pass) { throw ('Renderer test failed: '+$Name+'; exit='+$exitCode+'; output='+$text) }
}
Invoke-RenderTest 'actual-profile-render' $GuestInventoryPath $FixtureMapPath ''
$inventory=Get-Content -LiteralPath $GuestInventoryPath -Raw | ConvertFrom-Json
$inventory.machine.manufacturer='Example Devices Corporation'
$invalidInventory=Join-Path $testRoot 'invalid-inventory.json'
[IO.File]::WriteAllText($invalidInventory,($inventory | ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
Invoke-RenderTest 'reject-physical-profile' $invalidInventory $FixtureMapPath 'Fresh agreeing VMware original inventory is required'
$rows=@(Import-Csv -LiteralPath $FixtureMapPath -Delimiter "`t")
$nicRows=@($rows | Where-Object member -CEQ 'FakeMacAddress')
$firstReplacement=$nicRows[0].replacement
$nicRows[0].replacement=$nicRows[1].replacement
$nicRows[1].replacement=$firstReplacement
$invalidMap=Join-Path $testRoot 'swapped-nic-map.tsv'
$rows | Export-Csv -LiteralPath $invalidMap -Delimiter "`t" -NoTypeInformation -Encoding UTF8
Invoke-RenderTest 'reject-swapped-adapter-values' $GuestInventoryPath $invalidMap 'VM replacement differs for original MAC binding'
$rows=@(Import-Csv -LiteralPath $FixtureMapPath -Delimiter "`t")
@($rows | Where-Object member -CEQ 'FakeSMBIOSSerial')[0].original='WRONG_SERIAL'
$invalidMap=Join-Path $testRoot 'wrong-original-map.tsv'
$rows | Export-Csv -LiteralPath $invalidMap -Delimiter "`t" -NoTypeInformation -Encoding UTF8
Invoke-RenderTest 'reject-unproven-smbios-original' $GuestInventoryPath $invalidMap 'VM SMBIOS original lacks fresh two-reader provenance'
$rows=@(Import-Csv -LiteralPath $FixtureMapPath -Delimiter "`t")
@($rows | Where-Object member -CEQ 'FakeDiskSerial')[0].replacement='2222_2222_2222_2222_A1B2_C3D4_5566_7788.'
$invalidMap=Join-Path $testRoot 'wrong-replacement-map.tsv'
$rows | Export-Csv -LiteralPath $invalidMap -Delimiter "`t" -NoTypeInformation -Encoding UTF8
Invoke-RenderTest 'reject-new-disk-value' $GuestInventoryPath $invalidMap 'VM replacement differs from canonical value'
$priorPreference=$ErrorActionPreference
try {
    $ErrorActionPreference='Continue'
    $guardLines=@(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'guest-active.template.ps1') -RunRoot $testRoot 2>&1 | ForEach-Object{[string]$_})
    $guardExit=$LASTEXITCODE
} finally { $ErrorActionPreference=$priorPreference }
$guardPass=$guardExit -ne 0 -and ($guardLines -join ' ') -match 'RunRoot must be an existing authenticated guest mailbox run directory' -and -not (Test-Path -LiteralPath (Join-Path $testRoot 'result.json'))
$records.Add([ordered]@{name='guest-template-rejects-host-run-before-writes';pass=$guardPass;exitCode=$guardExit})
if (-not $guardPass) { throw ('Guest root guard failed: '+($guardLines -join ' | ')) }
function Assert-PurePhase([string]$Name,[scriptblock]$Body,[bool]$ShouldThrow=$false) {
    $caught=$false
    try { & $Body } catch { $caught=$true }
    $pass=$caught -eq $ShouldThrow
    $records.Add([ordered]@{name=$Name;pass=$pass;pureFunctionCheck=$true})
    if (-not $pass) { throw ('Pure phase guard failed: '+$Name) }
}
$tokens=$null;$parseErrors=$null
$templateAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'guest-active.template.ps1'),[ref]$tokens,[ref]$parseErrors)
foreach($name in @('Assert-CombinedVmPhaseIdentity','Assert-CombinedVmNicOriginalReport','New-CombinedVmRawBundle')) {
    $definitions=@($templateAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if ($definitions.Count -ne 1) { throw ('Expected one production phase guard: '+$name) }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
Import-Module (Join-Path $PSScriptRoot '..\combined_host\model.psm1') -Force
$vm=Get-Content -LiteralPath $GuestInventoryPath -Raw | ConvertFrom-Json
$identity=[pscustomobject]@{manufacturer=$vm.machine.manufacturer;model=$vm.machine.model;uuid=$vm.machine.uuidWmi}
$system=[pscustomobject]@{Manufacturer=$identity.manufacturer;Model=$identity.model}
$product=[pscustomobject]@{UUID=$identity.uuid}
Assert-PurePhase 'phase-identity-matches' {Assert-CombinedVmPhaseIdentity $system $product $identity}
$wrongIdentity=[pscustomobject]@{manufacturer=$identity.manufacturer;model=$identity.model;uuid='00000000-0000-0000-0000-000000000000'}
Assert-PurePhase 'phase-identity-rejects-wrong-uuid' {Assert-CombinedVmPhaseIdentity $system $product $wrongIdentity} $true
Assert-PurePhase 'phase-identity-mismatch-reports-exact-values' {
    $expectedIdentity=[pscustomobject]@{manufacturer='NORTHLAB';model='NODE';uuid='A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'}
    $message=$null
    try { Assert-CombinedVmPhaseIdentity $system $product $expectedIdentity } catch { $message=$_.Exception.Message }
    foreach($fragment in @(('manufacturer expected="NORTHLAB" actual="'+$system.Manufacturer+'"'),('model expected="NODE" actual="'+$system.Model+'"'),('uuid expected="'+$expectedIdentity.uuid+'" actual="'+$product.UUID+'"'))) {
        if ([string]::IsNullOrWhiteSpace($message) -or -not $message.Contains($fragment)) { throw ('Identity mismatch omitted exact expected/actual evidence: '+$fragment) }
    }
}
$identity=[pscustomobject]@{manufacturer='NORTHLAB';model='NODE';uuid='A17F24D0-68E2-4A09-9C31-2B7D8450E6F9'}
Assert-PurePhase 'active-identity-allows-exact-patched-values' {Assert-CombinedVmPhaseIdentity ([pscustomobject]@{Manufacturer='NORTHLAB';Model='NODE'}) ([pscustomobject]@{UUID=$identity.uuid}) $identity}
$assetsJson='[{"name":"model.psm1"},{"name":"readback.ps1"}]'
$assetAssignments=@($templateAst.FindAll({param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$embeddedAssets' -and $node.Right.Extent.Text -match 'ConvertFrom-Json'},$true))
if ($assetAssignments.Count -ne 1) { throw 'Expected one production asset decode assignment.' }
. ([scriptblock]::Create($assetAssignments[0].Extent.Text))
Assert-PurePhase 'powershell51-assets-remain-flat' {if($embeddedAssets.Count -ne 2 -or [string]$embeddedAssets[0].name -cne 'model.psm1' -or [string]$embeddedAssets[1].name -cne 'readback.ps1'){throw 'Asset array shape mismatch.'}}
$hostMap=New-CombinedBindingMap (Get-CombinedMapRows (Join-Path $repo 'spoof-handoff\reports\client-originals.tsv'))
$hostBindings=@($hostMap.bindings | Where-Object category -CEQ 'NIC')
$storedBaseline=Join-Path $repo 'spoof-handoff\reports\combined-host-20261018\o\c.8988cb76bf7448dcbe9414acce44595a\combined-readback.json'
$baseline=Get-Content -LiteralPath $storedBaseline -Raw | ConvertFrom-Json
$rawNic=Get-Content -LiteralPath $baseline.polls[0].evidence.nic.path -Raw | ConvertFrom-Json
Assert-PurePhase 'nic-original-raw-report-passes' {$null=Assert-CombinedVmNicOriginalReport $rawNic $hostBindings}
foreach($defect in @('mode','pnp','cardinality','value','ioctl')) {
    $badNic=$rawNic | ConvertTo-Json -Depth 35 | ConvertFrom-Json
    switch($defect) {
        'mode' {$badNic.expectedMode='Replacement'}
        'pnp' {$badNic.lockedOriginalEvaluations[0].pnpDeviceId='ROOT\WRONG\0000'}
        'cardinality' {$badNic.lockedOriginalEvaluations[0].getAdaptersAddressesMatchCount=2}
        'value' {$badNic.lockedOriginalEvaluations[0].observations.getAdaptersAddresses.value='02:11:22:33:44:55'}
        'ioctl' {$badNic.lockedOriginalEvaluations[0].ndisOidStatus.bytesReturned=0}
    }
    Assert-PurePhase ('nic-restore-rejects-forged-'+$defect) {$null=Assert-CombinedVmNicOriginalReport $badNic $hostBindings} $true
}
$bundleDirectory=Join-Path $testRoot 'raw'
[void][IO.Directory]::CreateDirectory($bundleDirectory)
$childPath=Join-Path $bundleDirectory 'f.0001.json'
[IO.File]::WriteAllText($childPath,'{"syntheticFixtureOnly":true}',[Text.UTF8Encoding]::new($false))
$fileRef=[ordered]@{archiveLeaf='f.0001.json';guestPath='C:\synthetic-unit-fixture.json';sha256=(Get-FileHash -LiteralPath $childPath -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $childPath).Length}
$bundle=New-CombinedVmRawBundle $bundleDirectory @($fileRef)
$syntheticMetadata=[ordered]@{schema='combined-vm-reader-phase.v1';status='PASS';syntheticFixtureOnly=$true;capturedFiles=@($fileRef);rawEvidenceBundle=$bundle}
$bundleMetadata=Join-Path $testRoot 'bundle.json'
[IO.File]::WriteAllText($bundleMetadata,($syntheticMetadata | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$expanded=Join-Path $testRoot 'expanded'
$extractor=Join-Path $PSScriptRoot 'expand-phase-evidence.ps1'
Assert-PurePhase 'raw-child-bundle-round-trip' {
    $lines=@(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $extractor -MetadataPath $bundleMetadata -OutputDirectory $expanded 2>&1)
    if($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath (Join-Path $expanded 'f.0001.json') -Algorithm SHA256).Hash -cne $fileRef.sha256){throw ('Raw bundle round trip failed: '+($lines -join ' | '))}
}
Assert-PurePhase 'raw-bundle-refuses-unsafe-name' {$null=New-CombinedVmRawBundle $bundleDirectory @([ordered]@{archiveLeaf='..\escape.json';sha256=$fileRef.sha256})} $true
$syntheticMetadata.rawEvidenceBundle.sha256='0'*64
[IO.File]::WriteAllText($bundleMetadata,($syntheticMetadata | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$tamperOutput=Join-Path $testRoot 'tampered'
Assert-PurePhase 'raw-bundle-refuses-tampered-archive-before-output' {
    $prior=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$lines=@(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $extractor -MetadataPath $bundleMetadata -OutputDirectory $tamperOutput 2>&1);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
    if($code -eq 0 -or (Test-Path -LiteralPath $tamperOutput)){throw 'Tampered archive was not rejected before output creation.'}
}
$report=[ordered]@{schema='combined-vm-phase-tests.v1';status='PASS';capturedUtc=[DateTime]::UtcNow.ToString('o');pureRendererAndGuardTestsOnly=$true;vmOrHostDeviceMutationExecuted=$false;tests=@($records.ToArray());sourcePins=@(foreach($leaf in @('guest-active.template.ps1','render-active.ps1','tests-active.ps1','expand-phase-evidence.ps1')){[ordered]@{path=(Join-Path $PSScriptRoot $leaf);sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $leaf) -Algorithm SHA256).Hash}})}
$reportPath=Join-Path $testRoot 'result.json'
[IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
[pscustomobject]@{status='PASS';tests=$records.Count;path=$reportPath;sha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash} | ConvertTo-Json
