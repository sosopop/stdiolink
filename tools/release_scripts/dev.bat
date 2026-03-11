@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "POWERSHELL_EXE="

where pwsh.exe >nul 2>nul
if not errorlevel 1 (
    set "POWERSHELL_EXE=pwsh.exe"
) else (
    where powershell.exe >nul 2>nul
    if not errorlevel 1 set "POWERSHELL_EXE=powershell.exe"
)

if not defined POWERSHELL_EXE (
    echo PowerShell not found. Unable to run "%SCRIPT_DIR%dev.ps1".
    exit /b 1
)

"%POWERSHELL_EXE%" -NoExit -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%dev.ps1" %*
