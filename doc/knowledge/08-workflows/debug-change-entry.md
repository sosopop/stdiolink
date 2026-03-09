# Debug Change Entry

## Purpose

按需求类型快速反查应修改的源码区域和最低测试面。

## Routing

- 改 JSONL 字段/状态：`src/stdiolink/protocol/` + Host/Driver/JS 绑定测试
- 改 Driver 执行逻辑：目标 `src/drivers/driver_*` + `src/stdiolink/driver/`
- 改 Task/并发等待：`src/stdiolink/host/{driver,task,wait_any}*`
- 改 JS 内置模块：`src/stdiolink_service/bindings/`
- 改 Service 配置 schema：`src/stdiolink_service/config/`
- 改 Project/调度：`src/stdiolink_server/{model,manager}/`
- 改 REST/SSE/WS：`src/stdiolink_server/http/` + `src/webui/src/api/`
- 改前端页面：`src/webui/src/{pages,components,stores}/`

## Minimum Tests

- 核心 C++ 行为 -> `src/tests/`
- 跨进程/跨子系统链路 -> `src/smoke_tests/`
- 前端逻辑 -> `src/webui/src/__tests__/`
- 前端端到端 -> `src/webui/e2e/`

## When In Doubt

- 先回 `../README.md` 走一级索引
- 再读对应子系统 `README.md`
- 最后进入单主题文档找“Modify Entry”
