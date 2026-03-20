#!/usr/bin/env bash
set -euo pipefail

RELEASE_DIR="${STDIOLINK_RELEASE_DIR:-/srv/stdiolink-release}"
SERVER_BIN="${RELEASE_DIR}/bin/stdiolink_server"
DATA_ROOT="${RELEASE_DIR}/data_root"
CONFIG_JSON="${DATA_ROOT}/config.json"

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

prepend_env_path() {
    local var_name="$1"
    local value="$2"
    local current="${!var_name:-}"

    if [[ -n "${current}" ]]; then
        export "${var_name}=${value}:${current}"
    else
        export "${var_name}=${value}"
    fi
}

[[ -d "${RELEASE_DIR}" ]] || die "mounted release directory not found: ${RELEASE_DIR}"
[[ -f "${SERVER_BIN}" ]] || die "server binary not found: ${SERVER_BIN}"
[[ -x "${SERVER_BIN}" ]] || die "server binary is not executable: ${SERVER_BIN}"
[[ -d "${DATA_ROOT}" ]] || die "data_root directory not found: ${DATA_ROOT}"
[[ -f "${CONFIG_JSON}" ]] || die "config.json not found: ${CONFIG_JSON}"

prepend_env_path PATH "${RELEASE_DIR}/bin"
prepend_env_path QT_PLUGIN_PATH "${RELEASE_DIR}/bin"
prepend_env_path LD_LIBRARY_PATH "${RELEASE_DIR}/bin"
prepend_env_path DYLD_LIBRARY_PATH "${RELEASE_DIR}/bin"

printf 'Starting stdiolink_server in Docker...\n'
printf '  release_dir: %s\n' "${RELEASE_DIR}"
printf '  data_root  : %s\n' "${DATA_ROOT}"
printf '  args       : %s\n' "$*"

exec "${SERVER_BIN}" --data-root="${DATA_ROOT}" "$@"
