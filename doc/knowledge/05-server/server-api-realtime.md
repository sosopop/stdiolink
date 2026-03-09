# Server API And Realtime

## Purpose

定位 HTTP API、SSE 事件流和 DriverLab WebSocket 的实现与边界。

## REST

- 主要资源：services、projects、instances、drivers
- 路由实现：`src/stdiolink_server/http/api_router.*`
- 辅助：`http/http_helpers.h`

## SSE

- 事件总线：`http/event_bus.*`
- 事件日志：`http/event_log.*`
- 流输出：`http/event_stream_handler.*`

## DriverLab WebSocket

- 处理器：`http/driverlab_ws_handler.*`
- 单连接会话：`http/driverlab_ws_connection.*`
- 用途：浏览器到 Driver/命令执行的双向调试通道

## Static And Service Files

- 静态文件：`http/static_file_server.*`
- 服务文件：`http/service_file_handler.*`
- 跨域：`http/cors_middleware.*`

## Modify Entry

- 新增 API：优先改 `api_router.*`，再补 tests 和 `doc/http_api.md`
- API 字段、枚举、路径、查询参数变更：同步检查 `src/webui/src/api/`、相关 `types/`、页面消费点，以及 `doc/todolist.md`
- 改事件模型：同步检查 SSE 客户端和 WebUI stores
- 改 DriverLab 协议：同步检查 `src/webui/src/api/driverlab-ws.ts`

## Sync Rules

- REST 变更至少联动：后端路由、后端测试、前端 API 客户端、前端类型、接口文档
- SSE 事件字段变更至少联动：`event_bus.*`、`event_stream_handler.*`、`src/webui/src/api/event-stream.ts`、相关 stores
- DriverLab WS 协议变更至少联动：后端 `driverlab_ws_*`、前端 `driverlab-ws.ts`、DriverLab 页面消费逻辑

## Tests

- `src/tests/test_api_router.cpp`
- `src/tests/test_event_bus.cpp`
- `src/tests/test_event_log.cpp`
- `src/tests/test_driverlab_ws_handler.cpp`

## Related

- `../06-webui/driverlab-and-events.md`
- `../08-workflows/debug-change-entry.md`
