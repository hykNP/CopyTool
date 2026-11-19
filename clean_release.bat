@echo off
setlocal

REM Remove non-runtime leftovers from a CopyTool folder.
REM Usage: clean_release.bat "path\to\CopyTool" [quiet]
REM Tip: drag the release folder onto this file.
REM It refuses to touch any folder that does not contain CopyToolWeb.exe.

set "TARGET=%~1"
set "QUIET=%~2"
if "%TARGET%"=="" goto usage
if not exist "%TARGET%\CopyToolWeb.exe" goto notpackage

echo Target: %TARGET%
echo Will remove: userdata, wv2data, WebUI, config.ini, host_log.txt, desktop.ini, *_dbg.exe, *.pdb, *.ilk, *.log
echo.

if /i not "%QUIET%"=="quiet" (
    choice /c YN /m "Continue?"
    if errorlevel 2 goto cancelled
)

call :del_dir "%TARGET%\userdata"
call :del_dir "%TARGET%\wv2data"
call :del_dir "%TARGET%\WebUI"
call :del_file "%TARGET%\config.ini"
call :del_file "%TARGET%\host_log.txt"
call :del_file "%TARGET%\desktop.ini"
del /f /q "%TARGET%\*_dbg.exe" 2>nul
del /f /q "%TARGET%\*.pdb" 2>nul
del /f /q "%TARGET%\*.ilk" 2>nul
del /f /q "%TARGET%\*.log" 2>nul
echo.

echo Remaining files:
for %%F in ("%TARGET%\*") do echo   %%~nxF  %%~zF bytes
echo Done.
exit /b 0

:del_dir
if exist "%~1" (
    rmdir /s /q "%~1"
    echo   removed dir : %~nx1
)
exit /b 0

:del_file
if exist "%~1" (
    del /f /q "%~1"
    echo   removed file: %~nx1
)
exit /b 0

:cancelled
echo Cancelled, nothing changed.
exit /b 1

:usage
echo Usage: clean_release.bat "path\to\CopyTool" [quiet]
echo Drag the release folder onto this file instead of typing the path.
exit /b 1

:notpackage
echo [ERROR] CopyToolWeb.exe not found in "%TARGET%"
echo Refusing to modify a folder that is not a CopyTool package.
exit /b 1
