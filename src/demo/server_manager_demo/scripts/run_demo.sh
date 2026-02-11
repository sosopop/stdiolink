#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_ROOT="${DEMO_ROOT}/data_root"

EXE_SUFFIX=""
if [[ "${OS:-}" == "Windows_NT" ]]; then
    EXE_SUFFIX=".exe"
fi

find_bin_dir() {
    local candidate

    candidate="$(cd "${DEMO_ROOT}/.." && pwd)"
    if [[ -x "${candidate}/stdiolink_server${EXE_SUFFIX}" && -x "${candidate}/stdiolink_service${EXE_SUFFIX}" ]]; then
        printf '%s\n' "${candidate}"
        return 0
    fi

    candidate="$(cd "${SCRIPT_DIR}/../../../../build/bin" 2>/dev/null && pwd || true)"
    if [[ -n "${candidate}" && -x "${candidate}/stdiolink_server${EXE_SUFFIX}" && -x "${candidate}/stdiolink_service${EXE_SUFFIX}" ]]; then
        printf '%s\n' "${candidate}"
        return 0
    fi

    return 1
}

prepare_demo_driver() {
    local bin_dir="$1"
    local src="${bin_dir}/calculator_driver${EXE_SUFFIX}"
    local dst_dir="${DATA_ROOT}/drivers/calculator_demo"
    local dst="${dst_dir}/calculator_driver${EXE_SUFFIX}"

    if [[ ! -x "${src}" ]]; then
        echo "[run_demo] calculator_driver not found, skip driver demo preparation"
        return 0
    fi

    mkdir -p "${dst_dir}"
    cp -f "${src}" "${dst}"
    chmod +x "${dst}" || true
    echo "[run_demo] prepared demo driver: ${dst}"
}

BIN_DIR="$(find_bin_dir || true)"
if [[ -z "${BIN_DIR}" ]]; then
    echo "Error: cannot locate build bin directory with stdiolink_server/stdiolink_service" >&2
    echo "Hint: run ./build.sh first, then execute this script from build/bin/server_manager_demo/scripts/" >&2
    exit 1
fi

SERVER_BIN="${BIN_DIR}/stdiolink_server${EXE_SUFFIX}"

if [[ ! -d "${DATA_ROOT}" ]]; then
    echo "Error: demo data_root not found: ${DATA_ROOT}" >&2
    exit 1
fi

prepare_demo_driver "${BIN_DIR}"

DISPLAY_HOST="127.0.0.1"
DISPLAY_PORT="18080"
for arg in "$@"; do
    case "${arg}" in
        --host=*)
            DISPLAY_HOST="${arg#--host=}"
            ;;
        --port=*)
            DISPLAY_PORT="${arg#--port=}"
            ;;
    esac
done

echo "[run_demo] bin dir    : ${BIN_DIR}"
echo "[run_demo] data root  : ${DATA_ROOT}"
echo "[run_demo] server     : ${SERVER_BIN}"
echo "[run_demo] endpoint   : http://${DISPLAY_HOST}:${DISPLAY_PORT}"
echo
echo "Try in another terminal:"
echo "  bash \"${DEMO_ROOT}/scripts/api_smoke.sh\""
echo
echo "Or manual APIs:"
echo "  curl -sS http://${DISPLAY_HOST}:${DISPLAY_PORT}/api/services"
echo "  curl -sS http://${DISPLAY_HOST}:${DISPLAY_PORT}/api/projects"
echo

exec "${SERVER_BIN}" --data-root="${DATA_ROOT}" "$@"
