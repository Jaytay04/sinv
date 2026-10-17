[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$ReportDirectory,
    [switch]$AuditMachineTrust
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskThumbprint='59DBD6780CFE60C020B3F4511211FEC22C397B85'
$taskCertificateHash='B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'
$taskDriverHash='6E2A36F3BEBEA7EF716CD4666ECBF0E235AF73E1D45A1FECB641D3E6A5EDE71D'
$taskRoot=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$taskOutput=[IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
$taskBoundary=Join-Path $taskRoot 'spoof-handoff\reports'
if(-not $taskOutput.StartsWith($taskBoundary+'\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'The review output must be under spoof-handoff/reports.'
}
$taskSignTool='C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
$taskCertificate=Join-Path $taskRoot 'components\hypervisor\hypersinv.cer'
$taskDriverSource=Join-Path $taskRoot 'components\hypervisor\build\hvr1-direct\src\driver\efi_runtime_companion\Release\hvr1_efi_runtime_companion.sys'
$taskDriverCopy=Join-Path $taskOutput 'signature-review-only.sys'
function Write-ReviewJson([string]$Name,$Value){
    $Value | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $taskOutput $Name) -Encoding UTF8
}
function Invoke-ReviewTool([string]$File,[string[]]$Arguments,[string]$Log){
    $ErrorActionPreference='Continue'
    $taskLines=@(& $File @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $taskExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    $taskLines | Set-Content -LiteralPath (Join-Path $taskOutput $Log) -Encoding UTF8
    return [pscustomobject]@{file=$File;arguments=$Arguments;exitCode=$taskExit;log=$Log}
}
function Read-ReviewStores {
    return @(foreach($taskStore in @('Root','TrustedPublisher')){
        [pscustomobject]@{
            name=$taskStore
            certificates=@(Get-ChildItem -LiteralPath ('Cert:\LocalMachine\'+$taskStore) |
                Sort-Object Thumbprint | ForEach-Object {
                    [pscustomobject]@{thumbprint=$_.Thumbprint;rawSha256=([BitConverter]::ToString(
                        [Security.Cryptography.SHA256]::Create().ComputeHash($_.RawData))).Replace('-','')}
                })
        }
    })
}
if((Get-FileHash -LiteralPath $taskCertificate -Algorithm SHA256).Hash -ne $taskCertificateHash){
    throw 'The public certificate hash differs from the existing signing identity.'
}
$taskPublicCertificate=New-Object Security.Cryptography.X509Certificates.X509Certificate2($taskCertificate)
if($taskPublicCertificate.Thumbprint -ne $taskThumbprint){throw 'Unexpected certificate identity.'}
if(-not $AuditMachineTrust){
    if(Test-Path -LiteralPath $taskOutput){throw 'An existing review directory cannot be reused.'}
    New-Item -ItemType Directory -Path $taskOutput | Out-Null
    if((Get-FileHash -LiteralPath $taskDriverSource -Algorithm SHA256).Hash -ne $taskDriverHash){
        throw 'The existing unsigned companion differs from the reviewed artifact.'
    }
    $taskSigningCertificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$taskThumbprint)
    if(-not $taskSigningCertificate.HasPrivateKey){throw 'The existing private signing key is unavailable.'}
    Copy-Item -LiteralPath $taskDriverSource -Destination $taskDriverCopy
    $taskSign=Invoke-ReviewTool $taskSignTool @('sign','/v','/fd','SHA256','/s','My','/sha1',$taskThumbprint,$taskDriverCopy) 'sign.log'
    if($taskSign.exitCode -ne 0){throw 'The copied artifact could not be signed.'}
    $taskSignedHash=(Get-FileHash -LiteralPath $taskDriverCopy -Algorithm SHA256).Hash
    Write-ReviewJson 'signed-copy.json' ([ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o');source=$taskDriverSource;sourceSha256=$taskDriverHash
        artifact=$taskDriverCopy;artifactSha256=$taskSignedHash;certificate=$taskCertificate
        certificateSha256=$taskCertificateHash;signerThumbprint=$taskThumbprint;sign=$taskSign
        privateKeyExported=$false;driverLoaded=$false;runtimeSubstitutionImplemented=$false
        purpose='File-only signature review of the existing companion; not a device substitution driver.'
    })
    $taskArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),
        '-RepositoryRoot',('"'+$taskRoot+'"'),'-ReportDirectory',('"'+$taskOutput+'"'),'-AuditMachineTrust')
    $taskProcess=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $taskArguments `
        -Verb RunAs -WindowStyle Hidden -PassThru -Wait
    if($taskProcess.ExitCode -ne 0){throw 'The elevated signing review did not complete. Inspect its evidence before retrying.'}
    Get-Content -LiteralPath (Join-Path $taskOutput 'trust-verification.json') -Raw
    exit 0
}
$taskPrincipal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    throw 'The machine-store review requires elevation.'
}
$taskSigned=Get-Content -LiteralPath (Join-Path $taskOutput 'signed-copy.json') -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath $taskDriverCopy -Algorithm SHA256).Hash -ne $taskSigned.artifactSha256){
    throw 'The signed copy changed before the elevated review.'
}
$taskSecureBefore=Confirm-SecureBootUEFI
if(-not $taskSecureBefore){throw 'Secure Boot must remain enabled for this review.'}
$taskBootBefore=Invoke-ReviewTool 'bcdedit.exe' @('/enum','all','/v') 'bcd-before.txt'
if($taskBootBefore.exitCode -ne 0){throw 'Cannot snapshot the active BCD entries.'}
$taskStoresBefore=Read-ReviewStores
Write-ReviewJson 'machine-stores-before.json' $taskStoresBefore
Write-ReviewJson 'host-before.json' ([ordered]@{
    capturedUtc=[DateTime]::UtcNow.ToString('o');secureBoot=$taskSecureBefore
    windows=Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture
    debugger=Get-CimInstance Win32_ComputerSystem | Select-Object HypervisorPresent
    bcd=$taskBootBefore
})
$taskImports=@();$taskVerifications=@();$taskCleanup=@();$taskFailure=$null
try {
    foreach($taskStore in $taskStoresBefore){
        $taskImport=Invoke-ReviewTool 'certutil.exe' @('-addstore',$taskStore.name,$taskCertificate) ('import-'+$taskStore.name+'.log')
        $taskImports+=$taskImport
        if($taskImport.exitCode -ne 0){throw ('Certificate import failed for '+$taskStore.name)}
        $taskInstalled=Get-Item -LiteralPath ('Cert:\LocalMachine\'+$taskStore.name+'\'+$taskThumbprint)
        if([Convert]::ToBase64String($taskInstalled.RawData) -cne [Convert]::ToBase64String($taskPublicCertificate.RawData)){
            throw 'The installed certificate differs from the pinned public certificate.'
        }
    }
    Write-ReviewJson 'machine-stores-during.json' (Read-ReviewStores)
    foreach($taskPolicy in @('/pa','/kp')){
        $taskVerifications+=Invoke-ReviewTool $taskSignTool @('verify','/v',$taskPolicy,$taskDriverCopy) ('verify-'+$taskPolicy.TrimStart('/')+'.log')
    }
} catch {
    $taskFailure=$_.Exception.ToString()
} finally {
    foreach($taskStore in $taskStoresBefore){
        $taskWasPresent=@($taskStore.certificates | Where-Object thumbprint -eq $taskThumbprint).Count -gt 0
        $taskInstalledPath='Cert:\LocalMachine\'+$taskStore.name+'\'+$taskThumbprint
        if(-not $taskWasPresent -and (Test-Path -LiteralPath $taskInstalledPath)){
            $taskCleanup+=Invoke-ReviewTool 'certutil.exe' @('-delstore',$taskStore.name,$taskThumbprint) ('restore-'+$taskStore.name+'.log')
        }
    }
    $taskStoresAfter=Read-ReviewStores
    Write-ReviewJson 'machine-stores-after.json' $taskStoresAfter
    $taskStoresRestored=($taskStoresBefore | ConvertTo-Json -Depth 6 -Compress) -ceq ($taskStoresAfter | ConvertTo-Json -Depth 6 -Compress)
    $taskSecureAfter=Confirm-SecureBootUEFI
    $taskBootAfter=Invoke-ReviewTool 'bcdedit.exe' @('/enum','all','/v') 'bcd-after.txt'
    $taskBcdEqual=(Get-FileHash -LiteralPath (Join-Path $taskOutput 'bcd-before.txt')).Hash -eq
        (Get-FileHash -LiteralPath (Join-Path $taskOutput 'bcd-after.txt')).Hash
    Write-ReviewJson 'trust-verification.json' ([ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o');imports=$taskImports;verifications=$taskVerifications
        cleanup=$taskCleanup;machineStoresRestored=$taskStoresRestored;secureBootBefore=$taskSecureBefore
        secureBootAfter=$taskSecureAfter;bcdByteIdentical=$taskBcdEqual;bcdAfter=$taskBootAfter
        driverLoaded=$false;serviceCreated=$false;espChanged=$false;bootArmed=$false;rebooted=$false
        debugOrTestSigningChanged=$false;failure=$taskFailure
        limits='Static signature-policy checks only; no claim of live Windows kernel admission or device substitution.'
    })
}
if($null -ne $taskFailure -or -not $taskStoresRestored -or -not $taskSecureAfter -or -not $taskBcdEqual -or $taskBootAfter.exitCode -ne 0){exit 1}
exit 0
