# Server Lifecycle

## Purpose

理解 Server 为什么管理三层对象，以及改编排能力时应落在哪个子系统。

## Core Model

- `Service`：只读模板，来自 `data_root/services/`
- `Project`：某个 Service 的实例化配置，来自 `data_root/projects/<projectId>/config.json + param.json`
- `Instance`：运行中的 `stdiolink_service` 子进程

## Main Subsystems

- 扫描：`scanner/{service_scanner,driver_manager_scanner}.*`
- Project 管理：`manager/project_manager.*`
- 实例管理：`manager/instance_manager.*`
- 调度：`manager/schedule_engine.*`
- 统一编排：`server_manager.*`
- Windows 桌面运行壳：`main.cpp` + `runtime/{server_runtime_support,windows_tray_controller}.*`

## Schedule Types

- `manual`：只手动触发
- `fixed_rate`：定时触发；首次先等一个 interval，再按固定节拍触发，不以上次执行结束时间为基准；若到 tick 时已达到 `maxConcurrent`，本次直接跳过
- `daemon`：常驻并自动重启

## Modify Entry

- 改 Service 目录约束 -> `service_scanner.*`
- 改 Driver 元数据扫描 -> `driver_manager_scanner.*`
- 改 Project 文件格式/校验 -> `project_manager.*`, `model/project.*`, `model/schedule.*`
- 改实例生命周期 -> `instance_manager.*`, `process_monitor.*`
- 改调度策略 -> `schedule_engine.*`
- 改 Windows 下 server 启动形态（无控制台、托盘、单实例） -> `main.cpp` + `runtime/{server_runtime_support,windows_tray_controller}.*`

## Windows Runtime Notes

- `stdiolink_server` 在 Windows 下默认是 GUI 壳，优先提供托盘菜单打开 Web 控制台/退出。
- 如果系统托盘不可用，启动流程会立即切换到 CLI 模式：附着父控制台，必要时分配新控制台，再继续运行。
- 同一 `data_root` 只允许一个 Windows server 实例；冲突时直接向 `stderr` 报错并返回非 0，不再弹模态框。
- 单实例 key 会对 `data_root` 做绝对路径、分隔符和 Windows 大小写归一化，避免同一路径不同写法绕过保护。
- 未显式传 `--data-root` 时，server 会优先使用可执行文件同级的 `../data_root`；这样直接运行发布包或 `runtime_<config>/bin/stdiolink_server` 也能落到正确的数据目录。

## Project Mutation Rules

- `update` / `enabled` 这类会改磁盘的接口，先保存 `config.json + param.json`，再停实例；后续停实例失败时必须回滚磁盘状态。
- `update` 会先停该 Project 的调度并终止当前实例；停成功后才用新配置重建调度。
- `update` 后的调度接管方式：
  - `manual`：不自动启动
  - `fixed_rate`：重建 timer，从更新完成后重新开始计时
  - `daemon`：按新配置立即尝试拉起
- `delete` 先停调度并等待该 Project 的 Instance 退出，再删除 `projects/<id>/`，否则 Windows 上容易因 `workspace/` 被占用而失败。
- `reload` 不改磁盘，只负责停实例、重载文件、重新验证并恢复调度。
- 同一 Project 的 `update`、`delete`、`reload`、`start`、`stop`、`enabled` 最好做项目级互斥；不要允许这些接口在同一 Project 上并发交错。

## Tests

- `src/tests/test_driver_manager_scanner.cpp`
- `src/tests/test_instance_manager.cpp`
- `src/tests/test_api_router.cpp`
- `src/tests/test_server_runtime_support.cpp`
- 改 Project 变更时序时，至少补“删除运行中项目”“保存后停实例失败回滚”“并发变更返回 409”三类回归

## Related

- `server-api-realtime.md`
- `../04-service/service-config-and-driver.md`
