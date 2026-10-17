[CmdletBinding()]
param([string]$RepositoryRoot,[string]$OutputDirectory,[string]$ReportDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if(-not $RepositoryRoot){$RepositoryRoot=$sourceRoot;if($RepositoryRoot -match '\\\.codex-build\\worktrees\\[^\\]+$'){$RepositoryRoot=[IO.Path]::GetFullPath((Join-Path $RepositoryRoot '..\..\..'))}}
$RepositoryRoot=[IO.Path]::GetFullPath($RepositoryRoot)
$runId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')
if(-not $OutputDirectory){$OutputDirectory=Join-Path $RepositoryRoot ('spoof-handoff\artifacts\storage-host-20261017\'+$runId)}
if(-not $ReportDirectory){$ReportDirectory=Join-Path $RepositoryRoot ('spoof-handoff\reports\storage-host-prep-20261017\build.'+$runId)}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory);$ReportDirectory=[IO.Path]::GetFullPath($ReportDirectory)
foreach($path in @($OutputDirectory,$ReportDirectory)){
    if(-not $path.StartsWith((Join-Path $RepositoryRoot 'spoof-handoff\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Release and build evidence must remain under spoof-handoff.'}
    if(Test-Path -LiteralPath $path){throw ('Refusing to overwrite existing release/evidence: '+$path)}
    [void][IO.Directory]::CreateDirectory($path)
}
$utf8=[Text.UTF8Encoding]::new($false)
$thumbprint='59DBD6780CFE60C020B3F4511211FEC22C397B85'
$certSha='B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'
$certificate=Join-Path $RepositoryRoot 'components\hypervisor\hypersinv.cer'
if((Get-FileHash -LiteralPath $certificate).Hash -cne $certSha){throw 'Existing public certificate hash differs.'}
if(-not (Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$thumbprint)).HasPrivateKey){throw 'Existing signing key is unavailable; no new key will be created.'}
Import-Module (Join-Path $PSScriptRoot 'signatures.psm1') -Force
$driverSource=Join-Path $sourceRoot 'spoof-handoff\tools\device_filter'
$infTemplate=Join-Path $sourceRoot 'spoof-handoff\tools\device_cycle_packages\client_storage_filter_pnp.inf'
$template=[IO.File]::ReadAllText($infTemplate)
if(([regex]::Matches($template,'(?m)^StartType=0\r?$')).Count -ne 1){throw 'The reviewed storage INF must declare exactly one StartType=0.'}
$hardwareId='SCSI\DiskNVMe_________________________INTEL_SSDPEKNU010TZ____002C'
$instanceId='SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000'
$original='0000_0000_0100_0000_E4D2_5CDF_3227_5501.'
$replacement='1111_1111_1111_1111_A1B2_C3D4_5566_7788.'
if(-not $template.Contains($hardwareId)){throw 'Reviewed storage INF physical hardware pin differs.'}
$driverText=[IO.File]::ReadAllText((Join-Path $driverSource 'storage\storage_filter.c'))
foreach($literal in @($original,$replacement,$instanceId.Replace('\','\\'))){if(-not $driverText.Contains($literal)){throw ('Storage source physical value/pin differs: '+$literal)}}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars=$null
foreach($install in @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)){
    $candidate=Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat';if(Test-Path -LiteralPath $candidate){$vcvars=$candidate;break}
}
if(-not $vcvars){throw 'An installed x64 MSVC toolchain is required.'}
$environment=& $env:ComSpec /d /s /c ('call "'+$vcvars+'" >nul && set')
if($LASTEXITCODE){throw 'MSVC initialization failed.'}
foreach($line in $environment){$equal=$line.IndexOf('=');if($equal -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$equal),$line.Substring($equal+1),'Process')}}
function Invoke-BuildTool([string]$Program,[string[]]$Arguments,[string]$LogName){
    $prior=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$output=@(& $Program @Arguments 2>&1|ForEach-Object {[string]$_});$code=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
    [IO.File]::WriteAllLines((Join-Path $ReportDirectory $LogName),[string[]]$output,$utf8)
    if($code -ne 0){throw ('Build/sign check failed: '+$Program+' exit='+$code+' log='+$LogName)}
}
$kit='C:\Program Files (x86)\Windows Kits\10'
$infverif=Join-Path $kit 'Tools\10.0.28000.0\x64\InfVerif.exe'
$inf2cat=Join-Path $kit 'bin\10.0.28000.0\x86\Inf2Cat.exe'
$signtool=Join-Path $kit 'bin\10.0.28000.0\x64\signtool.exe'
foreach($tool in @($infverif,$inf2cat,$signtool)){if(-not(Test-Path -LiteralPath $tool)){throw ('Missing WDK tool: '+$tool)}}
$temporary=Join-Path $OutputDirectory 'temporary';[void][IO.Directory]::CreateDirectory($temporary)
$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$env:TEMP=$temporary;$env:TMP=$temporary
$extensionId=[Guid]::NewGuid().ToString('B').ToUpperInvariant()
$packages=[ordered]@{};$receipts=[ordered]@{}
try{
    $version=0
    foreach($mode in @('transparent','replacement')){
        $build=Join-Path $OutputDirectory ('build-'+$mode)
        $package=Join-Path $OutputDirectory $mode
        [void][IO.Directory]::CreateDirectory($package)
        $rewrite=if($mode -ceq 'replacement'){'ON'}else{'OFF'}
        Invoke-BuildTool 'cmake.exe' @('-S',$driverSource,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON','-DDEVICE_FILTER_BUILD_KERNEL=ON',('-DDEVICE_FILTER_ENABLE_REWRITE='+$rewrite)) ($mode+'.configure.log')
        Invoke-BuildTool 'cmake.exe' @('--build',$build,'--target','client_storage_filter','storage_serial_round_trip','nic_address_round_trip','--parallel','2') ($mode+'.build.log')
        Invoke-BuildTool 'ctest.exe' @('--test-dir',$build,'--output-on-failure') ($mode+'.host-tests.log')
        Copy-Item -LiteralPath (Join-Path $build 'client_storage_filter.sys') -Destination (Join-Path $package 'client_storage_filter.sys')
        $inf=[regex]::Replace($template,'(?m)^ExtensionId=.*$',('ExtensionId='+$extensionId))
        $inf=[regex]::Replace($inf,'(?m)^DriverVer=.*$',('DriverVer='+[DateTime]::Now.ToString('MM/dd/yyyy')+',1.0.2.'+$version))
        [IO.File]::WriteAllText((Join-Path $package 'client_storage_filter.inf'),$inf,[Text.Encoding]::ASCII)
        Invoke-BuildTool $infverif @('/w','/v',(Join-Path $package 'client_storage_filter.inf')) ($mode+'.infverif.log')
        Invoke-BuildTool $signtool @('sign','/fd','SHA256','/s','My','/sha1',$thumbprint,(Join-Path $package 'client_storage_filter.sys')) ($mode+'.sign-sys.log')
        Invoke-BuildTool $inf2cat @(('/driver:'+$package),'/os:10_GE_X64','/verbose') ($mode+'.inf2cat.log')
        Invoke-BuildTool $signtool @('sign','/fd','SHA256','/s','My','/sha1',$thumbprint,(Join-Path $package 'client_storage_filter.cat')) ($mode+'.sign-cat.log')
        $receipts[$mode]=Test-StorageHostSignatures -PackageDirectory $package -CertificateFile $certificate
        $packages[$mode]=[ordered]@{directory=$mode;files=@(foreach($name in @('client_storage_filter.sys','client_storage_filter.inf','client_storage_filter.cat')){[ordered]@{name=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $package $name)).Hash}})}
        $version++
    }
    Copy-Item -LiteralPath $certificate -Destination (Join-Path $OutputDirectory 'hypersinv.cer')
    $receiptPath=Join-Path $OutputDirectory 'signature-verdict.json'
    [IO.File]::WriteAllText($receiptPath,(ConvertTo-Json -InputObject ([ordered]@{schema='storage-host-signature-receipt.v1';packages=$receipts;driverInstalledOrLoaded=$false;privateKeyExported=$false;trustStoresChanged=$false;kernelAdmissionProven=$false}) -Depth 10),$utf8)
    $manifest=[ordered]@{
        schema='storage-host-release.v1';createdUtc=[DateTime]::UtcNow.ToString('o');thumbprint=$thumbprint;certSha256=$certSha;certificateFile='hypersinv.cer'
        originalSerial=$original;replacementSerial=$replacement;pnpInstanceId=$instanceId;hardwareId=$hardwareId;startType=0;extensionId=$extensionId;packages=$packages
        signatureReceipt=[ordered]@{path='signature-verdict.json';sha256=(Get-FileHash -LiteralPath $receiptPath).Hash}
        sourceFiles=@(foreach($path in @((Join-Path $driverSource 'CMakeLists.txt'),(Join-Path $driverSource 'storage\storage_filter.c'),(Join-Path $driverSource 'storage\storage_serial_rewrite.c'),(Join-Path $driverSource 'storage\storage_serial_rewrite.h'),$infTemplate)){[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}})
        builtOnlyStorageDriver=$true;driverInstalledOrLoaded=$false;hostPolicyOrTrustChanged=$false;newSigningKeyCreated=$false
    }
    $manifestPath=Join-Path $OutputDirectory 'manifest.json'
    [IO.File]::WriteAllText($manifestPath,(ConvertTo-Json -InputObject $manifest -Depth 10),$utf8)
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $ReportDirectory 'release-manifest.json')
    $pointer=Join-Path $RepositoryRoot 'spoof-handoff\artifacts\storage-host-20261017\latest-release.json'
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $pointer))
    [IO.File]::WriteAllText($pointer,(ConvertTo-Json -InputObject ([ordered]@{releaseDirectory=$OutputDirectory;manifestSha256=(Get-FileHash -LiteralPath $manifestPath).Hash;reportDirectory=$ReportDirectory}) -Depth 4),$utf8)
    'SIGNED STORAGE-ONLY RELEASE PREPARED: '+$OutputDirectory
    'No driver installed, policy changed, certificate imported, or reboot requested.'
}finally{$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
