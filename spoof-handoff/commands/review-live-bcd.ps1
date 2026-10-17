[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RepositoryRoot,[Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$liveRepository=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$liveOutput=[IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
$livePackage=Join-Path $liveRepository 'spoof-handoff'
if(-not $liveOutput.StartsWith($livePackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'Output must stay under spoof-handoff/reports.'
}
$livePrincipal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $livePrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation is required.'}
if(Test-Path -LiteralPath $liveOutput){throw 'Output directory already exists.'}
New-Item -ItemType Directory -Path $liveOutput | Out-Null
$liveTemporary=Join-Path $liveOutput 'temporary'
New-Item -ItemType Directory -Path $liveTemporary | Out-Null
$env:TEMP=$liveTemporary;$env:TMP=$liveTemporary
. (Join-Path $PSScriptRoot '..\tools\smbios_boot\snapshot-reference.ps1') -RepoRoot $liveRepository -OutputBoundary $livePackage -LibraryOnly
$auditLog=Join-Path $liveOutput 'audit.log'
function Write-LiveJson([string]$Name,$Value){
    [IO.File]::WriteAllText((Join-Path $liveOutput $Name),($Value | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
function Invoke-LiveBcd([string[]]$Arguments,[string]$Log){
    $ErrorActionPreference='Continue'
    $liveLines=@(& bcdedit.exe @Arguments 2>&1 | ForEach-Object {$_.ToString()})
    $liveExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    [IO.File]::WriteAllLines((Join-Path $liveOutput $Log),[string[]]$liveLines,[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{arguments=$Arguments;exitCode=$liveExit;log=$Log}
}
$liveBefore=Invoke-LiveBcd @('/enum','all','/v') 'bcd-before.txt'
$liveCurrent=Invoke-LiveBcd @('/enum','{current}','/v') 'current-before.txt'
$liveExport=Invoke-LiveBcd @('/export',(Join-Path $liveOutput 'system-bcd.export')) 'export.log'
if($liveBefore.exitCode -ne 0 -or $liveCurrent.exitCode -ne 0 -or $liveExport.exitCode -ne 0){throw 'Could not capture live BCD baseline.'}
$liveCurrentText=Get-Content -LiteralPath (Join-Path $liveOutput 'current-before.txt') -Raw
$liveIdentifier=[regex]::Match($liveCurrentText,'(?m)^identifier\s+(\{[0-9A-Fa-f-]{36}\})\s*$').Groups[1].Value
if(-not $liveIdentifier){throw 'Current OS identifier could not be resolved.'}
$liveOptions=@(foreach($liveName in @('secureboot','dbgsigning')){
    $liveMatch=[regex]::Match($liveCurrentText,('(?m)^'+$liveName+'\s+([^\r\n]+)'))
    [pscustomobject]@{name=$liveName;present=$liveMatch.Success;value=$(if($liveMatch.Success){$liveMatch.Groups[1].Value}else{$null})}
})
$liveSecureBefore=Confirm-SecureBootUEFI
Initialize-ReadyFirmwareReader
[ReadyToArmFirmwareRead]::Enable()
$liveOrder=Read-VariableRecord 'BootOrder' $globalGuid
$liveNext=Read-VariableRecord 'BootNext' $globalGuid
$liveCurrentVariable=Read-VariableRecord 'BootCurrent' $globalGuid
$null=@(Read-ReadyBootEntries $liveOrder $liveNext $liveCurrentVariable $globalGuid)
foreach($liveName in @('SecureBoot','SetupMode','PK','KEK')){$null=Read-VariableRecord $liveName $globalGuid}
foreach($liveName in @('db','dbx')){$null=Read-VariableRecord $liveName $imageGuid}
Write-LiveJson 'firmware-before.json' $firmwareRecords.ToArray()
Write-LiveJson 'original-options.json' $liveOptions
$liveOldStore=Join-Path $liveRepository 'spoof-handoff\reports\temporary-cycle-preflight-20261014\offline-secureboot-disable.bcd'
$liveOfflineEntry=Invoke-LiveBcd @('/store',$liveOldStore,'/enum',$liveIdentifier,'/v') 'prior-offline-object.txt'
$liveSetResults=@();$liveRestores=@();$liveFailure=$null
try{
    $liveSetResults+=Invoke-LiveBcd @('/set','secureboot','off') 'live-secureboot-off.log'
    $liveSetResults+=Invoke-LiveBcd @('/set','dbgsigning','on') 'live-dbgsigning-on.log'
    $liveAfterAttempt=Invoke-LiveBcd @('/enum','{current}','/v') 'current-after-attempt.txt'
    $liveDefaultAttempt=Invoke-LiveBcd @('/enum') 'default-after-attempt.txt'
    Write-LiveJson 'live-attempts.json' ([ordered]@{commands=$liveSetResults;currentEnumeration=$liveAfterAttempt;defaultEnumeration=$liveDefaultAttempt})
}catch{$liveFailure=$_.Exception.ToString()}
finally{
    foreach($liveOption in $liveOptions){
        $liveAttempt=@($liveSetResults | Where-Object {$_.arguments[1] -ceq $liveOption.name})
        if($liveAttempt.Count -eq 1 -and $liveAttempt[0].exitCode -eq 0){
            $liveRestoreArguments=if($liveOption.present){@('/set',$liveIdentifier,$liveOption.name,$liveOption.value)}else{@('/deletevalue',$liveIdentifier,$liveOption.name)}
            $liveRestores+=Invoke-LiveBcd $liveRestoreArguments ('restore-'+$liveOption.name+'.log')
        }
    }
    $liveFinal=Invoke-LiveBcd @('/enum','all','/v') 'bcd-after.txt'
    $null=Invoke-LiveBcd @('/enum','{current}','/v') 'current-after.txt'
    $null=Invoke-LiveBcd @('/enum') 'default-after.txt'
    $liveSecureAfter=Confirm-SecureBootUEFI
    [ReadyToArmFirmwareRead]::Enable()
    $liveFirmwareEqual=$true
    try{Assert-UnchangedVariables}catch{$liveFirmwareEqual=$false;$liveFailure=$_.Exception.ToString()}
    $liveBcdEqual=(Get-Sha256 (Join-Path $liveOutput 'bcd-before.txt')) -ceq (Get-Sha256 (Join-Path $liveOutput 'bcd-after.txt'))
    Write-LiveJson 'result.json' ([ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o');currentOsIdentifier=$liveIdentifier;commands=$liveSetResults;restores=$liveRestores
        priorOfflineObjectEnumeration=$liveOfflineEntry;secureBootBefore=$liveSecureBefore;secureBootAfter=$liveSecureAfter
        bcdListingByteIdentical=$liveBcdEqual;firmwareByteIdentical=$liveFirmwareEqual;bcdFinalExit=$liveFinal.exitCode
        rebooted=$false;driverLoaded=$false;testSigningChanged=$false;failure=$liveFailure
    })
}
if($liveFailure -or -not $liveBcdEqual -or -not $liveFirmwareEqual -or $liveSecureAfter -ne $liveSecureBefore -or $liveFinal.exitCode -ne 0){throw 'Live-store review did not preserve state. Inspect result.json and the original snapshot.'}
'LIVE BCD REVIEW COMPLETE; ORIGINAL BCD AND FIRMWARE STATE VERIFIED.'
