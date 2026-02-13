# 里程碑 68：Schema 可视化编辑器

> **前置条件**: 里程碑 62 已完成（Services 模块已就绪，Schema Tab 已有只读展示），里程碑 63 已完成（SchemaForm 组件已实现只读渲染）
> **目标**: 实现 Schema 可视化编辑器：双向同步的可视化编辑与 JSON 编辑、字段 CRUD 操作、约束编辑、UI Hint 编辑、类型定义管理，支持在 Service 详情页中编辑 `config.schema.json`

---

## 1. 目标

- 实现 Schema 可视化编辑器：以表单方式编辑 Schema 字段定义
- 实现 JSON 编辑模式：Monaco Editor 直接编辑 Schema JSON，与可视化视图双向同步
- 实现字段 CRUD：添加/编辑/删除/排序字段
- 实现字段属性编辑：类型、必填、默认值、描述、约束（min/max/pattern/enumValues）
- 实现 UI Hint 编辑：group、order、advanced、readonly、placeholder、unit
- 实现嵌套字段编辑：Object 类型的子字段递归编辑、Array 类型的 items 编辑
- 实现 Schema 验证：编辑后调用 `POST /api/services/{id}/validate-schema` 验证
- 实现 Schema 保存：保存到 `config.schema.json` 文件

---

## 2. 背景与问题

M62 中 Schema Tab 仅以只读表格展示字段列表和 JSON 原文。开发者需要手动编辑 JSON 来修改 Schema，门槛较高且容易出错。可视化编辑器让用户通过表单操作来定义配置字段，降低 Schema 编写门槛。

**范围**：Schema 可视化编辑器（双模式）+ 字段 CRUD + 约束编辑 + UI Hint 编辑。

**与 M62 的关系**：本里程碑的 `SchemaEditor` 组件将替换 M62 中 Service 详情页的 Schema Tab（只读表格 + JSON 预览）。M62 的 Schema Tab 作为初始实现保留到 M68 完成后被替换，无需在 M62 中预留扩展点。修改文件清单中的 `Services/Detail.tsx` 即为此替换操作。

**非目标**：Schema 版本管理、Schema 导入导出模板。

---

## 3. 技术要点

### 3.1 双模式编辑器布局

```
┌─────────────────────────────────────────────────────────┐
│  Schema 编辑器                    [可视化] [JSON] [预览] │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  可视化模式：                                            │
│  ┌─────────────────────────────────────────────────────┐│
│  │ [+ 添加字段]                                        ││
│  │                                                     ││
│  │ ┌─ host (String, 必填) ──────────────── [编辑][删除]││
│  │ │  描述: 数据库主机地址                              ││
│  │ │  默认值: localhost                                 ││
│  │ │  约束: pattern=^[a-zA-Z0-9.-]+$                   ││
│  │ │  UI: placeholder="请输入主机地址"                   ││
│  │ └───────────────────────────────────────────────────││
│  │                                                     ││
│  │ ┌─ port (Int, 必填) ────────────────── [编辑][删除] ││
│  │ │  描述: 端口号                                     ││
│  │ │  默认值: 3306                                     ││
│  │ │  约束: min=1, max=65535                           ││
│  │ │  UI: unit="端口"                                  ││
│  │ └───────────────────────────────────────────────────││
│  │                                                     ││
│  │ ┌─ options (Object) ───────────── [编辑][展开][删除]││
│  │ │  ├─ timeout (Int)                                 ││
│  │ │  └─ retries (Int)                                 ││
│  │ └───────────────────────────────────────────────────││
│  └─────────────────────────────────────────────────────┘│
│                                                         │
│  [验证 Schema]  [保存]  [重置]                           │
└─────────────────────────────────────────────────────────┘
```

### 3.2 字段编辑对话框

点击"编辑"或"添加字段"时弹出对话框：

```
┌─ 编辑字段 ──────────────────────────────────┐
│                                              │
│  基本信息                                    │
│  ┌──────────────────────────────────────────┐│
│  │ 字段名:  [host                         ] ││
│  │ 类型:    [String ▼]                      ││
│  │ 必填:    [✓]                             ││
│  │ 描述:    [数据库主机地址                 ] ││
│  │ 默认值:  [localhost                     ] ││
│  └──────────────────────────────────────────┘│
│                                              │
│  约束                                        │
│  ┌──────────────────────────────────────────┐│
│  │ (根据类型动态显示)                        ││
│  │ String: minLength / maxLength / pattern   ││
│  │ Int/Double: min / max / step              ││
│  │ Array: minItems / maxItems                ││
│  │ Enum: enumValues (可编辑列表)             ││
│  └──────────────────────────────────────────┘│
│                                              │
│  UI 提示                                     │
│  ┌──────────────────────────────────────────┐│
│  │ 分组 (group):     [                    ] ││
│  │ 排序 (order):     [0                   ] ││
│  │ 高级选项:         [□]                    ││
│  │ 只读:             [□]                    ││
│  │ 占位符:           [请输入主机地址       ] ││
│  │ 单位:             [                    ] ││
│  └──────────────────────────────────────────┘│
│                                              │
│                        [取消]  [确定]         │
└──────────────────────────────────────────────┘
```

### 3.3 类型到约束的映射

| 字段类型 | 可用约束 |
|---------|---------|
| String | `minLength`、`maxLength`、`pattern` |
| Int / Int64 | `min`、`max`、`step` |
| Double | `min`、`max`、`step` |
| Bool | 无 |
| Enum | `enumValues`（字符串列表编辑器） |
| Object | 无（通过子字段编辑） |
| Array | `minItems`、`maxItems` + `items` 类型定义 |
| Any | 无 |

### 3.4 双向同步机制

```
可视化编辑 ──→ 更新内部 SchemaNode[] ──→ 序列化为 config.schema.json 对象 ──→ 同步到 JSON 编辑器
JSON 编辑器 ──→ 解析 config.schema.json 对象 ──→ 反序列化为 SchemaNode[] ──→ 同步到可视化视图
```

同步规则：
- 可视化编辑：每次操作立即更新内部状态，JSON 视图实时反映
- JSON 编辑：用户停止输入 500ms 后尝试解析，解析成功则同步到可视化视图
- JSON 解析失败：JSON 编辑器显示错误标记，可视化视图保持上次有效状态
- 切换模式时：以当前活跃模式的数据为准

### 3.5 Schema JSON 结构

```typescript
// config.schema.json 文件结构（后端 ServiceConfigSchema::fromJsonObject 的输入）
// 注意：根是对象映射，不是 FieldMeta 数组
export type ServiceConfigSchema = Record<string, SchemaFieldDescriptor>;

export interface SchemaFieldDescriptor {
  type?: 'string' | 'int' | 'int64' | 'double' | 'bool' | 'object' | 'array' | 'enum' | 'any';
  required?: boolean;
  description?: string;
  default?: unknown;

  // constraints 是嵌套对象（与 config.schema.json 一致）
  constraints?: {
    min?: number;
    max?: number;
    minLength?: number;
    maxLength?: number;
    pattern?: string;
    enumValues?: unknown[]; // 后端会兼容转换为 enum
    format?: string;
    minItems?: number;
    maxItems?: number;
  };

  // object / array 描述
  fields?: Record<string, SchemaFieldDescriptor>;
  items?: SchemaFieldDescriptor;
}

// 可视化编辑内部模型（便于排序、路径编辑）
export interface SchemaNode {
  name: string;
  descriptor: SchemaFieldDescriptor;
  children?: SchemaNode[];
}
```

### 3.6 预览模式

第三个 Tab "预览"：使用 M63 的 SchemaForm 组件，以只读模式渲染 Schema 生成的表单，让用户预览最终效果。

### 3.7 Zustand Store

```typescript
// src/stores/useSchemaEditorStore.ts
interface SchemaEditorState {
  // Schema 数据
  nodes: SchemaNode[];
  originalNodes: SchemaNode[];  // 用于重置
  // 编辑状态
  activeMode: 'visual' | 'json' | 'preview';
  jsonText: string;
  jsonError: string | null;
  dirty: boolean;               // 是否有未保存的修改
  // 验证
  validationErrors: string[];
  validating: boolean;
  // 操作
  setNodes: (nodes: SchemaNode[]) => void;
  addField: (field: SchemaNode, parentPath?: string) => void;
  updateField: (path: string, field: SchemaNode) => void;
  removeField: (path: string) => void;
  moveField: (path: string, direction: 'up' | 'down') => void;
  setJsonText: (text: string) => void;
  syncFromJson: () => boolean;   // 返回是否解析成功
  syncToJson: () => void;
  setActiveMode: (mode: 'visual' | 'json' | 'preview') => void;
  validate: (serviceId: string) => Promise<void>;
  save: (serviceId: string) => Promise<void>;
  reset: () => void;
  loadSchema: (serviceId: string) => Promise<void>;
}
```

---

## 4. 实现方案

### 4.1 组件树

```
SchemaEditor (替换 M62 的 SchemaTab)
├── ModeSwitch (可视化 / JSON / 预览 切换)
├── VisualEditor (可视化模式)
│   ├── AddFieldButton
│   └── FieldCard[] (字段卡片列表)
│       ├── FieldSummary (字段摘要：名称/类型/必填/描述)
│       ├── FieldActions (编辑/删除/上移/下移)
│       └── NestedFields (Object 类型的子字段，递归)
├── JsonEditor (JSON 模式)
│   └── MonacoEditor (复用 M62 的 Monaco 封装)
├── PreviewEditor (预览模式)
│   └── SchemaForm (复用 M63 的 SchemaForm，readOnly)
├── FieldEditModal (字段编辑对话框)
│   ├── BasicInfoSection (名称/类型/必填/描述/默认值)
│   ├── ConstraintsSection (根据类型动态渲染约束)
│   ├── UiHintsSection (UI 提示编辑)
│   └── EnumValuesEditor (Enum 类型专用)
└── SchemaToolbar (验证/保存/重置)
```

### 4.2 FieldCard 组件

```typescript
// src/components/SchemaEditor/FieldCard.tsx
interface FieldCardProps {
  field: SchemaNode;
  path: string;           // 字段路径，如 "database.host"
  level: number;          // 嵌套层级
  onEdit: (path: string) => void;
  onDelete: (path: string) => void;
  onMove: (path: string, direction: 'up' | 'down') => void;
  onAddChild: (parentPath: string) => void;  // Object 类型添加子字段
}
```

嵌套渲染：
- Object 类型显示展开/折叠按钮，子字段缩进 24px 渲染
- Array 类型显示 items 类型定义（可编辑）
- 递归深度限制为 5 层

### 4.3 FieldEditModal 组件

```typescript
// src/components/SchemaEditor/FieldEditModal.tsx
interface FieldEditModalProps {
  visible: boolean;
  field: SchemaNode | null;    // null 表示新建
  parentType?: string;         // 父字段类型（用于限制子字段类型）
  existingNames: string[];     // 同级已有字段名（用于名称唯一性校验）
  onSave: (field: SchemaNode) => void;
  onCancel: () => void;
}
```

### 4.4 ConstraintsSection 组件

```typescript
// src/components/SchemaEditor/ConstraintsSection.tsx
interface ConstraintsSectionProps {
  type: string;
  constraints: Record<string, unknown>;
  onChange: (constraints: Record<string, unknown>) => void;
}
```

根据字段类型动态渲染约束输入：
- String：minLength (InputNumber) + maxLength (InputNumber) + pattern (Input)
- Int/Double：min (InputNumber) + max (InputNumber) + step (InputNumber)
- Array：minItems (InputNumber) + maxItems (InputNumber)
- Enum：EnumValuesEditor（动态列表，支持添加/删除/排序）

### 4.5 EnumValuesEditor 组件

```typescript
// src/components/SchemaEditor/EnumValuesEditor.tsx
interface EnumValuesEditorProps {
  values: string[];
  onChange: (values: string[]) => void;
}
```

动态列表：每行一个输入框 + 删除按钮，底部"添加选项"按钮。支持拖拽排序。

### 4.6 字段路径操作工具

```typescript
// src/utils/schemaPath.ts

// 根据路径获取字段
function getFieldByPath(nodes: SchemaNode[], path: string): SchemaNode | null;

// 根据路径更新字段
function updateFieldByPath(nodes: SchemaNode[], path: string, updater: (f: SchemaNode) => SchemaNode): SchemaNode[];

// 根据路径删除字段
function removeFieldByPath(nodes: SchemaNode[], path: string): SchemaNode[];

// 根据路径添加子字段
function addFieldToPath(nodes: SchemaNode[], parentPath: string, field: SchemaNode): SchemaNode[];

// 移动字段（上/下）
function moveFieldInPath(nodes: SchemaNode[], path: string, direction: 'up' | 'down'): SchemaNode[];

// config.schema.json 对象与内部节点模型互转
function schemaToNodes(schema: ServiceConfigSchema): SchemaNode[];
function nodesToSchema(nodes: SchemaNode[]): ServiceConfigSchema;

// JSON 文本与 schema 对象互转
function schemaToJson(schema: ServiceConfigSchema): string;
function jsonToSchema(json: string): ServiceConfigSchema;
```

---

## 5. 文件变更清单

### 5.1 新增文件

**组件**：
- `src/webui/src/components/SchemaEditor/SchemaEditor.tsx` — 编辑器主组件
- `src/webui/src/components/SchemaEditor/VisualEditor.tsx` — 可视化编辑模式
- `src/webui/src/components/SchemaEditor/JsonEditor.tsx` — JSON 编辑模式
- `src/webui/src/components/SchemaEditor/PreviewEditor.tsx` — 预览模式
- `src/webui/src/components/SchemaEditor/FieldCard.tsx` — 字段卡片
- `src/webui/src/components/SchemaEditor/FieldEditModal.tsx` — 字段编辑对话框
- `src/webui/src/components/SchemaEditor/ConstraintsSection.tsx` — 约束编辑区
- `src/webui/src/components/SchemaEditor/UiHintsSection.tsx` — UI Hint 编辑区
- `src/webui/src/components/SchemaEditor/EnumValuesEditor.tsx` — Enum 值列表编辑器
- `src/webui/src/components/SchemaEditor/SchemaToolbar.tsx` — 工具栏

**Store**：
- `src/webui/src/stores/useSchemaEditorStore.ts`

**工具函数**：
- `src/webui/src/utils/schemaPath.ts` — 字段路径操作工具

**测试**：
- `src/webui/src/__tests__/components/SchemaEditor.test.tsx`
- `src/webui/src/__tests__/components/VisualEditor.test.tsx`
- `src/webui/src/__tests__/components/FieldCard.test.tsx`
- `src/webui/src/__tests__/components/FieldEditModal.test.tsx`
- `src/webui/src/__tests__/components/ConstraintsSection.test.tsx`
- `src/webui/src/__tests__/components/EnumValuesEditor.test.tsx`
- `src/webui/src/__tests__/stores/useSchemaEditorStore.test.ts`
- `src/webui/src/__tests__/utils/schemaPath.test.ts`

### 5.2 修改文件

- `src/webui/src/pages/Services/Detail.tsx` — Schema Tab 替换为 SchemaEditor 组件

---

## 6. 测试与验收

### 6.1 单元测试场景

**FieldCard（FieldCard.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染字段摘要 | 显示名称、类型、必填标记、描述 |
| 2 | String 类型 | 显示约束（minLength/maxLength/pattern） |
| 3 | Int 类型 | 显示约束（min/max） |
| 4 | Enum 类型 | 显示可选值列表 |
| 5 | Object 类型 | 显示子字段数量，可展开 |
| 6 | 展开 Object | 子字段缩进渲染 |
| 7 | 点击编辑 | 触发 onEdit 回调 |
| 8 | 点击删除 | 触发 onDelete 回调 |
| 9 | 上移/下移 | 触发 onMove 回调 |
| 10 | 第一个字段 | 上移按钮禁用 |
| 11 | 最后一个字段 | 下移按钮禁用 |

**FieldEditModal（FieldEditModal.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 12 | 新建字段 | 表单为空，标题为"添加字段" |
| 13 | 编辑字段 | 表单预填现有值，标题为"编辑字段" |
| 14 | 字段名校验 | 空名称提示错误 |
| 15 | 字段名唯一性 | 与同级已有字段重名提示错误 |
| 16 | 类型切换 | 切换类型后约束区域动态更新 |
| 17 | String 约束 | 显示 minLength/maxLength/pattern 输入 |
| 18 | Int 约束 | 显示 min/max/step 输入 |
| 19 | Enum 值编辑 | 显示 EnumValuesEditor |
| 20 | UI Hint 编辑 | 显示 group/order/advanced/readonly/placeholder/unit |
| 21 | 保存 | 触发 onSave 回调，传递完整 SchemaNode |
| 22 | 取消 | 触发 onCancel 回调，不保存 |

**ConstraintsSection（ConstraintsSection.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 23 | String 类型 | 渲染 minLength/maxLength/pattern |
| 24 | Int 类型 | 渲染 min/max/step |
| 25 | Double 类型 | 渲染 min/max/step（支持小数） |
| 26 | Array 类型 | 渲染 minItems/maxItems |
| 27 | Bool 类型 | 无约束，显示提示 |
| 28 | 值变更 | 触发 onChange 回调 |

**EnumValuesEditor（EnumValuesEditor.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 29 | 渲染值列表 | 每个值一行输入框 |
| 30 | 添加选项 | 列表末尾新增空行 |
| 31 | 删除选项 | 对应行移除 |
| 32 | 编辑选项 | 值变更触发 onChange |
| 33 | 空列表 | 显示"添加选项"按钮 |

**VisualEditor（VisualEditor.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 34 | 渲染字段列表 | 所有字段以 FieldCard 展示 |
| 35 | 添加字段 | 打开 FieldEditModal |
| 36 | 空 Schema | 显示引导提示 + 添加按钮 |
| 37 | Object 添加子字段 | 打开 FieldEditModal，parentPath 正确 |

**SchemaEditor（SchemaEditor.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 38 | 模式切换 | 可视化/JSON/预览 Tab 切换 |
| 39 | 可视化→JSON 同步 | 可视化编辑后 JSON 视图更新 |
| 40 | JSON→可视化同步 | JSON 编辑后可视化视图更新 |
| 41 | JSON 解析错误 | 显示错误提示，可视化视图不变 |
| 42 | 预览模式 | 渲染 SchemaForm 组件 |

**useSchemaEditorStore（useSchemaEditorStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 43 | `loadSchema()` | 从 API 加载 Schema，nodes 更新 |
| 44 | `addField()` | 顶层添加字段 |
| 45 | `addField()` 嵌套 | Object 下添加子字段 |
| 46 | `updateField()` | 更新指定路径的字段 |
| 47 | `removeField()` | 删除指定路径的字段 |
| 48 | `moveField()` 上移 | 字段顺序变更 |
| 49 | `moveField()` 下移 | 字段顺序变更 |
| 50 | `syncFromJson()` 成功 | JSON 解析为 schema 对象并同步为 nodes |
| 51 | `syncFromJson()` 失败 | jsonError 被设置，nodes 保持不变 |
| 52 | `syncToJson()` | nodes 序列化为 config.schema.json JSON |
| 53 | `validate()` | 调用验证 API，更新 validationErrors |
| 54 | `save()` | 调用文件写入 API |
| 55 | `reset()` | nodes 恢复为 originalNodes |
| 56 | `dirty` 标记 | 修改后 dirty=true，保存/重置后 dirty=false |

**schemaPath 工具（schemaPath.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 57 | `getFieldByPath()` 顶层 | 返回正确字段 |
| 58 | `getFieldByPath()` 嵌套 | 返回嵌套字段 |
| 59 | `getFieldByPath()` 不存在 | 返回 null |
| 60 | `updateFieldByPath()` | 更新后返回新数组 |
| 61 | `removeFieldByPath()` | 删除后返回新数组 |
| 62 | `addFieldToPath()` | 添加到指定父路径 |
| 63 | `moveFieldInPath()` 上移 | 顺序正确 |
| 64 | `moveFieldInPath()` 下移 | 顺序正确 |
| 65 | `schemaToJson()` | 序列化格式正确（根对象映射） |
| 66 | `jsonToSchema()` | 反序列化结构正确 |
| 67 | `jsonToSchema()` 非法 JSON | 抛出异常 |

### 6.2 验收标准

- Schema 可视化编辑器三种模式正常切换
- 字段 CRUD 操作正常（添加/编辑/删除/排序）
- 所有字段类型的约束编辑正常
- UI Hint 编辑正常
- 嵌套字段（Object/Array）编辑正常
- 可视化与 JSON 双向同步正常
- 预览模式正确渲染表单
- Schema 验证功能正常
- Schema 保存功能正常
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：双向同步导致编辑冲突或数据丢失
  - 控制：切换模式时以当前活跃模式数据为准；JSON 解析失败时保留可视化视图数据不变；每次同步前做深拷贝
- **风险 2**：深层嵌套字段编辑复杂度高
  - 控制：递归深度限制为 5 层；超出后提示用户使用 JSON 模式编辑
- **风险 3**：Schema 保存后 Service reload 失败
  - 控制：复用 M62 的文件保存逻辑，reload 失败时提示"已保存但热加载失败"

---

## 7. 风险与控制

- **风险 1**：双向同步导致编辑冲突或数据丢失
  - 控制：切换模式时以当前活跃模式数据为准；JSON 解析失败时保留可视化视图数据不变；每次同步前做深拷贝
- **风险 2**：深层嵌套字段编辑复杂度高
  - 控制：递归深度限制为 5 层；超出后提示用户使用 JSON 模式编辑
- **风险 3**：Schema 保存后 Service reload 失败
  - 控制：复用 M62 的文件保存逻辑，reload 失败时提示用户"已保存但热加载失败"

---

## 8. UI/UX 设计师建议 (针对 Style 06)

Schema 编辑器是高复杂度的配置工具，设计目标是在玻璃态环境中构建清晰的层级：

1.  **深度感构建 (Depth via Opacity)**：
    *   **嵌套层级**：在玻璃态背景下，深层嵌套不宜使用厚重的边框。建议为每一层嵌套容器叠加极低透明度的白色背景 (如 `rgba(255,255,255,0.02)`)。这种“层层叠加”的方式能自然地构建出视觉深度，且不破坏通透感。
    *   **连接线**：辅助垂直连接线应使用 `var(--border-subtle)`，保持隐约可见即可。

2.  **幽灵操作区 (Ghost Actions)**：
    *   **添加按钮**：“添加字段”按钮建议设计为虚线边框的 Ghost 按钮，颜色使用低调的 `var(--text-tertiary)`。仅在悬停时，边框变为实线并发出微弱的 Brand Glow，暗示其可交互性。

3.  **模式切换反馈**：
    *   **Tab 切换**：可视化/JSON 模式切换时，内容区域应有轻微的 `fade-in` 动画，避免生硬的跳变。

---

## 9. 里程碑完成定义（DoD）

- Schema 可视化编辑器完整实现（三种模式）
- 字段 CRUD 操作完整
- 约束和 UI Hint 编辑完整
- 双向同步正常
- Schema 验证和保存正常
- 对应单元测试完成并通过
- 本里程碑文档入库
