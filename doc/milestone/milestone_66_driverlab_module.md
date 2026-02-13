# 里程碑 66：DriverLab 交互式测试模块

> **前置条件**: 里程碑 60 已完成（布局框架已就绪），里程碑 65 已完成（Drivers 模块可用，DriverLab 需要选择 Driver）
> **目标**: 实现 DriverLab 模块的完整 UI：Driver 选择与连接管理、WebSocket 会话控制、Meta 驱动的命令表单自动生成、实时消息流展示、会话历史记录与导出

---

## 1. 目标

- 实现 DriverLab 页面：Driver 选择、运行模式配置、WebSocket 连接管理
- 实现 Meta 驱动的命令面板：根据 DriverMeta 自动生成命令列表和参数表单
- 实现实时消息流面板：时间线展示所有 WebSocket 消息（发送/接收），JSON 格式化
- 实现会话历史：消息记录、JSON 导出、清空
- 实现连接状态管理：连接/断开/重连、Driver 生命周期事件处理
- 实现 Zustand Store：`useDriverLabStore`
- 复用 M59 中的 `DriverLabWsClient` WebSocket 客户端

---

## 2. 背景与问题

DriverLab 是面向 Driver 开发者的协议调试器，提供 Driver 运行时沙箱。用户可以选择一个 Driver，通过 WebSocket 连接启动 Driver 进程，实时发送命令并观察响应流。设计文档将其定位为"协议调试器"，强调细粒度控制和 stdio 协议流的可观测性。

**范围**：DriverLab 完整页面（连接管理 + 命令面板 + 消息流 + 历史）。

**非目标**：多 Driver 并发会话（本里程碑仅支持单 Driver 会话）。

### 2.1 后端 WebSocket 端点（已实现，前端按现有契约接入）

当前后端 `stdiolink_server` 已实现 DriverLab WebSocket 端点（`/api/driverlab/{driverId}`），本里程碑以现有协议为准完成前端集成，不重复设计后端协议。

**端点**：`GET /api/driverlab/{driverId}` （WebSocket Upgrade）

**查询参数**：
- `runMode`：`oneshot` | `keepalive`（可选，默认 `oneshot`）
- `args`：启动参数，逗号分隔（可选）

**WebSocket 消息协议**：

客户端 → 服务端（JSON）：

| type | 说明 | 附加字段 |
|------|------|---------|
| `exec` | 执行命令 | `cmd`: string, `data`: object |
| `cancel` | 取消当前执行 | 无 |

服务端 → 客户端（JSON）：

| type | 说明 | 附加字段 |
|------|------|---------|
| `driver.started` | Driver 进程已启动 | `pid`: number |
| `driver.restarted` | Driver 进程已重启（oneshot 模式） | `pid`: number |
| `meta` | Driver 元数据 | `driverId`, `pid`, `runMode`, `meta` |
| `stdout` | 命令响应（ok/event/error） | `message`: object \| string |
| `driver.exited` | Driver 进程已退出 | `exitCode`: number, `exitStatus`: string, `reason`: string |
| `error` | 服务端错误 | `message`: string |

> **字段约定**：`stdout` 与 `error` 的文本/对象内容统一放在 `message` 字段，前端不应假设存在 `payload` 字段。

---

## 3. 技术要点

### 3.1 WebSocket 会话生命周期

```
用户选择 Driver → 配置运行模式 → 点击连接
  → WS 连接建立
  → 服务端自动启动 Driver 进程
  → 收到 driver.started（pid）
  → 收到 meta（DriverMeta 完整元数据）
  → 命令面板根据 meta.commands 自动生成
  → 用户选择命令 → 填写参数 → 发送 exec
  → 收到 stdout 消息（ok / event / error）
  → 用户可发送 cancel 取消执行中的命令
  → 用户断开连接 → WS 关闭 → 服务端终止 Driver 进程
```

运行模式差异：
- `oneshot`：Driver 执行完命令后自动退出，下次 exec 自动重启
- `keepalive`：Driver 常驻，退出时 WS 连接关闭

### 3.2 页面布局

```
┌─────────────────────────────────────────────────────────┐
│  DriverLab                              [连接状态指示器] │
├──────────────┬──────────────────────────────────────────┤
│              │                                          │
│  连接配置面板 │  消息流面板                               │
│  ┌──────────┐│  ┌──────────────────────────────────────┐│
│  │Driver选择││  │ ▼ 09:01:02 [recv] driver.started     ││
│  │运行模式  ││  │   { pid: 1234 }                      ││
│  │启动参数  ││  │ ▼ 09:01:02 [recv] meta               ││
│  │[连接]    ││  │   { driverId: "calc", ... }          ││
│  └──────────┘│  │ ▲ 09:01:05 [send] exec               ││
│              │  │   { cmd: "add", data: {a:1,b:2} }    ││
│  命令面板    │  │ ▼ 09:01:05 [recv] stdout              ││
│  ┌──────────┐│  │   { ok: 3 }                          ││
│  │命令列表  ││  └──────────────────────────────────────┘│
│  │参数表单  ││                                          │
│  │[执行]    ││  [清空] [导出JSON]                       │
│  │[取消]    ││                                          │
│  └──────────┘│                                          │
│              │                                          │
│  命令行示例  │                                          │
│  ┌──────────┐│                                          │
│  │> calc    ││                                          │
│  │ --cmd=add││                                          │
│  │ --a=1    ││                                          │
│  │ --b=2    ││                                          │
│  │   [复制] ││                                          │
│  └──────────┘│                                          │
│              │                                          │
├──────────────┴──────────────────────────────────────────┤
│  状态栏: PID: 1234 | 运行模式: keepalive | 已连接 2m30s │
└─────────────────────────────────────────────────────────┘
```

### 3.3 连接配置面板

| 字段 | 组件 | 说明 |
|------|------|------|
| Driver 选择 | Select | 从 `GET /api/drivers` 获取列表 |
| 运行模式 | Radio | `oneshot` / `keepalive` |
| 启动参数 | Input | 可选，逗号分隔的 args |
| 连接按钮 | Button | 连接/断开切换 |

### 3.4 命令面板

收到 `meta` 消息后，根据 `meta.commands` 自动生成命令列表：

```typescript
interface CommandPanelProps {
  commands: CommandMeta[];
  onExec: (command: string, data: Record<string, unknown>) => void;
  onCancel: () => void;
  executing: boolean;
}
```

- 命令列表：下拉选择或列表点击
- 参数表单：根据选中命令的 `params` 字段自动生成输入控件（复用 M63 的 SchemaField 逻辑）
- 执行按钮：发送 `{ type: 'exec', cmd, data }`
- 取消按钮：发送 `{ type: 'cancel' }`，仅在执行中可用

参数类型到控件映射（复用 M63 SchemaForm 的字段渲染组件）：

| 参数类型 | 控件 | 来源 |
|---------|------|------|
| String | Input | 复用 SchemaForm/fields/StringField |
| Int / Int64 / Double | InputNumber | 复用 SchemaForm/fields/NumberField |
| Bool | Switch | 复用 SchemaForm/fields/BoolField |
| Enum | Select | 复用 SchemaForm/fields/EnumField |
| Object / Array / Any | JSON textarea | 复用 SchemaForm/fields/AnyField |

> **说明**：不单独实现 ParamForm 的字段渲染逻辑。M63 的 SchemaForm 字段组件（StringField、NumberField 等）已实现 FieldMeta → UI 控件的映射，DriverLab 的 ParamForm 应直接复用这些组件，仅在布局层面做简化（不需要 group/advanced 等 UI Hint）。

### 3.5 命令行调用示例

对齐桌面版 DriverLab 的 `ParameterForm` 功能，在命令面板下方显示当前命令的命令行调用示例，方便开发者在终端中复现调用。

```typescript
// src/components/DriverLab/CommandLineExample.tsx
interface CommandLineExampleProps {
  driverId: string | null;       // 当前连接的 Driver ID
  command: string | null;        // 当前选中的命令名
  params: Record<string, unknown>; // 当前参数值
}
```

生成规则（与桌面版 `ParameterForm::updateCommandLineExample()` 一致）：

```
{driverProgram} --cmd={commandName} --{key1}={value1} --{key2}={value2} ...
```

- Driver 程序名：使用 `driverId` 作为程序名（实际路径由用户自行替换）
- 跳过值为空字符串或 null 的参数
- 字符串值含空格/特殊字符时用双引号包裹
- Object/Array 值序列化为紧凑 JSON 并用双引号包裹
- Bool 值输出 `true` / `false`
- 数值直接输出，整数不带小数点

显示要求：
- 等宽字体，深色背景代码块样式（与消息流面板的 JSON 展示风格一致）
- 右上角「复制」按钮，点击复制到剪贴板并显示 "已复制" 提示
- 未选择命令时显示占位文字 "选择命令后显示调用示例"
- 参数值变化时实时更新

### 3.6 消息流面板

所有 WebSocket 消息按时间线展示：

```typescript
interface MessageEntry {
  id: string;
  timestamp: number;
  direction: 'send' | 'recv';
  type: string;           // exec / cancel / driver.started / meta / stdout / driver.exited / error
  raw: WsMessage;         // 原始 WebSocket 消息
  payload: unknown;       // UI 展示用归一化内容（见下方映射规则）
  expanded: boolean;
}
```

视觉要求：
- 发送消息（▲）蓝色标记，接收消息（▼）绿色标记
- 错误消息红色标记
- `driver.started` / `driver.exited` 事件使用灰色系统消息样式
- JSON payload 默认折叠，点击展开格式化显示
- 自动滚动到最新消息（可关闭）
- 最多保留 500 条消息，超出后移除旧消息

payload 归一化规则：
- `stdout` / `error`：`payload = msg.message`
- `meta`：`payload = msg.meta`
- 其他类型：`payload = msg`

### 3.7 连接状态管理

```typescript
type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

interface ConnectionState {
  status: ConnectionStatus;
  driverId: string | null;
  runMode: 'oneshot' | 'keepalive';
  pid: number | null;
  connectedAt: number | null;
  meta: DriverMeta | null;
  error: string | null;
}
```

状态转换：
- `disconnected` → 点击连接 → `connecting`
- `connecting` → WS open → `connected`
- `connected` → 收到 `driver.exited`（keepalive 模式）→ `disconnected`
- `connected` → 用户断开 → `disconnected`
- 任意状态 → WS error → `error`

### 3.8 Zustand Store

```typescript
// src/stores/useDriverLabStore.ts
interface DriverLabState {
  // 连接状态
  connection: ConnectionState;
  // 消息历史
  messages: MessageEntry[];
  // 可用命令（从 meta 解析）
  commands: CommandMeta[];
  // 当前选中命令
  selectedCommand: string | null;
  // 命令参数值
  commandParams: Record<string, unknown>;
  // 执行状态
  executing: boolean;

  // Actions
  connect: (driverId: string, runMode: 'oneshot' | 'keepalive', args?: string[]) => void;
  disconnect: () => void;
  execCommand: (command: string, data: Record<string, unknown>) => void;
  cancelCommand: () => void;
  selectCommand: (name: string) => void;
  setCommandParams: (params: Record<string, unknown>) => void;
  clearMessages: () => void;
  appendMessage: (entry: MessageEntry) => void;

  // 内部：处理 WS 消息
  handleWsMessage: (msg: WsMessage) => void;
}
```

---

## 4. 实现方案

### 4.1 组件树

```
DriverLabPage
├── PageHeader (标题 + 连接状态指示器)
├── SplitLayout (左右分栏)
│   ├── LeftPanel
│   │   ├── ConnectionPanel
│   │   │   ├── DriverSelect
│   │   │   ├── RunModeRadio
│   │   │   ├── ArgsInput
│   │   │   └── ConnectButton
│   │   └── CommandPanel
│   │       ├── CommandSelect
│   │       ├── ParamForm (简化版参数表单)
│   │       ├── CommandLineExample (命令行调用示例)
│   │       ├── ExecButton
│   │       └── CancelButton
│   └── RightPanel
│       ├── MessageStream (消息流时间线)
│       │   └── MessageEntry[] (可展开消息条目)
│       └── MessageToolbar (清空/导出)
└── StatusBar (PID/运行模式/连接时长)
```

### 4.2 ConnectionPanel 组件

```typescript
// src/components/DriverLab/ConnectionPanel.tsx
interface ConnectionPanelProps {
  drivers: DriverInfo[];
  connection: ConnectionState;
  onConnect: (driverId: string, runMode: 'oneshot' | 'keepalive', args?: string[]) => void;
  onDisconnect: () => void;
}
```

### 4.3 CommandPanel 组件

```typescript
// src/components/DriverLab/CommandPanel.tsx
interface CommandPanelProps {
  commands: CommandMeta[];
  selectedCommand: string | null;
  commandParams: Record<string, unknown>;
  executing: boolean;
  connected: boolean;
  onSelectCommand: (name: string) => void;
  onParamsChange: (params: Record<string, unknown>) => void;
  onExec: () => void;
  onCancel: () => void;
}
```

### 4.4 MessageStream 组件

```typescript
// src/components/DriverLab/MessageStream.tsx
interface MessageStreamProps {
  messages: MessageEntry[];
  autoScroll: boolean;
  onToggleAutoScroll: () => void;
}
```

消息渲染逻辑：
- 使用虚拟列表（当消息量大时优化性能）
- 每条消息显示：方向图标 + 时间戳 + 类型标签 + payload 预览
- 点击展开显示完整 JSON（使用 `JSON.stringify(payload, null, 2)`）

### 4.5 ParamForm 组件

```typescript
// src/components/DriverLab/ParamForm.tsx
interface ParamFormProps {
  params: FieldMeta[];
  values: Record<string, unknown>;
  onChange: (values: Record<string, unknown>) => void;
}
```

简化版参数表单，根据 `FieldMeta.type` 渲染对应控件。复用 M63 SchemaForm 的字段组件（StringField/NumberField/BoolField/EnumField/AnyField），不需要完整的 SchemaForm 功能（无 ui.group / ui.advanced 等），仅处理基本类型映射和平铺布局。

### 4.6 会话导出

```typescript
// 导出为 JSON 文件
function exportMessages(messages: MessageEntry[], driverId: string): void {
  const data = messages.map(m => ({
    timestamp: new Date(m.timestamp).toISOString(),
    direction: m.direction,
    type: m.type,
    payload: m.payload,
  }));
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `driverlab_${driverId}_${Date.now()}.json`;
  a.click();
  URL.revokeObjectURL(url);
}
```

---

## 5. 文件变更清单

### 5.1 新增文件

**页面**：
- `src/webui/src/pages/DriverLab/index.tsx` — DriverLab 页面（替换占位）

**组件**：
- `src/webui/src/components/DriverLab/ConnectionPanel.tsx` — 连接配置面板
- `src/webui/src/components/DriverLab/CommandPanel.tsx` — 命令面板
- `src/webui/src/components/DriverLab/ParamForm.tsx` — 简化版参数表单
- `src/webui/src/components/DriverLab/CommandLineExample.tsx` — 命令行调用示例
- `src/webui/src/components/DriverLab/MessageStream.tsx` — 消息流面板
- `src/webui/src/components/DriverLab/MessageEntry.tsx` — 单条消息组件
- `src/webui/src/components/DriverLab/MessageToolbar.tsx` — 消息工具栏（清空/导出）
- `src/webui/src/components/DriverLab/StatusBar.tsx` — 底部状态栏

**Store**：
- `src/webui/src/stores/useDriverLabStore.ts`

**测试**：
- `src/webui/src/__tests__/pages/DriverLab.test.tsx`
- `src/webui/src/__tests__/components/ConnectionPanel.test.tsx`
- `src/webui/src/__tests__/components/CommandPanel.test.tsx`
- `src/webui/src/__tests__/components/ParamForm.test.tsx`
- `src/webui/src/__tests__/components/CommandLineExample.test.tsx`
- `src/webui/src/__tests__/components/MessageStream.test.tsx`
- `src/webui/src/__tests__/components/MessageEntry.test.tsx`
- `src/webui/src/__tests__/components/MessageToolbar.test.tsx`
- `src/webui/src/__tests__/stores/useDriverLabStore.test.ts`

---

## 6. 测试与验收

### 6.1 单元测试场景

**ConnectionPanel（ConnectionPanel.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染 Driver 下拉列表 | 显示可选 Driver |
| 2 | 选择运行模式 | oneshot / keepalive 切换 |
| 3 | 输入启动参数 | args 输入框可编辑 |
| 4 | 点击连接 | 调用 onConnect 回调，传递正确参数 |
| 5 | 已连接状态 | 连接按钮变为"断开"，Driver/模式选择禁用 |
| 6 | 点击断开 | 调用 onDisconnect 回调 |
| 7 | 连接中状态 | 按钮显示 loading，不可点击 |

**CommandPanel（CommandPanel.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | 渲染命令列表 | 显示所有可用命令 |
| 9 | 选择命令 | 触发 onSelectCommand，显示对应参数表单 |
| 10 | 无命令时 | 显示"等待 Driver 元数据" |
| 11 | 未连接时 | 执行按钮禁用 |
| 12 | 点击执行 | 触发 onExec 回调 |
| 13 | 执行中状态 | 执行按钮禁用，取消按钮可用 |
| 14 | 点击取消 | 触发 onCancel 回调 |

**ParamForm（ParamForm.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 15 | String 参数 | 渲染 Input 组件 |
| 16 | Int 参数 | 渲染 InputNumber 组件 |
| 17 | Bool 参数 | 渲染 Switch 组件 |
| 18 | Enum 参数 | 渲染 Select 组件，选项正确 |
| 19 | Object 参数 | 渲染 JSON textarea |
| 20 | 无参数命令 | 显示"该命令无参数" |
| 21 | 值变更 | 触发 onChange 回调 |

**CommandLineExample（CommandLineExample.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 22 | 未选择命令 | 显示占位文字 "选择命令后显示调用示例" |
| 23 | 基本生成 | 显示 `{driverId} --cmd={command}` 格式 |
| 24 | 带参数生成 | 参数以 `--key=value` 格式追加 |
| 25 | 特殊字符转义 | 含空格的字符串值用双引号包裹 |
| 26 | Object/Array 参数 | 序列化为紧凑 JSON 并用双引号包裹 |
| 27 | 空值参数跳过 | null 和空字符串参数不出现在命令行中 |
| 28 | 复制按钮 | 点击后内容写入剪贴板 |
| 29 | 参数变化实时更新 | 修改参数值后命令行示例同步更新 |

**MessageStream（MessageStream.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 30 | 渲染消息列表 | 显示所有消息条目 |
| 31 | 发送消息样式 | ▲ 蓝色标记 |
| 32 | 接收消息样式 | ▼ 绿色标记 |
| 33 | 错误消息样式 | 红色标记 |
| 34 | 系统事件样式 | driver.started / driver.exited 灰色 |
| 35 | 空消息列表 | 显示"暂无消息" |
| 36 | 自动滚动 | 新消息追加后滚动到底部 |
| 37 | 关闭自动滚动 | 新消息追加后不滚动 |

**MessageEntry（MessageEntry.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 38 | 折叠状态 | 显示 payload 预览（单行截断） |
| 39 | 展开状态 | 显示完整格式化 JSON |
| 40 | 点击切换 | 折叠/展开状态切换 |
| 41 | 时间戳格式 | 显示 HH:mm:ss.SSS |

**MessageToolbar（MessageToolbar.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 42 | 点击清空 | 触发清空回调 |
| 43 | 点击导出 | 触发文件下载 |
| 44 | 导出文件名 | `driverlab_{driverId}_{timestamp}.json` |
| 45 | 无消息时 | 导出按钮禁用 |

**useDriverLabStore（useDriverLabStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 46 | `connect()` | 创建 WS 连接，状态变为 connecting |
| 47 | WS open | 状态变为 connected |
| 48 | `handleWsMessage(driver.started)` | 更新 pid，追加系统消息 |
| 49 | `handleWsMessage(meta)` | 更新 meta 和 commands 列表 |
| 50 | `handleWsMessage(stdout)` | 追加接收消息 |
| 51 | `handleWsMessage(driver.exited)` | 追加系统消息，keepalive 模式下断开 |
| 52 | `handleWsMessage(error)` | 追加错误消息 |
| 53 | `execCommand()` | 发送 exec 消息，executing 设为 true |
| 54 | `cancelCommand()` | 发送 cancel 消息 |
| 55 | `disconnect()` | 关闭 WS，状态变为 disconnected |
| 56 | `clearMessages()` | 消息列表清空 |
| 57 | `appendMessage()` 超过 500 条 | 旧消息被移除 |
| 58 | `selectCommand()` | selectedCommand 更新，commandParams 重置 |

### 6.2 验收标准

- DriverLab 页面布局正确（左右分栏）
- Driver 选择和连接配置正常
- WebSocket 连接/断开/重连正常
- 收到 meta 后命令面板自动生成
- 命令行调用示例根据当前命令和参数实时生成，复制功能正常
- 命令执行和取消正常
- 消息流实时展示，方向/类型样式正确
- 消息折叠/展开正常
- 会话导出为 JSON 正常
- 连接状态指示器正确反映当前状态
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：WebSocket 连接不稳定导致消息丢失
  - 控制：连接断开时显示明确提示；不自动重连（用户手动重连）；断开前的消息保留在历史中
- **风险 2**：大量消息导致前端性能问题
  - 控制：消息上限 500 条，超出后移除旧消息；JSON 消息默认折叠减少 DOM 节点
- **风险 3**：Driver 快速崩溃重启导致消息风暴
  - 控制：服务端已有重启抑制机制；前端对 `error` 类型消息做去重（相同错误 1s 内仅显示一次）

---

## 8. 里程碑完成定义（DoD）

- DriverLab 页面完整实现
- WebSocket 连接管理正常
- Meta 驱动的命令面板自动生成
- 消息流实时展示正常
- 会话导出功能正常
- 对应单元测试完成并通过
- 本里程碑文档入库
