#!/bin/bash

export LANG=en_US.UTF-8

show_usage() {
    cat <<'EOF'
Usage:
  ./build.sh [BuildType] [--build-dir <dir>]

Examples:
  ./build.sh
  ./build.sh Release
  ./build.sh Release --build-dir build_release
EOF
}

BUILD_TYPE="Debug"
BUILD_DIR="build"
INSTALL_DIR="install"
BUILD_TYPE_SET=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            if [[ $# -lt 2 || -z "${2:-}" ]]; then
                echo "Error: missing value for --build-dir"
                show_usage
                exit 1
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        --*)
            echo "Error: unknown option '$1'"
            show_usage
            exit 1
            ;;
        *)
            if [[ "${BUILD_TYPE_SET}" -eq 1 ]]; then
                echo "Error: unexpected argument '$1'"
                show_usage
                exit 1
            fi
            BUILD_TYPE="$1"
            BUILD_TYPE_SET=1
            shift
            ;;
    esac
done

echo "========================================"
echo "Build Script for Unix"
echo "========================================"

echo "Build Type: $BUILD_TYPE"
echo "Build Directory: $BUILD_DIR"
echo "Install Directory: $INSTALL_DIR"

VCPKG_FOUND=0
VCPKG_TOOLCHAIN=""

if [ -n "$VCPKG_ROOT" ] && [ -f "$VCPKG_ROOT/vcpkg" ]; then
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    VCPKG_FOUND=1
    echo "Found vcpkg via VCPKG_ROOT: $VCPKG_ROOT"
fi

if [ $VCPKG_FOUND -eq 0 ] && [ -f "./vcpkg/vcpkg" ]; then
    VCPKG_ROOT="$(pwd)/vcpkg"
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    VCPKG_FOUND=1
    echo "Found vcpkg in current directory: $VCPKG_ROOT"
fi

if [ $VCPKG_FOUND -eq 0 ] && [ -f "../vcpkg/vcpkg" ]; then
    VCPKG_ROOT="$(cd .. && pwd)/vcpkg"
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    VCPKG_FOUND=1
    echo "Found vcpkg in parent directory: $VCPKG_ROOT"
fi

if [ $VCPKG_FOUND -eq 0 ]; then
    VCPKG_PATH=$(command -v vcpkg)
    if [ -n "$VCPKG_PATH" ]; then
        VCPKG_ROOT=$(dirname "$VCPKG_PATH")
        VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
        VCPKG_FOUND=1
        echo "Found vcpkg in PATH: $VCPKG_ROOT"
    fi
fi

if [ $VCPKG_FOUND -eq 0 ]; then
    echo "Error: vcpkg not found!"
    echo "Searched locations:"
    echo "  - VCPKG_ROOT environment variable"
    echo "  - Current directory: $(pwd)/vcpkg"
    echo "  - Parent directory: $(cd .. && pwd)/vcpkg"
    echo "  - System PATH"
    echo
    echo "Please install vcpkg or ensure it's in one of the above locations"
    exit 1
fi

if [ ! -f "$VCPKG_TOOLCHAIN" ]; then
    echo "Error: vcpkg toolchain file not found at $VCPKG_TOOLCHAIN"
    exit 1
fi

echo "Using vcpkg toolchain: $VCPKG_TOOLCHAIN"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

echo "========================================"
echo "Configuring project with CMake..."
echo "========================================"

OS_NAME=$(uname -s)
ARCH=$(uname -m)

case "$OS_NAME" in
    Darwin)
        if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
            TRIPLET="arm64-osx"
        else
            TRIPLET="x64-osx"
        fi
        ;;
    Linux)
        if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
            TRIPLET="arm64-linux"
        else
            TRIPLET="x64-linux"
        fi
        ;;
    *)
        echo "Error: Unsupported OS '$OS_NAME'. Please set VCPKG_TARGET_TRIPLET manually."
        exit 1
        ;;
esac

echo "Detected OS: $OS_NAME, Arch: $ARCH, vcpkg triplet: $TRIPLET"

OVERLAY_PORTS_DIR="$(pwd)/vcpkg-overlay-ports"
if [ -d "$OVERLAY_PORTS_DIR" ]; then
    echo "Using vcpkg overlay ports: $OVERLAY_PORTS_DIR"
fi

# Keep Linux/macOS triplet auto-detection, which has no equivalent in build.bat.
cmake -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/$INSTALL_DIR" \
    -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
    -DVCPKG_INSTALLED_DIR="$(pwd)/vcpkg_installed" \
    -DVCPKG_OVERLAY_PORTS="$OVERLAY_PORTS_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -G "Ninja" 

if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed"
    exit 1
fi

echo "========================================"
echo "Building project..."
echo "========================================"

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel 8

if [ $? -ne 0 ]; then
    echo "Error: Build failed"
    exit 1
fi

echo "========================================"
echo "Installing project..."
echo "========================================"

cmake --install "$BUILD_DIR" --config "$BUILD_TYPE"

if [ $? -ne 0 ]; then
    echo "Error: Installation failed"
    exit 1
fi

echo "========================================"
echo "Build completed successfully!"
BT_LOWER="$BUILD_TYPE"
if [ "$BUILD_TYPE" = "Debug" ] || [ "$BUILD_TYPE" = "debug" ]; then
    BT_LOWER="debug"
elif [ "$BUILD_TYPE" = "Release" ] || [ "$BUILD_TYPE" = "release" ]; then
    BT_LOWER="release"
fi
echo "Runtime directory: $BUILD_DIR/runtime_$BT_LOWER"
echo "========================================"
