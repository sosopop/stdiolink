#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
START_SCRIPT="${SCRIPT_DIR}/start.sh"
DEFAULT_USER="$(stat -c '%U' "${SCRIPT_DIR}")"
DEFAULT_GROUP="$(stat -c '%G' "${SCRIPT_DIR}")"

SERVICE_NAME="stdiolink_server"
SERVICE_USER="${DEFAULT_USER}"
SERVICE_GROUP="${DEFAULT_GROUP}"
UNIT_DIR="/etc/systemd/system"
ENABLE_ON_BOOT=1
START_AFTER_INSTALL=1
SERVER_ARGS=()

usage() {
    cat <<'EOF'
Usage: sudo ./install_service.sh [install-options] [stdiolink_server args...]

Install options:
  --service-name=<name>   systemd service name (default: stdiolink_server)
  --user=<user>           service user (default: package directory owner)
  --group=<group>         service group (default: package directory owner group)
  --unit-dir=<path>       unit file directory (default: /etc/systemd/system)
  --no-enable             install but do not enable on boot
  --no-start              install but do not start immediately
  -h, --help              show this help

Examples:
  sudo ./install_service.sh
  sudo ./install_service.sh --service-name=my_stdiolink --port=6201 --host=0.0.0.0
EOF
}

fail() {
    echo "Error: $*" >&2
    exit 1
}

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        fail "please run as root, for example: sudo ./install_service.sh"
    fi
}

require_systemd() {
    if ! command -v systemctl >/dev/null 2>&1; then
        fail "systemctl not found; this script only supports systemd-based Linux systems"
    fi
}

resolve_group() {
    local user_name="$1"
    id -gn "${user_name}"
}

ensure_no_whitespace() {
    local value="$1"
    local label="$2"
    if [[ "${value}" =~ [[:space:]] ]]; then
        fail "${label} contains whitespace, which is not supported by this installer: ${value}"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --service-name=*)
            SERVICE_NAME="${1#*=}"
            ;;
        --user=*)
            SERVICE_USER="${1#*=}"
            ;;
        --group=*)
            SERVICE_GROUP="${1#*=}"
            ;;
        --unit-dir=*)
            UNIT_DIR="${1#*=}"
            ;;
        --no-enable)
            ENABLE_ON_BOOT=0
            ;;
        --no-start)
            START_AFTER_INSTALL=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            SERVER_ARGS+=("$1")
            ;;
    esac
    shift
done

[[ -n "${SERVICE_NAME}" ]] || fail "service name cannot be empty"
[[ -f "${START_SCRIPT}" ]] || fail "start script not found: ${START_SCRIPT}"

require_root
require_systemd

if ! id -u "${SERVICE_USER}" >/dev/null 2>&1; then
    fail "service user does not exist: ${SERVICE_USER}"
fi

if [[ -z "${SERVICE_GROUP}" ]]; then
    SERVICE_GROUP="$(resolve_group "${SERVICE_USER}")"
fi

if ! getent group "${SERVICE_GROUP}" >/dev/null 2>&1; then
    fail "service group does not exist: ${SERVICE_GROUP}"
fi

mkdir -p "${UNIT_DIR}"

UNIT_PATH="${UNIT_DIR}/${SERVICE_NAME}.service"
ensure_no_whitespace "${SCRIPT_DIR}" "release directory"
ensure_no_whitespace "${START_SCRIPT}" "start script path"

EXEC_START="$(command -v bash) ${START_SCRIPT}"
for arg in "${SERVER_ARGS[@]}"; do
    ensure_no_whitespace "${arg}" "server argument"
    EXEC_START+=" ${arg}"
done

TMP_UNIT="$(mktemp)"
trap 'rm -f "${TMP_UNIT}"' EXIT
cat > "${TMP_UNIT}" <<EOF
[Unit]
Description=stdiolink_server service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_GROUP}
WorkingDirectory=${SCRIPT_DIR}
ExecStart=${EXEC_START}
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

install -m 0644 "${TMP_UNIT}" "${UNIT_PATH}"
rm -f "${TMP_UNIT}"
trap - EXIT

systemctl daemon-reload

if [[ "${ENABLE_ON_BOOT}" -eq 1 ]]; then
    systemctl enable "${SERVICE_NAME}.service"
fi

if [[ "${START_AFTER_INSTALL}" -eq 1 ]]; then
    if systemctl is-active --quiet "${SERVICE_NAME}.service"; then
        systemctl restart "${SERVICE_NAME}.service"
    else
        systemctl start "${SERVICE_NAME}.service"
    fi
fi

echo "Installed ${SERVICE_NAME}.service"
echo "  unit path : ${UNIT_PATH}"
echo "  user/group: ${SERVICE_USER}/${SERVICE_GROUP}"
echo "  work dir  : ${SCRIPT_DIR}"
echo "  exec      : ${EXEC_START}"
if [[ "${ENABLE_ON_BOOT}" -eq 1 ]]; then
    echo "  enabled   : yes"
else
    echo "  enabled   : no"
fi
if [[ "${START_AFTER_INSTALL}" -eq 1 ]]; then
    echo "  status    : $(systemctl is-active "${SERVICE_NAME}.service")"
else
    echo "  status    : installed, not started"
fi
