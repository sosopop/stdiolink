@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "RELEASE_BAT=%SCRIPT_DIR%tools\release.bat"
set "BUILD_TYPE=debug"
set "BUILD_DIR=build"
set "BUILD_TYPE_SET="

if not exist "%RELEASE_BAT%" (
    echo release.bat not found at "%RELEASE_BAT%" 1>&2
    exit /b 1
)

:parse_args
if "%~1"=="" goto run_build
if /I "%~1"=="--build-dir" (
    if "%~2"=="" (
        echo Error: missing value for --build-dir 1>&2
        exit /b 1
    )
    set "BUILD_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="-h" goto show_help
if /I "%~1"=="--help" goto show_help
if /I "%~1"=="/?" goto show_help
if defined BUILD_TYPE_SET (
    echo Error: unexpected argument "%~1" 1>&2
    exit /b 1
)
set "BUILD_TYPE=%~1"
set "BUILD_TYPE_SET=1"
shift
goto parse_args

:show_help
call "%RELEASE_BAT%" build --help
exit /b %errorlevel%

:run_build
call "%RELEASE_BAT%" build --config "%BUILD_TYPE%" --build-dir "%BUILD_DIR%"
exit /b %errorlevel%
