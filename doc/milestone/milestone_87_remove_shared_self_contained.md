# 里程碑 87：移除 shared 依赖与 Service 自包含迁移

> **前置条件**: M86（resolveDriver C++ 绑定与 data-root 参数传递）
> **目标**: 彻底移除 `shared/lib/driver_utils.js` 依赖，所有 JS service 改为直接调用 C++ 绑定 `resolveDriver()`，实现 service 自包含；同时将 production 内容从 `src/demo/` 分离到 `src/data_root/`

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| JS Services（server_manager_demo） | process_exec_service、driver_pipeline_service 迁移到 resolveDriver() |
| JS Services（data_root） | 三个 Modbus service 迁移到 resolveDriver() 并从 demo 迁出 |
| JS Runtime Demo | runtime_utils.js 迁移到 resolveDriver() |
| 源码目录 | production 内容从 src/demo/ 分离到 src/data_root/ |

- 删除 `src/demo/server_manager_demo/data_root/shared/` 目录
- `process_exec_service/index.js` 删除内联 `findDriver()`，改用 `resolveDriver()`
- `driver_pipeline_service/index.js` 删除 `../../shared/lib/driver_utils.js` 引用，改用 `resolveDriver()`
- 三个 Modbus service 删除 shared 引用，改用 `resolveDriver()`，并迁移到 `src/data_root/services/`
- `runtime_utils.js` 删除 `driverPathCandidates()`，改用 `resolveDriver()`
- 全局搜索确认无残留 `../../shared/` 引用

## 2. 背景与问题

- `shared/lib/driver_utils.js` 作为共享库被多个 service 通过相对路径 `../../shared/...` 引用，导致 service 无法独立部署
- 发布脚本 `publish_release.ps1` 从未正确复制 `shared/` 目录，发布后所有依赖 shared 的 service 加载失败
- M86 已提供 C++ 层 `resolveDriver()` 绑定，JS 层的路径猜测逻辑可以完全替换
- production 内容（Modbus service）与 demo 内容混放在 `src/demo/` 下，语义不清晰

**范围**:
- 一次性覆盖 `process_exec_service`、`driver_pipeline_service`、三个 Modbus service 的迁移
- `runtime_utils.js` 迁移（js_runtime_demo）
- `process_types/index.js` 迁移（js_runtime_demo）
- 删除 `shared/` 目录
- 将 Modbus service 和 project 从 `src/demo/` 迁移到 `src/data_root/`

**非目标**:
- 不修改 C++ 代码（M86 已完成）
- 不修改发布脚本或 CMakeLists（M88 负责）
- 不修改目录布局约定（M88 负责）
- 不新增 JS 绑定 API

## 3. 技术要点

### 3.1 迁移模式：shared import → resolveDriver()

所有 service 的驱动路径查找统一替换为 C++ 绑定调用：

Before（当前）:
```javascript
import { findDriverPath } from "../../shared/lib/driver_utils.js";
const driverPath = findDriverPath("stdio.drv.calculator");
if (!driverPath) throw new Error("driver not found");
```

After（迁移后）:
```javascript
import { resolveDriver } from "stdiolink/driver";
const driverPath = resolveDriver("stdio.drv.calculator");
// resolveDriver 失败时自动抛出含诊断信息的 Error，无需手动判空
```

关键差异：
- `resolveDriver()` 失败时直接抛出 Error（含已尝试位置），无需调用方判空
- 不再依赖相对路径 `../../shared/...`，service 可独立部署
- 路径解析由 C++ 层完成，利用 QDir 进行可靠的文件系统扫描

### 3.2 process_exec_service 内联 findDriver 替换

当前 `process_exec_service/index.js` 第 22-33 行有内联的 `findDriver()` 函数，使用 5 个硬编码候选路径：

```javascript
// 当前代码（将被删除）
function findDriver(baseName) {
    const ext = SYSTEM.isWindows ? ".exe" : "";
    const name = baseName + ext;
    const candidates = [
        `./${name}`, `./bin/${name}`, `../bin/${name}`,
        `../../bin/${name}`, `./build/bin/${name}`,];
    for (const p of candidates) {
        if (exists(p)) return p;
    }
    return null;
}
```

替换为：
```javascript
import { resolveDriver } from "stdiolink/driver";
// 直接调用，无需本地 helper
const driverPath = resolveDriver(spawnDriver);
```

### 3.3 源码目录分离

将 production 内容从 `src/demo/server_manager_demo/data_root/` 迁移到 `src/data_root/`：

| 内容 | 原位置 | 新位置 |
|------|--------|--------|
| modbustcp_server_service | src/demo/.../services/ | src/data_root/services/ |
| modbusrtu_server_service | src/demo/.../services/ | src/data_root/services/ |
| modbusrtu_serial_server_service | src/demo/.../services/ | src/data_root/services/ |
| manual_modbustcp_server.json | src/demo/.../projects/ | src/data_root/projects/ |
| manual_modbusrtu_server.json | src/demo/.../projects/ | src/data_root/projects/ |
| manual_modbusrtu_serial_server.json | src/demo/.../projects/ | src/data_root/projects/ |
| shared/ | src/demo/.../shared/ | 删除（不迁移） |

### 3.4 向后兼容

- 迁移后 demo service（quick_start、system_info 等）不受影响，它们不依赖 shared
- `runtime_utils.js` 保留 `firstSuccess()` 导出（`process_types/index.js` 仍在使用）
- M88 完成前，构建脚本暂时只复制 demo data_root；M88 会增加两层复制合并

## 4. 实现步骤

### 4.1 driver_pipeline_service 迁移

- 修改 `src/demo/server_manager_demo/data_root/services/driver_pipeline_service/index.js`：
  - 删除第 14 行 `import { findDriverPath } from "../../shared/lib/driver_utils.js";`
  - 新增 `import { resolveDriver } from "stdiolink/driver";`
  - `openCalc()` 函数（L22-28）中：
    - 删除 `const driverPath = findDriverPath("stdio.drv.calculator");`
    - 删除 `if (!driverPath) throw new Error("stdio.drv.calculator not found");`
    - 替换为 `const driverPath = resolveDriver("stdio.drv.calculator");`
  - 改动后 `openCalc()`：
    ```javascript
    async function openCalc() {
        const driverPath = resolveDriver("stdio.drv.calculator");
        return await openDriver(driverPath);
    }
    ```
  - 理由：`resolveDriver()` 失败时自动抛出含诊断信息的 Error，无需手动判空
  - 验收：集成测试（启动 driver_pipeline Project 验证驱动正常加载）

### 4.2 process_exec_service 迁移

- 修改 `src/demo/server_manager_demo/data_root/services/process_exec_service/index.js`：
  - 新增 `import { resolveDriver } from "stdiolink/driver";`（在现有 import 区末尾）
  - 删除第 22-33 行内联 `findDriver()` 函数（含 5 个硬编码候选路径）
  - 第 74 行 `const driverPath = findDriver(spawnDriver);` 替换为 `const driverPath = resolveDriver(spawnDriver);`
  - 删除第 128-132 行 `else` 分支（`findDriver` 返回 null 时的 skip 逻辑），因为 `resolveDriver()` 失败时直接抛出 Error
  - 改动后驱动查找区域：
    ```javascript
    import { resolveDriver } from "stdiolink/driver";
    // ...
    const driverPath = resolveDriver(spawnDriver);
    // 后续 execAsync / spawn 逻辑不变
    ```
  - 理由：删除 12 行内联 helper + 5 行 else 分支，净减约 17 行代码
  - 验收：集成测试（启动 process_exec Project 验证驱动正常加载）

### 4.3 三个 Modbus service 迁移

三个 Modbus service 的改动完全一致，仅驱动名不同：

- 迁移 `modbustcp_server_service/index.js`：
  - 删除 `import { findDriverPath } from "../../shared/lib/driver_utils.js";`（L11）
  - 新增 `import { resolveDriver } from "stdiolink/driver";`
  - L30-31 替换：
    ```javascript
    // Before
    const driverPath = findDriverPath("stdio.drv.modbustcp_server");
    if (!driverPath) throw new Error("stdio.drv.modbustcp_server not found");
    // After
    const driverPath = resolveDriver("stdio.drv.modbustcp_server");
    ```

- 迁移 `modbusrtu_server_service/index.js`：同上，驱动名改为 `stdio.drv.modbusrtu_server`
- 迁移 `modbusrtu_serial_server_service/index.js`：同上，驱动名改为 `stdio.drv.modbusrtu_serial_server`

- 理由：三个 service 的 shared 引用模式完全相同，批量替换
- 验收：集成测试（启动 Modbus TCP Server Project 验证驱动正常加载）

### 4.4 runtime_utils.js 迁移

- 修改 `src/demo/js_runtime_demo/shared/lib/runtime_utils.js`：
  - 删除 `driverPathCandidates()` 函数（L1-8）
  - 删除 `startDriverAuto()` 中的候选路径遍历逻辑，改用 `resolveDriver()`
  - 删除 `openDriverAuto()` 中的候选路径遍历逻辑，改用 `resolveDriver()`
  - 保留 `firstSuccess()` 导出（`process_types/index.js` 仍在使用）
  - 改动后：
    ```javascript
    import { resolveDriver } from "stdiolink/driver";

    export function startDriverAuto(driver, baseName, args = []) {
        const program = resolveDriver(baseName);
        if (!driver.start(program, args)) throw new Error(`driver.start failed: ${program}`);
        return program;
    }

    export async function openDriverAuto(openDriver, baseName, args = []) {
        const program = resolveDriver(baseName);
        return await openDriver(program, args);
    }

    export function firstSuccess(runners) {
        let lastError = null;
        for (const run of runners) {
            try { return run(); } catch (e) { lastError = e; }
        }
        throw (lastError || new Error("all runners failed"));
    }
    ```
  - 理由：`driverPathCandidates` 的 4 路径猜测被 C++ 三级解析完全替代；`firstSuccess` 是通用工具函数，保留
  - 验收：js_runtime_demo 端到端运行验证

### 4.5 process_types/index.js 迁移

- 修改 `src/demo/js_runtime_demo/services/process_types/index.js`：
  - L2 将 `import { driverPathCandidates, firstSuccess }` 改为 `import { firstSuccess }` from runtime_utils
  - 新增 `import { resolveDriver } from "stdiolink/driver";`
  - `exportTypeScriptDeclaration()`（L11-16）中：
    - 删除 `driverPathCandidates("stdio.drv.calculator").map(...)` 遍历逻辑
    - 替换为直接调用 `resolveDriver`：
    ```javascript
    function exportTypeScriptDeclaration() {
        const program = resolveDriver("stdio.drv.calculator");
        return exec(program, ["--export-doc=ts"]);
    }
    ```
  - 理由：不再需要 `firstSuccess` + `driverPathCandidates` 组合遍历，`resolveDriver` 一步到位
  - 验收：js_runtime_demo 端到端运行验证

### 4.6 目录迁移与 shared 删除

**4.6a 创建 src/data_root/ 目录结构**

```bash
mkdir -p src/data_root/services
mkdir -p src/data_root/projects
```

**4.6b 迁移 Modbus service 和 project**

```bash
# 迁移 3 个 Modbus 服务
mv src/demo/server_manager_demo/data_root/services/modbustcp_server_service \
   src/data_root/services/
mv src/demo/server_manager_demo/data_root/services/modbusrtu_server_service \
   src/data_root/services/
mv src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service \
   src/data_root/services/

# 迁移 3 个 Project 配置
mv src/demo/server_manager_demo/data_root/projects/manual_modbustcp_server.json \
   src/data_root/projects/
mv src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_server.json \
   src/data_root/projects/
mv src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_serial_server.json \
   src/data_root/projects/
```

**4.6c 删除 shared 目录**

```bash
rm -rf src/demo/server_manager_demo/data_root/shared/
```

**4.6d 全局搜索确认无残留引用**

```bash
grep -r "../../shared/" src/demo/server_manager_demo/ src/data_root/
# 预期：无输出
# 注意：不扫 src/demo/js_runtime_demo/，其 shared/ 是独立的 demo 共享库，不在本里程碑范围内
```

- 理由：shared 目录的所有消费方已在 4.1–4.5 中迁移完毕，可安全删除
- 验收：grep 无残留 + 构建通过

## 5. 文件变更清单

### 5.1 新增文件
- `src/data_root/services/modbustcp_server_service/manifest.json` — 从 demo 迁入
- `src/data_root/services/modbustcp_server_service/config.schema.json` — 从 demo 迁入
- `src/data_root/services/modbustcp_server_service/index.js` — 从 demo 迁入（已改用 resolveDriver）
- `src/data_root/services/modbusrtu_server_service/manifest.json` — 从 demo 迁入
- `src/data_root/services/modbusrtu_server_service/config.schema.json` — 从 demo 迁入
- `src/data_root/services/modbusrtu_server_service/index.js` — 从 demo 迁入（已改用 resolveDriver）
- `src/data_root/services/modbusrtu_serial_server_service/manifest.json` — 从 demo 迁入
- `src/data_root/services/modbusrtu_serial_server_service/config.schema.json` — 从 demo 迁入
- `src/data_root/services/modbusrtu_serial_server_service/index.js` — 从 demo 迁入（已改用 resolveDriver）
- `src/data_root/projects/manual_modbustcp_server.json` — 从 demo 迁入
- `src/data_root/projects/manual_modbusrtu_server.json` — 从 demo 迁入
- `src/data_root/projects/manual_modbusrtu_serial_server.json` — 从 demo 迁入

### 5.2 修改文件
- `src/demo/server_manager_demo/data_root/services/driver_pipeline_service/index.js` — 删除 shared 引用，改用 resolveDriver
- `src/demo/server_manager_demo/data_root/services/process_exec_service/index.js` — 删除内联 findDriver，改用 resolveDriver
- `src/demo/js_runtime_demo/shared/lib/runtime_utils.js` — 删除 driverPathCandidates，改用 resolveDriver
- `src/demo/js_runtime_demo/services/process_types/index.js` — 删除 driverPathCandidates 引用，改用 resolveDriver
- `src/demo/server_manager_demo/README.md` — 更新目录结构说明，移除 `shared/` 条目

### 5.3 删除文件
- `src/demo/server_manager_demo/data_root/shared/` — 整个目录删除（含 lib/driver_utils.js）
- `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/` — 已迁移到 src/data_root/
- `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/` — 已迁移到 src/data_root/
- `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/` — 已迁移到 src/data_root/
- `src/demo/server_manager_demo/data_root/projects/manual_modbustcp_server.json` — 已迁移到 src/data_root/
- `src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_server.json` — 已迁移到 src/data_root/
- `src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_serial_server.json` — 已迁移到 src/data_root/

### 5.4 测试文件
- `tools/validate_m87_migration.sh` — M87 静态校验脚本（T01–T09 自动化回归，见 §6.1 测试代码）
- `tools/validate_m87_migration.ps1` — 同上 PowerShell 版本（适配 Windows 开发环境）

## 6. 测试与验收

### 6.1 静态检查

- 测试对象: 迁移后的 JS 文件 import 引用正确性、shared 引用残留检查、目录结构完整性
- 用例分层: import 引用合法性、目录结构完整性、函数残留检查
- 断言要点: 无 `../../shared/` 残留引用；迁移后文件含正确的 `resolveDriver` import；src/data_root/ 目录结构完整
- 桩替身策略: 无需 mock（纯静态文件校验，不含 JS 语法解析——语法正确性由 §6.2 集成测试覆盖）
- 测试文件: 通过 shell 脚本验证

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| shared 引用残留检查 | `grep -r "../../shared/" src/demo/server_manager_demo/ src/data_root/` 无输出 | T01 |
| driver_utils.js 已删除 | `shared/lib/driver_utils.js` 不存在 | T02 |
| src/data_root/ 目录完整性 | 3 个 service 目录 + 3 个 project 文件存在 | T03 |
| 迁移后 service 含 resolveDriver import | 5 个 JS 文件均含 `stdiolink/driver` import | T04 |
| 迁移后 service 无 findDriverPath 调用 | 5 个 JS 文件均不含 `findDriverPath` | T05 |
| process_exec_service 无内联 findDriver | index.js 不含 `function findDriver` | T06 |
| runtime_utils.js 无 driverPathCandidates | 不含 `driverPathCandidates` 导出 | T07 |
| runtime_utils.js 保留 firstSuccess | 仍含 `firstSuccess` 导出 | T08 |
| demo service 不受影响 | quick_start_service 等无 shared 引用（本就没有） | T09 |

#### 用例详情

**T01 — shared 引用残留检查**
- 前置条件: 4.1–4.6 全部完成
- 输入: `grep -r "../../shared/" src/demo/server_manager_demo/ src/data_root/`
- 预期: 无输出（退出码 1）
- 断言: grep 退出码为 1（无匹配）
- 注意: 检查范围不含 `src/demo/js_runtime_demo/`，因为 `js_runtime_demo/shared/` 是独立的 demo 共享库，不在本里程碑删除范围内

**T02 — driver_utils.js 已删除**
- 前置条件: 4.6c 完成
- 输入: 检查 `src/demo/server_manager_demo/data_root/shared/` 目录是否存在
- 预期: 目录不存在
- 断言: `! -d src/demo/server_manager_demo/data_root/shared/`

**T03 — src/data_root/ 目录完整性**
- 前置条件: 4.6b 完成
- 输入: 检查 `src/data_root/services/` 下 3 个目录和 `src/data_root/projects/` 下 3 个文件
- 预期: 6 个条目均存在，每个 service 含 manifest.json + config.schema.json + index.js
- 断言: 所有路径存在且 manifest.json 可被 JSON.parse

**T04 — 迁移后 service 含 resolveDriver import**
- 前置条件: 4.1–4.5 完成
- 输入: 对 5 个已迁移 JS 文件执行 `grep "stdiolink/driver"`
- 预期: 每个文件均包含 `import { resolveDriver } from "stdiolink/driver"`
- 断言: grep 匹配成功

**T05 — 迁移后 service 无 findDriverPath 调用**
- 前置条件: 4.1–4.5 完成
- 输入: 对 5 个已迁移 JS 文件执行 `grep "findDriverPath"`
- 预期: 无匹配
- 断言: grep 退出码为 1

**T06 — process_exec_service 无内联 findDriver**
- 前置条件: 4.2 完成
- 输入: `grep "function findDriver" src/demo/.../process_exec_service/index.js`
- 预期: 无匹配
- 断言: grep 退出码为 1

**T07 — runtime_utils.js 无 driverPathCandidates**
- 前置条件: 4.4 完成
- 输入: `grep "driverPathCandidates" src/demo/js_runtime_demo/shared/lib/runtime_utils.js`
- 预期: 无匹配
- 断言: grep 退出码为 1

**T08 — runtime_utils.js 保留 firstSuccess**
- 前置条件: 4.4 完成
- 输入: `grep "export function firstSuccess" src/demo/js_runtime_demo/shared/lib/runtime_utils.js`
- 预期: 匹配成功
- 断言: grep 退出码为 0

**T09 — demo service 不受影响**
- 前置条件: 4.6 完成
- 输入: 检查 `src/demo/server_manager_demo/data_root/services/quick_start_service/index.js` 存在且无 shared 引用
- 预期: 文件存在，不含 `../../shared/`
- 断言: 文件存在 && grep 无匹配

#### 测试代码

```bash
#!/bin/bash
# validate_m87_migration.sh — M87 静态校验脚本
set -e

DEMO_ROOT="src/demo/server_manager_demo/data_root"
DATA_ROOT="src/data_root"
RUNTIME_UTILS="src/demo/js_runtime_demo/shared/lib/runtime_utils.js"
PROCESS_TYPES="src/demo/js_runtime_demo/services/process_types/index.js"

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
```

### 6.2 集成测试

#### 6.2.1 driver_pipeline_service 端到端验证

- 启动 `stdiolink_server --data-root src/demo/server_manager_demo/data_root`
- 通过 `POST /api/projects/{id}/run` 手动触发 `manual_driver_pipeline` Project
- Instance 状态变为 `running`，日志中出现 `driver opened` 和 `iteration` 输出
- 验证 `resolveDriver("stdio.drv.calculator")` 正常返回驱动路径（日志无 `driver not found` 错误）
- Instance 正常完成（状态变为 `stopped`）

#### 6.2.2 process_exec_service 端到端验证

- 通过 `POST /api/projects/{id}/run` 手动触发 `manual_process_exec` Project
- 验证日志中出现 `driver meta exported` 和 `spawn interaction done`
- 验证无 `skipped` 日志（旧版 findDriver 返回 null 时的 skip 分支已删除）

#### 6.2.3 Modbus TCP Server 端到端验证

- 注意：`src/data_root/` 仅含 services/ 和 projects/，不含 `config.json`，无法直接作为 `--data-root` 启动 Server（Server 启动时需要 `<data_root>/config.json`）
- 验证方式：使用 demo data_root 作为基础，手动将 `src/data_root/services/` 和 `src/data_root/projects/` 复制到 `src/demo/server_manager_demo/data_root/` 下（或等待 M88 两层复制合并后直接使用构建输出）
- 启动 `stdiolink_server --data-root src/demo/server_manager_demo/data_root`（合并后含 Modbus service 和 project）
- 通过 `POST /api/projects/{id}/run` 手动触发 `manual_modbustcp_server` Project
- Instance 状态变为 `running`（keepalive 模式）
- 使用 Modbus TCP 客户端工具连接 `127.0.0.1:502`，执行 FC 0x03 读取
- 验证读取成功（返回默认值 0）
- 通过 `POST /api/instances/{id}/stop` 停止 Instance

#### 6.2.4 js_runtime_demo 端到端验证

- 在构建目录下运行 `stdiolink_service src/demo/js_runtime_demo/services/process_types`
- 验证输出包含 `[M26] d.ts preview:` 和 `has DriverProxy type: true`
- 验证无 `cannot start driver` 或 `cannot open driver` 错误

### 6.3 验收标准

- [ ] `shared/` 目录已删除（T02）
- [ ] 全局搜索无 `../../shared/` 残留引用（T01）
- [ ] `src/data_root/` 目录结构完整：3 个 service + 3 个 project（T03）
- [ ] 5 个已迁移 JS 文件均含 `resolveDriver` import（T04）
- [ ] 5 个已迁移 JS 文件均不含 `findDriverPath`（T05）
- [ ] `process_exec_service` 无内联 `findDriver` 函数（T06）
- [ ] `runtime_utils.js` 无 `driverPathCandidates`，保留 `firstSuccess`（T07、T08）
- [ ] demo service（quick_start 等）不受影响（T09）
- [ ] driver_pipeline Project 端到端启动正常（§6.2.1）
- [ ] process_exec Project 端到端启动正常（§6.2.2）
- [ ] Modbus TCP Server Project 端到端启动正常（§6.2.3）
- [ ] js_runtime_demo process_types 端到端运行正常（§6.2.4）
- [ ] 既有 C++ 测试无回归：`stdiolink_tests` 全部通过

## 7. 风险与控制

- 风险: 迁移遗漏——某个 JS 文件仍残留 `../../shared/` 引用，运行时 import 失败
  - 控制: 4.6d 全局 grep 检查确保无残留；T01 静态校验脚本自动化验证
  - 测试覆盖: T01、T04、T05

- 风险: `resolveDriver()` 在开发环境下找不到驱动（M86 的 C++ 绑定尚未就绪）
  - 控制: M87 的前置条件为 M86 完成；开发环境下驱动位于 `build/bin/`，`resolveDriver` 的 appDir 级别可命中
  - 测试覆盖: §6.2.1–§6.2.4 集成测试

- 风险: Modbus service 迁移到 `src/data_root/` 后，构建脚本未复制到运行时目录，Server 启动后找不到 service
  - 控制: M87 范围内构建脚本暂不修改（M88 负责）；开发环境需将 `src/data_root/` 内容合并到 demo data_root 下验证（`src/data_root/` 本身不含 `config.json`，无法直接作为 `--data-root` 启动 Server）
  - 控制: §3.4 明确标注 M88 完成前的临时限制
  - 测试覆盖: §6.2.3（合并到 demo data_root 后验证）

- 风险: `process_exec_service` 删除 else 分支后，`resolveDriver` 抛出异常未被 try-catch 捕获，导致 service 崩溃
  - 控制: 当前代码中 `resolveDriver` 调用位于 IIFE async 内部，异常会被 stdiolink_service 的顶层 unhandledRejection 处理器捕获并输出错误日志
  - 测试覆盖: §6.2.2（端到端验证）

## 8. 里程碑完成定义（DoD）

- [ ] `shared/` 目录已彻底删除
- [ ] 全局搜索无 `../../shared/` 残留引用
- [ ] `driver_pipeline_service` 改用 `resolveDriver()`，删除 shared import
- [ ] `process_exec_service` 改用 `resolveDriver()`，删除内联 `findDriver()`
- [ ] 三个 Modbus service 改用 `resolveDriver()`，删除 shared import，迁移到 `src/data_root/`
- [ ] 三个 Modbus project 迁移到 `src/data_root/projects/`
- [ ] `runtime_utils.js` 改用 `resolveDriver()`，删除 `driverPathCandidates()`，保留 `firstSuccess()`
- [ ] `process_types/index.js` 改用 `resolveDriver()`，删除 `driverPathCandidates` 引用
- [ ] 静态校验脚本 T01–T09 全部通过
- [ ] 集成测试：driver_pipeline、process_exec、Modbus TCP Server、js_runtime_demo 端到端正常
- [ ] 既有 C++ 测试无回归：`stdiolink_tests` 全部通过
