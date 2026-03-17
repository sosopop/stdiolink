#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
RELEASE_PY="${SCRIPT_DIR}/release.py"

if [ ! -f "${RELEASE_PY}" ]; then
    echo "release.py not found at ${RELEASE_PY}" >&2
    exit 1
fi

PYTHON_BIN=""
if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
fi

if [ -z "${PYTHON_BIN}" ]; then
    echo "python3/python not found in PATH" >&2
    exit 1
fi

exec "${PYTHON_BIN}" "${RELEASE_PY}" "$@"
