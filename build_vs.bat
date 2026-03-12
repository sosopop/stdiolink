@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo Build Script for Windows ^(Visual Studio^)
echo ========================================

:: Build config: Debug / Release / RelWithDebInfo / MinSizeRel
set CONFIG=Debug
if not "%~1"=="" set CONFIG=%~1

:: VS generator and platform
set GENERATOR=Visual Studio 17 2022
set PLATFORM=x64

:: Use script location as project root
set SCRIPT_DIR=%~dp0
for %%i in ("%SCRIPT_DIR%.") do set PROJECT_ROOT=%%~fi

set BUILD_DIR=%PROJECT_ROOT%\build_vs
set INSTALL_DIR=%PROJECT_ROOT%\install

echo Configuration: %CONFIG%
echo Generator: %GENERATOR%
echo Platform: %PLATFORM%
echo Project Root: %PROJECT_ROOT%
echo Build Directory: %BUILD_DIR%
echo Install Directory: %INSTALL_DIR%

:: Auto-detect vcpkg installation
set VCPKG_FOUND=0
set VCPKG_TOOLCHAIN=

:: Method 1: Check VCPKG_ROOT
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        set VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
        set VCPKG_FOUND=1
        echo Found vcpkg via VCPKG_ROOT: %VCPKG_ROOT%
    )
)

:: Method 2: Check project root\vcpkg
if !VCPKG_FOUND! equ 0 (
    if exist "%PROJECT_ROOT%\vcpkg\vcpkg.exe" (
        set VCPKG_ROOT=%PROJECT_ROOT%\vcpkg
        set VCPKG_TOOLCHAIN=%PROJECT_ROOT%\vcpkg\scripts\buildsystems\vcpkg.cmake
        set VCPKG_FOUND=1
        echo Found vcpkg in project root: !VCPKG_ROOT!
    )
)

:: Method 3: Check parent directory
if !VCPKG_FOUND! equ 0 (
    if exist "%PROJECT_ROOT%\..\vcpkg\vcpkg.exe" (
        for %%i in ("%PROJECT_ROOT%\..") do set PARENT_DIR=%%~fi
        set VCPKG_ROOT=!PARENT_DIR!\vcpkg
        set VCPKG_TOOLCHAIN=!PARENT_DIR!\vcpkg\scripts\buildsystems\vcpkg.cmake
        set VCPKG_FOUND=1
        echo Found vcpkg in parent directory: !VCPKG_ROOT!
    )
)

:: Method 4: Check PATH
if !VCPKG_FOUND! equ 0 (
    where vcpkg >nul 2>nul
    if !errorlevel! equ 0 (
        for /f "delims=" %%i in ('where vcpkg') do (
            set VCPKG_EXE=%%i
            goto :found_vcpkg_from_path
        )
    )
)

goto :after_vcpkg_search

:found_vcpkg_from_path
for %%j in ("!VCPKG_EXE!") do set VCPKG_ROOT=%%~dpj
if "!VCPKG_ROOT:~-1!"=="\" set VCPKG_ROOT=!VCPKG_ROOT:~0,-1!
set VCPKG_TOOLCHAIN=!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake
set VCPKG_FOUND=1
echo Found vcpkg in PATH: !VCPKG_ROOT!

:after_vcpkg_search

if !VCPKG_FOUND! equ 0 (
    echo Error: vcpkg not found!
    echo Searched locations:
    echo   - VCPKG_ROOT environment variable
    echo   - %PROJECT_ROOT%\vcpkg
    echo   - %PROJECT_ROOT%\..\vcpkg
    echo   - System PATH
    exit /b 1
)

if not exist "%VCPKG_TOOLCHAIN%" (
    echo Error: vcpkg toolchain file not found at:
    echo   %VCPKG_TOOLCHAIN%
    exit /b 1
)

echo Using vcpkg toolchain: %VCPKG_TOOLCHAIN%

:: Create build directory
if not exist "%BUILD_DIR%" (
    echo Creating build directory...
    mkdir "%BUILD_DIR%"
)

cd /d "%BUILD_DIR%"

echo ========================================
echo Configuring project with CMake...
echo ========================================

:: Important:
:: 1) Visual Studio generator must be explicit
:: 2) Do NOT pass CMAKE_BUILD_TYPE for multi-config generators
cmake "%PROJECT_ROOT%" ^
    -G "%GENERATOR%" ^
    -A %PLATFORM% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DVCPKG_INSTALLED_DIR="%PROJECT_ROOT%\vcpkg_installed"

if %errorlevel% neq 0 (
    echo Error: CMake configuration failed
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

echo ========================================
echo Building project...
echo ========================================

cmake --build . --config %CONFIG% --parallel 8

if %errorlevel% neq 0 (
    echo Error: Build failed
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

echo ========================================
echo Installing project...
echo ========================================

cmake --install . --config %CONFIG%

if %errorlevel% neq 0 (
    echo Error: Installation failed
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

cd /d "%PROJECT_ROOT%"

echo ========================================
echo Build completed successfully!
echo ========================================