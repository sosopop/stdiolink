#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
ROOT_DIR=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd -P)
PARENT_DIR=$(CDPATH= cd -- "${ROOT_DIR}/.." && pwd -P)
VCPKG_DIR="${PARENT_DIR}/vcpkg"

WITH_WEBUI=1
WITH_PLAYWRIGHT=0
PRINT_ONLY=0
SKIP_VCPKG_BOOTSTRAP=0
SKIP_VCPKG_INSTALL=0

BASE_PACKAGES=(
    build-essential
    cmake
    ninja-build
    pkg-config
    git
    curl
    zip
    unzip
    tar
    python3
    ca-certificates
)

VCPKG_BUILD_PACKAGES=(
    autoconf
    automake
    bison
    flex
    gperf
    nasm
    libtool
    perl
)

QT_LINUX_PACKAGES=(
    libasound2-dev
    libdbus-1-dev
    libegl1-mesa-dev
    libfontconfig1-dev
    libfreetype6-dev
    libgl1-mesa-dev
    libglu1-mesa-dev
    libglib2.0-dev
    libice-dev
    libicu-dev
    libinput-dev
    libjpeg-dev
    libnss3-dev
    libopengl-dev
    libpng-dev
    libpulse-dev
    libsm-dev
    libssl-dev
    libudev-dev
    libx11-dev
    libx11-xcb-dev
    libxcb-glx0-dev
    libxcb-icccm4-dev
    libxcb-image0-dev
    libxcb-keysyms1-dev
    libxcb-randr0-dev
    libxcb-render-util0-dev
    libxcb-shape0-dev
    libxcb-shm0-dev
    libxcb-sync-dev
    libxcb-util-dev
    libxcb-xfixes0-dev
    libxcb-xinerama0-dev
    libxcb-xkb-dev
    libxcb1-dev
    libxext-dev
    libxfixes-dev
    libxi-dev
    libxkbcommon-dev
    libxkbcommon-x11-dev
    libxrender-dev
    zlib1g-dev
)

WEBUI_PACKAGES=(
    nodejs
    npm
)

usage() {
    cat <<'EOF'
Usage: tools/install_build_deps.sh [options]

Install Debian/Ubuntu packages needed to build stdiolink, bootstrap vcpkg in ../vcpkg,
and install the repository manifest dependencies.

Options:
  --without-webui         Skip Node.js/npm installation.
  --with-playwright       Also install Playwright browser/system dependencies.
  --skip-vcpkg-bootstrap  Do not clone/bootstrap ../vcpkg.
  --skip-vcpkg-install    Do not run vcpkg manifest install.
  --print-only            Print commands without executing them.
  -h, --help              Show this help.
EOF
}

log() {
    printf '%s\n' "$*"
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

run_cmd() {
    if [ "${PRINT_ONLY}" -eq 1 ]; then
        printf '+'
        for arg in "$@"; do
            printf ' %q' "${arg}"
        done
        printf '\n'
        return 0
    fi

    "$@"
}

need_sudo() {
    if [ "$(id -u)" -eq 0 ]; then
        return 1
    fi
    return 0
}

APT_PREFIX=()
if need_sudo; then
    APT_PREFIX=(sudo)
fi

while [ "$#" -gt 0 ]; do
    case "$1" in
        --without-webui)
            WITH_WEBUI=0
            ;;
        --with-playwright)
            WITH_PLAYWRIGHT=1
            ;;
        --skip-vcpkg-bootstrap)
            SKIP_VCPKG_BOOTSTRAP=1
            ;;
        --skip-vcpkg-install)
            SKIP_VCPKG_INSTALL=1
            ;;
        --print-only)
            PRINT_ONLY=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
    shift
done

if ! have_cmd apt-get; then
    die "this script currently supports Debian/Ubuntu apt only"
fi

APT_PACKAGES=(
    "${BASE_PACKAGES[@]}"
    "${VCPKG_BUILD_PACKAGES[@]}"
    "${QT_LINUX_PACKAGES[@]}"
)

if [ "${WITH_WEBUI}" -eq 1 ]; then
    APT_PACKAGES+=("${WEBUI_PACKAGES[@]}")
fi

log "Repository root: ${ROOT_DIR}"
log "Expected vcpkg path: ${VCPKG_DIR}"
log "Install WebUI deps: ${WITH_WEBUI}"
log "Install Playwright deps: ${WITH_PLAYWRIGHT}"

run_cmd "${APT_PREFIX[@]}" apt-get update
run_cmd "${APT_PREFIX[@]}" apt-get install -y "${APT_PACKAGES[@]}"

if [ "${SKIP_VCPKG_BOOTSTRAP}" -eq 0 ]; then
    if [ ! -d "${VCPKG_DIR}/.git" ]; then
        run_cmd git clone https://github.com/microsoft/vcpkg "${VCPKG_DIR}"
    else
        log "vcpkg already present at ${VCPKG_DIR}"
    fi

    if [ ! -x "${VCPKG_DIR}/vcpkg" ]; then
        run_cmd "${VCPKG_DIR}/bootstrap-vcpkg.sh" -disableMetrics
    else
        log "vcpkg bootstrap already completed"
    fi
fi

if [ "${SKIP_VCPKG_INSTALL}" -eq 0 ]; then
    VCPKG_BIN="${VCPKG_DIR}/vcpkg"
    [ -x "${VCPKG_BIN}" ] || die "vcpkg executable not found at ${VCPKG_BIN}"
    run_cmd "${VCPKG_BIN}" install --x-manifest-root="${ROOT_DIR}" --overlay-ports="${ROOT_DIR}/vcpkg-overlay-ports"
fi

if [ "${WITH_WEBUI}" -eq 1 ]; then
    if have_cmd node; then
        NODE_MAJOR=$(node -p 'process.versions.node.split(".")[0]' 2>/dev/null || printf '0')
        if [ "${NODE_MAJOR}" -lt 18 ]; then
            printf 'Warning: detected Node.js %s, but src/webui/package.json requires >= 18.\n' "$(node -v)" >&2
        fi
    else
        printf 'Warning: node was not found in PATH after apt installation.\n' >&2
    fi
fi

if [ "${WITH_PLAYWRIGHT}" -eq 1 ]; then
    [ "${WITH_WEBUI}" -eq 1 ] || die "--with-playwright requires WebUI dependencies"
    WEBUI_DIR="${ROOT_DIR}/src/webui"
    if [ ! -f "${WEBUI_DIR}/package.json" ]; then
        die "WebUI package.json not found at ${WEBUI_DIR}"
    fi
    if [ ! -d "${WEBUI_DIR}/node_modules" ]; then
        run_cmd npm --prefix "${WEBUI_DIR}" ci --ignore-scripts
    fi
    run_cmd "${APT_PREFIX[@]}" npx --yes playwright install --with-deps chromium
fi

log "Dependency installation steps completed."
log "Next build command: ${ROOT_DIR}/build.sh"
