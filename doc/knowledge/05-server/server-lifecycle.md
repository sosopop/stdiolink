# Server Lifecycle

## Purpose

理解 Server 为什么管理三层对象，以及改编排能力时应落在哪个子系统。

## Core Model

- `Service`：只读模板，来自 `data_root/services/`
- `Project`：某个 Service 的实例化配置，来自 `data_root/projects/*.json`
- `Instance`：运行中的 `stdiolink_service` 子进程

## Main Subsystems

- 扫描：`scanner/{service_scanner,driver_manager_scanner}.*`
- Project 管理：`manager/project_manager.*`
- 实例管理：`manager/instance_manager.*`
- 调度：`manager/schedule_engine.*`
- 统一编排：`server_manager.*`

## Schedule Types

- `manual`：只手动触发
- `fixed_rate`：定时触发
- `daemon`：常驻并自动重启

## Modify Entry

- 改 Service 目录约束 -> `service_scanner.*`
- 改 Driver 元数据扫描 -> `driver_manager_scanner.*`
- 改 Project 文件格式/校验 -> `project_manager.*`, `model/project.*`, `model/schedule.*`
- 改实例生命周期 -> `instance_manager.*`, `process_monitor.*`
- 改调度策略 -> `schedule_engine.*`

## Tests

- `src/tests/test_driver_manager_scanner.cpp`
- `src/tests/test_instance_manager.cpp`
- `src/tests/test_api_router.cpp`

## Related

- `server-api-realtime.md`
- `../04-service/service-config-and-driver.md`
