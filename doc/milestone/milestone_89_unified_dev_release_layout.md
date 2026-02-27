# 里程碑 89：统一开发与发布目录布局

> **前置条件**: M88（统一 runtime 目录布局与构建发布适配）
> **目标**: 重构构建系统，使开发环境（build）与发布环境（release）采用同构目录布局，消除 DriverManagerScanner 在开发环境下的功能缺失

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| CMake 构建系统 | 原始输出与运行时目录分离；新增 `assemble_runtime` 组装目标 |
| 发布脚本 | `publish_release` 基于 `runtime_release/` 打包，不再独立组装 |
| 构建脚本 | `build.bat` / `build.sh` 适配新目录结构 |
| Demo 脚本 | `run_demo.sh` 适配新路径；删除 `server_manager_demo` 目录概念 |

- CMake 原始输出目录从 `build/bin/` 改为 `build/debug/` 或 `build/release/`（仅编译产物，不参与运行）
- 新增 `build/runtime_debug/` 和 `build/runtime_release/` 目录，与发布包同构布局
- `runtime_*/` 下包含 `bin/`（可执行文件 + Qt 插件）和 `data_root/`（drivers + services + projects）
- DriverManagerScanner 在开发环境下可正常扫描 `data_root/drivers/`
- `resolveDriver` 在开发和发布环境均走 tier-1（dataRoot/drivers/）路径
- 单元测试和调试均在 `runtime_*/` 目录下执行
- `publish_release` 从 `runtime_release/` 打包，打包前强制清理重建

## 2. 背景与问题

- M88 声称"统一开发与发布目录布局"，但实际存在三个层面的不一致：
  1. **顶层结构**：开发环境 `data_root/` 嵌套在 `build/bin/server_manager_demo/` 下，发布环境 `data_root/` 与 `bin/` 平级
  2. **驱动布局**：开发环境驱动平铺在 `build/bin/`，发布环境按 `data_root/drivers/<name>/` 子目录组织
  3. **功能缺失**：`DriverManagerScanner` 扫描 `data_root/drivers/`，开发环境下该目录为空，导致 WebUI Drivers 页面、REST API `GET /api/drivers`、DriverLab 驱动列表全部失效
- `resolveDriver` 的 tier-2 回退（appDir）掩盖了布局不一致，JS service 层能跑通但 Server 层驱动发现链路断裂
- `run_demo.sh` 中的 `prepare_demo_drivers()` 是对此缺口的运行时补丁，不应作为长期方案
- 当前 `build/bin/` 混合了编译产物、测试桩、demo 资产，职责不清

**范围**:
- CMake 构建系统重构：原始输出与运行时目录分离
- 新增 `assemble_runtime` CMake 目标，组装同构运行时目录
- `build.bat` / `build.sh` 适配新目录结构
- `publish_release.ps1` / `.sh` 改为基于 `runtime_release/` 打包
- 删除 `server_manager_demo` 目录概念，demo 脚本迁移到 `runtime_*/scripts/`
- `js_runtime_demo` 和 `config_demo` 资产迁移到 `runtime_*/` 下
- 单元测试执行路径适配

**非目标**:
- 不修改 C++ 业务代码（M86 已完成）
- 不修改 JS service 代码（M87 已完成）
- 不修改 `resolveDriver` 或 `DriverManagerScanner` 的运行时逻辑
- 不修改 WebUI 代码
- 不新增 JS 绑定 API

## 3. 技术要点

### 3.1 目录布局 Before/After 对比

Before（当前）:
```
build/
├── bin/                              ← 混合：编译产物 + 测试桩 + Qt 插件
│   ├── stdiolink_server.exe
│   ├── stdiolink_service.exe
│   ├── stdiolink_tests.exe
│   ├── stdio.drv.calculator.exe      ← 驱动平铺
│   ├── stdio.drv.modbustcp.exe
│   ├── test_driver.exe               ← 测试桩
│   ├── platforms/                    ← Qt 插件
│   ├── server_manager_demo/
│   │   ├── data_root/               ← 嵌套在 demo 子目录下
│   │   │   ├── services/
│   │   │   ├── projects/
│   │   │   ├── drivers/             ← 空目录
│   │   │   └── config.json
│   │   ├── scripts/
│   │   └── README.md
│   ├── js_runtime_demo/
│   └── config_demo/
```

After（改动后）:
```
build/
├── debug/                            ← CMake 原始输出（Debug），仅编译产物
│   ├── stdiolink_server.exe
│   ├── stdiolink_service.exe
│   ├── stdiolink_tests.exe
│   ├── stdio.drv.calculator.exe
│   ├── test_driver.exe
│   └── ...
├── release/                          ← CMake 原始输出（Release），仅编译产物
│   └── ...
├── runtime_debug/                    ← 组装后运行时（与发布包同构）
│   ├── bin/
│   │   ├── stdiolink_server.exe
│   │   ├── stdiolink_service.exe
│   │   ├── stdiolink_tests.exe       ← 测试二进制也在此
│   │   └── platforms/                ← Qt 插件
│   ├── data_root/
│   │   ├── drivers/
│   │   │   ├── stdio.drv.calculator/
│   │   │   │   └── stdio.drv.calculator.exe
│   │   │   ├── stdio.drv.modbustcp/
│   │   │   │   └── stdio.drv.modbustcp.exe
│   │   │   └── ...
│   │   ├── services/                 ← 两层合并（production + demo）
│   │   ├── projects/                 ← 两层合并
│   │   ├── config.json
│   │   ├── workspaces/
│   │   └── logs/
│   ├── demos/
│   │   ├── js_runtime_demo/          ← JS runtime demo 资产
│   │   └── config_demo/              ← Config demo 资产
│   └── scripts/                      ← 原 server_manager_demo/scripts
│       ├── run_demo.sh
│       └── api_smoke.sh
└── runtime_release/                  ← 组装后运行时（Release）
    └── ...（同上结构）
```

发布包布局（不变）:
```
release/stdiolink_xxx/
├── bin/
│   ├── stdiolink_server.exe
│   ├── stdiolink_service.exe
│   └── platforms/
├── data_root/
│   ├── drivers/
│   │   └── stdio.drv.*/
│   ├── services/
│   ├── projects/
│   └── config.json
└── start.bat
```

关键同构点：`runtime_*/` 的 `bin/` + `data_root/` 顶层结构与发布包完全一致。

### 3.2 CMake 变量与目录约定

引入两个全局 CMake 变量，所有子目录 CMakeLists 统一引用。

**约束：仅支持 Ninja 等单配置生成器**（项目当前使用 `-G "Ninja"`）。多配置生成器（VS、Ninja Multi-Config）的 `CMAKE_BUILD_TYPE` 为空，会导致路径语义错误。根 CMakeLists 中增加防护：

```cmake
# CMakeLists.txt (root)

# 防护：拒绝多配置生成器
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_is_multi_config)
    message(FATAL_ERROR "Multi-config generators are not supported. Use -G Ninja.")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" _build_type_lower)
if(_build_type_lower STREQUAL "")
    set(_build_type_lower "debug")
endif()

# 原始编译输出目录（仅编译产物，不参与运行）
# 使用普通变量而非 CACHE，避免切换构建类型后路径不更新
set(STDIOLINK_RAW_DIR "${CMAKE_BINARY_DIR}/${_build_type_lower}")

# 运行时组装目录（与发布包同构）
set(STDIOLINK_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime_${_build_type_lower}")
```

所有 target 的 `RUNTIME_OUTPUT_DIRECTORY` 统一改为 `${STDIOLINK_RAW_DIR}`：

Before:
```cmake
set_target_properties(stdiolink_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
```

After:
```cmake
set_target_properties(stdiolink_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
```

### 3.3 assemble_runtime 组装目标

新增 CMake custom target `assemble_runtime`，负责从原始输出目录组装运行时目录：

```cmake
# cmake/assemble_runtime.cmake

set(RUNTIME_BIN_DIR "${STDIOLINK_RUNTIME_DIR}/bin")
set(RUNTIME_DATA_ROOT "${STDIOLINK_RUNTIME_DIR}/data_root")
set(RUNTIME_DEMOS_DIR "${STDIOLINK_RUNTIME_DIR}/demos")
set(RUNTIME_SCRIPTS_DIR "${STDIOLINK_RUNTIME_DIR}/scripts")

add_custom_target(assemble_runtime ALL
    # ── 1. 创建目录骨架 ──
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DATA_ROOT}/drivers"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DATA_ROOT}/services"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DATA_ROOT}/projects"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DATA_ROOT}/workspaces"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DATA_ROOT}/logs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DEMOS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_SCRIPTS_DIR}"

    # ── 2. 复制核心二进制到 bin/ ──
    # （通过 CMake 脚本遍历，见 §4.3 详细实现）

    # ── 3. 复制驱动到 data_root/drivers/<name>/ ──
    # （通过 CMake 脚本遍历 stdio.drv.* 文件）

    # ── 4. 两层合并 services/projects ──
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/data_root"
        "${RUNTIME_DATA_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/demo/server_manager_demo/data_root"
        "${RUNTIME_DATA_ROOT}"

    # ── 5. 复制 demo 资产 ──
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/demo/js_runtime_demo"
        "${RUNTIME_DEMOS_DIR}/js_runtime_demo"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/demo/config_demo"
        "${RUNTIME_DEMOS_DIR}/config_demo"

    # ── 6. 复制 demo 脚本 ──
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/demo/server_manager_demo/scripts"
        "${RUNTIME_SCRIPTS_DIR}"

    COMMENT "Assembling runtime directory: ${STDIOLINK_RUNTIME_DIR}"
)
```

组装步骤 2 和 3 需要通过 CMake 脚本（`cmake -P`）实现文件遍历和条件复制，因为 `add_custom_target` 的 COMMAND 不支持循环逻辑。详见 §4.3。

### 3.4 驱动复制策略

驱动可执行文件在两个位置共存（开发环境特有）：

| 位置 | 用途 | 生命周期 |
|------|------|----------|
| `build/debug/stdio.drv.*.exe` | CMake 原始输出 | CMake 增量编译维护 |
| `build/runtime_debug/data_root/drivers/<name>/stdio.drv.*.exe` | 运行时使用 | `assemble_runtime` 每次构建时复制 |

设计决策：
- **不从原始目录删除驱动**：删除会破坏 CMake 增量编译（CMake 检测到输出文件缺失会重新编译）
- **`assemble_runtime` 每次构建时重新复制**：确保运行时目录始终与最新编译产物同步
- **`check_duplicates` 仅在发布打包时检查**：开发环境允许两份共存，发布包中 `bin/` 下不应有驱动

驱动识别规则：文件名匹配 `stdio.drv.*` 前缀（与 `DriverManagerScanner::findDriverExecutable` 和 `PlatformUtils::isDriverExecutableName` 一致）。

子目录命名：使用驱动文件名去掉平台后缀作为子目录名（如 `stdio.drv.calculator.exe` → `stdio.drv.calculator/`），与发布脚本逻辑一致。

### 3.5 publish_release 改造策略

当前 `publish_release` 脚本（`.ps1` / `.sh`）自行组装发布包：从 `build/bin/` 复制二进制、从源码树复制 data_root、手动创建驱动子目录。M89 后，`runtime_release/` 已经是同构布局，发布脚本只需：

1. 调用 `build.bat Release` / `build.sh Release`（触发 CMake 构建 + `assemble_runtime`）
2. 从 `runtime_release/` 整体复制到发布包目录
3. 补充发布专属内容（WebUI 构建、启动脚本、RELEASE_MANIFEST.txt）
4. 运行 `check_duplicates`

Before（当前发布脚本核心流程）:
```
build.bat Release → build/bin/
                     ↓
publish_release:  build/bin/ → 筛选二进制 → package/bin/
                  build/bin/stdio.drv.* → package/data_root/drivers/<name>/
                  src/data_root/ → package/data_root/  (第1层)
                  src/demo/.../data_root/ → package/data_root/  (第2层)
                  生成 config.json / start.bat / MANIFEST
```

After（改造后发布脚本核心流程）:
```
build.bat Release → build/release/ (raw) + build/runtime_release/ (assembled)
                     ↓
publish_release:  runtime_release/bin/ → package/bin/  (直接复制，已无驱动)
                  runtime_release/data_root/ → package/data_root/  (已含驱动+services+projects)
                  补充 WebUI、config.json、start.bat、MANIFEST
```

关键简化：
- 删除 `should_skip_binary` / `Should-SkipBinary` 中的驱动过滤逻辑（`runtime_release/bin/` 中已无驱动）
- 删除"从 bin/ 复制驱动到 drivers/"的循环（`runtime_release/data_root/drivers/` 已就绪）
- 删除两层 data_root 合并逻辑（`assemble_runtime` 已完成合并）
- 保留 WebUI 构建、测试执行、启动脚本生成、MANIFEST 生成、`check_duplicates` 校验

`--build-dir` 参数语义变更：
- Before: 指向构建根目录（ps1 默认 `build_release`，sh 默认 `build`），脚本内部拼接 `bin/` 子目录
- After: 指向构建根目录（默认值不变），脚本内部拼接 `runtime_release/` 子目录

跨平台默认值约定：
| 平台 | 脚本 | `--build-dir` 默认值 | runtime 路径 |
|------|------|---------------------|-------------|
| Windows | `publish_release.ps1` | `build_release` | `build_release/runtime_release/` |
| Unix | `publish_release.sh` | `build` | `build/runtime_release/` |

示例命令：
```bash
# Windows
tools\publish_release.ps1 --build-dir build_release --skip-tests
# Unix
tools/publish_release.sh --build-dir build --skip-tests
```

打包前强制清理：发布脚本在构建前删除 `runtime_release/` 目录，确保不残留旧文件。

### 3.6 构建脚本适配

`build.bat` 和 `build.sh` 本身不需要大改，因为目录结构变更由 CMake 变量控制。但需要确保：

1. `build.bat` 的 `cmake --build` 会触发 `assemble_runtime` 目标（因为 `ALL` 依赖）
2. 构建完成后的提示信息更新，指向 `runtime_*/` 而非 `build/bin/`

`build.bat` 变更：
```batch
:: Before
echo Build completed successfully!

:: After
echo Build completed successfully!
echo Runtime directory: %BUILD_DIR%\runtime_%BUILD_TYPE_LOWER%
```

### 3.7 测试执行路径适配

单元测试二进制 `stdiolink_tests` 输出到 `build/debug/`（原始目录），同时被 `assemble_runtime` 复制到 `runtime_debug/bin/`。

测试执行策略：
- `tools/run_tests.sh` / `.ps1` 中的 GTest 路径从 `build/bin/stdiolink_tests` 改为 `build/runtime_debug/bin/stdiolink_tests`（或根据构建类型动态选择）
- `publish_release` 中的测试执行路径同步更新
- 测试桩（`test_driver`）也复制到 `runtime_*/bin/`，因为部分集成测试依赖 `resolveDriver` 从 `appDir` 查找测试桩

路径矩阵：

| 场景 | 测试二进制路径 | data_root 路径 |
|------|---------------|---------------|
| 开发调试 | `build/runtime_debug/bin/stdiolink_tests` | `build/runtime_debug/data_root/` |
| 发布前测试 | `build/runtime_release/bin/stdiolink_tests` | `build/runtime_release/data_root/` |
| 发布包内测试 | `release/xxx/bin/stdiolink_tests` | `release/xxx/data_root/` |

三个场景下 `bin/` 与 `data_root/` 的相对位置完全一致（`../data_root/`），`resolveDriver` 的 tier-1 路径在所有场景下均可命中。

### 3.8 向后兼容与边界

- **`build/bin/` 目录不再存在**：所有依赖 `build/bin/` 路径的外部脚本或 IDE 配置需更新
- **CMake 缓存失效**：首次切换到新布局时需清理 `build/` 目录重新配置（`CMakeCache.txt` 中的旧路径会导致冲突）
- **Qt 插件部署**：`windeployqt` 或手动复制的 Qt 插件目录（`platforms/`、`tls/` 等）需复制到 `runtime_*/bin/` 下，而非原始输出目录
- **IDE 调试配置**：VS Code `launch.json`、CLion Run Configuration 等需将工作目录和可执行文件路径指向 `runtime_*/`
- **`server_manager_demo` 目录概念删除**：`src/demo/server_manager_demo/` 源码目录保留（其中的 `data_root/` 和 `scripts/` 仍是源素材），但构建输出中不再有 `server_manager_demo/` 子目录

## 4. 实现步骤

### 4.1 根 CMakeLists.txt：引入全局目录变量

- 修改 `CMakeLists.txt`（根目录）：
  - `cmake_minimum_required` 从 `VERSION 3.20` 提升到 `VERSION 3.21`（`file(COPY_FILE)` 需要 3.21+）
  - 在 `project(stdiolink)` 之后、`find_package` 之前，新增全局目录变量：
    ```cmake
    # ── 目录布局变量 ──
    # 防护：拒绝多配置生成器
    get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_is_multi_config)
        message(FATAL_ERROR "Multi-config generators are not supported. Use -G Ninja.")
    endif()

    string(TOLOWER "${CMAKE_BUILD_TYPE}" _build_type_lower)
    if(_build_type_lower STREQUAL "")
        set(_build_type_lower "debug")
    endif()

    # 使用普通变量，避免 CACHE 导致切换构建类型后路径不更新
    set(STDIOLINK_RAW_DIR "${CMAKE_BINARY_DIR}/${_build_type_lower}")
    set(STDIOLINK_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime_${_build_type_lower}")
    ```
  - 在 `add_subdirectory(src)` 之后，引入组装脚本：
    ```cmake
    include(cmake/assemble_runtime.cmake)
    ```
  - 改动理由：统一所有子目录 CMakeLists 的输出路径引用，消除硬编码 `${CMAKE_BINARY_DIR}/bin`
  - 验收：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` 后，`STDIOLINK_RAW_DIR` = `build/debug`，`STDIOLINK_RUNTIME_DIR` = `build/runtime_debug`

### 4.2 所有 target 的 RUNTIME_OUTPUT_DIRECTORY 替换

涉及 26 处 `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"` 替换为 `"${STDIOLINK_RAW_DIR}"`。

- 修改 `src/stdiolink_server/CMakeLists.txt`（L80）：
  ```cmake
  # Before
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  # After
  RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
  ```

- 修改 `src/stdiolink_service/CMakeLists.txt`（L109）：同上替换

- 修改 `src/tests/CMakeLists.txt`（L178, L195, L204, L213, L224, L235, L246, L257, L268, L279）：
  - 共 10 处，全部替换为 `"${STDIOLINK_RAW_DIR}"`
  - 包含 `stdiolink_tests` 主测试二进制和各 `test_*` 测试桩

- 修改 `src/demo/calculator_driver/CMakeLists.txt`（L5）：同上替换
- 修改 `src/demo/device_simulator_driver/CMakeLists.txt`（L5）：同上替换
- 修改 `src/demo/file_processor_driver/CMakeLists.txt`（L5）：同上替换
- 修改 `src/demo/heartbeat_driver/CMakeLists.txt`（L5）：同上替换
- 修改 `src/demo/kv_store_driver/CMakeLists.txt`（L5）：同上替换
- 修改 `src/demo/demo_host/CMakeLists.txt`（L4）：同上替换

- 修改 `src/drivers/` 下所有驱动 CMakeLists.txt（共 8 个文件）：
  - `driver_modbustcp/CMakeLists.txt`（L36）
  - `driver_modbusrtu/CMakeLists.txt`（L36）
  - `driver_modbusrtu_serial/CMakeLists.txt`（L35）
  - `driver_modbustcp_server/CMakeLists.txt`（L35）
  - `driver_modbusrtu_server/CMakeLists.txt`（L35）
  - `driver_modbusrtu_serial_server/CMakeLists.txt`（L35）
  - `driver_plc_crane/CMakeLists.txt`（L35）
  - `driver_3dvision/CMakeLists.txt`（L15）
  - 全部替换为 `"${STDIOLINK_RAW_DIR}"`

- 改动理由：将 CMake 原始输出从 `build/bin/` 迁移到 `build/debug/` 或 `build/release/`
- 验收：构建后 `build/debug/` 下有所有 `.exe`，`build/bin/` 不再存在

### 4.3 新增 cmake/assemble_runtime.cmake

新增文件 `cmake/assemble_runtime.cmake`，实现运行时目录组装。

核心难点：`add_custom_target` 的 COMMAND 不支持循环逻辑，需要通过 `cmake -P` 调用外部脚本实现文件遍历。拆分为两个文件：

**文件 1: `cmake/assemble_runtime.cmake`**（CMake include 文件，定义 target）

```cmake
# cmake/assemble_runtime.cmake
# 组装运行时目录，使其与发布包同构

set(RUNTIME_BIN_DIR "${STDIOLINK_RUNTIME_DIR}/bin")
set(RUNTIME_DATA_ROOT "${STDIOLINK_RUNTIME_DIR}/data_root")
set(RUNTIME_DEMOS_DIR "${STDIOLINK_RUNTIME_DIR}/demos")
set(RUNTIME_SCRIPTS_DIR "${STDIOLINK_RUNTIME_DIR}/scripts")

add_custom_target(assemble_runtime ALL
    COMMAND ${CMAKE_COMMAND}
        -DRAW_DIR="${STDIOLINK_RAW_DIR}"
        -DRUNTIME_DIR="${STDIOLINK_RUNTIME_DIR}"
        -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/assemble_runtime_impl.cmake"
    COMMENT "Assembling runtime directory: ${STDIOLINK_RUNTIME_DIR}"
)

# assemble_runtime 依赖所有可执行 target，确保编译完成后再组装
# 使用全局属性自动收集，避免硬编码清单遗漏新增目标
# 各子目录 CMakeLists 中通过 set_property(GLOBAL APPEND ...) 注册
get_property(_all_exe_targets GLOBAL PROPERTY STDIOLINK_EXECUTABLE_TARGETS)
foreach(_target IN LISTS _all_exe_targets)
    if(TARGET ${_target})
        add_dependencies(assemble_runtime ${_target})
    endif()
endforeach()
```

各子目录 CMakeLists 在定义可执行 target 后，需追加注册：

```cmake
# 示例：src/stdiolink_server/CMakeLists.txt
add_executable(stdiolink_server ...)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS stdiolink_server)
```

若团队认为全局属性方案过重，可保留硬编码清单但增加校验：在 `assemble_runtime_impl.cmake` 末尾检查 `RAW_DIR` 中所有可执行文件是否都已被复制到 `RUNTIME_BIN` 或 `RUNTIME_DATA_ROOT/drivers/`，未覆盖则 `message(WARNING)`。

**文件 2: `cmake/assemble_runtime_impl.cmake`**（`cmake -P` 脚本，执行实际组装）

```cmake
# cmake/assemble_runtime_impl.cmake
# 由 assemble_runtime target 通过 cmake -P 调用
# 输入变量: RAW_DIR, RUNTIME_DIR, SOURCE_DIR

set(RUNTIME_BIN "${RUNTIME_DIR}/bin")
set(RUNTIME_DATA_ROOT "${RUNTIME_DIR}/data_root")
set(RUNTIME_DEMOS "${RUNTIME_DIR}/demos")
set(RUNTIME_SCRIPTS "${RUNTIME_DIR}/scripts")

# ── 0. 清理旧 runtime 目录，防止残留脏文件 ──
if(IS_DIRECTORY "${RUNTIME_DIR}")
    file(REMOVE_RECURSE "${RUNTIME_DIR}")
    message(STATUS "Cleaned stale runtime directory: ${RUNTIME_DIR}")
endif()

# ── 1. 创建目录骨架 ──
foreach(_dir
    "${RUNTIME_BIN}"
    "${RUNTIME_DATA_ROOT}/drivers"
    "${RUNTIME_DATA_ROOT}/services"
    "${RUNTIME_DATA_ROOT}/projects"
    "${RUNTIME_DATA_ROOT}/workspaces"
    "${RUNTIME_DATA_ROOT}/logs"
    "${RUNTIME_DEMOS}"
    "${RUNTIME_SCRIPTS}")
    file(MAKE_DIRECTORY "${_dir}")
endforeach()

# ── 2. 复制核心二进制到 bin/（排除驱动） ──
file(GLOB _raw_files "${RAW_DIR}/*")
foreach(_file IN LISTS _raw_files)
    if(IS_DIRECTORY "${_file}")
        continue()
    endif()
    get_filename_component(_name "${_file}" NAME)
    # 驱动文件不复制到 bin/，后续步骤 3 处理
    if(_name MATCHES "^stdio\\.drv\\.")
        continue()
    endif()
    file(COPY_FILE "${_file}" "${RUNTIME_BIN}/${_name}"
         RESULT _copy_result)
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy ${_name} to runtime/bin/: ${_copy_result}")
    endif()
endforeach()

# ── 3. 复制驱动到 data_root/drivers/<name>/ ──
file(GLOB _driver_files "${RAW_DIR}/stdio.drv.*")
foreach(_file IN LISTS _driver_files)
    get_filename_component(_name "${_file}" NAME)
    # 去掉平台后缀得到子目录名: stdio.drv.calculator.exe → stdio.drv.calculator
    string(REGEX REPLACE "\\.(exe|app)$" "" _drv_name "${_name}")
    set(_drv_dir "${RUNTIME_DATA_ROOT}/drivers/${_drv_name}")
    file(MAKE_DIRECTORY "${_drv_dir}")
    file(COPY_FILE "${_file}" "${_drv_dir}/${_name}"
         RESULT _copy_result)
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy driver ${_name}: ${_copy_result}")
    endif()
endforeach()

# ── 4. 复制 Qt 插件子目录到 bin/ ──
file(GLOB _raw_subdirs "${RAW_DIR}/*/")
foreach(_subdir IN LISTS _raw_subdirs)
    get_filename_component(_dirname "${_subdir}" NAME)
    # 跳过非 Qt 插件目录（如果有）
    file(COPY "${_subdir}" DESTINATION "${RUNTIME_BIN}")
endforeach()

# ── 5. 两层合并 data_root（production + demo） ──
set(_prod_data "${SOURCE_DIR}/src/data_root")
set(_demo_data "${SOURCE_DIR}/src/demo/server_manager_demo/data_root")
if(IS_DIRECTORY "${_prod_data}")
    file(COPY "${_prod_data}/" DESTINATION "${RUNTIME_DATA_ROOT}")
endif()
if(IS_DIRECTORY "${_demo_data}")
    file(COPY "${_demo_data}/" DESTINATION "${RUNTIME_DATA_ROOT}")
endif()

# ── 6. 复制 demo 资产 ──
set(_js_demo "${SOURCE_DIR}/src/demo/js_runtime_demo")
set(_cfg_demo "${SOURCE_DIR}/src/demo/config_demo")
if(IS_DIRECTORY "${_js_demo}")
    file(COPY "${_js_demo}/" DESTINATION "${RUNTIME_DEMOS}/js_runtime_demo")
endif()
if(IS_DIRECTORY "${_cfg_demo}")
    file(COPY "${_cfg_demo}/" DESTINATION "${RUNTIME_DEMOS}/config_demo")
endif()

# ── 7. 复制 demo 脚本 ──
set(_demo_scripts "${SOURCE_DIR}/src/demo/server_manager_demo/scripts")
if(IS_DIRECTORY "${_demo_scripts}")
    file(COPY "${_demo_scripts}/" DESTINATION "${RUNTIME_SCRIPTS}")
endif()

message(STATUS "Runtime assembled: ${RUNTIME_DIR}")
```

- 改动理由：`file(GLOB)` + `foreach` 实现驱动文件遍历和条件复制，`file(COPY_FILE)` 实现单文件复制（CMake 3.21+）
- 验收：构建后 `runtime_debug/data_root/drivers/` 下有各驱动子目录，每个子目录含对应可执行文件

### 4.4 删除旧 demo CMake 资产复制目标

`assemble_runtime` 已接管所有资产复制职责，旧的 demo 资产复制目标需删除或重构。

- 修改 `src/demo/server_manager_demo/CMakeLists.txt`：
  - 删除 `server_manager_demo_assets` custom target 及其全部依赖声明
  - 删除 `SERVER_MANAGER_DEMO_DST_DIR` 变量
  - 删除源层重叠检测逻辑（迁移到 `cmake/assemble_runtime.cmake` 中）
  - 文件仅保留空壳或完全删除（若无其他内容）
  ```cmake
  # src/demo/server_manager_demo/CMakeLists.txt
  # 资产复制已由 cmake/assemble_runtime.cmake 统一处理
  # 本文件保留为占位，源素材目录 data_root/ 和 scripts/ 由 assemble_runtime 引用
  ```

- 修改 `src/demo/js_runtime_demo/CMakeLists.txt`：
  - 删除 `js_runtime_demo_assets` custom target
  - 删除 `JS_RUNTIME_DEMO_DST_DIR` 变量
  ```cmake
  # src/demo/js_runtime_demo/CMakeLists.txt
  # 资产复制已由 cmake/assemble_runtime.cmake 统一处理
  ```

- 修改 `src/demo/config_demo/CMakeLists.txt`：
  - 删除 `config_demo_assets` custom target
  - 删除 `CONFIG_DEMO_DST_DIR` 变量
  ```cmake
  # src/demo/config_demo/CMakeLists.txt
  # 资产复制已由 cmake/assemble_runtime.cmake 统一处理
  ```

- 改动理由：消除重复的资产复制逻辑，统一由 `assemble_runtime` 管理
- 验收：构建不报错，`runtime_*/demos/` 下有 `js_runtime_demo/` 和 `config_demo/`

### 4.5 publish_release 脚本改造

**修改 `tools/publish_release.ps1`：**

- `$binDir` 变量从 `Join-Path $buildDirAbs "bin"` 改为 `Join-Path $buildDirAbs "runtime_release"`：
  ```powershell
  # Before
  $binDir = Join-Path $buildDirAbs "bin"
  # After
  $runtimeDir = Join-Path $buildDirAbs "runtime_release"
  ```

- 删除 `Should-SkipBinary` 函数中的驱动过滤逻辑（`runtime_release/bin/` 中已无驱动）

- 二进制复制段改为从 `$runtimeDir/bin` 复制：
  ```powershell
  $runtimeBinDir = Join-Path $runtimeDir "bin"
  $binFiles = Get-ChildItem -LiteralPath $runtimeBinDir -File
  foreach ($file in $binFiles) {
      if (Should-SkipBinary -Name $file.Name -WithTests $withTests) { continue }
      Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $packageDir "bin") -Force
  }
  ```

- Qt 插件复制段改为从 `$runtimeDir/bin` 的子目录复制（不再排除 demo 目录）：
  ```powershell
  $pluginDirs = Get-ChildItem -LiteralPath $runtimeBinDir -Directory
  ```

- 删除两层 data_root 合并逻辑（"Seeding production data" + "Seeding demo data" 两段），替换为整体复制：
  ```powershell
  Write-Host "Copying data_root from runtime..."
  $runtimeDataRoot = Join-Path $runtimeDir "data_root"
  Copy-Item -Path (Join-Path $runtimeDataRoot "*") -Destination (Join-Path $packageDir "data_root") -Recurse -Force
  ```

- 删除"Copying drivers into data_root/drivers"循环（`runtime_release/data_root/drivers/` 已就绪）

- 新增打包前清理 `runtime_release/`：
  ```powershell
  if (-not $skipBuild) {
      # 清理旧 runtime 目录，确保不残留
      $runtimeClean = Join-Path $buildDirAbs "runtime_release"
      if (Test-Path -LiteralPath $runtimeClean) {
          Remove-Item -LiteralPath $runtimeClean -Recurse -Force
      }
      # ... 调用 build.bat Release ...
  }
  ```

**修改 `tools/publish_release.sh`：**

- 同上逻辑的 bash 版本，关键变更：
  ```bash
  # Before
  BIN_DIR="${BUILD_DIR_ABS}/bin"
  # After
  RUNTIME_DIR="${BUILD_DIR_ABS}/runtime_release"
  ```

- 删除驱动复制循环、两层 data_root 合并、`SKIP_DIRS` 过滤
- 替换为从 `$RUNTIME_DIR` 整体复制

- 改动理由：发布脚本不再自行组装，直接使用 `assemble_runtime` 的输出
- 验收：`publish_release` 生成的发布包结构与改造前完全一致

### 4.6 run_demo.sh 适配

- 修改 `src/demo/server_manager_demo/scripts/run_demo.sh`：
  - `find_bin_dir()` 改为 `find_runtime_dir()`，优先从脚本自身位置推断 runtime 根目录（脚本已被复制到 `runtime_*/scripts/`），再回退到源码树相对路径：
    ```bash
    find_runtime_dir() {
        local candidate

        # 优先：脚本位于 runtime_*/scripts/ 下，SCRIPT_DIR/.. 即 runtime 根
        candidate="$(cd "${SCRIPT_DIR}/.." 2>/dev/null && pwd || true)"
        if [[ -n "${candidate}" && -x "${candidate}/bin/stdiolink_server${EXE_SUFFIX}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi

        # 回退：从源码树位置查找 build/runtime_*/
        for _type in debug release; do
            candidate="$(cd "${SCRIPT_DIR}/../../../../build/runtime_${_type}" 2>/dev/null && pwd || true)"
            if [[ -n "${candidate}" && -x "${candidate}/bin/stdiolink_server${EXE_SUFFIX}" ]]; then
                printf '%s\n' "${candidate}"
                return 0
            fi
        done
        return 1
    }
    ```
  - 删除 `prepare_demo_drivers()` 和 `copy_one_driver()` 函数（驱动已由 `assemble_runtime` 部署）
  - `DATA_ROOT` 改为从 runtime 目录获取：
    ```bash
    RUNTIME_DIR="$(find_runtime_dir || true)"
    DATA_ROOT="${RUNTIME_DIR}/data_root"
    SERVER_BIN="${RUNTIME_DIR}/bin/stdiolink_server${EXE_SUFFIX}"
    ```
  - 删除 `prepare_demo_drivers "${BIN_DIR}"` 调用

- 改动理由：`runtime_*/data_root/drivers/` 已包含驱动，无需运行时补丁
- 验收：`run_demo.sh` 启动后 `GET /api/drivers` 返回非空驱动列表

### 4.7 测试脚本路径适配

- 修改 `tools/run_tests.sh`：
  - GTest 二进制路径从 `build/bin/stdiolink_tests` 改为 `build/runtime_debug/bin/stdiolink_tests`：
    ```bash
    # Before
    GTEST_BIN="${BUILD_DIR}/bin/stdiolink_tests"
    # After
    GTEST_BIN="${BUILD_DIR}/runtime_debug/bin/stdiolink_tests"
    ```
  - 若支持 Release 模式测试，增加构建类型参数或自动探测

- 修改 `tools/run_tests.ps1`：
  - 同上逻辑的 PowerShell 版本

- 改动理由：测试二进制已迁移到 `runtime_*/bin/`
- 验收：`tools/run_tests.sh --gtest` 正常执行并通过

### 4.8 源层重叠检测迁移

当前 `src/demo/server_manager_demo/CMakeLists.txt` 中的 production/demo 重叠检测逻辑需迁移到 `cmake/assemble_runtime.cmake`：

```cmake
# cmake/assemble_runtime.cmake 中新增（在 add_custom_target 之前）
foreach(_subdir services projects)
    set(_prod_dir "${CMAKE_SOURCE_DIR}/src/data_root/${_subdir}")
    set(_demo_dir "${CMAKE_SOURCE_DIR}/src/demo/server_manager_demo/data_root/${_subdir}")
    if(IS_DIRECTORY "${_prod_dir}" AND IS_DIRECTORY "${_demo_dir}")
        file(GLOB _prod_entries RELATIVE "${_prod_dir}" "${_prod_dir}/*")
        file(GLOB _demo_entries RELATIVE "${_demo_dir}" "${_demo_dir}/*")
        set(_overlap "")
        foreach(_entry IN LISTS _prod_entries)
            if("${_entry}" IN_LIST _demo_entries)
                list(APPEND _overlap "${_entry}")
            endif()
        endforeach()
        if(_overlap)
            message(WARNING "Production and demo data_root/${_subdir}/ overlap: ${_overlap}")
        endif()
    endif()
endforeach()
```

- 改动理由：重叠检测逻辑随资产复制职责一起迁移
- 验收：若 production 和 demo 有同名 service/project，CMake configure 阶段输出 WARNING

## 5. 文件变更清单

### 5.1 新增文件

- `cmake/assemble_runtime.cmake` — 定义 `assemble_runtime` CMake target，声明依赖关系和源层重叠检测
- `cmake/assemble_runtime_impl.cmake` — `cmake -P` 脚本，执行运行时目录组装（文件遍历、驱动分发、资产合并）

### 5.2 修改文件

- `CMakeLists.txt`（根目录）— 新增 `STDIOLINK_RAW_DIR` / `STDIOLINK_RUNTIME_DIR` 变量，`include(cmake/assemble_runtime.cmake)`
- `src/stdiolink_server/CMakeLists.txt` — `RUNTIME_OUTPUT_DIRECTORY` → `${STDIOLINK_RAW_DIR}`
- `src/stdiolink_service/CMakeLists.txt` — 同上
- `src/tests/CMakeLists.txt` — 10 处 `RUNTIME_OUTPUT_DIRECTORY` 替换
- `src/demo/calculator_driver/CMakeLists.txt` — 同上
- `src/demo/device_simulator_driver/CMakeLists.txt` — 同上
- `src/demo/file_processor_driver/CMakeLists.txt` — 同上
- `src/demo/heartbeat_driver/CMakeLists.txt` — 同上
- `src/demo/kv_store_driver/CMakeLists.txt` — 同上
- `src/demo/demo_host/CMakeLists.txt` — 同上
- `src/drivers/driver_modbustcp/CMakeLists.txt` — 同上
- `src/drivers/driver_modbusrtu/CMakeLists.txt` — 同上
- `src/drivers/driver_modbusrtu_serial/CMakeLists.txt` — 同上
- `src/drivers/driver_modbustcp_server/CMakeLists.txt` — 同上
- `src/drivers/driver_modbusrtu_server/CMakeLists.txt` — 同上
- `src/drivers/driver_modbusrtu_serial_server/CMakeLists.txt` — 同上
- `src/drivers/driver_plc_crane/CMakeLists.txt` — 同上
- `src/drivers/driver_3dvision/CMakeLists.txt` — 同上
- `src/demo/server_manager_demo/CMakeLists.txt` — 删除 `server_manager_demo_assets` target
- `src/demo/js_runtime_demo/CMakeLists.txt` — 删除 `js_runtime_demo_assets` target
- `src/demo/config_demo/CMakeLists.txt` — 删除 `config_demo_assets` target
- `src/demo/server_manager_demo/scripts/run_demo.sh` — 适配 `runtime_*/` 路径，删除 `prepare_demo_drivers`
- `tools/publish_release.ps1` — 改为从 `runtime_release/` 打包
- `tools/publish_release.sh` — 同上 bash 版本
- `tools/run_tests.sh` — GTest 路径适配
- `tools/run_tests.ps1` — 同上 PowerShell 版本
- `build.bat` — 构建完成提示信息更新

### 5.3 测试文件

- `tools/validate_m89_layout.sh` — M89 静态校验脚本（bash）
- `tools/validate_m89_layout.ps1` — M89 静态校验脚本（PowerShell）

## 6. 测试与验收

### 6.1 单元测试（静态校验脚本）

本里程碑的变更对象是构建系统和脚本，不涉及 C++ 业务代码，因此"单元测试"以静态校验脚本形式实现，验证构建产物的目录结构正确性。

- 测试对象：构建后 `runtime_debug/` 目录结构、发布包目录结构
- 用例分层：正常路径（目录存在、文件就位）、边界值（空驱动列表）、异常输入（缺失源目录）
- 测试文件：`tools/validate_m89_layout.sh` / `tools/validate_m89_layout.ps1`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| CMake 原始输出目录 | `build/debug/` 存在且含 `stdiolink_server.exe` | T01 |
| CMake 原始输出目录 | `build/bin/` 不存在 | T02 |
| runtime 目录骨架 | `runtime_debug/bin/` 存在 | T03 |
| runtime 目录骨架 | `runtime_debug/data_root/` 存在 | T03 |
| 核心二进制复制 | `runtime_debug/bin/stdiolink_server.exe` 存在 | T04 |
| 核心二进制复制 | `runtime_debug/bin/` 下无 `stdio.drv.*` 文件 | T05 |
| 驱动子目录组织 | `runtime_debug/data_root/drivers/stdio.drv.calculator/stdio.drv.calculator.exe` 存在 | T06 |
| 驱动子目录组织 | 每个 `stdio.drv.*` 在独立子目录中 | T06 |
| services 两层合并 | `runtime_debug/data_root/services/` 含 production + demo services | T07 |
| projects 两层合并 | `runtime_debug/data_root/projects/` 含 production + demo projects | T08 |
| demo 资产复制 | `runtime_debug/demos/js_runtime_demo/` 存在 | T09 |
| demo 脚本复制 | `runtime_debug/scripts/run_demo.sh` 存在 | T10 |
| Qt 插件复制 | `runtime_debug/bin/platforms/` 存在 | T11 |
| 同构验证 | `runtime_*/` 顶层结构与发布包一致 | T12 |

覆盖要求：所有可达路径 100% 有用例。

#### 用例详情

**T01 — CMake 原始输出目录存在**
- 前置条件：执行 `build.bat Debug` 或 `build.sh Debug` 完成
- 输入：检查 `build/debug/` 目录
- 预期：目录存在，且包含 `stdiolink_server.exe`、`stdiolink_service.exe`、`stdiolink_tests.exe`
- 断言：`[ -d build/debug ] && [ -f build/debug/stdiolink_server* ]`

**T02 — 旧 build/bin/ 目录不存在**
- 前置条件：清理 build 目录后执行全新构建
- 输入：检查 `build/bin/` 目录
- 预期：目录不存在
- 断言：`[ ! -d build/bin ]`

**T03 — runtime 目录骨架完整**
- 前置条件：构建完成（`assemble_runtime` 已执行）
- 输入：检查 `build/runtime_debug/` 目录结构
- 预期：`bin/`、`data_root/`、`data_root/drivers/`、`data_root/services/`、`data_root/projects/`、`data_root/workspaces/`、`data_root/logs/`、`demos/`、`scripts/` 均存在
- 断言：逐一检查目录存在性

**T04 — 核心二进制已复制到 runtime bin/**
- 前置条件：构建完成
- 输入：检查 `build/runtime_debug/bin/`
- 预期：包含 `stdiolink_server.exe`、`stdiolink_service.exe`、`stdiolink_tests.exe`
- 断言：`[ -f runtime_debug/bin/stdiolink_server* ]`

**T05 — runtime bin/ 下无驱动文件**
- 前置条件：构建完成
- 输入：在 `build/runtime_debug/bin/` 中搜索 `stdio.drv.*`
- 预期：无匹配文件
- 断言：`ls runtime_debug/bin/stdio.drv.* 2>/dev/null` 返回空

**T06 — 驱动按子目录组织**
- 前置条件：构建完成
- 输入：检查 `build/runtime_debug/data_root/drivers/`
- 预期：每个 `stdio.drv.*` 驱动在独立子目录中，如 `stdio.drv.calculator/stdio.drv.calculator.exe`
- 断言：遍历 `build/debug/stdio.drv.*`，对每个驱动验证对应子目录和文件存在

**T07 — services 两层合并正确**
- 前置条件：构建完成
- 输入：检查 `build/runtime_debug/data_root/services/`
- 预期：同时包含 production services（如 `modbustcp_server_service`）和 demo services（如 `driver_pipeline_service`）
- 断言：`[ -d runtime_debug/data_root/services/modbustcp_server_service ] && [ -d runtime_debug/data_root/services/driver_pipeline_service ]`

**T08 — projects 两层合并正确**
- 前置条件：构建完成
- 输入：检查 `build/runtime_debug/data_root/projects/`
- 预期：同时包含 production projects 和 demo projects
- 断言：`[ -f runtime_debug/data_root/projects/manual_modbustcp_server.json ]`

**T09 — demo 资产已复制**
- 前置条件：构建完成
- 输入：检查 `build/runtime_debug/demos/`
- 预期：`js_runtime_demo/` 和 `config_demo/` 子目录存在且非空
- 断言：`[ -d runtime_debug/demos/js_runtime_demo/services ] && [ -d runtime_debug/demos/config_demo/services ]`

**T10 — demo 脚本已复制**
- 前置条件：构建完成
- 输入：检查 `build/runtime_debug/scripts/`
- 预期：`run_demo.sh` 和 `api_smoke.sh` 存在
- 断言：`[ -f runtime_debug/scripts/run_demo.sh ] && [ -f runtime_debug/scripts/api_smoke.sh ]`

**T11 — Qt 插件目录已复制**
- 前置条件：构建完成（Windows 环境）
- 输入：检查 `build/runtime_debug/bin/platforms/`
- 预期：目录存在且包含 Qt 平台插件 DLL
- 断言：`[ -d runtime_debug/bin/platforms ]`

**T12 — runtime 与发布包同构验证**
- 前置条件：构建完成 + 执行 `publish_release`
- 输入：对比 `runtime_release/` 与 `release/xxx/` 的目录树结构
- 预期：两者均包含 `bin/` + `data_root/`，`data_root/drivers/` 下驱动子目录一致，`bin/` 下均无 `stdio.drv.*`
- 断言：生成两侧目录树（排除日志/临时文件），对比 `bin/` 下文件名集合一致、`data_root/drivers/` 下子目录名集合一致；或使用 `diff <(tree runtime_release -I logs) <(tree release/xxx -I logs)` 进行结构 diff

#### 测试代码

```bash
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

# T11 — Qt 插件（仅 Windows 检查）
echo "T11: checking Qt plugins..."
if [[ "${OS:-}" == "Windows_NT" ]]; then
    [ -d "${RUNTIME_DIR}/bin/platforms" ] || fail "platforms/ missing"
    echo "OK: Qt plugins present"
else
    echo "SKIP: Qt plugin check (non-Windows)"
fi

# T12 — 同构验证（runtime vs 发布包结构 diff）
echo "T12: checking isomorphic layout..."
RELEASE_DIR="${3:-}"
if [[ -z "${RELEASE_DIR}" ]]; then
    echo "T12: no release dir provided (arg3), checking runtime self-consistency only"
    [ -d "${RUNTIME_DIR}/bin" ] || fail "runtime bin/ missing"
    [ -d "${RUNTIME_DIR}/data_root" ] || fail "runtime data_root/ missing"
    if ls "${RUNTIME_DIR}/bin/stdio.drv."* 2>/dev/null | grep -q .; then
        fail "found stdio.drv.* in runtime/bin/ (isomorphic violation)"
    fi
    DRIVER_SUBDIRS=$(ls -d "${RUNTIME_DIR}/data_root/drivers/"*/ 2>/dev/null | wc -l)
    [ "${DRIVER_SUBDIRS}" -gt 0 ] || fail "data_root/drivers/ has no subdirectories"
    echo "OK: runtime self-consistent (${DRIVER_SUBDIRS} driver subdirs)"
else
    # 对比 runtime 与发布包的 bin/ 文件名集合
    RUNTIME_BINS=$(ls "${RUNTIME_DIR}/bin/" 2>/dev/null | sort)
    RELEASE_BINS=$(ls "${RELEASE_DIR}/bin/" 2>/dev/null | sort)
    if [[ "${RUNTIME_BINS}" != "${RELEASE_BINS}" ]]; then
        echo "runtime bin/: ${RUNTIME_BINS}"
        echo "release bin/: ${RELEASE_BINS}"
        fail "bin/ file sets differ between runtime and release"
    fi
    # 对比 drivers/ 子目录名集合
    RUNTIME_DRVS=$(ls "${RUNTIME_DIR}/data_root/drivers/" 2>/dev/null | sort)
    RELEASE_DRVS=$(ls "${RELEASE_DIR}/data_root/drivers/" 2>/dev/null | sort)
    if [[ "${RUNTIME_DRVS}" != "${RELEASE_DRVS}" ]]; then
        echo "runtime drivers/: ${RUNTIME_DRVS}"
        echo "release drivers/: ${RELEASE_DRVS}"
        fail "data_root/drivers/ subdirectory sets differ"
    fi
    # 验证发布包 bin/ 下无驱动
    if ls "${RELEASE_DIR}/bin/stdio.drv."* 2>/dev/null | grep -q .; then
        fail "found stdio.drv.* in release bin/"
    fi
    echo "OK: isomorphic layout verified (runtime == release)"
fi

echo ""
echo "All M89 layout validations passed."
```

### 6.2 集成/端到端测试

本里程碑涉及构建系统与发布链路的跨模块交互，需以下集成验证：

- **构建链路 E2E**：执行 `build.bat Debug`，验证 `build/debug/` 和 `build/runtime_debug/` 均正确生成，运行 `validate_m89_layout.sh`
- **发布链路 E2E**：执行 `publish_release.ps1 --skip-tests --skip-webui`，验证发布包结构与改造前一致（对比 `check_duplicates` 输出）
- **DriverManagerScanner 功能验证**：在 `runtime_debug/` 下启动 `stdiolink_server`，调用 `GET /api/drivers`，验证返回非空驱动列表
- **resolveDriver tier-1 验证**：在 `runtime_debug/` 下启动 service，验证 `resolveDriver` 从 `data_root/drivers/` 解析驱动。可观测判据：`GET /api/drivers` 返回的驱动列表中每个驱动的 `path` 字段包含 `data_root/drivers/` 路径片段（而非 `bin/` 平铺路径），证明 tier-1 命中而非 tier-2 回退
- **既有 GTest 回归**：`runtime_debug/bin/stdiolink_tests` 全量通过

### 6.3 验收标准

- [ ] `build.bat Debug` 后 `build/debug/` 含所有编译产物，`build/bin/` 不存在（T01, T02）
- [ ] `build/runtime_debug/` 目录骨架完整（T03）
- [ ] `runtime_debug/bin/` 含核心二进制，不含驱动（T04, T05）
- [ ] `runtime_debug/data_root/drivers/` 下每个驱动在独立子目录中（T06）
- [ ] `runtime_debug/data_root/services/` 含 production + demo services（T07）
- [ ] `runtime_debug/data_root/projects/` 含 production + demo projects（T08）
- [ ] `runtime_debug/demos/` 和 `scripts/` 含 demo 资产和脚本（T09, T10）
- [ ] `publish_release` 生成的发布包通过 `check_duplicates` 校验（T12）
- [ ] `stdiolink_server --data-root=runtime_debug/data_root` 启动后 `GET /api/drivers` 返回非空列表
- [ ] `stdiolink_tests` 全量通过，无回归
- [ ] `validate_m89_layout.sh` 全部 T01-T12 通过

## 7. 风险与控制

- 风险：CMake 缓存残留导致构建失败（旧 `build/bin/` 路径硬编码在 `CMakeCache.txt` 中）
  - 控制：文档中明确要求首次切换时清理 `build/` 目录（`rm -rf build && build.bat Debug`）
  - 控制：`assemble_runtime_impl.cmake` 开头检查 `RAW_DIR` 是否存在，不存在时给出明确错误信息
  - 测试覆盖：T01（验证原始输出目录正确）

- 风险：`file(COPY_FILE)` 需要 CMake 3.21+，当前仓库要求 3.20
  - 控制：将 `cmake_minimum_required(VERSION 3.20)` 提升到 `cmake_minimum_required(VERSION 3.21)`，在 PR 描述和 CLAUDE.md 中明确说明版本要求变更
  - 控制：CI/CD 环境确认 CMake 版本 ≥ 3.21（主流发行版和 vcpkg 均已满足）

- 风险：`assemble_runtime` 每次构建都全量复制，增量构建变慢
  - 控制：使用 `file(COPY_FILE)` 的 `ONLY_IF_DIFFERENT` 语义（CMake 3.21 默认行为），仅在文件内容变化时实际写入
  - 控制：监控增量构建耗时，若超过 5 秒考虑改用 `cmake -E copy_if_different` 逐文件复制
  - 测试覆盖：手动测试增量构建场景

- 风险：发布脚本改造后遗漏文件，导致发布包不完整
  - 控制：`check_duplicates.ps1` / `.sh` 继续作为发布后校验门禁
  - 控制：T12 验证 runtime 与发布包同构
  - 测试覆盖：T12

- 风险：`run_demo.sh` 路径探测逻辑在不同执行位置下失效
  - 控制：`find_runtime_dir()` 支持多个候选路径（debug/release），并给出明确错误提示
  - 测试覆盖：T10（脚本存在性）+ 手动执行验证

## 8. 里程碑完成定义（DoD）

- [ ] `cmake_minimum_required` 提升到 `VERSION 3.21`
- [ ] 所有 CMakeLists.txt 中 `RUNTIME_OUTPUT_DIRECTORY` 从 `${CMAKE_BINARY_DIR}/bin` 改为 `${STDIOLINK_RAW_DIR}`（26 处）
- [ ] `cmake/assemble_runtime.cmake` 和 `cmake/assemble_runtime_impl.cmake` 新增并正常工作
- [ ] `build.bat Debug` 后 `build/debug/` 含编译产物，`build/runtime_debug/` 含同构运行时目录
- [ ] `build/bin/` 目录不再生成
- [ ] `runtime_debug/data_root/drivers/` 下每个驱动在独立子目录中，`DriverManagerScanner` 可正常扫描
- [ ] `publish_release` 从 `runtime_release/` 打包，发布包通过 `check_duplicates` 校验
- [ ] `run_demo.sh` 适配新路径，删除 `prepare_demo_drivers` 运行时补丁
- [ ] 旧 demo CMake 资产复制目标（`server_manager_demo_assets`、`js_runtime_demo_assets`、`config_demo_assets`）已删除
- [ ] `tools/validate_m89_layout.sh` 全部 T01-T12 通过
- [ ] `stdiolink_tests` 全量通过，无回归
- [ ] `GET /api/drivers` 在开发环境下返回非空驱动列表
- [ ] 文档同步完成（本里程碑文档 + CLAUDE.md 构建说明更新）

