#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

EXE_SUFFIX=""
if [[ "${OS:-}" == "Windows_NT" ]]; then
    EXE_SUFFIX=".exe"
fi

find_runtime_dir() {
    local candidate

    # 优先：脚本位于 runtime_*/scripts/ 下，SCRIPT_DIR/.. 即 runtime 根
    candidate="$(cd "${SCRIPT_DIR}/.." 2>/dev/null && pwd || true)"
    if [[ -n "${candidate}" && -x "${candidate}/bin/stdiolink_server${EXE_SUFFIX}" ]]; then
        printf '%s\n' "${candidate}"
        return 0
    fi

    # 回退：从源码树位置查找 build/runtime_*/
    for _type in debug release; do
        candidate="$(cd "${SCRIPT_DIR}/../../../../build/runtime_${_type}" 2>/dev/null && pwd || true)"
        if [[ -n "${candidate}" && -x "${candidate}/bin/stdiolink_server${EXE_SUFFIX}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

RUNTIME_DIR="$(find_runtime_dir || true)"
if [[ -z "${RUNTIME_DIR}" ]]; then
    echo "Error: cannot locate runtime directory with bin/stdiolink_server" >&2
    echo "Hint: run ./build.sh first" >&2
    exit 1
fi

DATA_ROOT="${RUNTIME_DIR}/data_root"
SERVER_BIN="${RUNTIME_DIR}/bin/stdiolink_server${EXE_SUFFIX}"

if [[ ! -d "${DATA_ROOT}" ]]; then
    echo "Error: data_root not found: ${DATA_ROOT}" >&2
    exit 1
fi

DISPLAY_HOST="127.0.0.1"
DISPLAY_PORT="6200"
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

echo "[run_demo] runtime   : ${RUNTIME_DIR}"
echo "[run_demo] data root : ${DATA_ROOT}"
echo "[run_demo] server    : ${SERVER_BIN}"
echo "[run_demo] endpoint  : http://${DISPLAY_HOST}:${DISPLAY_PORT}"
echo
echo "Try in another terminal:"
echo "  curl -sS http://${DISPLAY_HOST}:${DISPLAY_PORT}/api/drivers"
echo "  curl -sS http://${DISPLAY_HOST}:${DISPLAY_PORT}/api/services"
echo

exec "${SERVER_BIN}" --data-root="${DATA_ROOT}" "$@"
