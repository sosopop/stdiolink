#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="stdiolink_server"
UNIT_DIR="/etc/systemd/system"

usage() {
    cat <<'EOF'
Usage: sudo ./uninstall_service.sh [options]

Options:
  --service-name=<name>   systemd service name (default: stdiolink_server)
  --unit-dir=<path>       unit file directory (default: /etc/systemd/system)
  -h, --help              show this help
EOF
}

fail() {
    echo "Error: $*" >&2
    exit 1
}

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        fail "please run as root, for example: sudo ./uninstall_service.sh"
    fi
}

require_systemd() {
    if ! command -v systemctl >/dev/null 2>&1; then
        fail "systemctl not found; this script only supports systemd-based Linux systems"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --service-name=*)
            SERVICE_NAME="${1#*=}"
            ;;
        --unit-dir=*)
            UNIT_DIR="${1#*=}"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
    shift
done

[[ -n "${SERVICE_NAME}" ]] || fail "service name cannot be empty"

require_root
require_systemd

UNIT_PATH="${UNIT_DIR}/${SERVICE_NAME}.service"

systemctl disable --now "${SERVICE_NAME}.service" >/dev/null 2>&1 || true

if [[ -f "${UNIT_PATH}" ]]; then
    rm -f "${UNIT_PATH}"
fi

systemctl daemon-reload
systemctl reset-failed "${SERVICE_NAME}.service" >/dev/null 2>&1 || true

echo "Uninstalled ${SERVICE_NAME}.service"
echo "  unit path : ${UNIT_PATH}"
