# WebUI Structure

## Purpose

快速判断一个前端需求应该改页面、API 层、store 还是共享组件。

## Main Directories

- `src/webui/src/pages/`：页面入口和路由页面
- `src/webui/src/api/`：REST/SSE/WS 客户端
- `src/webui/src/stores/`：状态管理
- `src/webui/src/components/`：共享组件
- `src/webui/src/types/`：前端类型
- `src/webui/src/hooks/` / `utils/`：公共逻辑

## API Entry

- `api/client.ts`：HTTP 客户端底座
- `api/services.ts` / `projects.ts` / `instances.ts` / `drivers.ts`
- `api/event-stream.ts`：SSE
- `api/driverlab-ws.ts`：WebSocket

## Constraints

- 前端事实源是 Server API，不直接读取 `data_root`
- 接口变更必须联动 `types/`、`api/`、页面消费点
- 实时逻辑变更同时检查 SSE 和 DriverLab WS

## Tests

- 单元：`src/webui/src/__tests__/`
- 集成/E2E：`src/webui/e2e/`

## Related

- `driverlab-and-events.md`
- `../05-server/server-api-realtime.md`
