[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportDirectory,[Parameter(Mandatory=$true)][string]$ArtifactDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$deviceWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$deviceRepository=[IO.Path]::GetFullPath((Join-Path $deviceWorktree '..\..\..'))
$devicePackage=Join-Path $deviceRepository 'spoof-handoff'
$deviceReports=[IO.Path]::GetFullPath($ReportDirectory)
$deviceArtifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
foreach($devicePath in @($deviceReports,$deviceArtifacts)){
    if(-not $devicePath.StartsWith($devicePackage+'\',[StringComparison]::OrdinalIgnoreCase)){
        throw 'Build and evidence paths must stay in spoof-handoff.'
    }
    if(Test-Path -LiteralPath $devicePath){throw ('Build output already exists: '+$devicePath)}
    New-Item -ItemType Directory -Path $devicePath | Out-Null
}
$deviceTemporary=Join-Path $deviceArtifacts 'temporary'
New-Item -ItemType Directory -Path $deviceTemporary | Out-Null
$env:TEMP=$deviceTemporary;$env:TMP=$deviceTemporary
$deviceVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$deviceInstallations=@(& $deviceVsWhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
$deviceVcvars=$null
foreach($deviceInstallation in $deviceInstallations){
    $deviceCandidate=Join-Path $deviceInstallation 'VC\Auxiliary\Build\vcvars64.bat'
    if(Test-Path -LiteralPath $deviceCandidate){$deviceVcvars=$deviceCandidate;break}
}
if(-not $deviceVcvars){throw 'The installed x64 MSVC toolchain is unavailable.'}
$deviceEnvironment=& $env:ComSpec /d /s /c ('call "'+$deviceVcvars+'" >nul && set')
if($LASTEXITCODE -ne 0){throw 'MSVC toolchain initialization failed.'}
foreach($deviceLine in $deviceEnvironment){
    $deviceEqual=$deviceLine.IndexOf('=')
    if($deviceEqual -gt 0){[Environment]::SetEnvironmentVariable($deviceLine.Substring(0,$deviceEqual),$deviceLine.Substring($deviceEqual+1),'Process')}
}
function Invoke-DeviceBuild([string]$Program,[string[]]$Arguments,[string]$Log){
    $ErrorActionPreference='Continue'
    $deviceLines=@(& $Program @Arguments 2>&1 | ForEach-Object {$_.ToString()})
    $deviceExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    [IO.File]::WriteAllLines((Join-Path $deviceReports $Log),[string[]]$deviceLines,[Text.UTF8Encoding]::new($false))
    if($deviceExit -ne 0){throw ('Build/check failed ('+$deviceExit+'): '+$Program+'; log='+$Log)}
}
$deviceBuilds=@()
foreach($deviceMode in @('disabled','enabled')){
    $deviceBinary=Join-Path $deviceArtifacts $deviceMode
    $deviceSwitch=if($deviceMode -ceq 'enabled'){'ON'}else{'OFF'}
    Invoke-DeviceBuild 'cmake.exe' @('-S',$PSScriptRoot,'-B',$deviceBinary,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON',('-DDEVICE_FILTER_ENABLE_REWRITE='+$deviceSwitch)) ('configure-'+$deviceMode+'.log')
    Invoke-DeviceBuild 'cmake.exe' @('--build',$deviceBinary,'--parallel','2') ('build-'+$deviceMode+'.log')
    Invoke-DeviceBuild 'ctest.exe' @('--test-dir',$deviceBinary,'--output-on-failure') ('tests-'+$deviceMode+'.log')
    $deviceBuilds+=[pscustomobject]@{mode=$deviceMode;directory=$deviceBinary;hostTests='PASS';driversInstalled=$false}
}
[IO.File]::WriteAllText((Join-Path $deviceReports 'build-verdict.json'),([ordered]@{
    result='PASS';capturedUtc=[DateTime]::UtcNow.ToString('o');builds=$deviceBuilds
    nativeDeviceRoundTripProven=$false;wmiSubstitutionProven=$false;driverInstalledOrLoaded=$false
    limits='Static builds and portable buffer tests. Runtime path coverage requires separate validation.'
} | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
'DEVICE FILTER STATIC BUILDS AND HOST BUFFER TESTS PASS. No driver installed or loaded.'
