#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
RELEASE_SH="${SCRIPT_DIR}/tools/release.sh"
BUILD_TYPE="release"
BUILD_DIR="build"
BUILD_TYPE_SET=0

if [ ! -f "${RELEASE_SH}" ]; then
    echo "release.sh not found at ${RELEASE_SH}" >&2
    exit 1
fi

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            if [ "$#" -lt 2 ] || [ -z "${2:-}" ]; then
                echo "Error: missing value for --build-dir" >&2
                exit 1
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            exec "${RELEASE_SH}" build --help
            ;;
        --*)
            echo "Error: unknown option '$1'" >&2
            exit 1
            ;;
        *)
            if [ "${BUILD_TYPE_SET}" -eq 1 ]; then
                echo "Error: unexpected argument '$1'" >&2
                exit 1
            fi
            BUILD_TYPE="$1"
            BUILD_TYPE_SET=1
            shift
            ;;
    esac
done

exec "${RELEASE_SH}" build --config "${BUILD_TYPE}" --build-dir "${BUILD_DIR}"
