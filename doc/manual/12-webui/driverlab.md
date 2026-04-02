# DriverLab 交互调试

DriverLab 是 WebUI 内置的 Driver 实时调试工具，通过 WebSocket 与 Driver 进程双向通信，支持命令发送、响应查看和协议流分析。

## 界面布局

```text
┌──────────────────────────────────────────────────┐
│  连接面板：选择 Driver · 运行模式 · 连接/断开     │
├────────────────────┬─────────────────────────────┤
│  命令面板          │  协议流查看器                │
│  · 命令列表        │  · 请求/响应消息流           │
│  · 参数表单        │  · 展开/折叠 JSON            │
│  · 示例区          │  · 自动滚动                  │
│  · 命令行预览      │                             │
├────────────────────┴─────────────────────────────┤
│  状态栏：连接状态 · PID · 元数据信息              │
└──────────────────────────────────────────────────┘
```

## 执行边界

- DriverLab 中显示的命令行示例本质上是 **argv token** 展示和复制辅助
- 示例文本遵循与 Console 模式一致的 JSON -> CLI 规范
- DriverLab 实际执行仍走 WebSocket/JSON 请求，不以该字符串作为内部协议
- 命令行预览始终反映当前表单参数；点击 `Apply example` 后，示例参数才会写回表单并更新预览
- 因此，DriverLab 中看到的 CLI 预览与“实际如何启动 Driver”不是一回事；它不能直接用来推断 Driver 的进程生命周期

## 连接流程

1. 在连接面板选择目标 Driver
2. 选择运行模式：`oneshot` 或 `keepalive`
3. 点击连接，WebUI 通过 WebSocket 连接到后端
4. 后端启动 Driver 进程，返回元数据和命令列表
5. 命令面板自动填充可用命令、示例和参数表单

## 运行模式语义

- `oneshot`
  - DriverLab 通过 WebSocket 把单条命令写入 Driver stdin
  - Driver 处理完这一条请求后正常退出；WebSocket 会话保持，下一次执行时自动重启 Driver
  - 适合普通“请求 -> 响应”型命令
- `keepalive`
  - Driver 进程在同一会话中持续存活，可连续执行多条命令
  - 适合设备连接、订阅、服务端模拟器等长生命周期命令

需要特别注意：

- DriverLab 的 `oneshot` 走的是 **Stdio 协议链路**，不是命令行 `--cmd=...`
- 命令行 `--cmd=run` 走的是 **Console 模式**
- 所以某些 server 型 `run` 命令在命令行里可以常驻，在 DriverLab `oneshot` 里却会在发出 `started` 事件后退出；这是运行模式差异，不一定代表启动失败
- 调试这类命令时，优先选择 DriverLab 的 `keepalive`

## WebSocket 协议

连接地址：`ws://{host}/api/driverlab/{driverId}?runMode={mode}`

### 客户端发送

```json
{"type": "exec", "cmd": "command_name", "data": {"param": "value"}}
{"type": "cancel"}
```

### 服务端推送

| type | 说明 |
|------|------|
| `driver.started` | Driver 进程已启动 |
| `driver.restarted` | Driver 进程已重启（OneShot 模式） |
| `meta` | 元数据（命令列表、参数定义、examples） |
| `stdout` | Driver 的 JSONL 响应输出 |
| `driver.exited` | Driver 进程已退出 |
| `error` | 错误信息 |

## 命令行示例来源

- `meta.examples` 提供结构化 `params` 与 `mode`
- 前端按统一规则把结构化参数渲染成 CLI 预览
- DriverLab 指令面板默认只展示 `stdio` 示例
- 示例卡片标题统一显示为“示例”，界面不暴露具体 mode
