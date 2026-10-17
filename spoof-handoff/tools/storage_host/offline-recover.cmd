@echo off
setlocal EnableExtensions DisableDelayedExpansion
if "%~1"=="" goto usage
set "TARGET=%~1"
if /i not "%TARGET:~1,1%"==":" goto usage
if not "%TARGET:~2,1%"=="" goto usage
if /i "%TARGET%"=="%SystemDrive%" (
  echo Refusing to target the currently running Windows drive.
  exit /b 64
)
if not exist "%TARGET%\Windows\System32\Config\SYSTEM" (
  echo Target does not contain an offline Windows SYSTEM hive: %TARGET%
  exit /b 65
)
if not exist "%TARGET%\Windows\System32\ntoskrnl.exe" (
  echo Target does not contain an offline Windows kernel: %TARGET%
  exit /b 65
)
if not exist "%~dp0transparent\client_storage_filter.inf" (
  echo Prepared transparent INF is missing from the recovery kit.
  exit /b 66
)
if not exist "%~dp0replacement\client_storage_filter.inf" (
  echo Prepared replacement INF is missing from the recovery kit.
  exit /b 66
)
where dism.exe >nul 2>&1
if errorlevel 1 (
  echo DISM is unavailable in this recovery environment. Stop; do not edit the offline registry.
  exit /b 67
)
set "LOG=%~dp0offline-recover-%RANDOM%-%RANDOM%.log"
if exist "%LOG%" (
  echo Could not allocate a unique recovery log path.
  exit /b 68
)
echo Target Windows: %TARGET%\Windows
echo Recovery kit: %~dp0
echo This scans offline oem*.inf files and removes only byte-identical copies of the two prepared client_storage_filter packages.
echo No driver is removed until you type REMOVE.
set /p "CONFIRM=Type REMOVE to continue: "
if /i not "%CONFIRM%"=="REMOVE" (
  echo No changes made.
  exit /b 0
)
set "REMOVED=0"
set "FAILED=0"
set "REBOOT_REQUIRED=0"
for %%F in ("%TARGET%\Windows\INF\oem*.inf") do call :CheckInf "%%~fF" "%%~nxF"
if "%FAILED%"=="1" (
  echo Recovery stopped on the first comparison or DISM failure. Review: %LOG%
  exit /b 70
)
if "%REMOVED%"=="0" (
  echo No exact package INF match was found. No package was removed.
  echo No exact package INF match was found.>>"%LOG%"
  echo Log: %LOG%
  exit /b 3
)
echo Exact matching package removal completed. Review the DISM output before rebooting.
if "%REBOOT_REQUIRED%"=="1" echo DISM returned 3010; reboot is required before read-back verification.
echo Log: %LOG%
exit /b 0

:CheckInf
if "%FAILED%"=="1" exit /b 1
if not exist "%~1" exit /b 0
fc /b "%~1" "%~dp0transparent\client_storage_filter.inf" >nul 2>&1
if errorlevel 2 goto :ComparisonError
if errorlevel 1 goto :CompareReplacement
call :RemoveInf "%~2"
if errorlevel 1 (
  set "FAILED=1"
  exit /b 1
)
exit /b 0

:CompareReplacement
fc /b "%~1" "%~dp0replacement\client_storage_filter.inf" >nul 2>&1
if errorlevel 2 goto :ComparisonError
if errorlevel 1 exit /b 0
call :RemoveInf "%~2"
if errorlevel 1 (
  set "FAILED=1"
  exit /b 1
)
exit /b 0

:ComparisonError
echo FC could not read candidate %~2. Stopping without trying another path.
echo FC error reading %~1>>"%LOG%"
set "FAILED=1"
exit /b 1

:RemoveInf
echo Exact package match: %~1
echo Exact package match: %~1>>"%LOG%"
dism.exe /Image:%TARGET%\ /Remove-Driver /Driver:%~1 >>"%LOG%" 2>&1
set "DISM_EXIT=%ERRORLEVEL%"
echo DISM exit %DISM_EXIT% for %~1>>"%LOG%"
if "%DISM_EXIT%"=="0" goto :DismSuccess
if "%DISM_EXIT%"=="3010" (
  set "REBOOT_REQUIRED=1"
  goto :DismSuccess
)
if not "%DISM_EXIT%"=="0" (
  echo DISM failed with exit %DISM_EXIT% while removing %~1. Stop and retain the recovery log.
  exit /b 1
)
:DismSuccess
set "REMOVED=1"
exit /b 0

:usage
echo Usage: %~nx0 D:
echo Use a drive letter assigned to the offline Windows installation in WinRE.
echo This script makes no change unless the target is explicit and REMOVE is typed.
exit /b 64
