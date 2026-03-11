#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_ROOT="${SCRIPT_DIR}/data_root"
SERVER="${SCRIPT_DIR}/bin/stdiolink_server"

if [[ ! -f "${SERVER}" ]]; then
    echo "Error: server binary not found: ${SERVER}" >&2
    exit 1
fi

echo "Starting stdiolink_server..."
echo "  data_root : ${DATA_ROOT}"
echo "  args      : $*"
exec "${SERVER}" --data-root="${DATA_ROOT}" "$@"
