# Driver 可执行命名规范改造清单（`stdio.drv.` 前缀）

## 1. 目标与规则

- [ ] 统一规则：Driver 可执行文件名（去扩展名后的 basename）必须以 `stdio.drv.` 开头。
- [ ] 规则示例：
  - Windows: `stdio.drv.modbustcp.exe`
  - Linux/macOS: `stdio.drv.modbustcp`
- [ ] 明确适用范围：
  - Server 扫描链路（`DriverManagerScanner`）强制执行
  - Core 扫描链路（`DriverScanner`）与文档/示例同步

## 2. 核心代码改造（必须）

### 2.1 扫描逻辑：只识别前缀命名

- [ ] 修改 `src/stdiolink_server/scanner/driver_manager_scanner.cpp`
  - 在 `findDriverExecutable()` 中增加 basename 前缀校验
  - 当目录下存在可执行但不满足前缀时输出明确告警
  - 避免“取第一个可执行文件”的不确定行为
- [ ] 修改 `src/stdiolink/host/driver_catalog.cpp`
  - `findExecutableInDirectory()` 同步同一规则，避免双实现漂移

### 2.2 公共工具：抽出命名判断函数

- [ ] 修改 `src/stdiolink/platform/platform_utils.h`
  - 新增 `driverExecutablePrefix()`（返回 `stdio.drv.`）
  - 新增 `isDriverExecutableBaseName(const QString&)`
- [ ] 修改 `src/stdiolink/platform/platform_utils.cpp`
  - 实现上述函数（按 basename 判断，不含扩展名）

## 3. 构建产物命名改造（必须）

> 建议保留 CMake target 名称不变，仅改 `OUTPUT_NAME`，减少依赖联动。

- [ ] 修改 `src/drivers/driver_modbustcp/CMakeLists.txt`
- [ ] 修改 `src/drivers/driver_modbusrtu/CMakeLists.txt`
- [ ] 修改 `src/drivers/driver_3dvision/CMakeLists.txt`
- [ ] 修改 `src/demo/calculator_driver/CMakeLists.txt`
- [ ] 修改 `src/demo/device_simulator_driver/CMakeLists.txt`
- [ ] 修改 `src/demo/file_processor_driver/CMakeLists.txt`
- [ ] 修改 `src/demo/heartbeat_driver/CMakeLists.txt`
- [ ] 修改 `src/demo/kv_store_driver/CMakeLists.txt`

建议映射：
- `calculator_driver` -> `stdio.drv.calculator`
- `device_simulator_driver` -> `stdio.drv.device-simulator`
- `file_processor_driver` -> `stdio.drv.file-processor`
- `heartbeat_driver` -> `stdio.drv.heartbeat`
- `kv_store_driver` -> `stdio.drv.kv-store`
- `driver_modbusrtu` -> `stdio.drv.modbusrtu`
- `driver_modbustcp` -> `stdio.drv.modbustcp`
- `driver_3dvision` -> `stdio.drv.3dvision`

> 说明：本次按最终命名统一执行“前缀化 + 语义重命名”，清单中的路径、脚本、测试与文档均按该映射同步。

## 4. Demo/脚本与默认配置（必须）

- [ ] 修改 `src/demo/demo_host/main.cpp`
  - 启动路径从旧名称切换到新名称
- [ ] 修改 `src/demo/server_manager_demo/scripts/run_demo.sh`
  - 复制/探测的 driver 名称改为新规范
- [ ] 修改 `src/demo/server_manager_demo/README.md`
  - 示例名称同步
- [ ] 修改 `src/demo/README.md`
  - 运行示例与说明同步
- [ ] 修改 `src/demo/server_manager_demo/data_root/services/process_exec_service/config.schema.json`
  - `spawnDriver` 默认值改为新名称
- [ ] 修改 `src/demo/server_manager_demo/data_root/projects/manual_process_exec.json`
  - `spawnDriver` 默认值改为新名称
- [ ] 修改 `src/demo/server_manager_demo/data_root/services/process_exec_service/index.js`
  - `spawnDriver` 回退默认值改为新名称
- [ ] 修改 `src/demo/server_manager_demo/data_root/services/driver_pipeline_service/index.js`
  - `findDriverPath("calculator_driver")` 改为新名称
- [ ] 修改 `src/demo/js_runtime_demo/services/driver_task/index.js`
  - 自动启动基名改为新名称
- [ ] 修改 `src/demo/js_runtime_demo/services/wait_any/index.js`
  - `openDriverAuto` 调用中的基名改为新名称
- [ ] 修改 `src/demo/js_runtime_demo/services/proxy_scheduler/index.js`
  - `openDriverAuto` 调用中的基名改为新名称
- [ ] 修改 `src/demo/js_runtime_demo/services/process_types/index.js`
  - `driverPathCandidates("calculator_driver")` 改为新名称
- [ ] 修改 `src/demo/js_runtime_demo/README.md`
  - 示例说明中的 driver 名称同步
- [ ] 修改 `src/demo/js_runtime_demo/shared/lib/runtime_utils.js`
  - 路径候选逻辑保持不变，但示例入参同步新名称
- [ ] 修改 `src/demo/server_manager_demo/data_root/shared/lib/driver_utils.js`
  - 注释与示例同步新名称

### 4.2 示例代码

- [ ] 修改 `examples/basic_usage.js`
  - `calculator_driver.exe` 改为新名称
- [ ] 修改 `examples/proxy_usage.js`
  - `calculator_driver.exe` 改为新名称
- [ ] 修改 `examples/multi_driver.js`
  - `calculator_driver.exe` 改为新名称

### 4.3 发布脚本

- [ ] 修改 `tools/publish_release.ps1`
  - `_driver$` 和 `driver_` 的匹配模式改为识别 `stdio.drv.` 前缀，否则打包会漏掉所有 driver

## 5. 测试改造（必须）

### 5.1 扫描与目录测试

- [ ] 修改 `src/tests/test_driver_manager_scanner.cpp`
  - `driver_under_test` 重命名为 `stdio.drv.under_test`
  - 增加“不符合前缀应跳过/告警”的用例
- [ ] 修改 `src/tests/test_driver_catalog.cpp`
  - 目录内可执行文件样例改为前缀命名

### 5.2 运行时/集成测试

- [ ] 修改 `src/tests/test_host_driver.cpp`
- [ ] 修改 `src/tests/test_wait_any.cpp`
- [ ] 修改 `src/tests/test_js_integration.cpp`
- [ ] 修改 `src/tests/test_proxy_and_scheduler.cpp`
- [ ] 修改 `src/tests/test_driver_task_binding.cpp`
- [ ] 修改 `src/tests/test_doc_generator.cpp`
- [ ] 修改 `src/tests/test_driverlab_ws_handler.cpp`（若引用具体 driver 程序路径）

### 5.3 测试辅助可执行（建议）

- [ ] `src/tests/CMakeLists.txt` 中测试桩名称可保持不变（`test_*`），不必纳入前缀规范
- [ ] 明确“规范仅约束生产/示例 Driver，不约束测试桩”

## 6. 文档改造（必须）

- [ ] 修改 `doc/manual/11-server/driver-scanner.md`
  - 目录结构示例更新为 `stdio.drv.*`
- [ ] 修改 `doc/manual/11-server/http-api.md`
  - 示例 `program` 路径同步
- [ ] 修改 `doc/manual/10-js-service/getting-started.md`
- [ ] 修改 `doc/manual/10-js-service/proxy-and-scheduler.md`
- [ ] 修改 `doc/manual/10-js-service/driver-binding.md`
- [ ] 修改 `doc/manual/10-js-service/wait-any-binding.md`
- [ ] 修改 `doc/manual/08-best-practices.md`
- [ ] 修改 `doc/manual/09-troubleshooting.md`
- [ ] 修改其他出现 `calculator_driver`/`driver_modbus*` 的手册示例

## 7. 兼容策略

- [ ] 扫描器遇到不符合 `stdio.drv.` 前缀的可执行文件时输出 `qCWarning`，提示命名不规范
- [ ] 一步到位改完所有命名，不做两阶段发布或配置开关

> 注：`doc/milestone/` 下的历史里程碑文档引用了旧 driver 名，作为历史记录不修改。

## 8. 验收标准

- [ ] `POST /api/drivers/scan` 后，目录中仅前缀命名 Driver 被加载
- [ ] `/api/drivers` 返回的 `program` 均匹配 `stdio.drv.` 前缀
- [ ] Demo 脚本可跑通（`run_demo.sh` + `api_smoke.sh`）
- [ ] 关键测试通过：
  - `DriverManagerScannerTest.*`
  - `DriverCatalogTest.*`
  - `DriverIntegrationTest.*`
  - `JsDriverBindingTest.*`
  - `ProxyAndSchedulerTest.*`

## 9. 快速检索命令（执行改造时使用）

```bash
rg -n "calculator_driver|driver_modbus|driver_3dvision|driver_under_test|test_driver" src doc/manual
rg -n "findDriverExecutable|findExecutableInDirectory|driver.meta.json" src/stdiolink_server src/stdiolink
```
