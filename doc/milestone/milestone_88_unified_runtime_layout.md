# 里程碑 88：统一 runtime 目录布局与构建发布适配

> **前置条件**: M87（移除 shared 依赖与 Service 自包含迁移）
> **目标**: 统一开发与发布目录布局为同构 `runtime/` 结构，CMakeLists 和发布脚本实现两层复制合并（production + demo），新增重复副本检查脚本

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| CMakeLists（构建） | 两层复制合并：先 src/data_root/ 再 src/demo/.../data_root/ |
| 发布脚本（打包） | 两层复制合并 + 移除 shared 复制 + driver 不重复放入 bin/ |
| 检查脚本（质量） | 新增重复副本检查脚本，发布后自动调用 |

- `src/demo/server_manager_demo/CMakeLists.txt` 改为先复制 `src/data_root/` 再叠加 demo data_root
- `tools/publish_release.ps1` 改为先复制 `src/data_root/` 再叠加 demo data_root，移除 `data_root/shared` 创建
- `tools/publish_release.sh` 同步适配
- 新增 `tools/check_duplicates.ps1` / `.sh` 检查同名组件多副本
- 发布后 driver 可执行文件仅存在于 `data_root/drivers/<name>/`，不在 `bin/` 中重复

## 2. 背景与问题

- M87 将 production 内容（Modbus service/project）迁移到 `src/data_root/`，但构建脚本和发布脚本尚未感知这个新目录
- 当前 CMakeLists 仅复制 `src/demo/.../data_root/`，不复制 `src/data_root/`，导致构建输出中缺少 Modbus service
- 当前发布脚本 `publish_release.ps1` 仍创建 `data_root/shared` 目录（L235），但 M87 已删除 shared
- driver 可执行文件同时存在于 `bin/` 和 `data_root/drivers/<name>/` 中（L438-442 虽有 Remove-Item 但仅在 Windows 上生效），存在多副本风险

**范围**:
- CMakeLists 两层复制合并
- 发布脚本两层复制合并 + 清理 shared 残留
- 新增重复副本检查脚本
- 发布脚本打包完成后自动调用检查

**非目标**:
- 不修改 C++ 代码（M86 已完成）
- 不修改 JS service 代码（M87 已完成）
- 不修改 Server/Service 的运行时行为
- 不新增 JS 绑定 API

## 3. 技术要点

### 3.1 两层复制合并策略

构建和发布均采用相同的两层复制策略：

```
第 1 层: src/data_root/          → 目标 data_root/    (production 内容)
第 2 层: src/demo/.../data_root/ → 目标 data_root/    (demo 内容叠加)
```

合并后目标 `data_root/` 结构：

```
data_root/
├── services/
│   ├── modbustcp_server_service/       ← 来自第 1 层
│   ├── modbusrtu_server_service/       ← 来自第 1 层
│   ├── modbusrtu_serial_server_service/← 来自第 1 层
│   ├── quick_start_service/            ← 来自第 2 层
│   ├── system_info_service/            ← 来自第 2 层
│   ├── driver_pipeline_service/        ← 来自第 2 层
│   ├── process_exec_service/           ← 来自第 2 层
│   └── api_health_check_service/       ← 来自第 2 层
├── projects/
│   ├── manual_modbustcp_server.json    ← 来自第 1 层
│   ├── manual_modbusrtu_server.json    ← 来自第 1 层
│   ├── manual_modbusrtu_serial_server.json ← 来自第 1 层
│   ├── daemon_demo.json                ← 来自第 2 层
│   └── ...                             ← 来自第 2 层
├── drivers/
│   ├── stdio.drv.calculator/
│   └── stdio.drv.modbus*/
└── config.json
```

关键原则：
- 第 2 层叠加时，同名文件会覆盖第 1 层（`cmake -E copy_directory` 和 `Copy-Item -Force` 的默认行为）；当前 production 和 demo 的 service/project 名称无重叠，不存在覆盖风险
- `shared/` 目录不再创建（M87 已删除）
- driver 可执行文件仅存在于 `data_root/drivers/<name>/`

### 3.2 CMakeLists 改动

当前 `src/demo/server_manager_demo/CMakeLists.txt` 仅执行单层复制：

```cmake
# Before（当前，L3-6）
COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data_root"
    "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
```

改为两层复制：

```cmake
# After（改动后）
# 第 1 层: production data_root
COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/src/data_root"
    "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
# 第 2 层: demo data_root（叠加）
COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data_root"
    "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
```

注意：`cmake -E copy_directory` 的行为是合并而非覆盖——目标目录已有的文件会被同名文件覆盖，但不会删除目标中多出的文件。因此第 2 层叠加后，第 1 层的 Modbus service 仍然保留。

### 3.3 发布脚本改动要点

`tools/publish_release.ps1` 需要三处改动：

**改动 1：移除 shared 目录创建**

```powershell
# Before（L228-236 $dirsToCreate 数组）
$dirsToCreate = @(
    "bin",
    "data_root/drivers",
    "data_root/services",
    "data_root/projects",
    "data_root/workspaces",
    "data_root/logs",
    "data_root/shared"       # ← 删除此行
)
```

**改动 2：两层复制合并**

```powershell
# Before（L410-425 "Seed demo data" 区域）
# 仅复制 demo data_root

# After
# 第 1 层: production data_root
$prodDataRoot = Join-Path $rootDir "src/data_root"
if (Test-Path -LiteralPath $prodDataRoot -PathType Container) {
    $prodServices = Join-Path $prodDataRoot "services"
    if (Test-Path -LiteralPath $prodServices -PathType Container) {
        Copy-Item -Path (Join-Path $prodServices "*") `
            -Destination (Join-Path $packageDir "data_root/services") -Recurse -Force
    }
    $prodProjects = Join-Path $prodDataRoot "projects"
    if (Test-Path -LiteralPath $prodProjects -PathType Container) {
        Copy-Item -Path (Join-Path $prodProjects "*") `
            -Destination (Join-Path $packageDir "data_root/projects") -Recurse -Force
    }
    Write-Host "  Production services and projects seeded."
}
# 第 2 层: demo data_root（叠加）
# ... 现有 demo 复制逻辑保持不变 ...
```

**改动 3：确认 driver 不在 bin/ 中重复**

当前 L438-442 已有 Remove-Item 逻辑，确认其在所有平台上生效即可。

### 3.4 重复副本检查脚本

新增 `tools/check_duplicates.ps1`，扫描发布目录检查同名组件多副本：

```
检查规则：
1. bin/ 下不应存在 stdio.drv.* 可执行文件（driver 仅在 data_root/drivers/ 下）
2. data_root/drivers/ 下不应有同名 driver 可执行文件出现在多个子目录中
```

发现重复时输出告警并以非零退出码失败，发布脚本打包完成后自动调用。

## 4. 实现步骤

### 4.1 CMakeLists 两层复制合并

- 修改 `src/demo/server_manager_demo/CMakeLists.txt`：
  - 在现有 `copy_directory` 命令前新增第 1 层复制：
    ```cmake
    add_custom_target(server_manager_demo_assets ALL
        # 第 1 层: production data_root
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/data_root"
            "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
        # 第 2 层: demo data_root（叠加）
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data_root"
            "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
        # scripts 和 README 保持不变
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
            "${SERVER_MANAGER_DEMO_DST_DIR}/scripts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${SERVER_MANAGER_DEMO_DST_DIR}/README.md"
        COMMENT "Copy server manager demo assets to ${SERVER_MANAGER_DEMO_DST_DIR}"
    )
    ```
  - 在两层复制前新增源层重叠检测（纯 CMake，无外部依赖）：
    ```cmake
    # 源层重叠检测：production 与 demo 的 services/ 和 projects/ 不应有同名条目
    foreach(_subdir services projects)
        set(_prod_dir "${CMAKE_SOURCE_DIR}/src/data_root/${_subdir}")
        set(_demo_dir "${CMAKE_CURRENT_SOURCE_DIR}/data_root/${_subdir}")
        if (IS_DIRECTORY "${_prod_dir}" AND IS_DIRECTORY "${_demo_dir}")
            file(GLOB _prod_entries RELATIVE "${_prod_dir}" "${_prod_dir}/*")
            file(GLOB _demo_entries RELATIVE "${_demo_dir}" "${_demo_dir}/*")
            set(_overlap "")
            foreach(_entry IN LISTS _prod_entries)
                if ("${_entry}" IN_LIST _demo_entries)
                    list(APPEND _overlap "${_entry}")
                endif()
            endforeach()
            if (_overlap)
                message(WARNING "Production and demo data_root/${_subdir}/ have overlapping names: ${_overlap} — demo will overwrite production")
            endif()
        endif()
    endforeach()
    ```
  - 理由：第 1 层先复制 production 内容，第 2 层叠加 demo 内容；cmake copy_directory 合并语义保证两层内容共存；源层重叠检测覆盖 services/ 和 projects/，防止未来新增同名条目时静默覆盖
  - 验收：构建后 `build/bin/server_manager_demo/data_root/services/` 同时包含 Modbus service 和 demo service；若源层有同名 service 或 project 则构建输出 WARNING

### 4.2 发布脚本移除 shared 目录创建

- 修改 `tools/publish_release.ps1`：
  - L228-236 `$dirsToCreate` 数组中删除 `"data_root/shared"` 条目：
    ```powershell
    $dirsToCreate = @(
        "bin",
        "data_root/drivers",
        "data_root/services",
        "data_root/projects",
        "data_root/workspaces",
        "data_root/logs"
        # "data_root/shared" — 已删除（M87）
    )
    ```
  - 理由：M87 已删除 shared 目录，发布包中不应再创建空的 shared 目录
  - 验收：发布后 `data_root/` 下无 `shared/` 目录

### 4.3 发布脚本两层复制合并

- 修改 `tools/publish_release.ps1`：
  - 在 L410-425 "Seed demo data" 区域前新增第 1 层复制：
    ```powershell
    # 第 1 层: production data_root
    Write-Host "Seeding production data into data_root..."
    $prodDataRoot = Join-Path $rootDir "src/data_root"
    if (Test-Path -LiteralPath $prodDataRoot -PathType Container) {
        $prodServices = Join-Path $prodDataRoot "services"
        if (Test-Path -LiteralPath $prodServices -PathType Container) {
            Copy-Item -Path (Join-Path $prodServices "*") `
                -Destination (Join-Path $packageDir "data_root/services") `
                -Recurse -Force
        }
        $prodProjects = Join-Path $prodDataRoot "projects"
        if (Test-Path -LiteralPath $prodProjects -PathType Container) {
            Copy-Item -Path (Join-Path $prodProjects "*") `
                -Destination (Join-Path $packageDir "data_root/projects") `
                -Recurse -Force
        }
        Write-Host "  Production services and projects seeded."
    }
    ```
  - 现有 demo 复制逻辑（L410-425）保持不变，作为第 2 层叠加
  - 理由：production 内容先入，demo 内容叠加；同名文件 demo 覆盖 production（当前无冲突）
  - 验收：发布后 `data_root/services/` 同时包含 Modbus service 和 demo service

### 4.4 发布脚本同步适配 Unix 版

- 修改 `tools/publish_release.sh`：
  - 同步 4.2 和 4.3 的改动逻辑（移除 shared 创建 + 两层复制合并）
  - 使用 `cp -r` 替代 PowerShell 的 `Copy-Item`
  - 理由：保持 Windows/Unix 发布脚本行为一致
  - 验收：Unix 环境下发布打包输出与 Windows 一致

### 4.5 新增重复副本检查脚本

- 新增 `tools/check_duplicates.ps1`：
  ```powershell
  #!/usr/bin/env pwsh
  # check_duplicates.ps1 — 检查发布目录中同名组件多副本
  param(
      [Parameter(Mandatory = $true)]
      [string]$PackageDir
  )
  Set-StrictMode -Version Latest
  $ErrorActionPreference = "Stop"
  $errors = 0

  # 规则 1: bin/ 下不应存在 stdio.drv.* 可执行文件
  $binDir = Join-Path $PackageDir "bin"
  if (Test-Path $binDir) {
      $drvInBin = Get-ChildItem -LiteralPath $binDir -File |
          Where-Object { $_.Name -match "^stdio\.drv\." }
      foreach ($f in $drvInBin) {
          Write-Host "ERROR: driver in bin/: $($f.Name) (should be in data_root/drivers/ only)"
          $errors++
      }
  }

  # 规则 2: data_root/drivers/ 下同名 driver 可执行文件不应出现在多个子目录
  $driversDir = Join-Path $PackageDir "data_root/drivers"
  if (Test-Path $driversDir) {
      $allDriverFiles = Get-ChildItem -LiteralPath $driversDir -File -Recurse |
          Where-Object { $_.Name -match "^stdio\.drv\." } |
          Group-Object Name
      foreach ($group in $allDriverFiles) {
          if ($group.Count -gt 1) {
              $locations = ($group.Group | ForEach-Object { $_.Directory.Name }) -join ", "
              Write-Host "ERROR: duplicate driver '$($group.Name)' in: $locations"
              $errors++
          }
      }
  }

  if ($errors -gt 0) {
      Write-Error "$errors duplicate(s) found"
      exit 1
  }
  Write-Host "No duplicates found."
  ```
  - 理由：自动化检查防止同名组件多副本并存
  - 验收：对正确的发布目录运行退出码为 0；对故意放入重复文件的目录运行退出码为 1

### 4.6 发布脚本集成检查

- 修改 `tools/publish_release.ps1`：
  - 在打包完成后（L521 "Release package created" 之前）调用检查脚本：
    ```powershell
    # 重复副本检查
    Write-Host "Checking for duplicate components..."
    $checkScript = Join-Path $scriptDir "check_duplicates.ps1"
    & pwsh -File $checkScript -PackageDir $packageDir
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Duplicate check failed! See errors above."
        exit 1
    }
    ```
- 修改 `tools/publish_release.sh`：
  - 在打包完成后调用检查脚本：
    ```bash
    # 重复副本检查
    echo "Checking for duplicate components..."
    check_script="$(dirname "$0")/check_duplicates.sh"
    if ! bash "$check_script" "$package_dir"; then
        echo "ERROR: Duplicate check failed! See errors above." >&2
        exit 1
    fi
    ```
  - 理由：发布流程自动化质量门禁，发现重复即阻断发布
  - 验收：发布脚本正常完成时检查通过；故意引入重复时发布失败

## 5. 文件变更清单

### 5.1 新增文件
- `tools/check_duplicates.ps1` — 重复副本检查脚本（Windows）
- `tools/check_duplicates.sh` — 重复副本检查脚本（Unix）

### 5.2 修改文件
- `src/demo/server_manager_demo/CMakeLists.txt` — 新增第 1 层 production data_root 复制
- `tools/publish_release.ps1` — 移除 shared 创建 + 两层复制合并 + 集成检查脚本
- `tools/publish_release.sh` — 同步适配

### 5.3 测试文件
- 无新增测试文件（验证通过构建验证和发布打包验证完成，见 §6）

## 6. 测试与验收

### 6.1 构建验证

- 测试对象: CMakeLists 两层复制合并后的构建输出
- 用例分层: 目录完整性、production 内容存在、demo 内容存在、无 shared 残留
- 断言要点: 构建输出 data_root/ 同时包含两层内容；无 shared/ 目录
- 桩替身策略: 无需 mock（直接检查构建输出目录）
- 测试文件: 通过 shell 脚本验证

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| 构建输出含 production service | data_root/services/ 下有 3 个 Modbus service | T01 |
| 构建输出含 demo service | data_root/services/ 下有 quick_start 等 demo service | T02 |
| 构建输出无 shared | data_root/ 下无 shared/ 目录 | T03 |
| 发布包含 production service | 发布 data_root/services/ 下有 3 个 Modbus service | T04 |
| 发布包含 demo service | 发布 data_root/services/ 下有 demo service | T05 |
| 发布包无 shared | 发布 data_root/ 下无 shared/ 目录 | T06 |
| driver 不在 bin/ 中重复 | bin/ 下无 stdio.drv.* 文件 | T07 |
| driver 在 data_root/drivers/ 中 | data_root/drivers/ 下有 driver 子目录 | T08 |
| 检查脚本正常通过 | check_duplicates 对正确目录退出码 0 | T09 |
| 检查脚本检出重复 | check_duplicates 对含重复的目录退出码 1 | T10 |

#### 用例详情

**T01 — 构建输出含 production service**
- 前置条件: `build.bat Debug` 构建完成
- 输入: 检查 `build/bin/server_manager_demo/data_root/services/` 下是否存在 3 个 Modbus service 目录
- 预期: `modbustcp_server_service/`、`modbusrtu_server_service/`、`modbusrtu_serial_server_service/` 均存在
- 断言: 3 个目录存在且各含 manifest.json

**T02 — 构建输出含 demo service**
- 前置条件: T01 同
- 输入: 检查同一目录下是否存在 demo service
- 预期: `quick_start_service/`、`system_info_service/` 等存在
- 断言: 至少 3 个 demo service 目录存在

**T03 — 构建输出无 shared**
- 前置条件: T01 同
- 输入: 检查 `build/bin/server_manager_demo/data_root/shared/` 是否存在
- 预期: 不存在
- 断言: `! -d` 检查通过

**T04 — 发布包含 production service**
- 前置条件: `publish_release.ps1 --skip-tests --skip-webui` 完成
- 输入: 检查发布目录 `data_root/services/` 下是否存在 3 个 Modbus service
- 预期: 3 个 Modbus service 目录均存在且各含 manifest.json
- 断言: 目录存在 && manifest.json 可被 JSON.parse

**T05 — 发布包含 demo service**
- 前置条件: T04 同
- 输入: 检查发布目录 `data_root/services/` 下是否存在 demo service
- 预期: `quick_start_service/` 等存在
- 断言: 至少 3 个 demo service 目录存在

**T06 — 发布包无 shared**
- 前置条件: T04 同
- 输入: 检查发布目录 `data_root/shared/` 是否存在
- 预期: 不存在
- 断言: `! -d` 检查通过

**T07 — driver 不在 bin/ 中重复**
- 前置条件: T04 同
- 输入: 检查发布目录 `bin/` 下是否存在 `stdio.drv.*` 文件
- 预期: 无匹配
- 断言: `ls bin/stdio.drv.* 2>/dev/null` 无输出

**T08 — driver 在 data_root/drivers/ 中**
- 前置条件: T04 同
- 输入: 检查发布目录 `data_root/drivers/` 下是否存在 driver 子目录
- 预期: 至少 1 个 `stdio.drv.*` 子目录存在，内含可执行文件
- 断言: 子目录存在且含同名可执行文件

**T09 — 检查脚本正常通过**
- 前置条件: T04 同（正确的发布目录）
- 输入（Windows）: `check_duplicates.ps1 -PackageDir <发布目录>`
- 输入（Unix）: `check_duplicates.sh <发布目录>`
- 预期: 退出码 0，输出 "No duplicates found."
- 断言: 退出码为 0

**T10 — 检查脚本检出重复**
- 前置条件: 在发布目录 `bin/` 下手动放入一个 `stdio.drv.test.exe` 文件
- 输入（Windows）: `check_duplicates.ps1 -PackageDir <发布目录>`
- 输入（Unix）: `check_duplicates.sh <发布目录>`
- 预期: 退出码 1，输出包含 "ERROR: driver in bin/"
- 断言: 退出码非 0

#### 测试代码

```bash
#!/bin/bash
# validate_m88_layout.sh — M88 构建/发布布局校验脚本
set -e

# 参数: 目标 data_root 目录（构建输出或发布包）
TARGET_DIR="${1:?Usage: validate_m88_layout.sh <data_root_dir>}"

PROD_SERVICES=("modbustcp_server_service" "modbusrtu_server_service" "modbusrtu_serial_server_service")
DEMO_SERVICES=("quick_start_service" "system_info_service" "api_health_check_service")

# T01/T04 — production service 存在
echo "Checking production services..."
for svc in "${PROD_SERVICES[@]}"; do
    [ -d "$TARGET_DIR/services/$svc" ] || { echo "FAIL: $svc missing"; exit 1; }
    [ -f "$TARGET_DIR/services/$svc/manifest.json" ] || { echo "FAIL: $svc/manifest.json missing"; exit 1; }
done
echo "OK: production services present"

# T02/T05 — demo service 存在
echo "Checking demo services..."
DEMO_COUNT=0
for svc in "${DEMO_SERVICES[@]}"; do
    [ -d "$TARGET_DIR/services/$svc" ] && DEMO_COUNT=$((DEMO_COUNT + 1))
done
[ "$DEMO_COUNT" -ge 3 ] || { echo "FAIL: only $DEMO_COUNT demo services found"; exit 1; }
echo "OK: demo services present ($DEMO_COUNT)"

# T03/T06 — 无 shared 目录
echo "Checking no shared directory..."
if [ -d "$TARGET_DIR/shared" ]; then
    echo "FAIL: shared/ directory exists"; exit 1
fi
echo "OK: no shared directory"

echo "All M88 layout validations passed."
```

### 6.2 发布打包验证

- 执行 `tools/publish_release.ps1 --skip-tests --skip-webui`（Windows）或 `tools/publish_release.sh --skip-tests --skip-webui`（Unix）
- 验证发布目录结构：
  - `data_root/services/` 同时包含 3 个 Modbus service 和 demo service（T04、T05）
  - `data_root/` 下无 `shared/` 目录（T06）
  - `bin/` 下无 `stdio.drv.*` 文件（T07）
  - `data_root/drivers/` 下有 driver 子目录（T08）
  - `check_duplicates.ps1` / `.sh` 自动调用通过（T09）
- 在发布目录下启动 server 并触发 Modbus TCP Server Project，验证驱动路径解析正确

### 6.3 验收标准

- [ ] 构建输出 `data_root/services/` 同时包含 production 和 demo service（T01、T02）
- [ ] 构建输出无 `shared/` 目录（T03）
- [ ] 发布包 `data_root/services/` 同时包含 production 和 demo service（T04、T05）
- [ ] 发布包无 `shared/` 目录（T06）
- [ ] 发布包 `bin/` 下无 `stdio.drv.*` 文件（T07）
- [ ] 发布包 `data_root/drivers/` 下有 driver 子目录（T08）
- [ ] `check_duplicates.ps1` / `.sh` 对正确目录通过（T09）
- [ ] `check_duplicates.ps1` / `.sh` 对含重复的目录失败（T10）
- [ ] 发布目录下启动 server + 触发 Modbus TCP Server Project 正常（§6.2）
- [ ] 既有 C++ 测试无回归：`stdiolink_tests` 全部通过

## 7. 风险与控制

- 风险: `cmake -E copy_directory` 第 2 层叠加时覆盖第 1 层同名文件（如 production 和 demo 有同名 service 或 project）
  - 控制: 当前 production（Modbus）和 demo（quick_start 等）service/project 名称无重叠，不存在覆盖风险
  - 控制: CMakeLists 新增源层重叠检测（覆盖 services/ 和 projects/），构建时若发现同名条目输出 WARNING，防止静默覆盖
  - 控制: 如未来新增同名条目，第 2 层（demo）覆盖第 1 层（production）是合理行为——开发环境优先使用 demo 版本
  - 测试覆盖: T01、T02（构建输出同时包含两层内容）

- 风险: 发布脚本 `Copy-Item -Recurse -Force` 在 PowerShell 不同版本下行为差异（如 PS 5 vs PS 7 对目录合并的处理）
  - 控制: 使用 `Copy-Item -Path (Join-Path $dir "*")` 模式（复制目录内容而非目录本身），在 PS 5 和 PS 7 下行为一致
  - 测试覆盖: T04、T05（发布打包验证）

- 风险: `check_duplicates.ps1` 误报——某些非 driver 文件名恰好匹配 `stdio.drv.*` 模式
  - 控制: 检查规则仅匹配 `^stdio\.drv\.` 前缀，这是项目约定的 driver 命名规范，误报概率极低
  - 测试覆盖: T09、T10（正常通过 + 检出重复）

## 8. 里程碑完成定义（DoD）

- [ ] CMakeLists 两层复制合并：构建输出 `data_root/services/` 同时包含 production 和 demo service（T01、T02）
- [ ] 构建输出无 `shared/` 目录（T03）
- [ ] 发布脚本移除 `data_root/shared` 目录创建（T06）
- [ ] 发布脚本两层复制合并：发布包 `data_root/services/` 同时包含 production 和 demo service（T04、T05）
- [ ] 发布包 `bin/` 下无 `stdio.drv.*` 文件（T07）
- [ ] 发布包 `data_root/drivers/` 下有 driver 子目录（T08）
- [ ] 新增 `check_duplicates.ps1` / `.sh` 检查脚本，对正确目录通过（T09）、对含重复目录失败（T10）
- [ ] 发布脚本打包完成后自动调用检查脚本（.ps1 调 .ps1，.sh 调 .sh）
- [ ] `tools/publish_release.sh` 同步适配
- [ ] 发布目录下启动 server + 触发 Modbus TCP Server Project 正常（§6.2）
- [ ] 既有 C++ 测试无回归：`stdiolink_tests` 全部通过

