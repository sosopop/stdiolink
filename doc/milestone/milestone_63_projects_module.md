# 里程碑 63：Projects 项目编排模块

> **前置条件**: 里程碑 62 已完成（Services 模块已就绪）
> **目标**: 实现 Projects 模块的完整 UI，包含列表页（筛选/搜索/批量操作）、创建向导（选择 Service → 基本信息 → Schema 驱动智能表单 → 调度策略）、详情页（概览/配置/实例/日志/调度）、运行控制（启动/停止/重载）

---

## 1. 目标

- 实现 Projects 列表页（搜索、按 Service/状态/启用筛选、分页）
- 实现 Project 创建向导（4 步：选择 Service → 基本信息 → 配置参数 → 调度策略）
- 实现 Schema 驱动的智能表单（根据 Service 的 config.schema.json 自动生成表单控件）
- 实现 Project 详情页（概览 Tab、配置 Tab、实例 Tab、日志 Tab、调度 Tab）
- 实现运行控制操作（启动/停止/重载/启用开关）
- 实现 Project 编辑（配置修改、调度策略修改）
- 实现 Project 删除（确认对话框，运行中项目需先停止）
- 实现 Zustand Projects Store
- 单元测试覆盖所有组件和 Store

---

## 2. 背景与问题

Projects 是 stdiolink 的核心工作对象，用户通过 Project 实例化 Service 并管理其生命周期。设计文档 §5.3 定义了完整的 Project 管理交互，核心亮点是 Schema 驱动的智能表单——根据 Service 的 `config.schema.json` 自动生成配置表单，支持类型校验、分组、条件显示等高级特性。

API 依赖（均已实现）：
- `GET/POST /api/projects` — 列表/创建
- `GET/PUT/DELETE /api/projects/{id}` — 详情/更新/删除
- `POST /api/projects/{id}/start|stop|reload` — 运行控制
- `GET /api/projects/{id}/runtime` — 运行时状态
- `GET /api/projects/runtime` — 批量运行时查询
- `PATCH /api/projects/{id}/enabled` — 启用开关
- `GET /api/projects/{id}/logs` — 日志
- `POST /api/projects/{id}/validate` — 验证
- `GET /api/services/{id}` — 获取 Service Schema
- `POST /api/services/{id}/generate-defaults` — 生成默认配置
- `POST /api/services/{id}/validate-config` — 验证配置

---

## 3. 技术要点

### 3.1 列表页

功能：
- 搜索框（按 ID/名称模糊搜索）
- 筛选器：Service 下拉、状态（运行中/已停止/错误）、启用状态
- 表格列：ID、名称、Service、状态（带指示灯）、启用开关、操作
- 操作列：查看详情、启动/停止、删除
- 批量运行时查询（`GET /api/projects/runtime`）获取实时状态

### 3.2 创建向导

4 步向导，使用 Ant Design `Steps` 组件：

**步骤 1：选择 Service**
- 显示所有可用 Service 列表（卡片式选择）
- 搜索过滤
- 选中后显示 Service 基本信息

**步骤 2：基本信息**
- Project ID（正则校验 `^[a-zA-Z0-9_-]+$`）
- 名称（必填）
- 启用开关（默认启用）

**步骤 3：配置参数（Schema 驱动智能表单）**
- 根据 Service 的 `config.schema.json` 自动生成表单
- 调用 `POST /api/services/{id}/generate-defaults` 获取默认值
- 实时校验（调用 `POST /api/services/{id}/validate-config`）
- 支持重置为默认值

**步骤 4：调度策略**
- 调度类型选择：Manual / Daemon / FixedRate
- Daemon 配置：重启延迟、最大连续失败次数
- FixedRate 配置：执行间隔、最大并发数

### 3.3 Schema 驱动智能表单

核心组件 `SchemaForm`，根据 FieldMeta 递归生成表单控件：

```typescript
interface SchemaFormProps {
  schema: FieldMeta;          // Service 的 config schema
  value: Record<string, unknown>;
  onChange: (value: Record<string, unknown>) => void;
  errors?: Record<string, string>;
}
```

Schema 类型到 UI 组件的映射规则（对齐设计文档 §5.3）：

| Schema 字段类型 | 默认 UI 组件 | 规则 |
|----------------|--------------|------|
| String | Input | 支持 minLength/maxLength/pattern；ui.placeholder 映射占位符 |
| Int / Int64 | InputNumber | 支持 min/max；ui.step 映射步进 |
| Double | InputNumber | 支持小数与 min/max/step |
| Bool | Switch | 布尔开关 |
| Enum | Select / Radio | 选项来自 constraints.enumValues |
| Object | Collapse + FieldSet | 递归渲染 fields |
| Array | Dynamic List | 递归渲染 items；支持 minItems/maxItems |
| Any | JSON Editor | 高级字段，JSON 输入框 |

UI Hint 支持：
- `group`：分组标题（Collapse 面板）
- `order`：字段排序
- `placeholder`：占位符
- `advanced`：高级选项折叠
- `readonly`：只读展示
- `unit`：单位标签
- `step`：数值步进

### 3.4 详情页 Tab 结构

| Tab | 内容 | API |
|-----|------|-----|
| 概览 | 基本信息 + 运行时信息 + 配置摘要 | `GET /projects/{id}` + `GET /projects/{id}/runtime` |
| 配置 | Schema 驱动表单（可编辑） | `GET /projects/{id}` + `PUT /projects/{id}` |
| 实例 | 当前/历史实例列表 | `GET /instances?projectId=xxx`（前端过滤） |
| 日志 | 项目级日志查看器 | `GET /projects/{id}/logs` |
| 调度 | 调度策略配置（可编辑） | `GET /projects/{id}` + `PUT /projects/{id}` |

### 3.5 运行控制

```typescript
// 操作按钮状态逻辑
const canStart = project.enabled && project.valid && runtime?.status !== 'running';
const canStop = runtime?.status === 'running';
const canReload = true;  // 始终可用
```

操作确认：
- 停止：确认对话框（"确定停止项目 {name}？"）
- 删除：二次确认（输入项目 ID 确认）
- 运行中项目删除：需先停止

### 3.6 Projects Store

```typescript
// src/webui/src/stores/useProjectsStore.ts
interface ProjectsState {
  projects: Project[];
  runtimes: Map<string, ProjectRuntime>;
  currentProject: Project | null;
  currentRuntime: ProjectRuntime | null;
  loading: boolean;
  error: string | null;

  fetchProjects: () => Promise<void>;
  fetchProjectDetail: (id: string) => Promise<void>;
  fetchRuntimes: () => Promise<void>;
  createProject: (data: CreateProjectRequest) => Promise<boolean>;
  updateProject: (id: string, data: UpdateProjectRequest) => Promise<boolean>;
  deleteProject: (id: string) => Promise<boolean>;
  startProject: (id: string) => Promise<boolean>;
  stopProject: (id: string) => Promise<boolean>;
  reloadProject: (id: string) => Promise<boolean>;
  setEnabled: (id: string, enabled: boolean) => Promise<boolean>;
}
```

---

## 4. 文件变更清单

### 4.1 新增文件

- `src/webui/src/pages/Projects/index.tsx` — 列表页（替换占位）
- `src/webui/src/pages/Projects/Detail.tsx` — 详情页（替换占位）
- `src/webui/src/pages/Projects/components/ProjectTable.tsx` — 项目表格
- `src/webui/src/pages/Projects/components/ProjectCreateWizard.tsx` — 创建向导
- `src/webui/src/pages/Projects/components/ProjectOverview.tsx` — 概览 Tab
- `src/webui/src/pages/Projects/components/ProjectConfig.tsx` — 配置 Tab
- `src/webui/src/pages/Projects/components/ProjectInstances.tsx` — 实例 Tab
- `src/webui/src/pages/Projects/components/ProjectLogs.tsx` — 日志 Tab
- `src/webui/src/pages/Projects/components/ProjectSchedule.tsx` — 调度 Tab
- `src/webui/src/pages/Projects/components/ScheduleForm.tsx` — 调度策略表单
- `src/webui/src/components/SchemaForm/SchemaForm.tsx` — Schema 驱动表单（核心）
- `src/webui/src/components/SchemaForm/FieldRenderer.tsx` — 字段渲染器
- `src/webui/src/components/SchemaForm/fields/StringField.tsx` — 字符串字段
- `src/webui/src/components/SchemaForm/fields/NumberField.tsx` — 数值字段
- `src/webui/src/components/SchemaForm/fields/BoolField.tsx` — 布尔字段
- `src/webui/src/components/SchemaForm/fields/EnumField.tsx` — 枚举字段
- `src/webui/src/components/SchemaForm/fields/ObjectField.tsx` — 对象字段
- `src/webui/src/components/SchemaForm/fields/ArrayField.tsx` — 数组字段
- `src/webui/src/components/SchemaForm/fields/AnyField.tsx` — Any 字段（JSON 编辑器）
- `src/webui/src/components/LogViewer/LogViewer.tsx` — 日志查看器（可复用）
- `src/webui/src/stores/useProjectsStore.ts` — Projects Store

### 4.2 测试文件

- `src/webui/src/pages/Projects/__tests__/ProjectList.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectDetail.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectCreateWizard.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectOverview.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectConfig.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectSchedule.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/SchemaForm.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/FieldRenderer.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/StringField.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/NumberField.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/BoolField.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/EnumField.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/ObjectField.test.tsx`
- `src/webui/src/components/SchemaForm/__tests__/ArrayField.test.tsx`
- `src/webui/src/components/LogViewer/__tests__/LogViewer.test.tsx`
- `src/webui/src/stores/__tests__/useProjectsStore.test.ts`

---

## 5. 测试与验收

### 5.1 单元测试场景

**Projects 列表（ProjectList.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染项目列表 | 表格显示所有 Project |
| 2 | 搜索过滤 | 输入关键词后列表过滤 |
| 3 | Service 筛选 | 选择 Service 后仅显示对应项目 |
| 4 | 状态筛选 | 选择"运行中"后仅显示运行中项目 |
| 5 | 启用开关 | 切换开关调用 setEnabled |
| 6 | 启动按钮 | 调用 startProject |
| 7 | 停止按钮 | 确认后调用 stopProject |
| 8 | 状态指示灯 | 运行中=绿色，停止=灰色，错误=红色 |

**创建向导（ProjectCreateWizard.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 9 | 步骤 1：选择 Service | Service 列表可选 |
| 10 | 步骤 2：ID 格式校验 | 非法字符提示错误 |
| 11 | 步骤 2：ID 为空 | 下一步按钮禁用 |
| 12 | 步骤 3：Schema 表单生成 | 根据 Schema 生成对应控件 |
| 13 | 步骤 3：默认值填充 | 调用 generateDefaults 后填充 |
| 14 | 步骤 3：实时校验 | 输入后调用 validateConfig |
| 15 | 步骤 4：调度类型选择 | Manual/Daemon/FixedRate 可选 |
| 16 | 步骤 4：Daemon 配置 | 显示重启延迟和最大失败次数 |
| 17 | 创建成功 | 向导关闭，列表刷新 |
| 18 | 创建失败 | 显示错误消息 |

**SchemaForm（SchemaForm.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 19 | String 字段 | 渲染 Input 组件 |
| 20 | Int 字段 | 渲染 InputNumber 组件 |
| 21 | Bool 字段 | 渲染 Switch 组件 |
| 22 | Enum 字段 | 渲染 Select 组件，选项正确 |
| 23 | Object 字段 | 递归渲染子字段 |
| 24 | Array 字段 | 渲染动态列表，支持增删 |
| 25 | 必填标记 | required 字段显示红色星号 |
| 26 | 默认值 | 字段初始值为 defaultValue |
| 27 | min/max 约束 | InputNumber 设置 min/max |
| 28 | group 分组 | 同 group 字段在同一 Collapse 面板 |
| 29 | advanced 折叠 | advanced=true 的字段默认隐藏 |
| 30 | placeholder | 占位符文本正确显示 |
| 31 | 值变更回调 | onChange 返回完整配置对象 |
| 32 | 错误显示 | errors 对应字段显示红色提示 |

**字段组件**：

| # | 场景 | 验证点 |
|---|------|--------|
| 33 | StringField pattern 校验 | 不匹配 pattern 时显示错误 |
| 34 | NumberField step | 步进按钮按 step 增减 |
| 35 | EnumField 多选项 | 所有 enumValues 显示为选项 |
| 36 | ObjectField 嵌套 | 子字段正确渲染 |
| 37 | ArrayField 添加项 | 点击添加按钮增加一项 |
| 38 | ArrayField 删除项 | 点击删除按钮移除一项 |
| 39 | ArrayField minItems | 低于 minItems 时删除按钮禁用 |
| 40 | ArrayField maxItems | 达到 maxItems 时添加按钮禁用 |
| 41 | AnyField JSON 编辑 | 渲染 JSON 文本框 |

**LogViewer（LogViewer.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 42 | 渲染日志行 | 每行显示时间戳和内容 |
| 43 | 空日志 | 显示"暂无日志" |
| 44 | 自动滚动 | 新日志到达时滚动到底部 |
| 45 | 级别过滤 | 选择 ERROR 后仅显示错误日志 |

**Projects Store（useProjectsStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 46 | fetchProjects 成功 | projects 列表更新 |
| 47 | fetchRuntimes 成功 | runtimes Map 更新 |
| 48 | createProject 成功 | 返回 true |
| 49 | updateProject 成功 | currentProject 更新 |
| 50 | deleteProject 成功 | 从列表移除 |
| 51 | startProject 成功 | runtime 状态更新 |
| 52 | stopProject 成功 | runtime 状态更新 |
| 53 | setEnabled 成功 | project.enabled 更新 |

### 5.2 验收标准

- Projects 列表页正确渲染、搜索和筛选
- 创建向导 4 步流程完整可用
- Schema 驱动智能表单正确生成所有字段类型
- 详情页五个 Tab 正常切换
- 运行控制操作（启动/停止/重载）正常工作
- 启用开关正常工作
- 日志查看器正常显示
- 全部单元测试通过

---

## 6. 风险与控制

- **风险 1**：Schema 驱动表单对复杂嵌套结构支持不完整
  - 控制：首版支持 2 层嵌套（Object 内嵌基础类型）；深层嵌套降级为 JSON 编辑器
- **风险 2**：配置校验延迟影响用户体验
  - 控制：校验请求使用 debounce（300ms）；校验期间显示 loading 状态
- **风险 3**：运行控制操作的并发冲突
  - 控制：操作按钮在请求期间禁用；后端返回 409 时显示友好提示

---

## 7. 里程碑完成定义（DoD）

- Projects 列表页实现
- Project 创建向导实现（4 步）
- Schema 驱动智能表单实现
- Project 详情页实现（5 个 Tab）
- 运行控制操作实现
- LogViewer 组件实现
- Projects Store 实现
- 对应单元测试完成并通过
- 本里程碑文档入库
