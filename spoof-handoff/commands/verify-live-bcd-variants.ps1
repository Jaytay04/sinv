[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$SnapshotDirectory,
    [Parameter(Mandatory=$true)][string]$ReportDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$variantRoot=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$variantPackage=Join-Path $variantRoot 'spoof-handoff'
$variantOutput=[IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
$verifySnapshot=[IO.Path]::GetFullPath($SnapshotDirectory).TrimEnd('\')
foreach($path in @($variantOutput,$verifySnapshot)){
    if(-not $path.StartsWith($variantPackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must remain under spoof-handoff/reports.'}
}
$verifyPrincipal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $verifyPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation is required.'}
if(Test-Path -LiteralPath $variantOutput){throw 'Output directory must be new.'}
New-Item -ItemType Directory -Path $variantOutput | Out-Null
$verifyTemporary=Join-Path $variantOutput 'temporary'
New-Item -ItemType Directory -Path $verifyTemporary | Out-Null
$env:TEMP=$verifyTemporary;$env:TMP=$verifyTemporary
. (Join-Path $PSScriptRoot '..\tools\smbios_boot\snapshot-reference.ps1') -RepoRoot $variantRoot -OutputBoundary $variantPackage -LibraryOnly
$auditLog=Join-Path $variantOutput 'audit.log'
$variantCommands=[Collections.Generic.List[object]]::new()
# Reuse only the two process/log helpers, without invoking the diagnostic body or setters.
$verifyTokens=$null;$verifyErrors=$null
$verifyAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'diagnose-live-bcd-variants.ps1'),[ref]$verifyTokens,[ref]$verifyErrors)
if($verifyErrors.Count){throw 'Diagnostic source does not parse.'}
foreach($name in @('Save-VariantJson','Invoke-VariantBcd')){
    $definitions=@($verifyAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if($definitions.Count -ne 1){throw ('Cannot resolve logging helper: '+$name)}
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$verifyPrior=Get-Content -LiteralPath (Join-Path $verifySnapshot 'result.json') -Raw | ConvertFrom-Json
$verifyBaseline=Get-Content -LiteralPath (Join-Path $verifySnapshot 'baseline-all.stdout.txt') -Raw
$verifyBaselineError=[IO.File]::ReadAllText((Join-Path $verifySnapshot 'baseline-all.stderr.txt'))
$verifyMountAbsent=-not(Test-Path -LiteralPath 'S:\')
if(-not $verifyMountAbsent){throw 'The temporary ESP drive mapping is still present.'}
$verifyAll=Invoke-VariantBcd @('/enum','all','/v') 'all-after-unmount'
$null=Invoke-VariantBcd @('/enum','{current}','/v') 'current-after-unmount'
$null=Invoke-VariantBcd @('/enum','{bootmgr}','/v') 'bootmgr-after-unmount'
$null=Invoke-VariantBcd @('/enum','{dbgsettings}','/v') 'dbgsettings'
foreach($type in @('BOOTAPP','OSLOADER','BOOTMGR','FWBOOTMGR')){
    $null=Invoke-VariantBcd @('/?','TYPES',$type) ('help-types-'+$type.ToLowerInvariant())
}
$verifyExport=Invoke-VariantBcd @('/export',(Join-Path $variantOutput 'system-bcd-after.export')) 'export-after'
$verifySecure=Confirm-SecureBootUEFI
Initialize-ReadyFirmwareReader
[ReadyToArmFirmwareRead]::Enable()
$verifyBeforeVariables=Get-Content -LiteralPath (Join-Path $verifySnapshot 'firmware-before.json') -Raw | ConvertFrom-Json
$verifyChecks=@(foreach($record in $verifyBeforeVariables){
    $actual=Read-VariableRecord $record.name $record.guid
    [pscustomobject]@{name=$record.name;guid=$record.guid;matches=($actual.base64 -ceq $record.base64 -and $actual.attributes -eq $record.attributes -and $actual.win32Error -eq $record.win32Error);expectedSha256=$record.sha256;actualSha256=$actual.sha256}
})
Save-VariantJson 'firmware-after.json' $firmwareRecords.ToArray()
Save-VariantJson 'firmware-comparison.json' $verifyChecks
$verifyBcdEqual=($verifyAll.exitCode -eq 0 -and $verifyAll.stdout -ceq $verifyBaseline -and $verifyAll.stderr -ceq $verifyBaselineError)
$verifyRawExportEqual=($verifyExport.exitCode -eq 0 -and (Get-Sha256 (Join-Path $verifySnapshot 'system-bcd.export')) -ceq (Get-Sha256 (Join-Path $variantOutput 'system-bcd-after.export')))
$verifyPass=($verifyBcdEqual -and $verifySecure -eq $verifyPrior.secureBootBefore -and @($verifyChecks | Where-Object matches -eq $false).Count -eq 0 -and $verifyPrior.espLayoutAndFileContentIdentical -and $verifyMountAbsent)
Save-VariantJson 'result.json' ([ordered]@{
    finishedUtc=[DateTime]::UtcNow.ToString('o');pass=$verifyPass;bcdListingByteIdentical=$verifyBcdEqual
    bcdExportByteIdentical=$verifyRawExportEqual;secureBoot=$verifySecure
    capturedFirmwareByteIdentical=(@($verifyChecks | Where-Object matches -eq $false).Count -eq 0)
    verifiedFirmwareVariables=$verifyChecks.Count;priorEspFileVerificationPassed=$verifyPrior.espLayoutAndFileContentIdentical
    temporaryEspMountAbsent=$verifyMountAbsent;liveWritesInThisVerification=0;driverLoaded=$false;rebooted=$false
    explanation='Initial final-all was captured with S: mounted. This read-only verification compares enumeration after its removal with the original unmapped baseline. Initial raw diagnostic files are retained.'
})
if(-not $verifyPass){throw 'Read-only state verification failed; inspect result.json.'}
'PASS: ORIGINAL BCD LISTING, CAPTURED FIRMWARE AND ESP CONTENT VERIFIED; SECURE BOOT ON.'
