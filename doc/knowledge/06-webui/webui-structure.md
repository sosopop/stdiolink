# WebUI Structure

## Purpose

快速判断一个前端需求应该改页面、API 层、store 还是共享组件。

## Main Directories

- `src/webui/src/pages/`：页面入口和路由页面
- `src/webui/src/api/`：REST/SSE/WS 客户端
- `src/webui/src/stores/`：状态管理
- `src/webui/src/components/`：共享组件；其中 `SchemaForm/` 负责 Project/Service 配置 schema 渲染，primitive array 使用紧凑单行列表，`object[]` 保持卡片式编辑
- `src/webui/src/types/`：前端类型
- `src/webui/src/hooks/` / `utils/`：公共逻辑
- `src/webui/src/locales/`：WebUI 多语言文案事实源，默认以 `en.json` 为结构基准

## API Entry

- `api/client.ts`：HTTP 客户端底座
- `api/services.ts` / `projects.ts` / `instances.ts` / `drivers.ts`
- `api/event-stream.ts`：SSE
- `api/driverlab-ws.ts`：WebSocket

## Constraints

- 前端事实源是 Server API，不直接读取 `data_root`
- 接口变更必须联动 `types/`、`api/`、页面消费点
- 实时逻辑变更同时检查 SSE 和 DriverLab WS
- 新增或修改文案 key 时，同时检查所有 locale；不要只改 `en/zh/zh-TW`
- Project 状态、调度策略、Header 品牌副标题这类展示文案也必须走 locale，不要直接渲染枚举值或硬编码标题

## UI Baseline

- 保持现有设计体系，不要在已有界面上引入新的视觉语言
- 延续当前 "Style 06" 方向：Glassmorphism、Bento Grid、深浅双主题兼容
- 新组件或页面必须同时检查深色和浅色模式，不只看默认主题
- 涉及 DriverLab、Projects、Dashboard 等现有页面时，优先复用现有组件、tokens、stores 和交互模式

## Tests

- 单元：`src/webui/src/__tests__/`
- 集成/E2E：`src/webui/e2e/`

## Related

- `project-test-command-panel.md`
- `project-command-path-rules.md`
- `driverlab-and-events.md`
- `../05-server/server-api-realtime.md`
