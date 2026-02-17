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
  --skip-build         Skip C++ build (assume build/bin already exists)
  --skip-webui         Skip WebUI build
  -h, --help           Show this help

Example:
  tools/publish_release.sh --build-dir build --output-dir release
  tools/publish_release.sh --name my_release
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="build"
OUTPUT_DIR="release"
PACKAGE_NAME=""
WITH_TESTS=0
SKIP_BUILD=0
SKIP_WEBUI=0

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
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --skip-webui)
            SKIP_WEBUI=1
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

# ── C++ build ────────────────────────────────────────────────────────
if [[ "${SKIP_BUILD}" -eq 0 ]]; then
    echo "Building C++ project (Release)..."
    BUILD_SCRIPT="${ROOT_DIR}/build.sh"
    if [[ ! -f "${BUILD_SCRIPT}" ]]; then
        echo "Error: build.sh not found at ${BUILD_SCRIPT}" >&2
        exit 1
    fi
    pushd "${ROOT_DIR}" > /dev/null
    bash "${BUILD_SCRIPT}" Release
    popd > /dev/null
fi

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
echo "  skip webui  : ${SKIP_WEBUI}"

rm -rf "${PACKAGE_DIR}"

mkdir -p "${PACKAGE_DIR}/bin"
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

# ── WebUI build ──────────────────────────────────────────────────────
if [[ "${SKIP_WEBUI}" -eq 0 ]] && [[ -f "${ROOT_DIR}/src/webui/package.json" ]]; then
    echo "Building WebUI..."
    pushd "${ROOT_DIR}/src/webui" > /dev/null
    if ! command -v npm &> /dev/null; then
        echo "Error: npm not found. Install Node.js or use --skip-webui to skip WebUI build." >&2
        popd > /dev/null
        exit 1
    fi

    if ! npm ci --ignore-scripts; then
        echo "npm ci failed, retrying with npm install..."
        npm install --ignore-scripts
    fi
    npm run build
    if [[ ! -d "dist" ]]; then
        echo "Error: WebUI build succeeded but dist/ is missing" >&2
        popd > /dev/null
        exit 1
    fi

    mkdir -p "${PACKAGE_DIR}/data_root/webui"
    cp -r dist/* "${PACKAGE_DIR}/data_root/webui/"
    echo "WebUI build copied to ${PACKAGE_DIR}/data_root/webui/"

    popd > /dev/null
elif [[ "${SKIP_WEBUI}" -eq 0 ]]; then
    echo "WebUI source not found, skipping WebUI build."
fi

# ── Binaries ─────────────────────────────────────────────────────────
echo "Copying binaries..."
while IFS= read -r -d '' file; do
    base="$(basename "${file}")"
    if should_skip_binary "${base}"; then
        continue
    fi
    cp -f "${file}" "${PACKAGE_DIR}/bin/${base}"
done < <(find "${BIN_DIR}" -maxdepth 1 -type f -print0)

# Copy Qt plugin subdirectories (tls, platforms, networkinformation, etc.)
echo "Copying Qt plugin directories..."
SKIP_DIRS="config_demo|js_runtime_demo|server_manager_demo"
while IFS= read -r -d '' dir; do
    dirname="$(basename "${dir}")"
    if [[ "${dirname}" =~ ^(${SKIP_DIRS})$ ]]; then
        continue
    fi
    dest="${PACKAGE_DIR}/bin/${dirname}"
    mkdir -p "${dest}"
    find "${dir}" -maxdepth 1 -type f \( -name "*.so" -o -name "*.so.*" -o -name "*.dylib" -o -name "*.dll" \) -print0 |
        while IFS= read -r -d '' lib; do
            cp -f "${lib}" "${dest}/"
        done
    echo "  + ${dirname}/"
done < <(find "${BIN_DIR}" -mindepth 1 -maxdepth 1 -type d -print0)

echo "Copying docs..."
copy_file_if_exists "${ROOT_DIR}/doc/stdiolink_server.md" "${PACKAGE_DIR}/doc/"
copy_file_if_exists "${ROOT_DIR}/doc/http_api.md" "${PACKAGE_DIR}/doc/"
copy_file_if_exists "${ROOT_DIR}/doc/stdiolink-server-api-requirements.md" "${PACKAGE_DIR}/doc/"
copy_file_if_exists "${ROOT_DIR}/doc/milestone/milestone_39_server_manager_demo_and_release.md" "${PACKAGE_DIR}/doc/"

# ── Seed demo data ───────────────────────────────────────────────────
echo "Seeding demo data into data_root..."
DEMO_DATA_ROOT="${ROOT_DIR}/src/demo/server_manager_demo/data_root"
if [[ -d "${DEMO_DATA_ROOT}" ]]; then
    if [[ -d "${DEMO_DATA_ROOT}/services" ]]; then
        cp -R "${DEMO_DATA_ROOT}/services/"* "${PACKAGE_DIR}/data_root/services/" 2>/dev/null || true
    fi
    if [[ -d "${DEMO_DATA_ROOT}/projects" ]]; then
        cp -R "${DEMO_DATA_ROOT}/projects/"* "${PACKAGE_DIR}/data_root/projects/" 2>/dev/null || true
    fi
    echo "  Demo services and projects seeded."
else
    echo "  WARNING: Demo data_root not found at ${DEMO_DATA_ROOT}"
fi

# Copy driver binaries into data_root/drivers/<name>/<name>
# Scanner expects each driver in its own subdirectory.
echo "Copying drivers into data_root/drivers..."
DRIVERS_DEST="${PACKAGE_DIR}/data_root/drivers"
DRIVERS_COPIED=0
while IFS= read -r -d '' file; do
    base="$(basename "${file}")"
    stem="${base%.*}"
    if [[ -z "${stem##stdio.drv.*}" ]]; then
        driver_sub="${DRIVERS_DEST}/${stem}"
        mkdir -p "${driver_sub}"
        cp -f "${file}" "${driver_sub}/${base}"
        # Remove from bin/ to avoid duplication
        rm -f "${PACKAGE_DIR}/bin/${base}"
        echo "  + ${stem}/${base}"
        DRIVERS_COPIED=$((DRIVERS_COPIED + 1))
    fi
done < <(find "${BIN_DIR}" -maxdepth 1 -type f -print0)
echo "  ${DRIVERS_COPIED} driver(s) copied."

# ── Default config.json ──────────────────────────────────────────────
CONFIG_PATH="${PACKAGE_DIR}/data_root/config.json"
if [[ ! -f "${CONFIG_PATH}" ]]; then
    echo "Generating default config.json..."
    cat > "${CONFIG_PATH}" <<'CONFIGEOF'
{
    "host": "127.0.0.1",
    "port": 18080,
    "logLevel": "info"
}
CONFIGEOF
fi

# ── Startup launcher ─────────────────────────────────────────────────
echo "Generating startup scripts..."

cat > "${PACKAGE_DIR}/start.sh" <<'STARTEOF'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"${SCRIPT_DIR}/bin/stdiolink_server" --data-root="${SCRIPT_DIR}/data_root" "$@"
STARTEOF
chmod +x "${PACKAGE_DIR}/start.sh"

MANIFEST="${PACKAGE_DIR}/RELEASE_MANIFEST.txt"
{
    echo "package_name=${PACKAGE_NAME}"
    echo "created_at=$(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "git_commit=$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "build_dir=${BUILD_DIR_ABS}"
    echo "with_tests=${WITH_TESTS}"
    echo "skip_webui=${SKIP_WEBUI}"
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

echo ""
echo "=== Release package created ==="
echo "  Package : ${PACKAGE_DIR}"
echo "  Manifest: ${MANIFEST}"
echo ""
echo "To start the server:"
echo "  cd ${PACKAGE_DIR}"
echo "  ./start.sh              (bash)"
echo "  ./start.sh --port=8080  (custom port)"
