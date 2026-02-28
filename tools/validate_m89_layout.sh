#!/bin/bash
# tools/validate_m89_layout.sh — M89 静态校验脚本
set -e

BUILD_DIR="${1:-build}"
BUILD_TYPE="${2:-debug}"
RAW_DIR="${BUILD_DIR}/${BUILD_TYPE}"
RUNTIME_DIR="${BUILD_DIR}/runtime_${BUILD_TYPE}"

EXE_SUFFIX=""
if [[ "${OS:-}" == "Windows_NT" ]]; then
    EXE_SUFFIX=".exe"
fi

fail() { echo "FAIL: $1"; exit 1; }

# T01 — CMake 原始输出目录存在
echo "T01: checking raw output directory..."
[ -d "${RAW_DIR}" ] || fail "raw dir ${RAW_DIR} not found"
[ -f "${RAW_DIR}/stdiolink_server${EXE_SUFFIX}" ] || fail "stdiolink_server not in raw dir"
echo "OK: raw output directory exists"

# T02 — 旧 build/bin/ 不存在
echo "T02: checking build/bin/ removed..."
if [ -d "${BUILD_DIR}/bin" ]; then
    fail "build/bin/ still exists"
fi
echo "OK: build/bin/ does not exist"

# T03 — runtime 目录骨架完整
echo "T03: checking runtime directory skeleton..."
for dir in bin data_root data_root/drivers data_root/services \
           data_root/projects data_root/workspaces data_root/logs \
           demos scripts; do
    [ -d "${RUNTIME_DIR}/${dir}" ] || fail "runtime_${BUILD_TYPE}/${dir} missing"
done
echo "OK: runtime skeleton complete"

# T04 — 核心二进制已复制
echo "T04: checking core binaries in runtime/bin/..."
for bin in stdiolink_server stdiolink_service stdiolink_tests; do
    [ -f "${RUNTIME_DIR}/bin/${bin}${EXE_SUFFIX}" ] || fail "${bin} not in runtime/bin/"
done
echo "OK: core binaries present"

# T05 — runtime bin/ 下无驱动
echo "T05: checking no drivers in runtime/bin/..."
if ls "${RUNTIME_DIR}/bin/stdio.drv."* 2>/dev/null | grep -q .; then
    fail "found stdio.drv.* in runtime/bin/"
fi
echo "OK: no drivers in runtime/bin/"

# T06 — 驱动按子目录组织
echo "T06: checking driver subdirectories..."
DRIVER_COUNT=0
for drv_file in "${RAW_DIR}"/stdio.drv.*; do
    [ -f "${drv_file}" ] || continue
    drv_base="$(basename "${drv_file}")"
    drv_name="${drv_base%${EXE_SUFFIX}}"
    drv_name="${drv_name%.pdb}"
    drv_dir="${RUNTIME_DIR}/data_root/drivers/${drv_name}"
    [ -d "${drv_dir}" ] || fail "driver dir ${drv_name}/ missing"
    [ -f "${drv_dir}/${drv_base}" ] || fail "driver binary ${drv_name}/${drv_base} missing"
    DRIVER_COUNT=$((DRIVER_COUNT + 1))
done
[ "${DRIVER_COUNT}" -gt 0 ] || fail "no drivers found"
echo "OK: ${DRIVER_COUNT} driver(s) in subdirectories"

# T07 — services 两层合并
echo "T07: checking services merge..."
[ -d "${RUNTIME_DIR}/data_root/services/modbustcp_server_service" ] \
    || fail "production service missing"
[ -d "${RUNTIME_DIR}/data_root/services/driver_pipeline_service" ] \
    || fail "demo service missing"
echo "OK: services merged"

# T08 — projects 两层合并
echo "T08: checking projects merge..."
[ -f "${RUNTIME_DIR}/data_root/projects/manual_modbustcp_server.json" ] \
    || fail "production project missing"
echo "OK: projects merged"

# T09 — demo 资产
echo "T09: checking demo assets..."
[ -d "${RUNTIME_DIR}/demos/js_runtime_demo" ] || fail "js_runtime_demo missing"
[ -d "${RUNTIME_DIR}/demos/config_demo" ] || fail "config_demo missing"
echo "OK: demo assets present"

# T10 — demo 脚本
echo "T10: checking demo scripts..."
[ -f "${RUNTIME_DIR}/scripts/run_demo.sh" ] || fail "run_demo.sh missing"
[ -f "${RUNTIME_DIR}/scripts/api_smoke.sh" ] || fail "api_smoke.sh missing"
echo "OK: demo scripts present"

# T11 — Qt 插件
echo "T11: checking Qt plugins..."
if [ -d "${RUNTIME_DIR}/bin/platforms" ]; then
    echo "OK: Qt plugins present"
else
    echo "SKIP: platforms/ not found (Qt plugins not deployed)"
fi

# T12 — 同构验证
echo "T12: checking isomorphic layout..."
RELEASE_DIR="${3:-}"
if [[ -z "${RELEASE_DIR}" ]]; then
    [ -d "${RUNTIME_DIR}/bin" ] || fail "runtime bin/ missing"
    [ -d "${RUNTIME_DIR}/data_root" ] || fail "runtime data_root/ missing"
    if ls "${RUNTIME_DIR}/bin/stdio.drv."* 2>/dev/null | grep -q .; then
        fail "found stdio.drv.* in runtime/bin/ (isomorphic violation)"
    fi
    DRIVER_SUBDIRS=$(ls -d "${RUNTIME_DIR}/data_root/drivers/"*/ 2>/dev/null | wc -l)
    [ "${DRIVER_SUBDIRS}" -gt 0 ] || fail "data_root/drivers/ has no subdirectories"
    echo "OK: runtime self-consistent (${DRIVER_SUBDIRS} driver subdirs)"
else
    # Filter out binaries that publish_release excludes by default (WITH_TESTS=0)
    skip_bin() {
        local s="${1%.*}"
        case "${s}" in stdiolink_tests|test_*|gtest|demo_host|driverlab) return 0;; esac
        case "$1" in *.log|*.tmp|*.json) return 0;; esac
        return 1
    }
    RUNTIME_BINS=$(ls "${RUNTIME_DIR}/bin/" 2>/dev/null | while read -r f; do skip_bin "$f" || echo "$f"; done | sort)
    RELEASE_BINS=$(ls "${RELEASE_DIR}/bin/" 2>/dev/null | sort)
    if [[ "${RUNTIME_BINS}" != "${RELEASE_BINS}" ]]; then
        fail "bin/ file sets differ between runtime and release"
    fi
    RUNTIME_DRVS=$(ls "${RUNTIME_DIR}/data_root/drivers/" 2>/dev/null | sort)
    RELEASE_DRVS=$(ls "${RELEASE_DIR}/data_root/drivers/" 2>/dev/null | sort)
    if [[ "${RUNTIME_DRVS}" != "${RELEASE_DRVS}" ]]; then
        fail "data_root/drivers/ subdirectory sets differ"
    fi
    if ls "${RELEASE_DIR}/bin/stdio.drv."* 2>/dev/null | grep -q .; then
        fail "found stdio.drv.* in release bin/"
    fi
    echo "OK: isomorphic layout verified (runtime == release)"
fi

echo ""
echo "All M89 layout validations passed."
