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
  --skip-build         Skip C++ build (assume runtime_release already exists)
  --skip-webui         Skip WebUI build
  --skip-tests         Skip test execution before packaging
  -h, --help           Show this help

Example:
  tools/publish_release.sh --build-dir build_release --output-dir release
  tools/publish_release.sh --name my_release
EOF
}

resolve_absolute_path() {
    local path="$1"
    local root_dir="$2"

    if [[ "${path}" = /* ]]; then
        printf '%s\n' "${path}"
    else
        printf '%s/%s\n' "${root_dir}" "${path}"
    fi
}

get_git_rev() {
    local root_dir="$1"
    shift

    local value=""
    if value="$(git -C "${root_dir}" rev-parse "$@" 2>/dev/null)" && [[ -n "${value}" ]]; then
        value="${value//$'\r'/}"
        value="${value//$'\n'/}"
        printf '%s\n' "${value}"
        return 0
    fi

    printf '%s\n' "unknown"
}

copy_release_script_files() {
    local template_dir="$1"
    local destination_dir="$2"
    local script_name=""

    for script_name in start.sh dev.sh; do
        local source_path="${template_dir}/${script_name}"
        if [[ ! -f "${source_path}" ]]; then
            echo "Error: release launcher template not found: ${source_path}" >&2
            exit 1
        fi

        cp -f "${source_path}" "${destination_dir}/${script_name}"
        chmod +x "${destination_dir}/${script_name}"
    done
}

should_skip_binary() {
    local name="$1"
    local stem="${name%.*}"

    if [[ "${WITH_TESTS}" -eq 0 ]]; then
        case "${stem}" in
            stdiolink_tests|test_*|gtest)
                return 0
                ;;
        esac
    fi

    case "${stem}" in
        demo_host|driverlab)
            return 0
            ;;
    esac

    case "${name}" in
        *.log|*.tmp|*.json)
            return 0
            ;;
    esac

    return 1
}

find_python_cmd() {
    if command -v python3 >/dev/null 2>&1; then
        printf '%s\n' "python3"
        return 0
    fi

    if command -v python >/dev/null 2>&1; then
        printf '%s\n' "python"
        return 0
    fi

    return 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="build"
OUTPUT_DIR="release"
PACKAGE_NAME=""
WITH_TESTS=0
SKIP_BUILD=0
SKIP_WEBUI=0
SKIP_TESTS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            if [[ $# -lt 2 || -z "${2:-}" ]]; then
                echo "Error: missing value for --build-dir" >&2
                usage
                exit 1
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            if [[ $# -lt 2 || -z "${2:-}" ]]; then
                echo "Error: missing value for --output-dir" >&2
                usage
                exit 1
            fi
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --name)
            if [[ $# -lt 2 || -z "${2:-}" ]]; then
                echo "Error: missing value for --name" >&2
                usage
                exit 1
            fi
            PACKAGE_NAME="$2"
            shift 2
            ;;
        --with-tests)
            WITH_TESTS=1
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --skip-webui)
            SKIP_WEBUI=1
            shift
            ;;
        --skip-tests)
            SKIP_TESTS=1
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

BUILD_DIR_ABS="$(resolve_absolute_path "${BUILD_DIR}" "${ROOT_DIR}")"
OUTPUT_DIR_ABS="$(resolve_absolute_path "${OUTPUT_DIR}" "${ROOT_DIR}")"
RUNTIME_DIR="${BUILD_DIR_ABS}/runtime_release"
RELEASE_SCRIPT_TEMPLATE_DIR="${SCRIPT_DIR}/release_scripts_unix"

if [[ "${SKIP_BUILD}" -eq 0 ]]; then
    RUNTIME_CLEAN="${BUILD_DIR_ABS}/runtime_release"
    if [[ -d "${RUNTIME_CLEAN}" ]]; then
        rm -rf "${RUNTIME_CLEAN}"
    fi

    echo "Building C++ project (Release)..."
    BUILD_SCRIPT="${ROOT_DIR}/build.sh"
    if [[ ! -f "${BUILD_SCRIPT}" ]]; then
        echo "Error: build.sh not found at ${BUILD_SCRIPT}" >&2
        exit 1
    fi

    pushd "${ROOT_DIR}" > /dev/null
    bash "${BUILD_SCRIPT}" Release --build-dir "${BUILD_DIR}"
    popd > /dev/null
fi

if [[ ! -d "${RUNTIME_DIR}" ]]; then
    echo "Error: runtime directory not found: ${RUNTIME_DIR}" >&2
    echo "Please build project first, for example: ./build.sh Release" >&2
    exit 1
fi

if [[ -z "${PACKAGE_NAME}" ]]; then
    TS="$(date '+%Y%m%d_%H%M%S')"
    GIT_HASH="$(get_git_rev "${ROOT_DIR}" --short HEAD)"
    PACKAGE_NAME="stdiolink_${TS}_${GIT_HASH}"
fi

PACKAGE_DIR="${OUTPUT_DIR_ABS}/${PACKAGE_NAME}"

echo "Preparing release package:"
echo "  root        : ${ROOT_DIR}"
echo "  runtime     : ${RUNTIME_DIR}"
echo "  output root : ${OUTPUT_DIR_ABS}"
echo "  package dir : ${PACKAGE_DIR}"
echo "  with tests  : ${WITH_TESTS}"
echo "  skip webui  : ${SKIP_WEBUI}"
echo "  skip tests  : ${SKIP_TESTS}"

rm -rf "${PACKAGE_DIR}"

mkdir -p "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/data_root/drivers"
mkdir -p "${PACKAGE_DIR}/data_root/services"
mkdir -p "${PACKAGE_DIR}/data_root/projects"
mkdir -p "${PACKAGE_DIR}/data_root/workspaces"
mkdir -p "${PACKAGE_DIR}/data_root/logs"

if [[ "${SKIP_WEBUI}" -eq 0 ]] && [[ -f "${ROOT_DIR}/src/webui/package.json" ]]; then
    echo "Building WebUI..."
    pushd "${ROOT_DIR}/src/webui" > /dev/null
    if ! command -v npm >/dev/null 2>&1; then
        echo "Error: npm not found. Install Node.js or use --skip-webui to skip WebUI build." >&2
        popd > /dev/null
        exit 1
    fi

    echo "  npm ci ..."
    if ! npm ci --ignore-scripts; then
        echo "  npm ci failed, retrying with npm install ..."
        npm install --ignore-scripts
    fi

    echo "  npm run build ..."
    npm run build

    if [[ ! -d "${ROOT_DIR}/src/webui/dist" ]]; then
        echo "Error: WebUI build succeeded but dist/ is missing" >&2
        popd > /dev/null
        exit 1
    fi

    WEBUI_DEST="${PACKAGE_DIR}/data_root/webui"
    mkdir -p "${WEBUI_DEST}"
    cp -R "${ROOT_DIR}/src/webui/dist/." "${WEBUI_DEST}/"
    echo "  WebUI copied to ${WEBUI_DEST}"
    popd > /dev/null
elif [[ "${SKIP_WEBUI}" -eq 0 ]]; then
    echo "WebUI source not found, skipping WebUI build."
fi

if [[ "${SKIP_TESTS}" -eq 0 ]]; then
    echo "=== Running test suites ==="

    RUNTIME_BIN_DIR="${RUNTIME_DIR}/bin"
    GTEST_BIN="${RUNTIME_BIN_DIR}/stdiolink_tests"
    if [[ -f "${GTEST_BIN}" ]]; then
        echo "--- GTest (C++) ---"
        "${GTEST_BIN}"
        echo "  GTest passed."
    else
        echo "WARNING: GTest binary not found at ${GTEST_BIN}, skipping C++ tests."
    fi

    if command -v npm >/dev/null 2>&1 && [[ -d "${ROOT_DIR}/src/webui/node_modules" ]]; then
        echo "--- Vitest (WebUI unit tests) ---"
        pushd "${ROOT_DIR}/src/webui" > /dev/null
        npm run test
        popd > /dev/null
        echo "  Vitest passed."
    else
        echo "WARNING: npm or node_modules not available, skipping Vitest."
    fi

    if command -v npx >/dev/null 2>&1 && [[ -d "${ROOT_DIR}/src/webui/node_modules" ]]; then
        echo "--- Playwright (E2E tests) ---"
        pushd "${ROOT_DIR}/src/webui" > /dev/null
        npx playwright install chromium
        npx playwright test
        popd > /dev/null
        echo "  Playwright passed."
    else
        echo "WARNING: npx or node_modules not available, skipping Playwright."
    fi

    PYTHON_CMD="$(find_python_cmd || true)"
    if [[ -n "${PYTHON_CMD}" ]]; then
        SMOKE_SCRIPT="${ROOT_DIR}/src/smoke_tests/run_smoke.py"
        if [[ -f "${SMOKE_SCRIPT}" ]]; then
            echo "--- Smoke tests (Python) ---"
            "${PYTHON_CMD}" "${SMOKE_SCRIPT}" --plan all
            echo "  Smoke tests passed."
        else
            echo "WARNING: Smoke test script not found at ${SMOKE_SCRIPT}, skipping smoke tests."
        fi
    else
        echo "WARNING: Python not found, skipping smoke tests."
    fi

    echo "=== All test suites passed ==="
fi

echo "Copying binaries from runtime..."
RUNTIME_BIN="${RUNTIME_DIR}/bin"
while IFS= read -r -d '' file; do
    base="$(basename "${file}")"
    if should_skip_binary "${base}"; then
        continue
    fi
    cp -f "${file}" "${PACKAGE_DIR}/bin/${base}"
done < <(find "${RUNTIME_BIN}" -maxdepth 1 -type f -print0)

echo "Copying Qt plugin directories..."
while IFS= read -r -d '' dir; do
    dir_name="$(basename "${dir}")"
    dest="${PACKAGE_DIR}/bin/${dir_name}"
    mkdir -p "${dest}"
    while IFS= read -r -d '' lib; do
        cp -f "${lib}" "${dest}/"
    done < <(find "${dir}" -maxdepth 1 -type f \( -name "*.so" -o -name "*.so.*" -o -name "*.dylib" -o -name "*.dll" \) -print0)
    echo "  + ${dir_name}/"
done < <(find "${RUNTIME_BIN}" -mindepth 1 -maxdepth 1 -type d -print0)

echo "Copying data_root from runtime..."
RUNTIME_DATA_ROOT="${RUNTIME_DIR}/data_root"
if [[ -d "${RUNTIME_DATA_ROOT}" ]]; then
    cp -R "${RUNTIME_DATA_ROOT}/." "${PACKAGE_DIR}/data_root/"
fi

CONFIG_PATH="${PACKAGE_DIR}/data_root/config.json"
if [[ ! -f "${CONFIG_PATH}" ]]; then
    echo "Generating default config.json..."
    cat > "${CONFIG_PATH}" <<'CONFIGEOF'
{
    "host": "127.0.0.1",
    "port": 6200,
    "logLevel": "info"
}
CONFIGEOF
fi

echo "Copying release launcher scripts..."
copy_release_script_files "${RELEASE_SCRIPT_TEMPLATE_DIR}" "${PACKAGE_DIR}"

MANIFEST="${PACKAGE_DIR}/RELEASE_MANIFEST.txt"
{
    echo "package_name=${PACKAGE_NAME}"
    echo "created_at=$(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "git_commit=$(get_git_rev "${ROOT_DIR}" HEAD)"
    echo "build_dir=${BUILD_DIR_ABS}"
    echo "with_tests=${WITH_TESTS}"
    echo "skip_webui=${SKIP_WEBUI}"
    echo "skip_tests=${SKIP_TESTS}"
    echo
    echo "[bin]"
    find "${PACKAGE_DIR}/bin" -maxdepth 1 -type f -exec basename {} \; | sort
    echo
    echo "[webui]"
    if [[ -f "${PACKAGE_DIR}/data_root/webui/index.html" ]]; then
        echo "status=bundled"
        find "${PACKAGE_DIR}/data_root/webui" -type f -exec basename {} \; | sort
    else
        echo "status=not_included"
    fi
} > "${MANIFEST}"

echo "Checking for duplicate components..."
CHECK_SCRIPT="${SCRIPT_DIR}/check_duplicates.sh"
if [[ -f "${CHECK_SCRIPT}" ]]; then
    if ! bash "${CHECK_SCRIPT}" "${PACKAGE_DIR}"; then
        echo "ERROR: Duplicate check failed! See errors above." >&2
        exit 1
    fi
else
    echo "ERROR: check_duplicates.sh not found at ${CHECK_SCRIPT}" >&2
    exit 1
fi

echo ""
echo "=== Release package created ==="
echo "  Package : ${PACKAGE_DIR}"
echo "  Manifest: ${MANIFEST}"
echo ""
echo "To start the server:"
echo "  cd ${PACKAGE_DIR}"
echo "  ./start.sh              (bash)"
echo "  ./start.sh --port=6200  (custom port)"
echo ""
echo "For development (with driver aliases):"
echo "  ./dev.sh                (interactive bash with configured environment)"
