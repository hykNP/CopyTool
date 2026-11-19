@echo off
setlocal enabledelayedexpansion

REM Build a clean release package from an explicit whitelist.
REM Usage: pack_release.bat [version]   e.g. pack_release.bat 1.0.1

set "SCRIPT_DIR=%~dp0"
set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=1.0.0"

set "BIN=%SCRIPT_DIR%CopyToolWeb\bin"
set "CORE=%SCRIPT_DIR%CopyCore\x64\Release"
set "DIST=%SCRIPT_DIR%dist"
set "OUT=%DIST%\CopyTool-v%VERSION%-win64"
set "ZIP=%DIST%\CopyTool-v%VERSION%-win64.zip"
set "STRIP=C:\mingw64\bin\strip.exe"

echo === Pack CopyTool v%VERSION% ===

if not exist "%BIN%\CopyToolWeb.exe" goto missing
if not exist "%CORE%\CopyCore.dll" goto missing
if not exist "%BIN%\WebView2Loader.dll" goto missing

if not exist "%DIST%" mkdir "%DIST%"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
if errorlevel 1 goto failed

copy /y "%BIN%\CopyToolWeb.exe" "%OUT%\" >nul
if errorlevel 1 goto failed
copy /y "%CORE%\CopyCore.dll" "%OUT%\" >nul
if errorlevel 1 goto failed
copy /y "%BIN%\WebView2Loader.dll" "%OUT%\" >nul
if errorlevel 1 goto failed

if exist "%STRIP%" (
    "%STRIP%" --strip-all "%OUT%\CopyToolWeb.exe"
    "%STRIP%" --strip-all "%OUT%\CopyCore.dll"
    echo [OK] stripped debug sections
) else (
    echo [WARN] strip.exe not found - binaries will keep debug info
)

if exist "%SCRIPT_DIR%THIRD-PARTY-NOTICES.txt" copy /y "%SCRIPT_DIR%THIRD-PARTY-NOTICES.txt" "%OUT%\" >nul
if exist "%SCRIPT_DIR%RELEASE-README.txt" copy /y "%SCRIPT_DIR%RELEASE-README.txt" "%OUT%\README.txt" >nul

if exist "%OUT%\CopyToolWeb.exe" (goto zipstep) else (goto failed)

:zipstep
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell"
if exist "%ZIP%" del /f /q "%ZIP%"
"%PS%" -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath '%ZIP%' -CompressionLevel Optimal -Force"
if errorlevel 1 goto failed

echo.
echo Package: %ZIP%
for %%F in ("%OUT%\*") do echo   %%~nxF  %%~zF bytes
echo Staging : %OUT%
echo Done.
exit /b 0

:missing
echo [ERROR] build output missing. Run these first:
echo   CopyCore\build.bat
echo   CopyToolWeb\build.bat
exit /b 1

:failed
echo [ERROR] packing failed
exit /b 1
