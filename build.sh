#!/bin/bash

# 设置 UTF-8 编码
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
echo "Build Script"
echo "========================================"

echo "Build Type: $BUILD_TYPE"
echo "Build Directory: $BUILD_DIR"
echo "Install Directory: $INSTALL_DIR"

# 3. 自动检测 vcpkg 安装路径 [cite: 1, 2, 3, 4]
VCPKG_FOUND=0
VCPKG_TOOLCHAIN=""

# 方法 1: 检查 VCPKG_ROOT 环境变量 [cite: 1]
if [ -n "$VCPKG_ROOT" ] && [ -f "$VCPKG_ROOT/vcpkg" ]; then
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    VCPKG_FOUND=1
    echo "Found vcpkg via VCPKG_ROOT: $VCPKG_ROOT"
fi

# 方法 2: 检查当前目录 [cite: 2]
if [ $VCPKG_FOUND -eq 0 ] && [ -f "./vcpkg/vcpkg" ]; then
    VCPKG_ROOT="$(pwd)/vcpkg"
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    VCPKG_FOUND=1
    echo "Found vcpkg in current directory: $VCPKG_ROOT"
fi

# 方法 3: 检查上级目录 [cite: 3]
if [ $VCPKG_FOUND -eq 0 ] && [ -f "../vcpkg/vcpkg" ]; then
    VCPKG_ROOT="$(cd .. && pwd)/vcpkg"
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    VCPKG_FOUND=1
    echo "Found vcpkg in parent directory: $VCPKG_ROOT"
fi

# 方法 4: 检查系统 PATH [cite: 4, 5]
if [ $VCPKG_FOUND -eq 0 ]; then
    VCPKG_PATH=$(command -v vcpkg)
    if [ -n "$VCPKG_PATH" ]; then
        VCPKG_ROOT=$(dirname "$VCPKG_PATH")
        VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
        VCPKG_FOUND=1
        echo "Found vcpkg in PATH: $VCPKG_ROOT"
    fi
fi

# 如果未找到 vcpkg，报错退出 [cite: 6]
if [ $VCPKG_FOUND -eq 0 ]; then
    echo "Error: vcpkg not found!"
    echo "Please install vcpkg or set VCPKG_ROOT."
    exit 1
fi

# 检查工具链文件是否存在 [cite: 7]
if [ ! -f "$VCPKG_TOOLCHAIN" ]; then
    echo "Error: vcpkg toolchain file not found at $VCPKG_TOOLCHAIN"
    exit 1
fi

echo "Using vcpkg toolchain: $VCPKG_TOOLCHAIN"

# 4. 创建构建目录 [cite: 7]
mkdir -p "$BUILD_DIR"

echo "========================================"
echo "Configuring project with CMake..."
echo "========================================"

# 5. 使用 CMake 配置项目 [cite: 8, 9, 10]
# 根据系统与架构自动选择 vcpkg triplet
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

# 6. 执行构建 
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel 8

if [ $? -ne 0 ]; then
    echo "Error: Build failed"
    exit 1
fi

echo "========================================"
echo "Installing project..."
echo "========================================"

# 7. 执行安装 [cite: 11]
cmake --install "$BUILD_DIR" --config "$BUILD_TYPE"

if [ $? -ne 0 ]; then
    echo "Error: Installation failed"
    exit 1
fi

echo "========================================"
echo "Build completed successfully!"
echo "========================================"
