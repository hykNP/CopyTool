@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
cd /d "%~dp0"

REM Commit all changes and push main + tags to GitHub.
REM Usage: push_github.bat ["commit message"] [/auto]
REM   /auto  skip the confirmation prompt
REM The script refuses to push binaries, build output, WebView2 data,
REM or any single file larger than 50 MB.

set "MSG=%~1"
set "AUTO=%~2"
if /i "%MSG%"=="/auto" ( set "AUTO=/auto" & set "MSG=" )
if "%MSG%"=="" set "MSG=chore: sync %DATE% %TIME%"

set "GIT=git"
where git >nul 2>nul || set "GIT=C:\Program Files\Git\cmd\git.exe"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

"%GIT%" rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    echo [ERROR] This folder is not a git repository.
    exit /b 1
)

echo === Pre-flight check ===
"%GIT%" add -A
"%GIT%" diff --cached --name-only > "%TEMP%\ct_staged.txt"

for /f %%C in ('type "%TEMP%\ct_staged.txt" ^| find /c /v ""') do set "COUNT=%%C"
if "%COUNT%"=="0" (
    echo No changes to commit.
) else (
    echo Staged files: %COUNT%
)

"%PS%" -NoProfile -Command "$bad = Get-Content -LiteralPath '%TEMP%\ct_staged.txt' | Where-Object { $_ -match '\.(exe|dll|zip|7z|rar|pdb|ilk|obj|lib|a|log|db|sqlite|user|suo)$' -or $_ -match '(^|[\\/])(bin|obj|userdata|wv2data|dist|\.vs)([\\/]|$)' -or (Test-Path -LiteralPath $_ -PathType Leaf -and (Get-Item -LiteralPath $_).Length -gt 50MB) }; $bad" > "%TEMP%\ct_bad.txt" 2>nul

for %%A in ("%TEMP%\ct_bad.txt") do set "BADCOUNT=%%~zA"
if not "%BADCOUNT%"=="0" (
    echo.
    echo [ABORT] These files must not go to GitHub:
    type "%TEMP%\ct_bad.txt"
    echo.
    echo Remove them from the staging area ^(git reset^) or add them to .gitignore,
    echo then run this script again. Nothing was committed or pushed.
    exit /b 1
)

echo.
"%GIT%" diff --cached --stat

if /i not "%AUTO%"=="/auto" (
    echo.
    choice /c YN /m "Commit and push to origin/main?"
    if errorlevel 2 (
        echo Cancelled. Files stay staged, nothing was pushed.
        exit /b 1
    )
)

echo.
if not "%COUNT%"=="0" (
    "%GIT%" commit -m "%MSG%"
    if errorlevel 1 (
        echo [ERROR] Commit failed.
        exit /b 1
    )
) else (
    echo Skip commit, checking for unpushed tags.
)

echo === Sync with remote ===
"%GIT%" fetch origin
if errorlevel 1 (
    echo [ERROR] Cannot reach origin. Check network or credentials.
    exit /b 1
)

set "AHEAD=0"
set "BEHIND=0"
for /f "tokens=1,2" %%A in ('"%GIT%" rev-list --left-right --count main...origin/main') do (
    set "AHEAD=%%A"
    set "BEHIND=%%B"
)
echo Local ahead: %AHEAD%   remote ahead: %BEHIND%

if not "%BEHIND%"=="0" (
    if "%AHEAD%"=="0" (
        echo Remote is ahead, fast-forwarding...
        "%GIT%" pull --ff-only origin main
    ) else (
        echo Branches diverged, rebasing local commit on top of origin/main...
        "%GIT%" pull --rebase origin main
    )
    if errorlevel 1 (
        echo [ERROR] Rebase failed. Resolve conflicts manually, then rerun.
        exit /b 1
    )
)

echo === Push ===
"%GIT%" push origin main
if errorlevel 1 (
    echo [ERROR] Push failed. Sign-in may be required, or the remote rejected it.
    exit /b 1
)

"%GIT%" push origin --tags
if errorlevel 1 (
    echo [WARN] Code pushed, but tags failed to push.
)

echo.
echo Done.
"%GIT%" log --oneline -1
"%GIT%" status --short --branch
exit /b 0
