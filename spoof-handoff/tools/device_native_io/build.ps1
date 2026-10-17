param([string]$ReportDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

$nativeWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$nativeRepo=[IO.Path]::GetFullPath((Join-Path $nativeWorktree '..\..\..'))
$nativePackage=Join-Path $nativeRepo 'spoof-handoff'
$nativeBuild=Join-Path $nativePackage '.build\device-native-io-20261016'
if(-not $ReportDirectory){$ReportDirectory=Join-Path $nativePackage 'reports\device-runtime-package-20261016\native'}
$nativeReports=[IO.Path]::GetFullPath($ReportDirectory)
foreach($target in @($nativeBuild,$nativeReports)){
    if(-not $target.StartsWith($nativePackage.TrimEnd([char]92)+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Build and evidence outputs must remain under spoof-handoff.'}
    $ancestor=$target
    while($ancestor -and $ancestor.Length -gt $nativePackage.Length){
        if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Output path contains a reparse point.'}
        $ancestor=Split-Path -Parent $ancestor
    }
}
New-Item -ItemType Directory -Path $nativeBuild -Force|Out-Null
New-Item -ItemType Directory -Path $nativeReports -Force|Out-Null
$nativeTemp=Join-Path $nativeBuild 'temporary'
New-Item -ItemType Directory -Path $nativeTemp -Force|Out-Null
$env:TEMP=$nativeTemp;$env:TMP=$nativeTemp

$nativeVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$nativeInstallations=@(& $nativeVswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
$nativeVcvars=$null
foreach($installation in $nativeInstallations){$candidate=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat';if(Test-Path -LiteralPath $candidate){$nativeVcvars=$candidate;break}}
if(-not $nativeVcvars){throw 'The existing x64 MSVC toolchain is unavailable.'}
$nativeToolchain=& $env:ComSpec /d /s /c ('call "'+$nativeVcvars+'" >nul && set')
if($LASTEXITCODE -ne 0){throw 'MSVC environment setup failed.'}
foreach($line in $nativeToolchain){$equal=$line.IndexOf('=');if($equal -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$equal),$line.Substring($equal+1),'Process')}}

$nativeExe=Join-Path $nativeBuild 'native_device_io.exe'
$nativeTestExe=Join-Path $nativeBuild 'native_device_io_core_tests.exe'
$nativeBuildLog=Join-Path $nativeReports ('build.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.log')
Push-Location -LiteralPath $nativeBuild
try{
    & cl.exe /nologo /W4 /WX /EHsc /std:c++17 /utf-8 /permissive- /MT /DUNICODE /D_UNICODE (Join-Path $PSScriptRoot 'device_native_io.cpp') (Join-Path $PSScriptRoot 'device_native_io_core.cpp') ('/Fe'+$nativeExe) /link setupapi.lib advapi32.lib 2>&1|Tee-Object -FilePath $nativeBuildLog
    if($LASTEXITCODE -ne 0){throw ('Native reader build failed. See '+$nativeBuildLog)}
    & cl.exe /nologo /W4 /WX /EHsc /std:c++17 /utf-8 /permissive- /MT (Join-Path $PSScriptRoot 'tests\device_native_io_core_tests.cpp') (Join-Path $PSScriptRoot 'device_native_io_core.cpp') ('/Fe'+$nativeTestExe) 2>&1|Tee-Object -FilePath $nativeBuildLog -Append
    if($LASTEXITCODE -ne 0){throw ('Native reader unit-test build failed. See '+$nativeBuildLog)}
}finally{Pop-Location}

$nativeTestOutput=& $nativeTestExe 2>&1
$nativeTestExit=$LASTEXITCODE
$nativeTestPath=Join-Path $nativeReports ('unit-test.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.txt')
Set-Content -LiteralPath $nativeTestPath -Value ($nativeTestOutput -join "`r`n") -Encoding UTF8
if($nativeTestExit -ne 0){throw ('Native reader core tests failed ('+$nativeTestExit+'). See '+$nativeTestPath)}

$nativeCapturePath=Join-Path $nativeReports ('live-native-read.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.json')
$nativeStdout=$nativeCapturePath+'.stdout.txt'
$nativeStderr=$nativeCapturePath+'.stderr.txt'
$nativeArguments='--capture "'+$nativeCapturePath+'"'
$nativeProcess=Start-Process -FilePath $nativeExe -ArgumentList $nativeArguments -PassThru -Wait -NoNewWindow -RedirectStandardOutput $nativeStdout -RedirectStandardError $nativeStderr
$nativeCaptureSha256=$null
if(Test-Path -LiteralPath $nativeCapturePath){$nativeCaptureSha256=(Get-FileHash -LiteralPath $nativeCapturePath -Algorithm SHA256).Hash}
$nativeRun=[ordered]@{schema='device-native-io-live-run.v1';capturedUtc=[DateTime]::UtcNow.ToString('o');executable=$nativeExe;executableSha256=(Get-FileHash -LiteralPath $nativeExe -Algorithm SHA256).Hash;arguments=$nativeArguments;exitCode=$nativeProcess.ExitCode;stdout=$nativeStdout;stderr=$nativeStderr;capture=$nativeCapturePath;captureSha256=$nativeCaptureSha256;unitTest=$nativeTestPath;unitTestExitCode=$nativeTestExit;buildLog=$nativeBuildLog}
$nativeRunPath=Join-Path $nativeReports ('live-run.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.json')
$nativeRun|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $nativeRunPath -Encoding UTF8
'NATIVE READER '+$nativeExe
'UNIT TEST '+$nativeTestPath
'LIVE QUERY EXIT '+$nativeProcess.ExitCode
'LIVE REPORT '+$nativeCapturePath
'RUN RECORD '+$nativeRunPath
if($nativeProcess.ExitCode -ne 0){throw ('One or more direct native device queries failed with exit '+$nativeProcess.ExitCode+'. Inspect the per-device JSON and exact Win32 errors above; no cache fallback was used.')}
