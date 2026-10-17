[CmdletBinding()]
param()

$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'model.psm1') -Force
$script:TestCount=0

function Assert-True {
    param([bool]$Condition,[string]$Name)
    if(-not $Condition){throw "FAIL: $Name"}
    $script:TestCount++
}
function Assert-Throws {
    param([scriptblock]$Body,[string]$Name)
    $threw=$false
    try { & $Body | Out-Null } catch { $threw=$true }
    Assert-True -Condition $threw -Name $Name
}
function Assert-Equal {
    param($Expected,$Actual,[string]$Name)
    if($Expected -is [array] -or $Actual -is [array]){
        $expectedText=@($Expected) -join '|';$actualText=@($Actual) -join '|'
        Assert-True -Condition ($expectedText -ceq $actualText) -Name ($Name+" expected=[$expectedText] actual=[$actualText]")
    } else { Assert-True -Condition ([string]$Expected -ceq [string]$Actual) -Name ($Name+" expected=[$Expected] actual=[$Actual]") }
}

Assert-Equal 'Prepared' (Get-StorageHostAllowedTransition -Phase New -Action Prepare) 'new run starts only from Prepare'
Assert-Equal 'PolicyPendingReboot' (Get-StorageHostAllowedTransition -Phase Prepared -Action EnableTestMode) 'policy is staged before reboot'
Assert-Equal 'TransparentInstallPendingReboot' (Get-StorageHostAllowedTransition -Phase PolicyPendingReboot -Action InstallTransparent) 'transparent package phase order'
Assert-Equal 'TransparentRemovalPendingReboot' (Get-StorageHostAllowedTransition -Phase TransparentInstallPendingReboot -Action Remove) 'pending transparent package can be cleaned up'
Assert-Equal 'ReplacementRemovalPendingReboot' (Get-StorageHostAllowedTransition -Phase ReplacementInstallPendingReboot -Action Remove) 'pending replacement package can be cleaned up'
Assert-Equal 'RecoveryReconciled' (Get-StorageHostAllowedTransition -Phase ReplacementInstallPendingReboot -Action RecoverOriginal) 'external recovery can reconcile a non-complete run'
Assert-Equal 'PolicyRestorePendingReboot' (Get-StorageHostAllowedTransition -Phase RecoveryReconciled -Action RestorePolicy) 'reconciled originals permit policy cleanup'
Assert-Throws { Get-StorageHostAllowedTransition -Phase PolicyPendingReboot -Action InstallReplacement } 'replacement cannot skip the transparent test'
Assert-Throws { Get-StorageHostAllowedTransition -Phase Complete -Action RecoverOriginal } 'completed run cannot be silently reopened'

$goodFiles=@(
    [pscustomobject]@{name='client_storage_filter.sys';sha256=('A'*64)},
    [pscustomobject]@{name='client_storage_filter.inf';sha256=('B'*64)},
    [pscustomobject]@{name='client_storage_filter.cat';sha256=('C'*64)}
)
$goodManifest=[pscustomobject]@{
    schema='storage-host-release.v1';thumbprint='59DBD6780CFE60C020B3F4511211FEC22C397B85';certSha256='B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'
    originalSerial='0000_0000_0100_0000_E4D2_5CDF_3227_5501.';replacementSerial='1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
    pnpInstanceId='SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000';hardwareId='SCSI\DiskNVMe_________________________INTEL_SSDPEKNU010TZ____002C'
    startType=0;certificateFile='hypersinv.cer';packages=[pscustomobject]@{
        transparent=[pscustomobject]@{directory='transparent';files=$goodFiles}
        replacement=[pscustomobject]@{directory='replacement';files=$goodFiles}
    }
}
Assert-True ([bool](Assert-StorageHostManifest -Manifest $goodManifest)) 'complete pinned release manifest accepted'
Assert-True ([bool](Assert-StorageHostReceipt -Receipt ([pscustomobject]@{path='signature-verdict.json';sha256=('D'*64)}))) 'complete pinned signature receipt accepted'
Assert-Throws { Assert-StorageHostReceipt -Receipt ([pscustomobject]@{path='signature-verdict.json'}) } 'partial signature receipt rejected'
Assert-Throws { Assert-StorageHostReceipt -Receipt ([pscustomobject]@{path='other.json';sha256=('D'*64)}) } 'wrong signature receipt path rejected'
Assert-Throws { Assert-StorageHostStartType -Manifest ([pscustomobject]@{startType=3}) } 'StartType 3 release rejected'
$wrongSigner=$goodManifest.PSObject.Copy();$wrongSigner.thumbprint='00'*20
Assert-Throws { Assert-StorageHostManifest -Manifest $wrongSigner } 'wrong signer manifest rejected'
$partial=$goodManifest.PSObject.Copy();$partial.packages=[pscustomobject]@{transparent=$goodManifest.packages.transparent;replacement=$null}
Assert-Throws { Assert-StorageHostManifest -Manifest $partial } 'partial package manifest rejected'

Assert-Equal '0000_0000_0100_0000_E4D2_5CDF_3227_5501.' (Assert-StorageHostExpectedSerial -Mode Original -NativeSerial '0000_0000_0100_0000_E4D2_5CDF_3227_5501.' -WmiSerial '0000_0000_0100_0000_E4D2_5CDF_3227_5501.') 'locked original serial accepted'
Assert-Equal '1111_1111_1111_1111_A1B2_C3D4_5566_7788.' (Assert-StorageHostExpectedSerial -Mode Replacement -NativeSerial '1111_1111_1111_1111_A1B2_C3D4_5566_7788.' -WmiSerial '1111_1111_1111_1111_A1B2_C3D4_5566_7788.') 'locked replacement serial accepted'
Assert-Throws { Assert-StorageHostExpectedSerial -Mode Replacement -NativeSerial '0000_0000_0100_0000_E4D2_5CDF_3227_5501.' -WmiSerial '0000_0000_0100_0000_E4D2_5CDF_3227_5501.' } 'spoof mismatch emits expected-versus-actual failure'

Assert-True (Test-StorageHostOwnedInf -PublishedName 'oem42.inf' -ExpectedSha256 ('A'*64) -ActualSha256 ('A'*64) -ExpectedPnpInstanceId $goodManifest.pnpInstanceId -ActualPnpInstanceId $goodManifest.pnpInstanceId) 'exact published INF ownership passes without a serial or running-stack input'
Assert-True (-not (Test-StorageHostOwnedInf -PublishedName 'oem42.inf' -ExpectedSha256 ('A'*64) -ActualSha256 ('B'*64) -ExpectedPnpInstanceId $goodManifest.pnpInstanceId -ActualPnpInstanceId $goodManifest.pnpInstanceId)) 'INF hash mismatch blocks removal ownership'
Assert-True (-not (Test-StorageHostOwnedInf -PublishedName 'oem42.inf' -ExpectedSha256 ('A'*64) -ActualSha256 ('A'*64) -ExpectedPnpInstanceId $goodManifest.pnpInstanceId -ActualPnpInstanceId 'OTHER\DEVICE')) 'wrong physical device blocks removal ownership'
Assert-True (Test-StorageHostRecoveryDisk -RecoveryDiskNumbers @(1) -WindowsDiskNumbers @(0)) 'external recovery disk is distinct'
Assert-True (-not (Test-StorageHostRecoveryDisk -RecoveryDiskNumbers @(0) -WindowsDiskNumbers @(0))) 'Windows disk cannot host only recovery copy'
Assert-True (-not (Test-StorageHostRecoveryDisk -RecoveryDiskNumbers @() -WindowsDiskNumbers @(0))) 'unknown recovery disk is rejected'

$stackOutput=@('Instance ID: SCSI\DISK0','Device Stack:','  \Driver\client_storage_filter.sys','  \Driver\disk','','Matching Drivers:','  client_storage_filter')
$stackLines=@(Get-StorageHostStackLines -PnpUtilOutput $stackOutput)
Assert-Equal 2 $stackLines.Count 'stack parser returns a flat two-line device stack'
Assert-True (Test-StorageHostStackContainsFilter -PnpUtilOutput $stackOutput) 'actual Device Stack filter detected'
Assert-Equal 0 (@(Get-StorageHostStackLines -PnpUtilOutput @('Matching Drivers:','  client_storage_filter')).Count) 'Matching Drivers section is not treated as active stack'
Assert-True (-not (Test-StorageHostStackContainsFilter -PnpUtilOutput @('Matching Drivers:','  client_storage_filter'))) 'matching driver alone is not active'

$missingSigning=Get-StorageHostTestSigningElement -BcdLines @('identifier {current}','description Windows')
Assert-True (-not $missingSigning.exists) 'absent testsigning value is preserved as absent'
Assert-True (Test-StorageHostPolicyRestore -Expected $missingSigning -Actual (Get-StorageHostTestSigningElement -BcdLines @('identifier {current}')) -ExpectedRoot $false -ActualRoot $false -ExpectedPublisher $false -ActualPublisher $false -ExpectedCodeIntegrityOptions 0x400 -ActualCodeIntegrityOptions 0x20400) 'absent BCD testsigning and unrelated CI bits restore correctly'
Assert-True (-not (Test-StorageHostPolicyRestore -Expected $missingSigning -Actual (Get-StorageHostTestSigningElement -BcdLines @('testsigning Yes')) -ExpectedRoot $false -ActualRoot $false -ExpectedPublisher $false -ActualPublisher $false -ExpectedCodeIntegrityOptions 0x400 -ActualCodeIntegrityOptions 0x402)) 'unexpected testsigning presence fails pristine policy check'

# Exercise the real read-helper boundary without executing its native or device code.
$bcdReadTokens=$null;$bcdReadErrors=$null
$bcdReadAst=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'read.ps1'),[ref]$bcdReadTokens,[ref]$bcdReadErrors)
$bcdReaders=@($bcdReadAst.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-StorageHostBcdElement'},$true))
Assert-Equal 1 $bcdReaders.Count 'exactly one production BCD read helper is tested'
. ([scriptblock]::Create($bcdReaders[0].Extent.Text))
$signingWithBlanks=Get-StorageHostBcdElement -Lines @('','Windows Boot Loader','-------------------','identifier {current}','','testsigning Yes','')
Assert-True ($signingWithBlanks.exists -and $signingWithBlanks.value -ceq 'yes') 'production helper accepts normal BCDEdit blank lines around testsigning Yes'
$unsignedWithBlanks=Get-StorageHostBcdElement -Lines @('','identifier {current}','','testsigning No','')
Assert-True ($unsignedWithBlanks.exists -and $unsignedWithBlanks.value -ceq 'no') 'production helper preserves explicit testsigning No with blank lines'
Assert-True (-not (Get-StorageHostBcdElement -Lines @('','identifier {current}','')).exists) 'blank lines do not invent a testsigning element'
Assert-True (-not (Get-StorageHostBcdElement -Lines @()).exists) 'empty command output does not invent testsigning; command exit checks remain with the caller'

$transparentProof=[pscustomobject]@{
    expectedMode='Original';result='PASS';mismatches=@();nativeSerial='0000_0000_0100_0000_E4D2_5CDF_3227_5501.';wmiSerial='0000_0000_0100_0000_E4D2_5CDF_3227_5501.'
    fileIo=[pscustomobject]@{match=$true};filterService=[pscustomobject]@{present=$true;services=@([pscustomobject]@{State='Running'});registry=[pscustomobject]@{start=0}}
    actualStack=@('\Driver\client_storage_filter.sys','\Driver\disk');previousBootTime='2026-10-17T01:00:00Z';currentBootTime='2026-10-17T02:00:00Z'
}
Assert-True (Test-StorageHostTransparentProof -Proof $transparentProof) 'transparent pass proof meets replacement gate'
$spoofedProof=$transparentProof.PSObject.Copy();$spoofedProof.nativeSerial='1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
Assert-True (-not (Test-StorageHostTransparentProof -Proof $spoofedProof)) 'transparent gate rejects a spoofed serial instead of original'
$wrongStackProof=$transparentProof.PSObject.Copy();$wrongStackProof.actualStack=@('\Driver\disk');$wrongStackProof.filterService=[pscustomobject]@{present=$true;services=@([pscustomobject]@{State='Running'});registry=[pscustomobject]@{start=0}}
Assert-True (-not (Test-StorageHostTransparentProof -Proof $wrongStackProof)) 'transparent gate rejects driver outside the actual stack'
$wrongServiceProof=$transparentProof.PSObject.Copy();$wrongServiceProof.filterService=[pscustomobject]@{present=$true;services=@([pscustomobject]@{State='Stopped'});registry=[pscustomobject]@{start=0}}
Assert-True (-not (Test-StorageHostTransparentProof -Proof $wrongServiceProof)) 'transparent gate rejects non-running service'
$wrongStartProof=$transparentProof.PSObject.Copy();$wrongStartProof.filterService=[pscustomobject]@{present=$true;services=@([pscustomobject]@{State='Running'});registry=[pscustomobject]@{start=3}}
Assert-True (-not (Test-StorageHostTransparentProof -Proof $wrongStartProof)) 'transparent gate rejects nonzero service StartType'

Assert-Equal 'RemovedTransparent' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 0 -ReplacementCompareExit 2 -DismExitCode 0) 'transparent exact match stops after one removal; no second FC read'
Assert-Equal 'CompareReplacement' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 1) 'transparent mismatch proceeds to replacement comparison'
Assert-Equal 'NoMatch' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 1 -ReplacementCompareExit 1) 'ordinary FC mismatches are a no-match, not fatal'
Assert-Equal 'ComparisonFailure' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 2) 'FC read error stops scanning'
Assert-Equal 'ComparisonFailure' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 1 -ReplacementCompareExit 2) 'replacement FC read error stops scanning'
Assert-Equal 'DismFailure' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 1 -ReplacementCompareExit 0 -DismExitCode 5) 'DISM error latches recovery failure'
Assert-Equal 'RemovedReplacement' (Get-StorageHostOfflineInfDecision -TransparentCompareExit 1 -ReplacementCompareExit 0 -DismExitCode 3010) 'DISM reboot-required status is recorded as accepted'

$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$runPath=Join-Path $PSScriptRoot 'run.ps1'
$readPath=Join-Path $PSScriptRoot 'read.ps1'
$testPath=$PSCommandPath
foreach($path in @($runPath,$readPath,$testPath)){
    $tokens=$null;$parseErrors=$null
    [void][System.Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$parseErrors)
    Assert-True ($parseErrors.Count -eq 0) ('PowerShell AST parses: '+$path+' '+(@($parseErrors|ForEach-Object {$_.Message}) -join '; '))
}
$runText=[IO.File]::ReadAllText($runPath)
$readText=[IO.File]::ReadAllText($readPath)
$runTokens=$null;$runErrors=$null
$runAst=[System.Management.Automation.Language.Parser]::ParseFile($runPath,[ref]$runTokens,[ref]$runErrors)
$removeFunction=@($runAst.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Invoke-StorageHostRemovePackage'},$true))[0]
$ownedFunction=@($runAst.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Assert-StorageHostOwnedPackageForRemoval'},$true))[0]
Assert-True ($null -ne $removeFunction -and $removeFunction.Extent.Text -notmatch 'Assert-StorageHostExpectedSerial' -and $removeFunction.Extent.Text -notmatch 'fileIo\.match.{0,80}throw') 'cleanup path does not require spoof readback or file-I/O success'
Assert-True ($null -ne $ownedFunction -and $ownedFunction.Extent.Text -notmatch 'Removal requires the owned storage filter Running') 'cleanup ownership check does not require a running stack filter'
Assert-True ($runText -match 'Test-StorageHostTransparentProof' -and $runText -match 'verifiedTransparentProof') 'replacement install requires persisted transparent proof'
Assert-True ($readText -match '\$item\.PSObject\.Properties\[\$name\]' -and $readText -notmatch '\$item\.Group') 'optional driver registry values are guarded under StrictMode'
$recoverFunction=@($runAst.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Invoke-StorageHostRecoverOriginal'},$true))[0]
Assert-True ($null -ne $recoverFunction -and $recoverFunction.Extent.Text -match 'storage service registry key expected absent' -and $recoverFunction.Extent.Text -match 'release INF expected no matching') 'manual recovery reconciliation checks service registry and both release INF hashes'
Assert-True ($runText -match 'hardware ID expected exactly once' -and $runText -match 'PnP instance expected=') 'test and final readbacks enforce pinned device identity'

$cmdPath=Join-Path $PSScriptRoot 'offline-recover.cmd'
$cmdLines=[IO.File]::ReadAllLines($cmdPath)
Assert-True (@($cmdLines|Where-Object {$_ -match '^if errorlevel 1 goto :CompareReplacement$'}).Count -eq 1) 'FC status 1 routes to the replacement comparison'
Assert-True (@($cmdLines|Where-Object {$_ -match '^if errorlevel 2 goto :ComparisonError$'}).Count -eq 2) 'FC status 2 is fatal for either candidate comparison'
Assert-True (@($cmdLines|Where-Object {$_ -match '^if "%DISM_EXIT%"=="0" goto :DismSuccess$'}).Count -eq 1 -and @($cmdLines|Where-Object {$_ -match '^if "%DISM_EXIT%"=="3010" \($'}).Count -eq 1) 'offline removal accepts DISM success and reboot-required status only'
Assert-True (@($cmdLines|Where-Object {$_ -match '^echo DISM exit %DISM_EXIT% for %~1>>"%LOG%"$'}).Count -eq 1 -and @($cmdLines|Where-Object {$_ -match '^if "%REBOOT_REQUIRED%"=="1" echo DISM returned 3010'}).Count -eq 1) 'offline recovery records DISM exit code and reports reboot requirement'
Assert-True (@($cmdLines|Where-Object {$_ -match '^\s*set "FAILED=1"$'}).Count -ge 2) 'offline failures are latched across the scan loop'
$usageIndex=[Array]::FindIndex([string[]]$cmdLines,[Predicate[string]]{param($line) $line -match '^echo Usage: %~nx0 D:'})
Assert-True ($usageIndex -ge 0 -and $cmdLines[$usageIndex+1] -match '^echo Use a drive letter') 'offline usage output is split across lines'
Assert-True (@($cmdLines|Where-Object {$_ -match '^if "%FAILED%"=="1" \($'}).Count -eq 1) 'offline scan checks the latched failure after iteration'

$nonEmptyRunLines=@([IO.File]::ReadAllLines($runPath)|Where-Object {-not [string]::IsNullOrWhiteSpace($_)})
Assert-Equal 'Start-StorageHostWorkflow' $nonEmptyRunLines[$nonEmptyRunLines.Count-1] 'entry dispatch is the final statement after every function definition'

. (Join-Path $PSScriptRoot 'tests-enable.ps1')
. (Join-Path $PSScriptRoot 'tests-install.ps1')

Write-Output ('PASS: '+$script:TestCount+' storage-host model, synthetic action, cleanup, recovery, release, and syntax assertions; no device, elevation, driver, BCD, firmware, DISM, reboot, or network operations were run.')
