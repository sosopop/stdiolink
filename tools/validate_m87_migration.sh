#!/bin/bash
# validate_m87_migration.sh — M87 静态校验脚本
set -e

DEMO_ROOT="src/demo/server_manager_demo/data_root"
DATA_ROOT="src/data_root"
RUNTIME_UTILS="src/demo/js_runtime_demo/shared/lib/runtime_utils.js"

# T01 — shared 引用残留检查
echo "T01: checking shared reference residuals..."
if grep -r "../../shared/" src/demo/server_manager_demo/ src/data_root/ 2>/dev/null; then
    echo "FAIL: found ../../shared/ references"; exit 1
fi
echo "OK: no shared references"

# T02 — shared 目录已删除
echo "T02: checking shared directory deleted..."
if [ -d "$DEMO_ROOT/shared" ]; then
    echo "FAIL: shared/ directory still exists"; exit 1
fi
echo "OK: shared/ deleted"

# T03 — src/data_root/ 目录完整性
echo "T03: checking data_root structure..."
SERVICES=("modbustcp_server_service" "modbusrtu_server_service" "modbusrtu_serial_server_service")
PROJECTS=("manual_modbustcp_server" "manual_modbusrtu_server" "manual_modbusrtu_serial_server")
for svc in "${SERVICES[@]}"; do
    for f in manifest.json config.schema.json index.js; do
        [ -f "$DATA_ROOT/services/$svc/$f" ] || { echo "FAIL: $svc/$f missing"; exit 1; }
    done
done
for prj in "${PROJECTS[@]}"; do
    [ -f "$DATA_ROOT/projects/$prj.json" ] || { echo "FAIL: $prj.json missing"; exit 1; }
done
echo "OK: data_root structure complete"

# T04 — 迁移后 service 含 resolveDriver import
echo "T04: checking resolveDriver imports..."
MIGRATED_FILES=(
    "$DEMO_ROOT/services/driver_pipeline_service/index.js"
    "$DEMO_ROOT/services/process_exec_service/index.js"
    "$DATA_ROOT/services/modbustcp_server_service/index.js"
    "$DATA_ROOT/services/modbusrtu_server_service/index.js"
    "$DATA_ROOT/services/modbusrtu_serial_server_service/index.js"
)
for f in "${MIGRATED_FILES[@]}"; do
    grep -q "stdiolink/driver" "$f" || { echo "FAIL: $f missing resolveDriver import"; exit 1; }
done
echo "OK: all migrated files have resolveDriver import"

# T05 — 无 findDriverPath 残留
echo "T05: checking no findDriverPath residuals..."
for f in "${MIGRATED_FILES[@]}"; do
    if grep -q "findDriverPath" "$f"; then
        echo "FAIL: $f still contains findDriverPath"; exit 1
    fi
done
echo "OK: no findDriverPath residuals"

# T06 — process_exec_service 无内联 findDriver
echo "T06: checking no inline findDriver..."
if grep -q "function findDriver" "$DEMO_ROOT/services/process_exec_service/index.js"; then
    echo "FAIL: inline findDriver still exists"; exit 1
fi
echo "OK: no inline findDriver"

# T07 — runtime_utils.js 无 driverPathCandidates
echo "T07: checking no driverPathCandidates..."
if grep -q "driverPathCandidates" "$RUNTIME_UTILS"; then
    echo "FAIL: driverPathCandidates still exists"; exit 1
fi
echo "OK: no driverPathCandidates"

# T08 — runtime_utils.js 保留 firstSuccess
echo "T08: checking firstSuccess preserved..."
grep -q "export function firstSuccess" "$RUNTIME_UTILS" || { echo "FAIL: firstSuccess missing"; exit 1; }
echo "OK: firstSuccess preserved"

# T09 — demo service 不受影响
echo "T09: checking demo services unaffected..."
[ -f "$DEMO_ROOT/services/quick_start_service/index.js" ] || { echo "FAIL: quick_start missing"; exit 1; }
if grep -q "../../shared/" "$DEMO_ROOT/services/quick_start_service/index.js" 2>/dev/null; then
    echo "FAIL: quick_start has shared reference"; exit 1
fi
echo "OK: demo services unaffected"

echo "All M87 static validations passed."
