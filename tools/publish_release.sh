#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  tools/publish_release.sh [options]

Options:
  --build-dir <dir>    Build directory (default: build)
  --output-dir <dir>   Release output root (default: release)
  --name <name>        Package name (default: stdiolink_<timestamp>_<git>)
  --with-tests         Include test binaries in bin/
  -h, --help           Show this help

Example:
  tools/publish_release.sh --build-dir build --output-dir release
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="build"
OUTPUT_DIR="release"
PACKAGE_NAME=""
WITH_TESTS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="${2:-}"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="${2:-}"
            shift 2
            ;;
        --name)
            PACKAGE_NAME="${2:-}"
            shift 2
            ;;
        --with-tests)
            WITH_TESTS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "${BUILD_DIR}" || -z "${OUTPUT_DIR}" ]]; then
    echo "Error: --build-dir and --output-dir cannot be empty" >&2
    exit 1
fi

to_abs_path() {
    local path="$1"
    if [[ "${path}" = /* ]]; then
        printf '%s\n' "${path}"
    else
        printf '%s/%s\n' "${ROOT_DIR}" "${path}"
    fi
}

BUILD_DIR_ABS="$(to_abs_path "${BUILD_DIR}")"
OUTPUT_DIR_ABS="$(to_abs_path "${OUTPUT_DIR}")"
BIN_DIR="${BUILD_DIR_ABS}/bin"

if [[ ! -d "${BIN_DIR}" ]]; then
    echo "Error: build bin directory not found: ${BIN_DIR}" >&2
    echo "Please build project first, for example: ./build.sh Release" >&2
    exit 1
fi

if [[ -z "${PACKAGE_NAME}" ]]; then
    TS="$(date '+%Y%m%d_%H%M%S')"
    GIT_HASH="$(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    PACKAGE_NAME="stdiolink_${TS}_${GIT_HASH}"
fi

PACKAGE_DIR="${OUTPUT_DIR_ABS}/${PACKAGE_NAME}"

echo "Preparing release package:"
echo "  root        : ${ROOT_DIR}"
echo "  build bin   : ${BIN_DIR}"
echo "  output root : ${OUTPUT_DIR_ABS}"
echo "  package dir : ${PACKAGE_DIR}"
echo "  with tests  : ${WITH_TESTS}"

rm -rf "${PACKAGE_DIR}"

mkdir -p "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/demo"
mkdir -p "${PACKAGE_DIR}/doc"
mkdir -p "${PACKAGE_DIR}/data_root/drivers"
mkdir -p "${PACKAGE_DIR}/data_root/services"
mkdir -p "${PACKAGE_DIR}/data_root/projects"
mkdir -p "${PACKAGE_DIR}/data_root/workspaces"
mkdir -p "${PACKAGE_DIR}/data_root/logs"
mkdir -p "${PACKAGE_DIR}/data_root/shared"

copy_file_if_exists() {
    local src="$1"
    local dst="$2"
    if [[ -f "${src}" ]]; then
        cp -f "${src}" "${dst}"
    fi
}

copy_dir_clean() {
    local src="$1"
    local dst="$2"
    rm -rf "${dst}"
    mkdir -p "$(dirname "${dst}")"
    cp -R "${src}" "${dst}"
}

should_skip_binary() {
    local name="$1"

    if [[ "${WITH_TESTS}" -eq 0 ]]; then
        case "${name}" in
            stdiolink_tests|test_driver|test_meta_driver|test_service_stub)
                return 0
                ;;
        esac
    fi

    case "${name}" in
        *.log|*.tmp|*.json)
            return 0
            ;;
    esac

    return 1
}

echo "Copying binaries..."
while IFS= read -r -d '' file; do
    base="$(basename "${file}")"
    if should_skip_binary "${base}"; then
        continue
    fi
    cp -f "${file}" "${PACKAGE_DIR}/bin/${base}"
done < <(find "${BIN_DIR}" -maxdepth 1 -type f -print0)

echo "Copying demo assets..."
if [[ -d "${BIN_DIR}/config_demo" ]]; then
    copy_dir_clean "${BIN_DIR}/config_demo" "${PACKAGE_DIR}/demo/config_demo"
elif [[ -d "${ROOT_DIR}/src/demo/config_demo/services" ]]; then
    mkdir -p "${PACKAGE_DIR}/demo/config_demo"
    copy_dir_clean "${ROOT_DIR}/src/demo/config_demo/services" "${PACKAGE_DIR}/demo/config_demo/services"
fi

if [[ -d "${BIN_DIR}/js_runtime_demo" ]]; then
    copy_dir_clean "${BIN_DIR}/js_runtime_demo" "${PACKAGE_DIR}/demo/js_runtime_demo"
elif [[ -d "${ROOT_DIR}/src/demo/js_runtime_demo/services" ]]; then
    mkdir -p "${PACKAGE_DIR}/demo/js_runtime_demo"
    copy_dir_clean "${ROOT_DIR}/src/demo/js_runtime_demo/services" "${PACKAGE_DIR}/demo/js_runtime_demo/services"
    copy_dir_clean "${ROOT_DIR}/src/demo/js_runtime_demo/shared" "${PACKAGE_DIR}/demo/js_runtime_demo/shared"
fi

if [[ -d "${BIN_DIR}/server_manager_demo" ]]; then
    copy_dir_clean "${BIN_DIR}/server_manager_demo" "${PACKAGE_DIR}/demo/server_manager_demo"
elif [[ -d "${ROOT_DIR}/src/demo/server_manager_demo" ]]; then
    copy_dir_clean "${ROOT_DIR}/src/demo/server_manager_demo" "${PACKAGE_DIR}/demo/server_manager_demo"
fi

echo "Copying docs..."
copy_file_if_exists "${ROOT_DIR}/doc/stdiolink_server.md" "${PACKAGE_DIR}/doc/"
copy_file_if_exists "${ROOT_DIR}/doc/milestone/milestone_39_server_manager_demo_and_release.md" "${PACKAGE_DIR}/doc/"

MANIFEST="${PACKAGE_DIR}/RELEASE_MANIFEST.txt"
{
    echo "package_name=${PACKAGE_NAME}"
    echo "created_at=$(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "git_commit=$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "build_dir=${BUILD_DIR_ABS}"
    echo "with_tests=${WITH_TESTS}"
    echo
    echo "[bin]"
    find "${PACKAGE_DIR}/bin" -maxdepth 1 -type f -exec basename {} \; | sort
    echo
    echo "[demo]"
    find "${PACKAGE_DIR}/demo" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort
} > "${MANIFEST}"

echo "Release package created: ${PACKAGE_DIR}"
echo "Manifest file: ${MANIFEST}"
