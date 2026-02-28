#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  tools/run_tests.sh [options]

Options:
  --build-dir <dir>   Build directory (default: build)
  --config <type>     Build config: debug or release (default: auto-detect)
  --gtest             Run only GTest (C++) tests
  --vitest            Run only Vitest (WebUI unit) tests
  --playwright        Run only Playwright (E2E) tests
  -h, --help          Show this help

If no test filter is specified, all three suites are executed.

Example:
  tools/run_tests.sh
  tools/run_tests.sh --gtest
  tools/run_tests.sh --vitest --playwright
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="build"
BUILD_CONFIG=""
RUN_GTEST=0
RUN_VITEST=0
RUN_PLAYWRIGHT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="${2:-}"
            shift 2
            ;;
        --config)
            BUILD_CONFIG="${2:-}"
            shift 2
            ;;
        --gtest)
            RUN_GTEST=1
            shift
            ;;
        --vitest)
            RUN_VITEST=1
            shift
            ;;
        --playwright)
            RUN_PLAYWRIGHT=1
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

# If no filter specified, run all
if [[ "${RUN_GTEST}" -eq 0 && "${RUN_VITEST}" -eq 0 && "${RUN_PLAYWRIGHT}" -eq 0 ]]; then
    RUN_GTEST=1
    RUN_VITEST=1
    RUN_PLAYWRIGHT=1
fi

# Auto-detect build config if not specified
if [[ -z "${BUILD_CONFIG}" ]]; then
    if [[ "${BUILD_DIR}" = /* ]]; then
        _base="${BUILD_DIR}"
    else
        _base="${ROOT_DIR}/${BUILD_DIR}"
    fi
    if [[ -d "${_base}/runtime_debug" ]]; then
        BUILD_CONFIG="debug"
    elif [[ -d "${_base}/runtime_release" ]]; then
        BUILD_CONFIG="release"
    else
        BUILD_CONFIG="debug"
    fi
fi

if [[ "${BUILD_DIR}" = /* ]]; then
    BIN_DIR="${BUILD_DIR}/runtime_${BUILD_CONFIG}/bin"
else
    BIN_DIR="${ROOT_DIR}/${BUILD_DIR}/runtime_${BUILD_CONFIG}/bin"
fi

WEBUI_DIR="${ROOT_DIR}/src/webui"
PASSED=0
FAILED=0
FAILED_NAMES=""

# ── GTest (C++) ───────────────────────────────────────────────────────
if [[ "${RUN_GTEST}" -eq 1 ]]; then
    echo "=== GTest (C++) ==="
    GTEST_BIN="${BIN_DIR}/stdiolink_tests"
    if [[ -f "${GTEST_BIN}" ]]; then
        if "${GTEST_BIN}"; then
            echo "  GTest passed."
            PASSED=$((PASSED + 1))
        else
            echo "FAIL: GTest failed (exit code $?)"
            FAILED=$((FAILED + 1))
            FAILED_NAMES="${FAILED_NAMES}  - GTest\n"
        fi
    else
        echo "SKIP: GTest binary not found at ${GTEST_BIN}"
        FAILED=$((FAILED + 1))
        FAILED_NAMES="${FAILED_NAMES}  - GTest (binary not found)\n"
    fi
fi

# ── Vitest (WebUI unit) ──────────────────────────────────────────────
if [[ "${RUN_VITEST}" -eq 1 ]]; then
    echo "=== Vitest (WebUI unit tests) ==="
    if [[ ! -d "${WEBUI_DIR}/node_modules" ]]; then
        echo "SKIP: node_modules not found in ${WEBUI_DIR}"
        FAILED=$((FAILED + 1))
        FAILED_NAMES="${FAILED_NAMES}  - Vitest (node_modules not found)\n"
    else
        pushd "${WEBUI_DIR}" > /dev/null
        if npm run test; then
            echo "  Vitest passed."
            PASSED=$((PASSED + 1))
        else
            echo "FAIL: Vitest failed (exit code $?)"
            FAILED=$((FAILED + 1))
            FAILED_NAMES="${FAILED_NAMES}  - Vitest\n"
        fi
        popd > /dev/null
    fi
fi

# ── Playwright (E2E) ─────────────────────────────────────────────────
if [[ "${RUN_PLAYWRIGHT}" -eq 1 ]]; then
    echo "=== Playwright (E2E tests) ==="
    if [[ ! -d "${WEBUI_DIR}/node_modules" ]]; then
        echo "SKIP: node_modules not found in ${WEBUI_DIR}"
        FAILED=$((FAILED + 1))
        FAILED_NAMES="${FAILED_NAMES}  - Playwright (node_modules not found)\n"
    else
        pushd "${WEBUI_DIR}" > /dev/null
        if npx playwright install chromium && npx playwright test; then
            echo "  Playwright passed."
            PASSED=$((PASSED + 1))
        else
            echo "FAIL: Playwright failed (exit code $?)"
            FAILED=$((FAILED + 1))
            FAILED_NAMES="${FAILED_NAMES}  - Playwright\n"
        fi
        popd > /dev/null
    fi
fi

echo ""
if [[ "${FAILED}" -gt 0 ]]; then
    echo "=== ${PASSED} passed, ${FAILED} failed ==="
    echo "Failed suites:"
    printf "${FAILED_NAMES}"
    exit 1
else
    echo "=== All ${PASSED} test suite(s) passed ==="
fi
