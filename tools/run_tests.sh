#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  tools/run_tests.sh [options]

Options:
  --build-dir <dir>   Build directory (default: build)
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
RUN_GTEST=0
RUN_VITEST=0
RUN_PLAYWRIGHT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="${2:-}"
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

if [[ "${BUILD_DIR}" = /* ]]; then
    BIN_DIR="${BUILD_DIR}/bin"
else
    BIN_DIR="${ROOT_DIR}/${BUILD_DIR}/bin"
fi

WEBUI_DIR="${ROOT_DIR}/src/webui"
PASSED=0
FAILED=0

# ── GTest (C++) ───────────────────────────────────────────────────────
if [[ "${RUN_GTEST}" -eq 1 ]]; then
    echo "=== GTest (C++) ==="
    GTEST_BIN="${BIN_DIR}/stdiolink_tests"
    if [[ -f "${GTEST_BIN}" ]]; then
        "${GTEST_BIN}"
        echo "  GTest passed."
        PASSED=$((PASSED + 1))
    else
        echo "Error: GTest binary not found at ${GTEST_BIN}" >&2
        echo "Build the project first or use --build-dir to specify the build directory." >&2
        exit 1
    fi
fi

# ── Vitest (WebUI unit) ──────────────────────────────────────────────
if [[ "${RUN_VITEST}" -eq 1 ]]; then
    echo "=== Vitest (WebUI unit tests) ==="
    if [[ ! -d "${WEBUI_DIR}/node_modules" ]]; then
        echo "Error: node_modules not found in ${WEBUI_DIR}" >&2
        echo "Run 'npm ci' in src/webui first." >&2
        exit 1
    fi
    pushd "${WEBUI_DIR}" > /dev/null
    npm run test
    popd > /dev/null
    echo "  Vitest passed."
    PASSED=$((PASSED + 1))
fi

# ── Playwright (E2E) ─────────────────────────────────────────────────
if [[ "${RUN_PLAYWRIGHT}" -eq 1 ]]; then
    echo "=== Playwright (E2E tests) ==="
    if [[ ! -d "${WEBUI_DIR}/node_modules" ]]; then
        echo "Error: node_modules not found in ${WEBUI_DIR}" >&2
        echo "Run 'npm ci' in src/webui first." >&2
        exit 1
    fi
    pushd "${WEBUI_DIR}" > /dev/null
    npx playwright install chromium
    npx playwright test
    popd > /dev/null
    echo "  Playwright passed."
    PASSED=$((PASSED + 1))
fi

echo ""
echo "=== All ${PASSED} test suite(s) passed ==="
