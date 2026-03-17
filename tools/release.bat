@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "RELEASE_PS1=%SCRIPT_DIR%release.ps1"

if not exist "%RELEASE_PS1%" (
    echo release.ps1 not found at "%RELEASE_PS1%" 1>&2
    exit /b 1
)

set "POWERSHELL_EXE="
where pwsh >nul 2>nul
if %errorlevel% equ 0 (
    set "POWERSHELL_EXE=pwsh"
) else (
    where powershell >nul 2>nul
    if %errorlevel% equ 0 (
        set "POWERSHELL_EXE=powershell"
    )
)

if not defined POWERSHELL_EXE (
    echo pwsh/powershell not found in PATH 1>&2
    exit /b 1
)

%POWERSHELL_EXE% -ExecutionPolicy Bypass -File "%RELEASE_PS1%" %*
exit /b %errorlevel%
