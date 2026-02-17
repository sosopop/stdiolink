# 实时通信

WebUI 通过两种机制实现实时数据更新：SSE（Server-Sent Events）用于全局事件推送，WebSocket 用于 DriverLab 双向通信。

## SSE 事件流

### 连接

订阅地址：`GET /api/events/stream?filter={event_types}`

`filter` 参数为逗号分隔的事件类型列表，留空则接收所有事件。

### 事件类型

| 事件 | 说明 |
|------|------|
| `instance.started` | 实例已启动 |
| `instance.finished` | 实例已结束 |
| `schedule.triggered` | 调度已触发 |
| `schedule.suppressed` | 调度被抑制 |

### 前端集成

SSE 通过三层封装集成到 UI：

1. **EventStream 类**（`api/event-stream.ts`）：封装 `EventSource`，提供 `connect/close/on/off` 接口
2. **useEventStream Hook**（`hooks/useEventStream.ts`）：React Hook，自动管理连接生命周期
3. **useGlobalEventStream Hook**：全局 SSE 监听，驱动 Dashboard 事件流更新

## WebSocket（DriverLab）

DriverLab 使用 WebSocket 实现与 Driver 进程的双向通信，详见 [DriverLab 交互调试](driverlab.md)。

连接地址：`ws://{host}/api/driverlab/{driverId}?runMode={mode}`

前端通过 `DriverLabWsClient` 类（`api/driverlab-ws.ts`）管理 WebSocket 连接，提供 `connect/disconnect/exec/cancel/on/off` 接口。

## 轮询

对于不支持实时推送的数据，WebUI 使用轮询作为补充：

| Hook | 说明 |
|------|------|
| `usePolling` | 固定间隔轮询，Dashboard 默认 30 秒 |
| `useSmartPolling` | 智能轮询，根据页面可见性自动暂停/恢复 |

SSE + 轮询双通道确保数据的实时性和可靠性：SSE 负责即时事件推送，轮询负责全量状态同步。
