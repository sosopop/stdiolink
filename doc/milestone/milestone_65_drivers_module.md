# 里程碑 65：Drivers 驱动管理模块

> **前置条件**: 里程碑 60 已完成（布局框架已就绪）
> **目标**: 实现 Drivers 模块的完整 UI：列表页（搜索/扫描）、详情页（元数据展示/命令列表/配置 Schema/文档），展示 Driver 的完整 DriverMeta 信息

---

## 1. 目标

- 实现 Drivers 列表页：表格展示、搜索、扫描目录按钮
- 实现 Driver 详情页：元数据 Tab、命令列表 Tab、文档 Tab
- 展示完整 DriverMeta：info、commands（含参数详情）、config、types、errors、examples
- 命令参数以表格形式展示（名称/类型/必填/默认值/描述/约束）
- 支持导出元数据 JSON
- 实现 Zustand Store：`useDriversStore`
- 提供"在 DriverLab 中测试"快捷入口

---

## 2. 背景与问题

Drivers 是 stdiolink 的底层通信驱动，每个 Driver 通过 DriverMeta 自描述其能力（支持的命令、参数类型、配置项等）。WebUI 需要以结构化方式展示这些元数据，帮助开发者理解 Driver 的接口规范。

**范围**：Drivers 列表 + 详情展示 + 文档生成与导出。Driver 是只读资源（通过文件系统扫描发现），不支持在线创建/编辑。

### 2.1 后端文档生成 API（需新增）

核心库 `src/stdiolink/doc/doc_generator.h` 已实现完整的文档生成器（`DocGenerator` 类），支持 Markdown、HTML、OpenAPI 3.0、TypeScript 四种输出格式。本里程碑对 WebUI 仅暴露 Markdown/HTML/TypeScript 三种（不暴露 OpenAPI）。

**端点**：`GET /api/drivers/{id}/docs`

**查询参数**：
- `format`：`markdown` | `html` | `typescript`（必填）

**响应**：

| format | Content-Type | 响应体 |
|--------|-------------|--------|
| `markdown` | `text/markdown; charset=utf-8` | Markdown 文本 |
| `html` | `text/html; charset=utf-8` | 自包含 HTML（含内嵌 CSS/JS） |
| `typescript` | `text/plain; charset=utf-8` | TypeScript 声明文件 |

**实现建议**：在 `api_router.cpp` 中注册路由，从 `DriverManagerScanner` 获取 `DriverMeta`，调用 `DocGenerator::toMarkdown()` / `toHtml()` / `toTypeScript()` 生成内容并返回。不暴露 `toOpenAPI()`（Driver 是 stdin/stdout JSONL 程序，OpenAPI 的 HTTP 路径映射无实际意义）。

---

## 3. 技术要点

### 3.1 列表页功能

| 功能 | 实现 |
|------|------|
| 表格列 | ID、名称、版本、命令数、操作 |
| 搜索 | 前端过滤（ID/名称模糊匹配） |
| 扫描目录 | 调用 `POST /api/drivers/scan`，刷新列表 |
| 行操作 | 查看详情、在 DriverLab 中测试 |
| 空状态 | 无 Driver 时显示引导提示 |

### 3.2 详情页 Tab 结构

| Tab | 内容 | 数据来源 |
|-----|------|----------|
| 元数据 | 基本信息（ID/名称/版本/描述/厂商/入口/Capabilities/Profiles）| `meta.info` |
| 命令 | 命令列表 + 每个命令的参数详情表格 | `meta.commands` |
| 文档 | 后端生成的 Markdown 文档渲染展示 | `GET /api/drivers/{id}/docs?format=markdown` |

### 3.3 新增依赖

```json
{
  "dependencies": {
    "react-markdown": "^9.x",
    "remark-gfm": "^4.x"
  }
}
```

> **说明**：`react-markdown` 用于将后端返回的 Markdown 文档渲染为 React 组件。`remark-gfm` 插件支持 GFM 扩展语法（表格、删除线、任务列表等），后端 `DocGenerator::toMarkdown()` 生成的文档包含 GFM 表格。

### 3.4 命令详情展示

每个命令展示为可展开的卡片：

```
┌─ add (两数相加) ──────────────────────────────────┐
│                                                     │
│  参数:                                              │
│  ┌──────────┬────────┬──────┬────────┬───────────┐ │
│  │ 名称     │ 类型   │ 必填 │ 默认值 │ 描述      │ │
│  ├──────────┼────────┼──────┼────────┼───────────┤ │
│  │ a        │ Double │ ✓   │ -      │ 第一个数  │ │
│  │ b        │ Double │ ✓   │ -      │ 第二个数  │ │
│  └──────────┴────────┴──────┴────────┴───────────┘ │
│                                                     │
│  返回值: Double                                     │
│                                                     │
│  [在 DriverLab 中测试]                              │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 3.5 元数据 JSON 导出

详情页提供"导出元数据 JSON"按钮，将完整 `meta` 对象格式化为 JSON 并触发浏览器下载。

### 3.6 Zustand Store

```typescript
// src/stores/useDriversStore.ts
interface DriversState {
  drivers: DriverInfo[];
  currentDriver: DriverInfo | null;
  docsMarkdown: string | null;   // 文档 Tab 的 Markdown 内容
  docsLoading: boolean;
  loading: boolean;
  error: string | null;

  fetchDrivers: () => Promise<void>;
  fetchDriverDetail: (id: string) => Promise<void>;
  fetchDriverDocs: (id: string, format?: string) => Promise<string>;  // 返回文档内容
  scanDrivers: () => Promise<void>;
}
```

---

## 4. 实现方案

### 4.1 组件树

```
DriversPage (列表)
├── PageHeader (标题 + 扫描按钮)
├── SearchBar
└── DriversTable
    └── ActionMenu (详情/测试)

DriverDetailPage (详情)
├── PageHeader (返回 + 标题 + [在 DriverLab 中测试] 按钮)
├── Tabs
│   ├── MetadataTab
│   │   └── DriverInfoCard (基本信息)
│   ├── CommandsTab
│   │   └── CommandCard[] (可展开命令卡片)
│   │       ├── ParamsTable (参数表格)
│   │       └── ReturnType (返回值)
│   └── DocsTab
│       └── DriverDocs (Markdown 文档渲染)
├── DocExportButton (多格式文档导出)
└── ExportMetaButton
```

### 4.2 CommandCard 组件

```typescript
// src/components/Drivers/CommandCard.tsx
interface CommandCardProps {
  command: CommandMeta;
  driverId: string;
  onTest: (commandName: string) => void;
}
```

### 4.3 ParamsTable 组件

```typescript
// src/components/Drivers/ParamsTable.tsx
interface ParamsTableProps {
  params: FieldMeta[];
}
```

表格列：名称、类型、必填、默认值、描述、约束（min/max/pattern 等）。

嵌套参数（Object 类型的 fields）使用缩进或展开子表格展示。

### 4.4 DriverDocs 组件

调用后端 `GET /api/drivers/{id}/docs?format=markdown` 获取 Markdown 文档，前端渲染展示。

```typescript
// src/components/Drivers/DriverDocs.tsx
interface DriverDocsProps {
  driverId: string;
}
```

实现要点：
- 组件挂载时调用 `GET /api/drivers/{id}/docs?format=markdown` 获取文档内容
- 使用 `react-markdown`（或 `marked` + `DOMPurify`）将 Markdown 渲染为 HTML
- 代码块使用等宽字体 + 深色背景样式（与全局暗色主题一致）
- 表格使用 Ant Design 风格样式覆盖
- 加载中显示骨架屏，加载失败显示错误提示和重试按钮

后端 `DocGenerator::toMarkdown()` 生成的文档结构：
- Driver 基本信息（名称/版本/描述/厂商）
- 命令参考（每个命令的签名、参数表格含嵌套字段和约束、返回值）
- 配置项（config 字段列表）
- 类型定义（types）
- 错误码（errors）

### 4.5 DocExportButton 组件

提供多格式文档导出，复用后端 `DocGenerator` 的三种输出格式：

```typescript
// src/components/Drivers/DocExportButton.tsx
interface DocExportButtonProps {
  driverId: string;
}
```

下拉菜单提供三种导出选项：

| 选项 | API 调用 | 下载文件名 |
|------|---------|-----------|
| Markdown | `?format=markdown` | `{driverId}.md` |
| HTML | `?format=html` | `{driverId}.html` |
| TypeScript | `?format=typescript` | `{driverId}.d.ts` |

实现：调用 API 获取内容，构造 Blob 触发浏览器下载。

---

## 5. 文件变更清单

### 5.1 新增文件

**页面**：
- `src/webui/src/pages/Drivers/index.tsx` — 列表页（替换占位）
- `src/webui/src/pages/Drivers/Detail.tsx` — 详情页（替换占位）

**组件**：
- `src/webui/src/components/Drivers/DriversTable.tsx`
- `src/webui/src/components/Drivers/DriverInfoCard.tsx`
- `src/webui/src/components/Drivers/CommandCard.tsx`
- `src/webui/src/components/Drivers/ParamsTable.tsx`
- `src/webui/src/components/Drivers/DriverDocs.tsx`
- `src/webui/src/components/Drivers/DocExportButton.tsx`
- `src/webui/src/components/Drivers/ExportMetaButton.tsx`

**Store**：
- `src/webui/src/stores/useDriversStore.ts`

**测试**：
- `src/webui/src/__tests__/pages/Drivers.test.tsx`
- `src/webui/src/__tests__/pages/DriverDetail.test.tsx`
- `src/webui/src/__tests__/components/DriversTable.test.tsx`
- `src/webui/src/__tests__/components/CommandCard.test.tsx`
- `src/webui/src/__tests__/components/ParamsTable.test.tsx`
- `src/webui/src/__tests__/components/DriverDocs.test.tsx`
- `src/webui/src/__tests__/components/DocExportButton.test.tsx`
- `src/webui/src/__tests__/components/ExportMetaButton.test.tsx`
- `src/webui/src/__tests__/stores/useDriversStore.test.ts`

### 5.2 修改文件

- `src/stdiolink_server/http/api_router.cpp` — 注册 `GET /api/drivers/{id}/docs` 路由
- `src/stdiolink_server/http/api_router.h` — 添加 `handleDriverDocs` 处理方法声明
- `src/webui/package.json` — 添加 `react-markdown`、`remark-gfm` 依赖

---

## 6. 测试与验收

### 6.1 单元测试场景

**DriversTable（DriversTable.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染驱动列表 | 显示 ID、名称、版本、命令数 |
| 2 | 空列表 | 显示"暂无驱动" |
| 3 | 搜索过滤 | 输入关键词后列表过滤 |
| 4 | 点击行 | 导航到详情页 |
| 5 | 扫描按钮 | 调用 `driversApi.scan()` |

**CommandCard（CommandCard.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 6 | 渲染命令名称和描述 | 显示 "add (两数相加)" |
| 7 | 展开参数表格 | 显示参数列表 |
| 8 | 无参数命令 | 显示"无参数" |
| 9 | 测试按钮 | 触发 onTest 回调 |
| 10 | 返回值类型 | 显示返回值类型 |

**ParamsTable（ParamsTable.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 11 | 渲染参数列表 | 显示名称、类型、必填、默认值、描述 |
| 12 | 必填标记 | 必填参数显示 ✓ |
| 13 | 约束显示 | min/max 约束显示在描述列 |
| 14 | 嵌套参数 | Object 类型的子字段正确展示 |
| 15 | 空参数列表 | 显示"无参数" |

**DriverDocs（DriverDocs.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 16 | 加载文档 | 调用 `GET /api/drivers/{id}/docs?format=markdown` |
| 17 | 渲染 Markdown | 标题、表格、代码块正确渲染 |
| 18 | GFM 表格支持 | 参数表格正确渲染为 HTML table |
| 19 | 加载中状态 | 显示骨架屏 |
| 20 | 加载失败 | 显示错误提示和重试按钮 |
| 21 | 点击重试 | 重新调用 API |
| 22 | 无 meta 数据 | 显示"元数据不可用" |

**DocExportButton（DocExportButton.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 23 | 渲染下拉菜单 | 显示 Markdown/HTML/TypeScript 三个选项 |
| 24 | 导出 Markdown | 调用 `?format=markdown`，下载 `{driverId}.md` |
| 25 | 导出 HTML | 调用 `?format=html`，下载 `{driverId}.html` |
| 26 | 导出 TypeScript | 调用 `?format=typescript`，下载 `{driverId}.d.ts` |
| 27 | 导出失败 | 显示错误提示 |

**ExportMetaButton（ExportMetaButton.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 28 | 点击导出 | 触发文件下载 |
| 29 | 文件名格式 | `{driverId}_meta.json` |
| 30 | JSON 格式化 | 内容为格式化的 JSON |

**useDriversStore（useDriversStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 31 | `fetchDrivers()` 成功 | drivers 列表更新 |
| 32 | `fetchDrivers()` 失败 | error 被设置 |
| 33 | `fetchDriverDetail(id)` | currentDriver 更新，含完整 meta |
| 34 | `fetchDriverDocs(id)` 成功 | docsMarkdown 更新 |
| 35 | `fetchDriverDocs(id)` 失败 | error 被设置，docsMarkdown 为 null |
| 36 | `fetchDriverDocs(id, 'html')` | 返回 HTML 内容 |
| 37 | `scanDrivers()` | 调用扫描 API 并刷新列表 |

### 6.2 验收标准

- Drivers 列表页正确展示、搜索、扫描
- Driver 详情页三个 Tab 正常工作
- 命令列表正确展示参数详情
- 嵌套参数正确渲染
- 文档 Tab 正确调用后端 API 并渲染 Markdown（标题/表格/代码块）
- 多格式文档导出正常（Markdown/HTML/TypeScript）
- 元数据 JSON 导出正常
- "在 DriverLab 中测试"跳转正常
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：DriverMeta 结构复杂，字段可能缺失
  - 控制：所有字段使用可选链访问（`meta?.info?.name`），缺失时显示"未知"或隐藏对应区域
- **风险 2**：嵌套参数层级过深
  - 控制：递归渲染深度限制为 5 层

---

## 7. 风险与控制

- **风险 1**：DriverMeta 结构复杂，字段可能缺失
  - 控制：所有字段使用可选链访问（`meta?.info?.name`），缺失时显示"未知"或隐藏对应区域
- **风险 2**：嵌套参数层级过深
  - 控制：递归渲染深度限制为 5 层

---

## 8. UI/UX 设计师建议

Drivers 模块主要面向开发者，设计应侧重于文档的易读性和信息的结构化展示：

1.  **文档阅读体验 (Documentation Readability)**：
    *   **排版节奏**：Markdown 渲染内容的标题 (`h1`-`h3`) 上方应保留充足的间距 (`32px`+)，段落行高设定为 `1.7`，提升长文阅读舒适度。
    *   **表格样式**：参数表格应使用斑马纹 (`Surface-Layer1` / `Surface-Base`) 或仅保留水平分割线，表头文字加粗并使用较浅的灰色背景。

2.  **代码块样式 (Code Blocks)**：
    *   **对比度**：文档中的代码块背景色应比页面背景略亮 (`#1E222D` vs `#0F1117`)，形成卡片式的视觉包裹感。
    *   **字体**：代码内容强制使用 `JetBrains Mono`，字号可比正文略小 (`13px`)。

3.  **操作按钮 (Action Buttons)**：
    *   **导出按钮**：文档导出和元数据导出属于低频操作，建议使用“幽灵按钮” (Ghost Button) 或纯图标按钮，避免在页面上产生过多的视觉噪点。
    *   **测试入口**：“在 DriverLab 中测试”是核心引导操作，可以使用 Primary 色（Indigo）的文字链接或小尺寸按钮，置于显眼位置（如命令卡片右上角）。

---

## 9. 里程碑完成定义（DoD）

- 后端 `GET /api/drivers/{id}/docs` 端点实现，支持三种格式（Markdown/HTML/TypeScript）
- Drivers 列表页完整实现
- Driver 详情页（元数据/命令/文档）完整实现
- 文档 Tab 调用后端 API 渲染 Markdown 文档
- 多格式文档导出功能正常（Markdown/HTML/TypeScript）
- 命令参数详情正确展示
- 元数据导出功能正常
- 对应单元测试完成并通过
- 本里程碑文档入库
