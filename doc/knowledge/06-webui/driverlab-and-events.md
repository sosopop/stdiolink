# DriverLab And Events

## Purpose

覆盖 WebUI 中最容易和后端协议耦合的两类能力：全局事件流、DriverLab 交互调试。

## SSE

- 入口：`src/webui/src/api/event-stream.ts`
- 来源：Server `/api/events/stream`
- 用途：实例启停、调度、日志等实时状态刷新

## DriverLab

- 入口：`src/webui/src/api/driverlab-ws.ts`
- 后端：`src/stdiolink_server/http/driverlab_ws_*`
- 依赖：Driver 元数据、命令表单、执行结果流
- 指令面板示例区默认只展示 `mode=stdio` 的示例；界面不显示 mode 标签
- 示例 JSON 默认保持单行展示；超长内容只允许在容器内横向滚动，不应撑宽整个面板
- 示例区提供单独的“自动换行”切换按钮，只影响当前示例卡片

## High Risk Changes

- 改 WebSocket 消息结构
- 改 Driver 元数据字段名
- 改 REST 与实时事件的状态枚举

## Modify Entry

- 浏览器连接/重连：`driverlab-ws.ts`
- 事件订阅/分发：`event-stream.ts` + 相关 stores
- Driver 表单/命令执行 UI：对应页面与共享组件

## Related

- `webui-structure.md`
- `../02-driver/driver-meta.md`
- `../05-server/server-api-realtime.md`
