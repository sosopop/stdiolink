# 里程碑 93：Schema Visual Editor 缺陷修复与能力补全（TDD）

> **前置条件**: 里程碑 90（array<object> 配置能力改造）已完成；`FieldEditModal.tsx`、`FieldCard.tsx`、`schemaPath.ts`、`useSchemaEditorStore.ts`、`PreviewEditor.tsx`、`ArrayField.tsx` 现存代码经静态审查确认存在本文档所列缺陷
> **目标**: 以 TDD 方式修复 Schema Visual Editor 中 10 处已知缺陷，使 Visual 模式下 `array<object>` 可视化编辑能力完整、编辑操作不丢数据、模式切换行为安全

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `schemaPath.ts` 类型与路径工具 | 补齐 `SchemaFieldDescriptor` 缺失属性（`requiredKeys`、`additionalProperties`、`ui`）；`schemaToNodes`/`nodesToSchema` 支持 `array.items.fields` 双向映射 |
| `FieldEditModal.tsx` 编辑弹窗 | 保存逻辑改为增量合并，不丢失未暴露键；新增 `items.type` 选择控件；禁止字段名含 `.` |
| `FieldCard.tsx` 字段卡片 | 展开逻辑从 `type==='object'` 扩展为 `type==='object' \|\| (type==='array' && items.type==='object')`，支持 array<object> 子节点可视化 |
| `useSchemaEditorStore.ts` 状态管理 | `setActiveMode` 在 `syncFromJson()` 失败时阻止模式切换 |
| `PreviewEditor.tsx` 预览 | `descriptorToFieldMeta` 补传 `ui`、`requiredKeys`、`additionalProperties` |
| `ArrayField.tsx` 表单 | "Add Item" 按钮文案国际化 |
| i18n `locales/*.json` | 新增 4 个翻译键 |
| 测试 | 新增 Vitest 单元测试 19 条，覆盖所有修复路径 |

- Visual 模式下编辑已有 `array` 字段后，`descriptor.items`、`requiredKeys`、`additionalProperties` 原样保留
- Visual 模式下可为 `array` 字段选择 `items.type`，当 `items.type=object` 时可通过展开树编辑子字段
- `schemaToNodes` ↔ `nodesToSchema` 对含 `items.fields` 的 schema 实现无损往返
- JSON 非法时切换到 Visual/Preview 被阻止，保留在 JSON 模式并显示错误
- 字段名含 `.` 时弹窗校验报错
- Preview 模式应用 schema 中的 `ui` 属性（widget、placeholder、readonly 等）
- "Add Item" 按钮使用 i18n 翻译
- 新增测试全部通过，现有测试套件无回归

---

## 2. 背景与问题

里程碑 90 完成了 C++ 后端和 SchemaForm 渲染端的 `array<object>` 支持，但 Schema Visual Editor（可视化编辑器）仍存在以下独立缺陷，导致编辑体验不完整甚至数据丢失：

- **缺陷 A**（P0）：`FieldEditModal.handleOk` 全量重建 `descriptor`，编辑 array 字段后 `items` 等未暴露键丢失
- **缺陷 B**（P0）：`object` 的 `requiredKeys`/`additionalProperties` 无编辑入口，且编辑后被清空
- **缺陷 C**（P1）：Visual 新建 `array` 时无法配置 `items.type`，预览退化为 `JSON.stringify`
- **缺陷 D**（P3）：`schemaPath.ts` 中所有路径函数使用 `split('.')` 解析，字段名含 `.` 会导致错位
- **缺陷 E**（P2）：`setActiveMode` 在 `syncFromJson()` 失败时忽略返回值，仍切换 `activeMode`
- **缺陷 F**（P1）：`schemaToNodes` 仅处理 `desc.fields`，不展开 `desc.items.fields`，导致 Visual 模式看不到 array 子字段
- **缺陷 G**（P1）：`FieldCard` 仅对 `type==='object'` 展示子节点，`array` 类型无子节点展开 UI
- **缺陷 H**（P0）：`SchemaFieldDescriptor` 类型缺少 `requiredKeys`、`additionalProperties`、`ui` 属性定义，TypeScript 类型系统阻碍合并保留
- **缺陷 I**（P3）：`PreviewEditor.descriptorToFieldMeta` 未映射 `ui`、`requiredKeys`、`additionalProperties`，Preview 模式不生效
- **缺陷 J**（P3）：`ArrayField` 的 "Add Item" 按钮文案硬编码英文，未国际化

**范围**：
- `src/webui/src/utils/schemaPath.ts`（缺陷 D、F、H）
- `src/webui/src/components/SchemaEditor/FieldEditModal.tsx`（缺陷 A、B、C、D）
- `src/webui/src/components/SchemaEditor/FieldCard.tsx`（缺陷 G）
- `src/webui/src/stores/useSchemaEditorStore.ts`（缺陷 E）
- `src/webui/src/components/SchemaEditor/PreviewEditor.tsx`（缺陷 I）
- `src/webui/src/components/SchemaForm/fields/ArrayField.tsx`（缺陷 J）
- `src/webui/src/locales/*.json`（i18n）
- 对应单元测试文件

**非目标**：
- 不实现 `requiredKeys`/`additionalProperties` 的 Visual 编辑控件（本期仅确保编辑不丢失，完整 UI 控件留后续里程碑）
- 不支持三层及以上嵌套 `array<array<object>>` 的 Visual 编辑（JSON Editor 降级可用）
- 不引入 JSON Pointer 替换路径模型（本期仅做字段名 `.` 校验拦截，对已存在于 JSON/历史数据中的含点字段名不做迁移——此类字段在 Visual 模式下仍可能路径错位，需用 JSON 模式编辑）
- 不变更后端 C++ 代码

---

## 3. 技术要点

### 3.1 `SchemaFieldDescriptor` 类型补齐（Before / After）

**Before**：类型缺少 `requiredKeys`、`additionalProperties`、`ui`，代码被迫用 `as any` 绕行：

```typescript
// schemaPath.ts — 当前
export interface SchemaFieldDescriptor {
  type?: '...' | '...';
  required?: boolean;
  description?: string;
  default?: unknown;
  constraints?: { /* ... */ };
  fields?: Record<string, SchemaFieldDescriptor>;
  items?: SchemaFieldDescriptor;
  // ❌ 缺少 requiredKeys、additionalProperties、ui
}
```

**After**：与后端 `service_config_schema.cpp:48,62-68` 和前端 `FieldMeta`（`service.ts:60-64`）完全对齐：

```typescript
export interface SchemaFieldDescriptor {
  type?: 'string' | 'int' | 'int64' | 'double' | 'bool' | 'object' | 'array' | 'enum' | 'any';
  required?: boolean;
  description?: string;
  default?: unknown;
  constraints?: { /* 不变 */ };
  fields?: Record<string, SchemaFieldDescriptor>;
  items?: SchemaFieldDescriptor;
  requiredKeys?: string[];            // ✅ 新增
  additionalProperties?: boolean;      // ✅ 新增
  ui?: {                               // ✅ 新增
    widget?: string;
    group?: string;
    order?: number;
    placeholder?: string;
    advanced?: boolean;
    readonly?: boolean;
    visibleIf?: string;
    unit?: string;
    step?: number;
  };
}
```

### 3.2 `FieldEditModal.handleOk` 增量合并策略

**核心决策**：保存时以原 `field?.descriptor` 为基准 clone，仅覆盖当前 UI 可编辑键，未暴露键原样保留。

```
保存流程：
  base = isEdit ? { ...field.descriptor } : {}
  ↓
  覆盖 UI 可编辑键: type, required, description, default, constraints, ui
  ↓
  用户清空的键: delete 对应属性
  ↓
  未暴露键 (items, fields, requiredKeys, additionalProperties) 自动随 base 保留
  ↓
  输出 { name, descriptor: merged }
```

**当用户变更 type 时的清理规则**：

| 原 type → 新 type | 清理的保留键 | 理由 |
|---|---|---|
| `array` → 非 `array` | 删除 `descriptor.items` | items 对非数组无意义 |
| `object` → 非 `object` | 删除 `descriptor.fields`、`descriptor.requiredKeys`、`descriptor.additionalProperties` | fields 对非对象无意义 |
| 其他变更 | 无需清理 | constraints 已由 `ConstraintsSection` 按 type 切换时重置 |

### 3.3 `schemaToNodes`/`nodesToSchema` 对 `array.items.fields` 的双向映射

**关键设计决策**：`array<object>` 的 `items.fields` 在 Node 树中复用 `children` 字段，与 `object.fields` 共享同一棵可编辑子节点树。映射方向由 `descriptor.type` 决定回写目标。

```
schemaToNodes:
  if desc.type === 'array' && desc.items?.type === 'object' && desc.items?.fields:
    node.children = schemaToNodes(desc.items.fields)

nodesToSchema:
  if node.children.length > 0:
    if desc.type === 'array' && desc.items?.type === 'object':
      desc.items = { ...desc.items, fields: nodesToSchema(children) }
    else if desc.type !== 'array':
      desc.fields = nodesToSchema(children)
    // array 但 items.type !== 'object' 时忽略 children，避免静默类型改写
```

> **关键约束**：`nodesToSchema` 仅在 `items.type === 'object'` 时才将 children 回写到 `items.fields`，避免对 `array<string>` 等非 object 元素产生静默类型改写。

**向后兼容**：
- 无 `items.fields` 的 `array`（如 `array<string>`）不产生 children，行为不变
- 仅有 `fields` 的 `object` 类型，行为不变

### 3.4 `setActiveMode` 模式切换守卫

**Before**：`syncFromJson()` 返回值被忽略

```typescript
setActiveMode: (mode) => {
    if (prev === 'json' && mode !== 'json') {
      get().syncFromJson();  // 返回值被忽略
    }
    set({ activeMode: mode });  // 无论成败都切换
},
```

**After**：失败时阻止切换

```typescript
setActiveMode: (mode) => {
    if (prev === 'json' && mode !== 'json') {
      const ok = get().syncFromJson();
      if (!ok) return;  // JSON 无效 → 不切换，jsonError 已设置
    }
    set({ activeMode: mode });
},
```

---

## 4. 实现步骤（TDD Red-Green-Refactor）

### 4.1 Red — 编写失败测试

每条测试在修复前必须运行并确认失败（**Red**）。

#### 4.1.1 `schemaPath.test.ts` 失败测试

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R01` | F | 含 `items.fields` 的 array schema | `node.children` 为 `undefined` | `node.children.length === 2` |
| `R02` | F | `schemaToNodes → nodesToSchema` 往返 | `items.fields` 丢失 | schema 与输入完全一致 |

```typescript
// src/webui/src/utils/__tests__/schemaPath.test.ts（新增）

const arrayObjectSchema: ServiceConfigSchema = {
  radars: {
    type: 'array',
    items: {
      type: 'object',
      fields: {
        ip: { type: 'string', required: true },
        port: { type: 'int', default: 502 },
      },
    },
  },
};

// R01
it('schemaToNodes 将 array.items.fields 展开为 children', () => {
  const nodes = schemaToNodes(arrayObjectSchema);
  expect(nodes[0].children).toHaveLength(2);       // ← 修复前失败
  expect(nodes[0].children![0].name).toBe('ip');
  expect(nodes[0].children![1].name).toBe('port');
});

// R02
it('array<object> schema 经 schemaToNodes→nodesToSchema 往返无损', () => {
  const nodes = schemaToNodes(arrayObjectSchema);
  const back = nodesToSchema(nodes);
  expect(back).toEqual(arrayObjectSchema);           // ← 修复前失败
});
```

#### 4.1.2 `FieldEditModal.test.tsx` 失败测试

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R03` | A | 编辑含 `items` 的 array 字段，仅修改 description | `items` 丢失 | `items` 原样保留 |
| `R04` | B | 编辑含 `requiredKeys` 的 object 字段 | `requiredKeys` 丢失 | `requiredKeys` 保留 |
| `R05` | D | 新建字段名为 `radar.port` | 正常保存 | 报错阻止保存 |

```typescript
// src/webui/src/components/SchemaEditor/__tests__/FieldEditModal.test.tsx（新增）

// R03
it('编辑 array 字段时保留未暴露的 items 属性', async () => {
  const field: SchemaNode = {
    name: 'radars',
    descriptor: {
      type: 'array',
      items: { type: 'object', fields: { ip: { type: 'string' } } },
    },
  };
  const onSave = vi.fn();
  wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
  fireEvent.change(screen.getByTestId('field-description-input'),
    { target: { value: 'Radar list' } });
  fireEvent.click(screen.getByText('OK'));
  await waitFor(() => {
    expect(onSave).toHaveBeenCalled();
    const saved = onSave.mock.calls[0][0];
    expect(saved.descriptor.items).toBeDefined();                       // ← 修复前失败
    expect(saved.descriptor.items.fields.ip).toBeDefined();
    expect(saved.descriptor.description).toBe('Radar list');
  });
});

// R04
it('编辑 object 字段时保留 requiredKeys', async () => {
  const field: SchemaNode = {
    name: 'server',
    descriptor: {
      type: 'object',
      requiredKeys: ['host', 'port'],
      additionalProperties: false,
    },
  };
  const onSave = vi.fn();
  wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
  fireEvent.change(screen.getByTestId('field-description-input'),
    { target: { value: 'Server config' } });
  fireEvent.click(screen.getByText('OK'));
  await waitFor(() => {
    expect(onSave).toHaveBeenCalled();
    const saved = onSave.mock.calls[0][0];
    expect(saved.descriptor.requiredKeys).toEqual(['host', 'port']); // ← 修复前失败
    expect(saved.descriptor.additionalProperties).toBe(false);
  });
});

// R05
it('字段名含 . 时报错阻止保存', async () => {
  const onSave = vi.fn();
  wrap(<FieldEditModal {...defaultProps} onSave={onSave} />);
  fireEvent.change(screen.getByTestId('field-name-input'),
    { target: { value: 'radar.port' } });
  fireEvent.click(screen.getByText('OK'));
  await waitFor(() => {
    expect(screen.getByText(/cannot contain/i)).toBeInTheDocument(); // ← 修复前失败
  });
  expect(onSave).not.toHaveBeenCalled();
});
```

#### 4.1.3 `FieldCard.test.tsx` 失败测试

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R06` | G | array 字段含 children | 不显示展开按钮，不渲染子节点 | 显示展开按钮并渲染子节点 |

```typescript
// src/webui/src/components/SchemaEditor/__tests__/FieldCard.test.tsx（新增）

// R06
it('array 类型字段展示展开按钮和子节点', () => {
  const field: SchemaNode = {
    name: 'radars',
    descriptor: { type: 'array' },
    children: [
      { name: 'ip', descriptor: { type: 'string' } },
    ],
  };
  render(
    <FieldCard
      field={field} path="radars" level={0}
      isFirst isLast
      onEdit={vi.fn()} onDelete={vi.fn()} onMove={vi.fn()} onAddChild={vi.fn()}
    />
  );
  expect(screen.getByTestId('field-toggle-radars')).toBeInTheDocument();   // ← 修复前失败
  expect(screen.getByTestId('field-card-radars.ip')).toBeInTheDocument();
});
```

#### 4.1.4 `useSchemaEditorStore` 失败测试

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R07` | E | 设置非法 JSON 后尝试切换到 visual | `activeMode` 变为 `'visual'` | `activeMode` 仍为 `'json'` |

```typescript
// src/webui/src/stores/__tests__/useSchemaEditorStore.test.ts（扩展已有文件，追加用例）
import { describe, it, expect, beforeEach } from 'vitest';
import { useSchemaEditorStore } from '../useSchemaEditorStore';

describe('useSchemaEditorStore', () => {
  beforeEach(() => {
    useSchemaEditorStore.setState({
      nodes: [],
      originalNodes: [],
      activeMode: 'json',
      jsonText: '{}',
      jsonError: null,
      dirty: false,
      validationErrors: [],
      validating: false,
      saving: false,
    });
  });

  // R07
  it('syncFromJson 失败时 setActiveMode 不切换', () => {
    const store = useSchemaEditorStore.getState();
    store.setJsonText('invalid json!!!');
    useSchemaEditorStore.getState().setActiveMode('visual');
    expect(useSchemaEditorStore.getState().activeMode).toBe('json');   // ← 修复前失败
    expect(useSchemaEditorStore.getState().jsonError).not.toBeNull();
  });

  // R08 — 正常 JSON 切换成功（回归验证）
  it('合法 JSON 时 setActiveMode 正常切换', () => {
    const store = useSchemaEditorStore.getState();
    store.setJsonText('{"host": {"type": "string"}}');
    useSchemaEditorStore.getState().setActiveMode('visual');
    expect(useSchemaEditorStore.getState().activeMode).toBe('visual');
    expect(useSchemaEditorStore.getState().jsonError).toBeNull();
  });
});
```

#### 4.1.5 `PreviewEditor.test.tsx` 失败测试

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R09` | I | 含 `ui` 属性的 descriptor | 转换后 `meta.ui` 为 `undefined` | `meta.ui` 保留 |

```typescript
// src/webui/src/components/SchemaEditor/__tests__/PreviewEditor.test.tsx（新增）
import { nodesToFieldMeta } from '../PreviewEditor';
import type { SchemaNode } from '@/utils/schemaPath';

// R09
it('descriptorToFieldMeta 传递 ui 属性', () => {
  const nodes: SchemaNode[] = [{
    name: 'timeout',
    descriptor: {
      type: 'int',
      ui: { widget: 'slider', unit: 'ms', readonly: true },
    },
  }];
  const fields = nodesToFieldMeta(nodes);
  expect(fields[0].ui).toBeDefined();                             // ← 修复前失败
  expect(fields[0].ui?.widget).toBe('slider');
  expect(fields[0].ui?.unit).toBe('ms');
  expect(fields[0].ui?.readonly).toBe(true);
});

// R10 — requiredKeys 和 additionalProperties 传递
it('descriptorToFieldMeta 传递 requiredKeys 和 additionalProperties', () => {
  const nodes: SchemaNode[] = [{
    name: 'server',
    descriptor: {
      type: 'object',
      requiredKeys: ['host'],
      additionalProperties: false,
    },
  }];
  const fields = nodesToFieldMeta(nodes);
  expect(fields[0].requiredKeys).toEqual(['host']);               // ← 修复前失败
  expect(fields[0].additionalProperties).toBe(false);
});
```

**Red 阶段确认**：在提交任何修复代码前，运行上述全部用例并记录失败输出，留存为 Red 证据。

---

### 4.2 Green — 最小修复实现

#### 改动 1：`src/webui/src/utils/schemaPath.ts`

修复缺陷 F 和 H。

**步骤 1-a：补齐 `SchemaFieldDescriptor` 类型**：

```typescript
export interface SchemaFieldDescriptor {
  type?: 'string' | 'int' | 'int64' | 'double' | 'bool' | 'object' | 'array' | 'enum' | 'any';
  required?: boolean;
  description?: string;
  default?: unknown;
  constraints?: {
    min?: number;
    max?: number;
    minLength?: number;
    maxLength?: number;
    pattern?: string;
    enumValues?: unknown[];
    format?: string;
    minItems?: number;
    maxItems?: number;
  };
  fields?: Record<string, SchemaFieldDescriptor>;
  items?: SchemaFieldDescriptor;
  requiredKeys?: string[];            // ✅ 新增
  additionalProperties?: boolean;      // ✅ 新增
  ui?: {                               // ✅ 新增
    widget?: string;
    group?: string;
    order?: number;
    placeholder?: string;
    advanced?: boolean;
    readonly?: boolean;
    visibleIf?: string;
    unit?: string;
    step?: number;
  };
}
```

**步骤 1-b：`schemaToNodes` 支持 `items.fields`**：

```typescript
export function schemaToNodes(schema: ServiceConfigSchema): SchemaNode[] {
  return Object.entries(schema).map(([name, desc]) => {
    const node: SchemaNode = { name, descriptor: desc };
    if (desc.type === 'array' && desc.items?.type === 'object' && desc.items?.fields) {
      // array<object>：仅 items.type==='object' 时展开 items.fields 为可编辑子节点
      node.children = schemaToNodes(desc.items.fields);
    } else if (desc.fields) {
      // object：原有逻辑
      node.children = schemaToNodes(desc.fields);
    }
    return node;
  });
}
```

**步骤 1-c：`nodesToSchema` 按 type 回写**：

```typescript
export function nodesToSchema(nodes: SchemaNode[]): ServiceConfigSchema {
  const schema: ServiceConfigSchema = {};
  for (const node of nodes) {
    const desc = { ...node.descriptor };
    if (node.children && node.children.length > 0) {
      if (desc.type === 'array' && desc.items?.type === 'object') {
        // 仅 items.type==='object' 时回写 children 到 items.fields
        desc.items = { ...desc.items, fields: nodesToSchema(node.children) };
      } else if (desc.type !== 'array') {
        // object 或其他非 array 类型
        desc.fields = nodesToSchema(node.children);
      }
      // array 但 items.type !== 'object'：忽略 children，避免静默类型改写
    }
    schema[node.name] = desc;
  }
  return schema;
}
```

**改动理由**：使 Visual 模式的节点树能感知并编辑 `array<object>` 的子字段结构。

**验收方式**：`R01`、`R02` 由 Red 变为 Green。

---

#### 改动 2：`src/webui/src/components/SchemaEditor/FieldEditModal.tsx`

修复缺陷 A、B、C、D。

**步骤 2-a：`handleOk` 改为增量合并**：

```typescript
const handleOk = () => {
  const trimmed = name.trim();
  if (!trimmed) {
    setNameError(t('schema.field_name_required'));
    return;
  }
  // ✅ 新增：禁止字段名含 .
  if (trimmed.includes('.')) {
    setNameError(t('schema.field_name_no_dot'));
    return;
  }
  const otherNames = isEdit
    ? existingNames.filter((n) => n !== field!.name)
    : existingNames;
  if (otherNames.includes(trimmed)) {
    setNameError(t('schema.field_name_exists'));
    return;
  }

  // ✅ 改为增量合并：以原 descriptor 为基准
  const base: SchemaFieldDescriptor = isEdit && field
    ? { ...field.descriptor }
    : {};
  const descriptor: SchemaFieldDescriptor = {
    ...base,           // 保留 items, fields, requiredKeys, additionalProperties 等
    type: type as any,
  };

  // 类型变更时清理不兼容的保留键
  if (type !== 'array') delete descriptor.items;
  if (type !== 'object') {
    delete descriptor.fields;
    delete descriptor.requiredKeys;
    delete descriptor.additionalProperties;
  }

  if (required) descriptor.required = true;
  else delete descriptor.required;
  if (description) descriptor.description = description;
  else delete descriptor.description;
  if (defaultValue) {
    try { descriptor.default = JSON.parse(defaultValue); }
    catch { descriptor.default = defaultValue; }
  } else delete descriptor.default;

  const cleanConstraints = Object.fromEntries(
    Object.entries(constraints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
  );
  if (Object.keys(cleanConstraints).length > 0) {
    descriptor.constraints = cleanConstraints as any;
  } else delete descriptor.constraints;

  const cleanHints = Object.fromEntries(
    Object.entries(uiHints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
  );
  if (Object.keys(cleanHints).length > 0) {
    descriptor.ui = cleanHints as any;
  } else delete descriptor.ui;

  // ✅ 新增：array 类型写入 items.type
  if (type === 'array' && itemsType) {
    descriptor.items = { ...(descriptor.items ?? {}), type: itemsType as any };
  }

  const node: SchemaNode = { name: trimmed, descriptor };
  if (isEdit && field!.children) {
    node.children = field!.children;
  }
  onSave(node);
};
```

**步骤 2-b：新增 `itemsType` state 和 UI**：

```typescript
// state 新增
const [itemsType, setItemsType] = useState<string>('string');

// useEffect 中初始化
if (field) {
  // ... 现有初始化 ...
  setItemsType(field.descriptor.items?.type ?? 'string');
}

// 渲染：在 Default Value Form.Item 之后、Collapse 之前
// itemsType 仅允许基础类型 + object，排除 array（不支持 array<array> Visual 编辑）
const ITEMS_TYPES = FIELD_TYPES.filter((ft) => ft !== 'array');

{type === 'array' && (
  <Form.Item label={t('schema.items_type')}>
    <Select
      value={itemsType}
      onChange={(v) => setItemsType(v)}
      data-testid="field-items-type-select"
      options={ITEMS_TYPES.map((ft) => ({ label: ft, value: ft }))}
    />
  </Form.Item>
)}
```

**验收方式**：`R03`、`R04`、`R05` 由 Red 变为 Green。

---

#### 改动 3：`src/webui/src/components/SchemaEditor/FieldCard.tsx`

修复缺陷 G。

```typescript
// 修改前
const hasChildren = desc.type === 'object' && field.children && field.children.length > 0;
const isObject = desc.type === 'object';

// 修改后：array 仅在 items.type==='object' 时视为容器
const isArrayObject = desc.type === 'array' && desc.items?.type === 'object';
const isContainer = desc.type === 'object' || isArrayObject;
const hasChildren = isContainer && field.children && field.children.length > 0;
```

所有引用 `isObject` 的地方替换为 `isContainer`（展开按钮、子节点渲染区、"Add Child" 按钮）。

array 类型子节点区域增加视觉标签：

```tsx
{desc.type === 'array' && expanded && (
  <Tag color="blue" style={{ marginLeft: (level + 1) * 24, marginBottom: 4 }}>
    {t('schema.array_item_fields')}
  </Tag>
)}
```

**验收方式**：`R06` 由 Red 变为 Green。

---

#### 改动 4：`src/webui/src/stores/useSchemaEditorStore.ts`

修复缺陷 E。

```typescript
setActiveMode: (mode) => {
  const prev = get().activeMode;
  if (prev === 'json' && mode !== 'json') {
    const ok = get().syncFromJson();
    if (!ok) return;  // ✅ JSON 无效时阻止切换
  }
  if (prev !== 'json' && mode === 'json') {
    get().syncToJson();
  }
  set({ activeMode: mode });
},
```

**验收方式**：`R07` 由 Red 变为 Green，`R08` 回归验证继续通过。

---

#### 改动 5：`src/webui/src/components/SchemaEditor/PreviewEditor.tsx`

修复缺陷 I。

```typescript
function descriptorToFieldMeta(name: string, descriptor: SchemaFieldDescriptor): FieldMeta {
  const meta: FieldMeta = {
    name,
    type: descriptor.type ?? 'any',
    required: descriptor.required,
    description: descriptor.description,
    default: descriptor.default,
    min: descriptor.constraints?.min,
    max: descriptor.constraints?.max,
    minLength: descriptor.constraints?.minLength,
    maxLength: descriptor.constraints?.maxLength,
    pattern: descriptor.constraints?.pattern,
    enum: descriptor.constraints?.enumValues,
    format: descriptor.constraints?.format,
    minItems: descriptor.constraints?.minItems,
    maxItems: descriptor.constraints?.maxItems,
    ui: descriptor.ui,                             // ✅ 新增
    requiredKeys: descriptor.requiredKeys,           // ✅ 新增
    additionalProperties: descriptor.additionalProperties,  // ✅ 新增
  };
  // ... fields / items 递归不变 ...
```

**验收方式**：`R09`、`R10` 由 Red 变为 Green。

---

#### 改动 6：`src/webui/src/components/SchemaForm/fields/ArrayField.tsx`

修复缺陷 J。

```typescript
// 顶部新增
import { useTranslation } from 'react-i18next';

// 组件内
const { t } = useTranslation();

// 按钮文案
<Button ... data-testid={`add-item-${field.name}`}>
  {t('schema.add_item')}   {/* 替换硬编码 "Add Item" */}
</Button>
```

**验收方式**：`R18` 验证按钮使用 i18n 翻译后文案正确渲染。

---

#### 改动 7：i18n 文件

在 `src/webui/src/locales/en.json` 的 `"schema"` 对象中追加（仅保留本次代码实际引用的键）：

```json
"items_type": "Items Type",
"array_item_fields": "Item Fields",
"field_name_no_dot": "Field name cannot contain '.'",
"add_item": "Add Item"
```

`zh.json` 追加：

```json
"items_type": "元素类型",
"array_item_fields": "元素字段",
"field_name_no_dot": "字段名不能包含 '.'",
"add_item": "添加项"
```

> 其余语言文件无需同步添加，项目已配置 `fallbackLng: 'en'`，缺失键自动回退到英文。

---

**Green 阶段确认**：完成全部改动后运行全量 Vitest 套件：

```powershell
cd d:\code\stdiolink\src\webui
npx vitest run
```

确认 `R01`–`R19` 全部通过（Green），现有测试套件无回归。

---

### 4.3 Refactor — 代码整理

1. **`FieldEditModal` 中 `as any` 清理**：补齐 `SchemaFieldDescriptor` 类型后，移除 `handleOk` 中 `(descriptor as any).ui = cleanHints` 等 `as any` 用法，改用类型安全的直接赋值。

2. **`FieldCard` 变量重命名**：将残留的 `isObject` 标识符统一替换为 `isContainer`，保持语义一致。

3. **`PreviewEditor` 旧 `as any` 清理**：`(field.descriptor as any).ui` 替换为 `field.descriptor.ui`。

4. **确认无死代码**：检查 `FieldEditModal` 原 `handleOk` 中 `(descriptor as any).ui = cleanHints` 是否已被新的类型安全赋值完全替代，删除残留。

重构后全量测试（`R01`–`R19` + 原有套件）必须仍全部通过。

---

## 5. 文件变更清单

### 5.1 新增文件

- 无

### 5.2 修改文件

| 文件 | 改动内容 |
|------|---------|
| `src/webui/src/utils/schemaPath.ts` | 补齐 `SchemaFieldDescriptor` 属性（缺陷 H）；`schemaToNodes`/`nodesToSchema` 支持 `items.fields`（缺陷 F） |
| `src/webui/src/components/SchemaEditor/FieldEditModal.tsx` | `handleOk` 增量合并（缺陷 A、B）；新增 `items.type` 选择器（缺陷 C）；字段名 `.` 校验（缺陷 D） |
| `src/webui/src/components/SchemaEditor/FieldCard.tsx` | `isObject` → `isContainer` 支持 array 展开（缺陷 G） |
| `src/webui/src/stores/useSchemaEditorStore.ts` | `setActiveMode` 失败阻止切换（缺陷 E） |
| `src/webui/src/components/SchemaEditor/PreviewEditor.tsx` | `descriptorToFieldMeta` 补传 `ui`/`requiredKeys`/`additionalProperties`（缺陷 I） |
| `src/webui/src/components/SchemaForm/fields/ArrayField.tsx` | "Add Item" 国际化（缺陷 J） |
| `src/webui/src/locales/en.json` | 新增 4 个 schema.* 翻译键 |
| `src/webui/src/locales/zh.json` | 新增 4 个 schema.* 翻译键（中文） |

### 5.3 测试文件

| 文件 | 改动内容 |
|------|---------|
| `src/webui/src/utils/__tests__/schemaPath.test.ts` | 新增 `R01`、`R02`、`R11`、`R19`（array items.fields 往返与守卫分支） |
| `src/webui/src/components/SchemaEditor/__tests__/FieldEditModal.test.tsx` | 新增 `R03`、`R04`、`R05`、`R12`、`R13`、`R14`（增量合并、`.` 校验、type 变更清理、itemsType 写入） |
| `src/webui/src/components/SchemaEditor/__tests__/FieldCard.test.tsx` | 新增 `R06`、`R15`（array 展开与非容器回归） |
| `src/webui/src/stores/__tests__/useSchemaEditorStore.test.ts` | 扩展已有文件，新增 `R07`、`R08`、`R16`（模式切换守卫+回归） |
| `src/webui/src/components/SchemaEditor/__tests__/PreviewEditor.test.tsx` | 新增 `R09`、`R10`、`R17`（ui/requiredKeys 传递与回归） |
| `src/webui/src/components/SchemaForm/__tests__/SchemaForm.test.tsx` | 扩展已有文件，新增 `R18`（ArrayField i18n 文案） |

---

## 6. 测试与验收

### 6.1 单元测试

- **测试对象**：`schemaToNodes`、`nodesToSchema`、`FieldEditModal.handleOk`、`FieldCard` 渲染逻辑、`useSchemaEditorStore.setActiveMode`、`PreviewEditor.descriptorToFieldMeta`
- **用例分层**：正常路径（R01-R04, R06, R08-R10）、边界值（R11-R14, R19）、异常输入（R05, R07）、回归（R15-R18）
- **断言要点**：返回值字段存在性与内容、DOM 元素存在（testId/文本）、状态值精确匹配
- **桩替身**：前端测试使用 `@testing-library/react` + Vitest `vi.fn()`；Store 测试直接使用 `useSchemaEditorStore.setState` 注入状态
- **测试文件**：见 §5.3

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `schemaToNodes`: `desc.type==='array' && desc.items?.type==='object' && desc.items?.fields` 存在 | 展开 items.fields 为 children | R01 |
| `schemaToNodes`: `desc.type==='array'` 但 `items.type!=='object'` 或 `items.fields` 不存在 | 不产生 children | R11 |
| `schemaToNodes`: `desc.type==='object' && desc.fields` 存在 | 展开 fields 为 children（原有行为） | 已有用例 |
| `nodesToSchema`: `desc.type==='array'` 且 `items.type==='object'` 且有 children | 回写到 `items.fields` | R02 |
| `nodesToSchema`: `desc.type==='object'` 且有 children | 回写到 `fields`（原有行为） | 已有用例 |
| `handleOk`: `isEdit=true` 且原 descriptor 含 items | base 展开保留 items | R03 |
| `handleOk`: `isEdit=true` 且原 descriptor 含 requiredKeys | base 展开保留 requiredKeys | R04 |
| `handleOk`: `isEdit=false`（新建模式） | base 为空对象 | R12 |
| `handleOk`: type 从 array 改为 string | 清理 items | R13 |
| `handleOk`: name 含 `.` | 报错，不调用 onSave | R05 |
| `handleOk`: type=array 时设置 itemsType | descriptor.items.type 写入 | R14 |
| `FieldCard`: `desc.type==='array'` 且 `items.type==='object'` 且有 children | 渲染展开按钮和子节点 | R06 |
| `FieldCard`: `desc.type==='string'`（非容器） | 不渲染展开按钮 | R15 |
| `setActiveMode`: JSON→Visual，syncFromJson 返回 false | 阻止切换，activeMode 不变 | R07 |
| `setActiveMode`: JSON→Visual，syncFromJson 返回 true | 正常切换 | R08 |
| `setActiveMode`: Visual→JSON | syncToJson 被调用 | R16 |
| `descriptorToFieldMeta`: descriptor 含 `ui` | meta.ui 正确传递 | R09 |
| `descriptorToFieldMeta`: descriptor 含 `requiredKeys`/`additionalProperties` | meta 对应属性传递 | R10 |
| `descriptorToFieldMeta`: descriptor 不含 `ui` | meta.ui 为 undefined | R17 |
| `ArrayField` "Add Item" 文案 | 使用 i18n `t('schema.add_item')` | R18 |
| `nodesToSchema`: `desc.type==='array'` 且 `items.type!=='object'` 但有 children | 忽略 children，不回写 | R19 |

覆盖要求（硬性）：所有可达路径 100% 有用例。

#### 用例详情

**R01 — schemaToNodes 将 array.items.fields 展开为 children**
- 前置条件：无
- 输入：`arrayObjectSchema`（radars 字段，items.type=object，items.fields 含 ip/port）
- 预期：`nodes[0].children.length === 2`，`children[0].name === 'ip'`
- 断言：`expect(nodes[0].children).toHaveLength(2)`

**R02 — array<object> schema 往返无损**
- 前置条件：无
- 输入：`schemaToNodes(arrayObjectSchema)` → `nodesToSchema(nodes)`
- 预期：输出与输入 `arrayObjectSchema` 深度相等
- 断言：`expect(back).toEqual(arrayObjectSchema)`

**R03 — 编辑 array 字段保留 items**
- 前置条件：render FieldEditModal，field.descriptor 含 items
- 输入：修改 description 后点击 OK
- 预期：onSave 被调用，saved.descriptor.items 存在且 items.fields.ip 存在
- 断言：`expect(saved.descriptor.items).toBeDefined()`

**R04 — 编辑 object 字段保留 requiredKeys**
- 前置条件：render FieldEditModal，field.descriptor 含 requiredKeys=['host','port']
- 输入：修改 description 后点击 OK
- 预期：saved.descriptor.requiredKeys 为 ['host','port']
- 断言：`expect(saved.descriptor.requiredKeys).toEqual(['host', 'port'])`

**R05 — 字段名含 . 时报错**
- 前置条件：render FieldEditModal，新建模式
- 输入：输入名称 `radar.port`，点击 OK
- 预期：显示错误文案，onSave 未调用
- 断言：`expect(screen.getByText(/cannot contain/i)).toBeInTheDocument()`

**R06 — array 类型展示展开按钮和子节点**
- 前置条件：render FieldCard，type=array，children 含 1 个子节点
- 输入：初始渲染
- 预期：展开按钮 `field-toggle-radars` 存在，子节点卡片 `field-card-radars.ip` 存在
- 断言：`expect(screen.getByTestId('field-toggle-radars')).toBeInTheDocument()`

**R07 — syncFromJson 失败时不切换**
- 前置条件：store.activeMode='json'，jsonText 设为非法 JSON
- 输入：调用 `setActiveMode('visual')`
- 预期：activeMode 仍为 'json'，jsonError 非空
- 断言：`expect(getState().activeMode).toBe('json')`

**R08 — 合法 JSON 正常切换（回归）**
- 前置条件：store.activeMode='json'，jsonText 为合法 JSON
- 输入：调用 `setActiveMode('visual')`
- 预期：activeMode 变为 'visual'，jsonError 为 null
- 断言：`expect(getState().activeMode).toBe('visual')`

**R09 — descriptorToFieldMeta 传递 ui 属性**
- 前置条件：构造含 ui 的 SchemaNode
- 输入：调用 `nodesToFieldMeta(nodes)`
- 预期：fields[0].ui.widget === 'slider'
- 断言：`expect(fields[0].ui?.widget).toBe('slider')`

**R10 — descriptorToFieldMeta 传递 requiredKeys/additionalProperties**
- 前置条件：构造含 requiredKeys 的 SchemaNode
- 输入：调用 `nodesToFieldMeta(nodes)`
- 预期：fields[0].requiredKeys === ['host']
- 断言：`expect(fields[0].requiredKeys).toEqual(['host'])`

**R11 — array<string> 无 items.fields 时不产生 children**
- 前置条件：无
- 输入：`{ tags: { type: 'array', items: { type: 'string' } } }`
- 预期：`nodes[0].children` 为 undefined
- 断言：`expect(nodes[0].children).toBeUndefined()`

**R12 — 新建模式 handleOk 以空 base 开始**
- 前置条件：render FieldEditModal，field=null
- 输入：填写 name='port'，点击 OK
- 预期：onSave 被调用，descriptor 不含 items/requiredKeys
- 断言：`expect(saved.descriptor.items).toBeUndefined()`

**R13 — type 从 array 改为 string 时清除 items**
- 前置条件：render FieldEditModal，field.descriptor.type='array' 含 items
- 输入：将 type 改为 string，点击 OK
- 预期：saved.descriptor.items 为 undefined
- 断言：`expect(saved.descriptor.items).toBeUndefined()`

**R14 — type=array 时 itemsType 写入 descriptor.items**
- 前置条件：render FieldEditModal，新建模式
- 输入：选 type=array，选 itemsType=object，点击 OK
- 预期：saved.descriptor.items.type === 'object'
- 断言：`expect(saved.descriptor.items?.type).toBe('object')`

**R15 — string 类型不渲染展开按钮（回归）**
- 前置条件：render FieldCard，type='string'，无 children
- 输入：初始渲染
- 预期：不存在 toggle 按钮
- 断言：`expect(screen.queryByTestId('field-toggle-host')).toBeNull()`

**R16 — Visual→JSON 触发 syncToJson（回归）**
- 前置条件：store.activeMode='visual'，nodes 非空
- 输入：调用 `setActiveMode('json')`
- 预期：activeMode='json'，jsonText 为 nodes 序列化结果
- 断言：`expect(getState().jsonText).toContain(nodes[0].name)`

**R17 — descriptor 不含 ui 时 meta.ui 为 undefined（回归）**
- 前置条件：构造不含 ui 的 SchemaNode
- 输入：调用 `nodesToFieldMeta(nodes)`
- 预期：fields[0].ui 为 undefined
- 断言：`expect(fields[0].ui).toBeUndefined()`

**R18 — ArrayField Add Item 按钮使用 i18n**
- 前置条件：切换 i18n 到 **中文**（`await i18n.changeLanguage('zh')`），render ArrayField（测试结束后恢复 `en`）
- 输入：查找 `add-item-radars` testId 对应按钮
- 预期：按钮文本为中文翻译值 `"添加项"`
- 断言：`expect(screen.getByTestId('add-item-radars')).toHaveTextContent('添加项')`
- Red 证据：修复前按钮文本为硬编码 `"Add Item"`，中文环境下仍显示 `"Add Item"` 而非 `"添加项"`，断言失败

**R19 — nodesToSchema 对 items.type!=='object' 的 array 忽略 children**
- 前置条件：构造 type='array'、items.type='string' 的节点，手动附加 children
- 输入：调用 `nodesToSchema([node])`
- 预期：输出 schema 中 items.type 仍为 'string'，无 items.fields
- 断言：`expect(back.tags.items?.type).toBe('string')` / `expect(back.tags.items?.fields).toBeUndefined()`

#### 测试代码

```typescript
// schemaPath.test.ts 新增
describe('array<object> round-trip', () => {
  const arrayObjectSchema: ServiceConfigSchema = {
    radars: {
      type: 'array',
      items: {
        type: 'object',
        fields: {
          ip: { type: 'string', required: true },
          port: { type: 'int', default: 502 },
        },
      },
    },
  };

  // R01
  it('schemaToNodes 将 items.fields 展开为 children', () => {
    const nodes = schemaToNodes(arrayObjectSchema);
    expect(nodes[0].children).toHaveLength(2);
    expect(nodes[0].children![0].name).toBe('ip');
  });

  // R02
  it('nodesToSchema 将 array children 回写到 items.fields', () => {
    const nodes = schemaToNodes(arrayObjectSchema);
    const back = nodesToSchema(nodes);
    expect(back).toEqual(arrayObjectSchema);
  });

  // R11
  it('array<string> 无 items.fields 不产生 children', () => {
    const schema: ServiceConfigSchema = {
      tags: { type: 'array', items: { type: 'string' } },
    };
    const nodes = schemaToNodes(schema);
    expect(nodes[0].children).toBeUndefined();
  });
});
```

```typescript
// useSchemaEditorStore.test.ts
describe('useSchemaEditorStore', () => {
  beforeEach(() => {
    useSchemaEditorStore.setState({
      nodes: [], originalNodes: [], activeMode: 'json',
      jsonText: '{}', jsonError: null, dirty: false,
      validationErrors: [], validating: false, saving: false,
    });
  });

  // R07
  it('syncFromJson 失败时 setActiveMode 不切换', () => {
    useSchemaEditorStore.getState().setJsonText('invalid!!!');
    useSchemaEditorStore.getState().setActiveMode('visual');
    expect(useSchemaEditorStore.getState().activeMode).toBe('json');
    expect(useSchemaEditorStore.getState().jsonError).not.toBeNull();
  });

  // R08
  it('合法 JSON 时正常切换', () => {
    useSchemaEditorStore.getState().setJsonText('{"host":{"type":"string"}}');
    useSchemaEditorStore.getState().setActiveMode('visual');
    expect(useSchemaEditorStore.getState().activeMode).toBe('visual');
  });

  // R16
  it('Visual→JSON 触发 syncToJson', () => {
    useSchemaEditorStore.setState({ activeMode: 'visual', nodes: [
      { name: 'port', descriptor: { type: 'int' } }
    ] });
    useSchemaEditorStore.getState().setActiveMode('json');
    expect(useSchemaEditorStore.getState().activeMode).toBe('json');
    expect(useSchemaEditorStore.getState().jsonText).toContain('port');
  });
});
```

### 6.2 集成 / 端到端测试

本里程碑纯前端修改，无跨进程通信变更。集成验证通过手动操作 WebUI 完成（见 §6.3）。

### 6.3 验收标准

- [ ] 编辑含 `items` 的 array 字段后 items 未丢失（对应 `R03`）
- [ ] 编辑含 `requiredKeys` 的 object 字段后 requiredKeys 未丢失（对应 `R04`）
- [ ] Visual 新建 array 字段可选择 items.type（对应 `R14`）
- [ ] 当 items.type=object 时，FieldCard 展开子节点树可编辑（对应 `R06`）
- [ ] `schemaToNodes` ↔ `nodesToSchema` 对 array<object> schema 无损往返（对应 `R01`、`R02`）
- [ ] 字段名含 `.` 时报错阻止保存（对应 `R05`）
- [ ] JSON 非法时切换到 Visual 被阻止（对应 `R07`）
- [ ] Preview 模式应用 schema 的 ui 属性（对应 `R09`）
- [ ] "Add Item" 按钮文案已国际化（对应 `R18`）
- [ ] 新增 19 条测试全部通过，现有测试套件无回归

**测试执行入口**：

```powershell
cd d:\code\stdiolink\src\webui
npx vitest run
```

---

## 7. 风险与控制

- 风险：`schemaToNodes` 对 `array<object>` 的 items.fields 展开后，若同一节点同时存在 `desc.fields` 和 `desc.items.fields`（schema 定义不合理），children 来源歧义
  - 控制：代码中使用 `if/else if` 链，`array+items.type==='object'+items.fields` 优先于 `fields`，语义明确
  - 控制：当前后端 `service_config_schema.cpp` 会分别处理 `fields` 和 `items`，未做互斥校验。前端侧在 `handleOk` 中已根据 type 清理不兼容键（array 时删 fields，非 array 时删 items），从 Visual 创建路径不会产生同时存在的情况。但通过 JSON 模式或外部工具仍可能构造此类 schema，前端优先取 `items.fields` 分支
  - 测试覆盖：`R01`、`R02`、`R11`、`R19` 覆盖主要分支

- 风险：`itemsType` 选择与子节点编辑规则冲突，可能产生静默类型改写
  - 控制：`itemsType` 选择器已从 `FIELD_TYPES` 中过滤掉 `array` 类型；`FieldCard` 仅在 `items.type==='object'` 时显示“Add Child”按钮
  - 控制：`nodesToSchema` 增加守卫，`items.type!=='object'` 时不回写 children
  - 测试覆盖：`R19` 覆盖 items.type!=='object' 时 children 被忽略

- 风险：`handleOk` 增量合并后，用户改变 type 但原 descriptor 中残留的不兼容键（如 array→string 时残留 items）导致 schema 无效
  - 控制：在 `handleOk` 中根据新 type 显式删除不兼容键（items/fields/requiredKeys/additionalProperties）
  - 测试覆盖：`R13` 覆盖 type 变更清理

- 风险：`FieldCard` 中 `isObject` → `isContainer` 替换遗漏，导致某些交互路径仍使用旧变量
  - 控制：Refactor 阶段全局搜索 `isObject` 确认无残留引用
  - 控制：`R06`、`R15` 分别覆盖 array 展开和 string 不展开

- 风险：`setActiveMode` 阻止切换后用户体验不明确（不知道为什么没切换）
  - 控制：`syncFromJson` 失败时已设置 `jsonError`，JSON 编辑器界面会显示红色错误提示，用户可感知
  - 测试覆盖：`R07` 验证 jsonError 非空

---

## 8. 里程碑完成定义（DoD）

- [ ] 代码实现完成（8 个源文件改动 + 6 个测试文件扩展），经代码评审通过
- [ ] 文档同步完成（本里程碑文档入库 `doc/milestone/`）
- [ ] 向后兼容确认：现有 object 字段编辑、JSON↔Visual 切换、Preview 渲染行为测试无回归
- [ ] 【问题修复类】Red 阶段：`R01`–`R10`、`R18`、`R19` 共 12 条失败测试有执行记录（含失败输出摘要），确认在修复前为 Red
- [ ] 【问题修复类】Green 阶段：`R01`–`R19` 共 19 条测试在修复后全部通过（Green），现有 Vitest 测试套件无回归
- [ ] 【问题修复类】Refactor 阶段：清除 `as any` 用法、`isObject` 残留引用；全量测试仍通过
- [ ] i18n 翻译键在 en.json 和 zh.json 中同步添加
- [ ] 手工验收：启动 WebUI dev server 后，完成"JSON 模式写 array<object> schema → Visual 模式展开编辑子字段 → 编辑不丢 items → Preview 预览正确"完整流程
