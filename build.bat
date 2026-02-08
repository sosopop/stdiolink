@echo off
chcp 65001

setlocal enabledelayedexpansion

echo ========================================
echo Build Script for Windows
echo ========================================

:: Set build configuration (default to Release)
set BUILD_TYPE=Debug
if not "%1"=="" set BUILD_TYPE=%1

:: Set build directory
set BUILD_DIR=build
set INSTALL_DIR=install

echo Build Type: %BUILD_TYPE%
echo Build Directory: %BUILD_DIR%
echo Install Directory: %INSTALL_DIR%

:: Auto-detect vcpkg installation
set VCPKG_FOUND=0
set VCPKG_TOOLCHAIN=

:: Method 1: Check if VCPKG_ROOT is already set
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        set VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
        set VCPKG_FOUND=1
        echo Found vcpkg via VCPKG_ROOT: %VCPKG_ROOT%
    )
)

:: Method 2: Check current directory for vcpkg
if !VCPKG_FOUND! equ 0 (
    if exist "vcpkg\vcpkg.exe" (
        set VCPKG_ROOT=%CD%\vcpkg
        set VCPKG_TOOLCHAIN=%CD%\vcpkg\scripts\buildsystems\vcpkg.cmake
        set VCPKG_FOUND=1
        echo Found vcpkg in current directory: !VCPKG_ROOT!
    )
)

:: Method 3: Check parent directory for vcpkg
if !VCPKG_FOUND! equ 0 (
    if exist "..\vcpkg\vcpkg.exe" (
        for %%i in ("%CD%\..") do set PARENT_DIR=%%~fi
        set VCPKG_ROOT=!PARENT_DIR!\vcpkg
        set VCPKG_TOOLCHAIN=!PARENT_DIR!\vcpkg\scripts\buildsystems\vcpkg.cmake
        set VCPKG_FOUND=1
        echo Found vcpkg in parent directory: !VCPKG_ROOT!
    )
)

:: Method 4: Check if vcpkg is in PATH
if !VCPKG_FOUND! equ 0 (
    where vcpkg >nul 2>nul
    if !errorlevel! equ 0 (
        for /f "tokens=*" %%i in ('where vcpkg') do (
            set VCPKG_PATH=%%i
            for %%j in ("!VCPKG_PATH!") do set VCPKG_DIR=%%~dpj
            set VCPKG_ROOT=!VCPKG_DIR!
            set VCPKG_TOOLCHAIN=!VCPKG_DIR!scripts\buildsystems\vcpkg.cmake
            set VCPKG_FOUND=1
            echo Found vcpkg in PATH: !VCPKG_ROOT!
        )
    )
)

:: If vcpkg not found, show error
if !VCPKG_FOUND! equ 0 (
    echo Error: vcpkg not found!
    echo Searched locations:
    echo   - VCPKG_ROOT environment variable
    echo   - Current directory: %CD%\vcpkg
    echo   - Parent directory: %CD%\..\vcpkg
    echo   - System PATH
    echo.
    echo Please install vcpkg or ensure it's in one of the above locations
    exit /b 1
)

echo Using vcpkg toolchain: %VCPKG_TOOLCHAIN%

:: Check if toolchain file exists
if not exist "%VCPKG_TOOLCHAIN%" (
    echo Error: vcpkg toolchain file not found at %VCPKG_TOOLCHAIN%
    exit /b 1
)

:: Create build directory if it doesn't exist (optional, cmake -B does this too)
if not exist %BUILD_DIR% (
    echo Creating build directory...
    mkdir %BUILD_DIR%
)

:: Note: We do NOT 'cd' into the build directory anymore.

echo ========================================
echo Configuring project with CMake...
echo ========================================

:: Configure with CMake
:: -S . : Source is current directory
:: -B "%BUILD_DIR%" : Build directory target
cmake -S . -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_INSTALL_PREFIX="%CD%\%INSTALL_DIR%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DVCPKG_INSTALLED_DIR="%CD%\..\vcpkg_installed" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -G "Ninja"

if %errorlevel% neq 0 (
    echo Error: CMake configuration failed
    exit /b 1
)

echo ========================================
echo Building project...
echo ========================================

:: Build the project inside the specified build dir
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel 8

if %errorlevel% neq 0 (
    echo Error: Build failed
    exit /b 1
)

echo ========================================
echo Installing project...
echo ========================================

:: Install the project from the specified build dir
cmake --install "%BUILD_DIR%" --config %BUILD_TYPE%

if %errorlevel% neq 0 (
    echo Error: Installation failed
    exit /b 1
)

echo ========================================
echo Build completed successfully!
echo ========================================