# 里程碑 90：WebUI Service `array<object>` 配置能力改造（TDD）

> **前置条件**: 里程碑 63（Projects 模块）、里程碑 64（Instances 模块）已完成；`service_config_schema.cpp`、`service_config_validator.cpp`、`ArrayField.tsx`、`PreviewEditor.tsx` 现存代码经审查确认存在本文档所列缺陷
> **目标**: 以 TDD 方式修复 C++ 解析层、校验层及 WebUI 表单渲染链路上的 5 处已知缺陷，使 `array<object>` 配置在保存、校验、表单渲染三条链路上行为完整一致

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `stdiolink_service` Schema 解析（C++） | 修复 `parseObject()` items 分支缺少 fields/description/requiredKeys 递归；`fromJsObject()` 统一调用 `parseObject()` 消除重复实现 |
| `stdiolink_service` 配置校验（C++） | `rejectUnknownFields` 补充 `array<object>` 递归分支，错误路径格式统一为 `field[i].subkey` |
| WebUI SchemaEditor（TypeScript/React） | `PreviewEditor.nodesToFieldMeta` 修复 `items.fields` 丢失，补全 constraints 转换 |
| WebUI SchemaForm（TypeScript/React） | `ArrayField` 默认值策略完整化、数组项 label 改善、errors prop 透传 |
| 测试（C++ / TypeScript） | 新增 C++ 单元测试 11 条、API 回归测试 3 条、前端测试 6 条，全部覆盖 `array<object>` 路径 |

- `GET /api/services/:id` 返回的 `configSchemaFields` 中 `array<object>` 字段 `items.fields` 完整存在
- `POST /api/services/:id/validate-config` 对数组项内未知字段返回 `errorField` 格式为 `"radars[i].bad_key"`
- 项目配置页对 `array<object>` 可完整执行新增 / 删除 / 编辑操作，子字段正确渲染
- Schema Editor Preview 模式可预览 `array<object>` 子字段表单
- 新增测试全部通过，现有测试套件无回归

---

## 2. 背景与问题

当前实现在 `array<object>` 配置场景存在 5 处独立缺陷，导致从 Schema 定义到 WebUI 渲染的完整链路断裂：

- **缺陷 A**（P0）：`parseObject()`（匿名命名空间）的 items 分支仅解析 `type` 和 `constraints`，缺少 `fields`、`description`、`requiredKeys` 递归，导致 `array<object>` 的子字段结构不进入 `FieldMeta`。
- **缺陷 B**（P0）：`fromJsObject()` 是独立实现路径，不经过 `parseObject()`，items 分支存在同类缺陷，且额外缺少 `description` 字段，造成 JS API 路径与文件路径行为不一致。
- **缺陷 C**（P0）：`rejectUnknownFields` 对 `object` 类型已有递归校验，`array` 类型完全跳过，数组项内的未知字段无法被检测。
- **缺陷 D**（P1）：`PreviewEditor.nodesToFieldMeta` 转换 `items` 时直接丢弃 `fields` 和完整 `constraints`，导致 `array<object>` 在 Preview 模式下子字段完全不渲染（原方案误标为 P2，本期提升为 P1）。
- **缺陷 E**（P1）：`ArrayField` 的 `handleAdd` 对非 object/array 类型使用统一的 `''` 默认值、数组项 label 显示原始索引数字、未向子 `FieldRenderer` 透传 `errors`。

**范围**：
- `src/stdiolink_service/config/service_config_schema.cpp`（缺陷 A、B）
- `src/stdiolink_service/config/service_config_validator.cpp`（缺陷 C）
- `src/webui/src/components/SchemaEditor/PreviewEditor.tsx`（缺陷 D）
- `src/webui/src/components/SchemaForm/fields/ArrayField.tsx`（缺陷 E）
- 对应单元测试与 API 测试文件

**非目标**：
- 不变更 `config.schema.json` 格式规范
- 不实现 VisualEditor 对 `array.items.fields` 的可视化编辑（列为 P2 后续里程碑）
- 不支持三层及以上嵌套 `array<array<object>>` 的完整 UI 编辑（JSON Editor 降级可用）

---

## 3. 技术要点

### 3.1 目标 FieldMeta 数据结构（Before / After）

**Before**：`parseObject` 解析含 `array<object>` 字段的 schema 后，`items` 子对象缺失 `fields`：

```json
// GET /api/services/:id → configSchemaFields（当前错误输出）
[
  {
    "name": "radars",
    "type": "array",
    "required": true,
    "minItems": 1,
    "items": {
      "name": "items",
      "type": "object"
      // ❌ fields 缺失
    }
  }
]
```

**After**：修复后 `items.fields` 完整存在：

```json
// GET /api/services/:id → configSchemaFields（修复后）
[
  {
    "name": "radars",
    "type": "array",
    "required": true,
    "description": "激光雷达设备列表",
    "minItems": 1,
    "items": {
      "name": "items",
      "type": "object",
      "fields": [
        { "name": "id",   "type": "string", "required": true, "description": "雷达唯一标识" },
        { "name": "host", "type": "string", "required": true, "description": "设备 IP" },
        { "name": "port", "type": "int",    "required": true, "min": 1, "max": 65535 }
      ]
    }
  }
]
```

### 3.2 C++ 辅助函数 `parseFieldMeta()` 签名

修复的核心是在 `service_config_schema.cpp` 匿名命名空间内引入辅助函数，令 `parseObject()` 的字段循环和 items 分支共用同一解析逻辑：

> **编译依赖说明**：`parseFieldMeta()` 内部调用 `parseObject()`（处理 `fields` 递归），而 `parseObject()` 又调用 `parseFieldMeta()`（处理每个字段）。两者存在互相调用关系，需在匿名命名空间内对 `parseObject` 做前向声明，确保编译顺序正确。
>
> **备选方案**：也可采用更小的改动方案——仅在 `parseObject()` 现有 items 分支中补充 `fields`/`description`/`requiredKeys` 的解析逻辑（类似已有的 `"fields"` 分支），而不引入 `parseFieldMeta()` 中间函数。该方案改动范围更小、风险更低，但会在 items 分支和字段循环之间产生一定的代码重复。实现时可根据实际代码量选择。

```cpp
// 匿名命名空间内，service_config_schema.cpp
static FieldMeta parseFieldMeta(const QString& name,
                                const QJsonObject& desc,
                                const QString& fieldPath,
                                QString& error);

// parseObject 内循环改为：
for (auto it = fields.begin(); it != fields.end(); ++it) {
    const QString path = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
    FieldMeta field = parseFieldMeta(it.key(),
                                     it.value().toObject(), path, error);
    if (!error.isEmpty()) return {};
    schema.fields.append(field);  // fields 为 QVector<FieldMeta>（值类型）
}

// items 分支改为：
if (desc.contains("items")) {
    const QString itemPath = fieldPath + ".items";
    FieldMeta itemMeta = parseFieldMeta("items",
                                        desc.value("items").toObject(),
                                        itemPath, error);
    if (!error.isEmpty()) return {};
    field.items = std::make_shared<FieldMeta>(itemMeta);
}
```

### 3.3 `fromJsObject()` 重构为复用 `parseObject()`

```cpp
// 修改前：独立 for 循环，items 不完整
ServiceConfigSchema ServiceConfigSchema::fromJsObject(const QJsonObject& obj) {
    ServiceConfigSchema schema;
    // ... 独立解析逻辑 ...
    return schema;
}

// 修改后：调用 parseObject，但使用宽松模式跳过类型校验以保持历史静默行为
ServiceConfigSchema ServiceConfigSchema::fromJsObject(const QJsonObject& obj) {
    QString error;
    return parseObject(obj, QString(), error, /*strictTypeCheck=*/false);
    // fromJsObject 历史上不报错且不做类型校验，strictTypeCheck=false 保持该行为
}
```

> **兼容性说明**：`parseObject` 已处理 `enumValues` → `enum` 重命名，JS API 所有旧路径（string/int/bool/enum/object/array）保持不变。
>
> **⚠️ 行为差异处理**：原 `fromJsObject()` 不执行 `isKnownFieldType` 类型校验（遇到未知类型静默降级为 `any`），而 `parseObject()` 对未知类型返回 error 并产生空 schema。为避免破坏性行为变更，需为 `parseObject()` 新增 `bool strictTypeCheck = true` 参数，`fromJsObject` 调用时传入 `false` 以跳过类型校验，保持历史静默行为。

### 3.4 `rejectUnknownFields` 错误路径规范

新增 array 递归分支，错误路径格式与 `MetaValidator::validateArray` 对齐：

```cpp
// 新增：array<object> 递归分支
if (field->type == FieldType::Array
    && field->items
    && !field->items->fields.isEmpty()
    && it.value().isArray()) {

    ServiceConfigSchema itemSchema;
    itemSchema.fields = field->items->fields;
    const QJsonArray arr = it.value().toArray();

    for (int i = 0; i < arr.size(); ++i) {
        if (!arr[i].isObject()) continue;
        // 格式：radars[1].bad_key
        const QString itemPrefix = QString("%1[%2]").arg(fullPath).arg(i);
        auto r = rejectUnknownFields(itemSchema, arr[i].toObject(), itemPrefix);
        if (!r.valid) return r;
    }
}
```

**错误路径格式统一规范**：

| 场景 | 错误路径格式 | 示例 |
|------|-------------|------|
| 顶层未知字段 | `"key"` | `"bad_key"` |
| object 子字段未知 | `"parent.key"` | `"server.bad_key"` |
| array 项子字段未知 | `"field[i].key"` | `"radars[1].bad_key"` |
| array 项自身类型错误 | `"field[i]"` | `"radars[1]"`（由 MetaValidator 报告）|

### 3.5 WebUI `PreviewEditor.nodesToFieldMeta` 修复要点

```typescript
// 修复前：items 转换丢失 fields 和完整 constraints
if (n.descriptor.items) {
  meta.items = {
    name: 'items',
    type: n.descriptor.items.type ?? 'any',
    description: n.descriptor.items.description,
    // ❌ 缺少 fields、constraints.*
  };
}

// 修复后：完整保留 items.fields（递归转换）和 constraints
if (n.descriptor.items) {
  const itemDesc = n.descriptor.items;
  meta.items = {
    name: 'items',
    type: itemDesc.type ?? 'any',
    description: itemDesc.description,
    min: itemDesc.constraints?.min,
    max: itemDesc.constraints?.max,
    minItems: itemDesc.constraints?.minItems,
    maxItems: itemDesc.constraints?.maxItems,
    // ✅ 关键：递归转换 items.fields
    fields: itemDesc.fields
      ? Object.entries(itemDesc.fields).map(([fname, fdesc]) =>
          nodesToFieldMeta([{ name: fname, descriptor: fdesc }])[0]
        )
      : undefined,
  };
}
```

### 3.6 WebUI `ArrayField` 默认值策略与 props 接口变更

`getDefaultItem` 定义于独立工具模块 `SchemaForm/utils/fieldDefaults.ts`（Refactor 阶段提取，供后续里程碑复用）：

```typescript
// src/webui/src/components/SchemaForm/utils/fieldDefaults.ts
export function getDefaultItem(type?: FieldMeta['type']): unknown {
  switch (type) {
    case 'object':  return {};
    case 'array':   return [];
    case 'bool':    return false;
    case 'int':
    case 'int64':
    case 'double':  return 0;
    case 'string':
    case 'enum':
    default:        return '';
  }
}
```

`handleAdd` 对 `array<object>` 执行**深度初始化**：遍历 `items.fields`，对每个子字段调用 `getDefaultItem`，使新增项携带类型正确的默认值（int 字段初始化为 `0` 而非 `''`，避免后续校验类型错误）：

```typescript
// handleAdd：array<object> 深度初始化
const handleAdd = () => {
  const subFields = field.items?.type === 'object' ? (field.items?.fields ?? []) : [];
  if (subFields.length > 0) {
    const newItem: Record<string, unknown> = {};
    for (const sf of subFields) {
      newItem[sf.name] = getDefaultItem(sf.type);
    }
    onChange([...arr, newItem]);
  } else {
    // 非 object items（array<string> 等）退回浅层初始化
    onChange([...arr, getDefaultItem(field.items?.type)]);
  }
};
```

```typescript
// props 接口新增 errors
interface ArrayFieldProps {
  field: FieldMeta;
  value: unknown[];
  onChange: (value: unknown[]) => void;
  error?: string;
  errors?: Record<string, string>;  // ✅ 新增
}
```

> **errors key 格式约定**：`errors` 字典始终保持**全局扁平化**，key 使用完整绝对路径格式（如 `"radars[0].port"`、`"radars[1].host"`）。`ArrayField` 在渲染第 `i` 项时，**不**对 `errors` 做前缀过滤和去前缀处理，而是将完整的 `errors` 字典连同当前层级的 `basePath`（即 `"${field.name}[${i}]"`）一起向下透传给子 `FieldRenderer`。最底层的实际输入控件（如 `StringField`）通过 `errors[absolutePath]` 直接获取对应的错误信息。此方案与现有 `ObjectField` 的 `errors?.[${field.name}.${child.name}]` 模式一致，避免了逐层字符串切片的性能损耗和嵌套场景（如 `array<array<object>>`）下的出错风险。

---

## 4. 实现步骤（TDD Red-Green-Refactor）

### 4.1 Red — 编写失败测试（先于所有修复代码）

#### 4.1.1 C++ 失败测试（`src/tests/test_service_config_schema.cpp`）

每条测试在修复前必须运行并确认失败（**Red**）：

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R_CPP_01` | 缺陷 A | schema JSON 含 `array` 字段，`items.fields` 有 3 个子字段 | `items->fields` 为空 | `items->fields.size() == 3` |
| `R_CPP_02` | 缺陷 A | `items` 含 `description` 字段 | `items->description` 为空 | `items->description` == 期望值 |
| `R_CPP_03` | 缺陷 A | `items.constraints` 含 `minItems` | 未解析 | `items->constraints.minItems == 1` |
| `R_CPP_04` | 缺陷 B | 通过 `fromJsObject` 解析相同 schema | `items->fields` 为空 | `items->fields.size() == 3` |

```cpp
// src/tests/test_service_config_schema.cpp（新增用例）

static const char* kArrayObjectSchema = R"({
  "radars": {
    "type": "array",
    "required": true,
    "description": "激光雷达设备列表",
    "constraints": { "minItems": 1 },
    "items": {
      "type": "object",
      "description": "单个雷达配置",
      "fields": {
        "id":   { "type": "string", "required": true, "description": "雷达唯一标识" },
        "host": { "type": "string", "required": true, "description": "设备 IP" },
        "port": { "type": "int",    "required": true, "constraints": { "min": 1, "max": 65535 } }
      }
    }
  }
})";

// R_CPP_01
TEST(ServiceConfigSchemaTest, ParseArrayObjectItems_HasFields) {
    QString error;
    auto schema = ServiceConfigSchema::fromJsonObject(
        QJsonDocument::fromJson(kArrayObjectSchema).object(), error);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    ASSERT_EQ(schema.fields.size(), 1);

    const auto& radars = schema.fields[0];
    ASSERT_NE(radars.items, nullptr);
    EXPECT_EQ(radars.items->fields.size(), 3);  // ← 修复前失败

    const auto& portField = *std::find_if(
        radars.items->fields.begin(), radars.items->fields.end(),
        [](const auto& f){ return f.name == "port"; });
    EXPECT_EQ(portField.type, FieldType::Int);
    EXPECT_EQ(portField.constraints.min, 1);
    EXPECT_EQ(portField.constraints.max, 65535);
}

// R_CPP_02
TEST(ServiceConfigSchemaTest, ParseArrayObjectItems_Description) {
    QString error;
    auto schema = ServiceConfigSchema::fromJsonObject(
        QJsonDocument::fromJson(kArrayObjectSchema).object(), error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_NE(schema.fields[0].items, nullptr);
    EXPECT_EQ(schema.fields[0].items->description,
              QString("单个雷达配置"));  // ← 修复前失败
}

// R_CPP_03
TEST(ServiceConfigSchemaTest, ParseArrayObjectItems_Constraints) {
    QString error;
    auto schema = ServiceConfigSchema::fromJsonObject(
        QJsonDocument::fromJson(kArrayObjectSchema).object(), error);
    ASSERT_TRUE(error.isEmpty());
    EXPECT_EQ(schema.fields[0].constraints.minItems, 1);
}

// R_CPP_04
TEST(ServiceConfigSchemaTest, ParseArrayObjectItems_fromJsObject_HasFields) {
    auto schema = ServiceConfigSchema::fromJsObject(
        QJsonDocument::fromJson(kArrayObjectSchema).object());
    ASSERT_EQ(schema.fields.size(), 1);
    ASSERT_NE(schema.fields[0].items, nullptr);
    EXPECT_EQ(schema.fields[0].items->fields.size(), 3);  // ← 修复前失败
    EXPECT_EQ(schema.fields[0].items->description,
              QString("单个雷达配置"));
}
```

#### 4.1.2 C++ 失败测试（`src/tests/test_service_config_validator.cpp`）

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R_CPP_05` | 缺陷 C | `radars[0]` 含未知字段 `bad_key` | 校验通过（未检测） | 返回失败，`errorField == "radars[0].bad_key"` |
| `R_CPP_06` | 缺陷 C | `radars[1]` 含未知字段 | 校验通过 | `errorField == "radars[1].bad_key"` |

```cpp
// src/tests/test_service_config_validator.cpp（新增用例）

class ValidatorArrayObjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 构造含 array<object> 的 schema
        QString error;
        m_schema = ServiceConfigSchema::fromJsonObject(
            QJsonDocument::fromJson(kArrayObjectSchema).object(), error);
        ASSERT_TRUE(error.isEmpty());
    }
    ServiceConfigSchema m_schema;
};

// R_CPP_05
TEST_F(ValidatorArrayObjectTest, RejectUnknownField_InsideArrayObjectItem_FirstItem) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": [
        { "id": "r1", "host": "192.168.1.1", "port": 2368, "bad_key": "value" }
      ]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_FALSE(result.valid);                          // ← 修复前可能通过
    EXPECT_EQ(result.errorField, QString("radars[0].bad_key"));
}

// R_CPP_06
TEST_F(ValidatorArrayObjectTest, RejectUnknownField_InsideArrayObjectItem_SecondItem) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": [
        { "id": "r1", "host": "192.168.1.1", "port": 2368 },
        { "id": "r2", "host": "192.168.1.2", "port": 2369, "bad_key": "x" }
      ]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorField, QString("radars[1].bad_key"));
}

// 正向验证（新增，修复前后均应通过）
TEST_F(ValidatorArrayObjectTest, ValidArrayObject_AllFieldsKnown) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": [
        { "id": "r1", "host": "192.168.1.1", "port": 2368 }
      ]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_TRUE(result.valid);
}

// array<string> 不触发递归（修复前后均应通过）
TEST(ValidatorTest, RejectUnknownField_ArrayOfPrimitive_Skipped) {
    ServiceConfigSchema schema;
    FieldMeta tagsMeta;
    tagsMeta.name = "tags";
    tagsMeta.type = FieldType::Array;
    auto itemsMeta = std::make_shared<FieldMeta>();
    itemsMeta->type = FieldType::String;
    tagsMeta.items = itemsMeta;
    schema.fields.append(tagsMeta);

    const QJsonObject config = QJsonDocument::fromJson(R"({"tags":["a","b"]})").object();
    auto result = ServiceConfigValidator::rejectUnknownFields(schema, config, "");
    EXPECT_TRUE(result.valid);
}
```

#### 4.1.3 前端失败测试（`src/webui/src/components/SchemaForm/__tests__/SchemaForm.test.tsx`）

| 测试 ID | 缺陷 | 场景 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| `R_FE_01` | 缺陷 E | 点击 Add Item 后新增 `array<object>` 项，int 子字段初始化 | 新增项为 `{}`，int 子字段默认值为 `''` | 深度初始化：`{ id: '', port: 0 }`（string→`''`，int→`0`） |
| `R_FE_01_NONOBJ` | 缺陷 E | `array<string>` 点击 Add Item | — | 默认值为 `''`（浅层初始化，行为不变） |
| `R_FE_02` | 缺陷 E | 数组项 label | label 文本为 `"0"` | label 文本含 `"radar 1"` |
| `R_FE_03` | 缺陷 D | PreviewEditor 含 `array<object>` | 子字段控件不渲染 | 渲染出 `id`、`port` 等子字段控件 |

```typescript
// src/webui/src/components/SchemaForm/__tests__/SchemaForm.test.tsx（新增）
import { render, screen, fireEvent } from '@testing-library/react';
import ArrayField from '../fields/ArrayField';

const radarSchema: FieldMeta = {
  name: 'radars',
  type: 'array',
  items: {
    name: 'radar',
    type: 'object',
    fields: [
      { name: 'id',   type: 'string', required: true },
      { name: 'port', type: 'int',    required: true },
    ],
  },
};

// R_FE_01：深度初始化 — int 子字段应为 0，string 子字段应为 ''
test('array<object> handleAdd deeply initializes subfields with type-correct defaults', () => {
  const onChange = jest.fn();
  render(<ArrayField field={radarSchema} value={[]} onChange={onChange} />);
  fireEvent.click(screen.getByText(/add/i));
  // 深度初始化：string→''，int→0；不应为 {} 或 { id: '', port: '' }
  expect(onChange).toHaveBeenCalledWith([{ id: '', port: 0 }]);
});

// R_FE_01_NONOBJ：array<string> 浅层初始化不受影响
test('array<string> handleAdd initializes with empty string', () => {
  const stringArrayField: FieldMeta = {
    name: 'tags', type: 'array', items: { name: 'tag', type: 'string' },
  };
  const onChange = jest.fn();
  render(<ArrayField field={stringArrayField} value={[]} onChange={onChange} />);
  fireEvent.click(screen.getByText(/add/i));
  expect(onChange).toHaveBeenCalledWith(['']);
});

// R_FE_02
test('array item label shows human-readable index', () => {
  render(<ArrayField field={radarSchema} value={[{}]} onChange={jest.fn()} />);
  expect(screen.getByText(/radar 1/i)).toBeInTheDocument();  // ← 修复前失败
});
```

```typescript
// src/webui/src/components/SchemaEditor/__tests__/PreviewEditor.test.tsx（新增）
import { render, screen } from '@testing-library/react';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import { PreviewEditor } from '../PreviewEditor';

// R_FE_03：通过 nodesToFieldMeta 单元测试验证转换正确性
// PreviewEditor 为无 props 组件，从 Zustand store 读取 nodes
// 测试方式：直接导出并测试 nodesToFieldMeta 函数，或通过 store 注入 nodes 后渲染
test('preview renders array<object> subfields', () => {
  // 通过 store 注入测试数据
  useSchemaEditorStore.setState({
    nodes: [{
      name: 'radars',
      descriptor: {
        type: 'array',
        items: {
          type: 'object',
          fields: {
            id:   { type: 'string' },
            port: { type: 'int' },
          },
        },
      },
    }],
  });
  render(<PreviewEditor />);
  // data-testid 格式为 field-${field.name}（与 ArrayField 实际实现一致）
  expect(screen.queryByTestId('field-radars')).toBeInTheDocument();
});
```

**Red 阶段确认**：在提交任何修复代码前，运行上述用例并记录失败输出，留存为 Red 证据。

---

### 4.2 Green — 最小修复实现

#### 改动 1：`src/stdiolink_service/config/service_config_schema.cpp`

修复缺陷 A 和缺陷 B，两处合并为一次重构。

**新增 `parseFieldMeta()` 辅助函数**（匿名命名空间内）：

```cpp
static FieldMeta parseFieldMeta(const QString& name,
                                const QJsonObject& desc,
                                const QString& fieldPath,
                                QString& error) {
    FieldMeta field;
    field.name = name;
    field.type = fieldTypeFromString(desc.value("type").toString("any"));
    field.description = desc.value("description").toString();
    field.required = desc.value("required").toBool(false);

    if (desc.contains("constraints")) {
        QJsonObject cObj = desc.value("constraints").toObject();
        if (cObj.contains("enumValues")) {
            cObj["enum"] = cObj.take("enumValues");
        }
        field.constraints = Constraints::fromJson(cObj);
    }

    // ✅ 递归解析 fields（支持 object/array<object>）
    if (desc.contains("fields")) {
        if (!desc.value("fields").isObject()) {
            error = QString("\"fields\" at \"%1\" must be a JSON object")
                        .arg(fieldPath);
            return {};
        }
        ServiceConfigSchema nested = parseObject(
            desc.value("fields").toObject(), fieldPath, error);
        if (!error.isEmpty()) return {};
        field.fields = nested.fields;
    }

    // ✅ 递归解析 items（支持 array）
    if (desc.contains("items")) {
        const QString itemPath = fieldPath + ".items";
        FieldMeta itemMeta = parseFieldMeta(
            "items", desc.value("items").toObject(), itemPath, error);
        if (!error.isEmpty()) return {};
        field.items = std::make_shared<FieldMeta>(itemMeta);
    }

    // requiredKeys
    if (desc.contains("requiredKeys")) {
        for (const auto& v : desc.value("requiredKeys").toArray()) {
            field.requiredKeys.append(v.toString());
        }
    }

    return field;
}
```

**修改 `parseObject()` 内的字段循环**，调用 `parseFieldMeta()`：

```cpp
// 修改前：parseObject 内字段循环直接内联解析
for (auto it = fields.begin(); it != fields.end(); ++it) {
    // ... 内联解析 type/constraints/items ...
}

// 修改后：委托给 parseFieldMeta
for (auto it = fields.begin(); it != fields.end(); ++it) {
    const QString path = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
    FieldMeta field = parseFieldMeta(it.key(),
                                     it.value().toObject(), path, error);
    if (!error.isEmpty()) return {};
    schema.fields.append(field);  // fields 为 QVector<FieldMeta>（值类型）
}
```

**修改 `fromJsObject()`**，统一调用 `parseObject()`，使用宽松模式保持历史行为：

```cpp
ServiceConfigSchema ServiceConfigSchema::fromJsObject(const QJsonObject& obj) {
    QString error;
    return parseObject(obj, QString(), error, /*strictTypeCheck=*/false);
    // 历史行为：不做类型校验，遇错时返回空 schema，静默不抛出
}
```

**改动理由**：消除 `parseObject` 与 `fromJsObject` 两条独立路径的重复实现，使所有解析入口对 `items.fields` 具备完整递归能力。

**验收方式**：`R_CPP_01`、`R_CPP_02`、`R_CPP_03`、`R_CPP_04` 全部由 Red 变为 Green。

---

#### 改动 2：`src/stdiolink_service/config/service_config_validator.cpp`

修复缺陷 C，在 `rejectUnknownFields` 中补充 array 递归分支。

```cpp
// 在已有 object 递归之后追加 array 递归分支
// 已有：
if (field->type == FieldType::Object
    && !field->fields.isEmpty()
    && it.value().isObject()) {
    ServiceConfigSchema nested;
    nested.fields = field->fields;
    auto r = rejectUnknownFields(nested, it.value().toObject(), fullPath);
    if (!r.valid) return r;
}

// ✅ 新增：array<object> 递归
if (field->type == FieldType::Array
    && field->items
    && !field->items->fields.isEmpty()
    && it.value().isArray()) {
    ServiceConfigSchema itemSchema;
    itemSchema.fields = field->items->fields;
    const QJsonArray arr = it.value().toArray();
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr[i].isObject()) continue;
        const QString itemPrefix =
            QString("%1[%2]").arg(fullPath).arg(i);
        auto r = rejectUnknownFields(itemSchema, arr[i].toObject(), itemPrefix);
        if (!r.valid) return r;
    }
}
```

**改动理由**：与现有 `MetaValidator::validateArray` 的错误路径格式 `field[i].subkey` 对齐，使校验链路语义一致。

**验收方式**：`R_CPP_05`、`R_CPP_06` 由 Red 变为 Green，正向验证与 array<string> 用例继续 Green。

---

#### 改动 3：`src/webui/src/components/SchemaEditor/PreviewEditor.tsx`

修复缺陷 D，补全 `nodesToFieldMeta` 中 items 转换。

```typescript
// 替换 items 分支（在 nodesToFieldMeta 函数内）
if (n.descriptor.items) {
  const itemDesc = n.descriptor.items;
  meta.items = {
    name: 'items',
    type: itemDesc.type ?? 'any',
    description: itemDesc.description,
    min: itemDesc.constraints?.min,
    max: itemDesc.constraints?.max,
    minItems: itemDesc.constraints?.minItems,
    maxItems: itemDesc.constraints?.maxItems,
    // ✅ 关键修复：递归转换 items.fields
    fields: itemDesc.fields
      ? Object.entries(itemDesc.fields).map(([fname, fdesc]) =>
          nodesToFieldMeta([{ name: fname, descriptor: fdesc as SchemaFieldDescriptor }])[0]
        )
      : undefined,
  };
}
```

**改动理由**：`SchemaNode.descriptor.items.fields` 已正确存储于 `descriptor.items`，转换时丢弃 `fields` 导致 Preview 模式渲染链路断裂，此为 P1 Bug 而非 P2 增强。

**验收方式**：`R_FE_03` 由 Red 变为 Green。

---

#### 改动 4：`src/webui/src/components/SchemaForm/fields/ArrayField.tsx`

修复缺陷 E，分三步最小改动：

**步骤 4-a：新增 `getDefaultItem` 至共享工具模块**（在 Refactor 阶段正式提取，Green 阶段先内联，接口定义与最终一致）：

```typescript
// src/webui/src/components/SchemaForm/utils/fieldDefaults.ts（Refactor 阶段新建）
export function getDefaultItem(type?: FieldMeta['type']): unknown {
  switch (type) {
    case 'object':  return {};
    case 'array':   return [];
    case 'bool':    return false;
    case 'int':
    case 'int64':
    case 'double':  return 0;
    default:        return '';  // string / enum / any
  }
}
```

**步骤 4-b：改造 `handleAdd`**（含 `array<object>` 深度初始化）：

```typescript
// 修改前
const handleAdd = () => {
  onChange([...arr, field.items?.type === 'object' ? {} : '']);
};

// 修改后：array<object> 时遍历 items.fields 按子字段类型深度初始化，避免类型错误
const handleAdd = () => {
  const subFields = field.items?.type === 'object' ? (field.items?.fields ?? []) : [];
  if (subFields.length > 0) {
    const newItem: Record<string, unknown> = {};
    for (const sf of subFields) {
      newItem[sf.name] = getDefaultItem(sf.type);
    }
    onChange([...arr, newItem]);
  } else {
    onChange([...arr, getDefaultItem(field.items?.type)]);
  }
};
```

**步骤 4-c：更新 `FieldRenderer` 调用（label 改善 + errors 透传）**：

```tsx
// 修改前
<FieldRenderer
  field={field.items}
  value={item}
  onChange={(v) => handleItemChange(i, v)}
/>

// 修改后
<FieldRenderer
  field={{
    ...field.items,
    name: `${field.items?.name ?? field.name} ${i + 1}`,  // "radar 1", "radar 2"
  }}
  value={item}
  onChange={(v) => handleItemChange(i, v)}
  errors={errors}  // ✅ 错误透传
/>
```

**步骤 4-d：更新 `ArrayFieldProps` 接口**：

```typescript
interface ArrayFieldProps {
  field: FieldMeta;
  value: unknown[];
  onChange: (value: unknown[]) => void;
  error?: string;
  errors?: Record<string, string>;  // ✅ 新增
}
```

**验收方式**：`R_FE_01`、`R_FE_02` 由 Red 变为 Green。

**Green 阶段确认**：完成四处改动后，运行全量测试套件（C++ gtest + Vitest/Jest），确认：
- `R_CPP_01` 至 `R_CPP_06`、`R_FE_01` 至 `R_FE_03` 全部通过（Green）
- 已有测试套件无回归

---

### 4.3 Refactor — 代码整理

完成 Green 阶段后，执行以下轻量重构：

1. **`parseObject()` 内联逻辑清理**：确认旧的内联 items 解析代码（type/constraints 分支）已被 `parseFieldMeta()` 完全替代，删除残留内联代码，避免死代码。

2. **`fromJsObject()` 测试覆盖旧路径确认**：为 `fromJsObject` 补充基础类型回归测试（string/int/bool/enum/object），确认 `parseObject` 的 `enumValues → enum` 重命名处理对 JS API 路径保持透明。

3. **提取 `getDefaultItem` 为共享工具模块**：将 `getDefaultItem` 从 `ArrayField.tsx` 组件内部移至 `src/webui/src/components/SchemaForm/utils/fieldDefaults.ts`，`ArrayField.tsx` 改为 `import { getDefaultItem } from '../utils/fieldDefaults'`。此工具函数供后续里程碑（如 M92）复用，避免重复定义。

4. **`ArrayField` 注释**：在 `handleAdd` 函数旁添加 JSDoc 注释，说明深度初始化策略意图，便于后续维护。

重构后全量测试（`R_CPP_01`–`R_FE_03` + 原有套件）必须仍全部通过。

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/webui/src/components/SchemaForm/utils/fieldDefaults.ts` — `getDefaultItem` 共享工具函数（Refactor 阶段从 `ArrayField.tsx` 提取，供后续里程碑复用）

### 5.2 修改文件

| 文件 | 改动内容 |
|------|---------|
| `src/stdiolink_service/config/service_config_schema.cpp` | 新增 `parseFieldMeta()` 辅助函数；`parseObject()` 字段循环改为调用 `parseFieldMeta()`；`fromJsObject()` 改为调用 `parseObject()`（缺陷 A、B） |
| `src/stdiolink_service/config/service_config_validator.cpp` | `rejectUnknownFields` 新增 `array<object>` 递归分支（缺陷 C） |
| `src/webui/src/components/SchemaEditor/PreviewEditor.tsx` | `nodesToFieldMeta` 补全 `items.fields` 递归转换及 constraints（缺陷 D） |
| `src/webui/src/components/SchemaForm/fields/ArrayField.tsx` | 新增 `getDefaultItem`；`handleAdd` 使用新函数；`FieldRenderer` 调用补 label 改善和 errors 透传；`ArrayFieldProps` 新增 `errors` 字段（缺陷 E） |

### 5.3 测试文件

| 文件 | 改动内容 |
|------|---------|
| `src/tests/test_service_config_schema.cpp` | 新增用例：`R_CPP_01`–`R_CPP_04`（4 条 array<object> 解析）、`R_CPP_09`（array<array<object>> 嵌套鲁棒性） |
| `src/tests/test_service_config_validator.cpp` | 新增用例：`R_CPP_05`–`R_CPP_08` 正向/边界/array<string>，`R_CPP_10`（嵌套 array 不递归不崩溃），共 6 条 |
| `src/tests/test_api_router.cpp` | 新增用例：`GET_ServiceDetail_ArrayObject_HasItemsFields`、`POST_ValidateConfig_ArrayObject_Valid`、`POST_ValidateConfig_ArrayObject_UnknownSubfield`（3 条） |
| `src/webui/src/components/SchemaForm/__tests__/SchemaForm.test.tsx` | 新增用例：`R_FE_01`（深度初始化）、`R_FE_01_NONOBJ`（浅层初始化回归）、`R_FE_01_INT`（getDefaultItem int 默认值）、`R_FE_02`（label），共 4 条 |
| `src/webui/src/components/SchemaEditor/__tests__/PreviewEditor.test.tsx` | 新增用例：`R_FE_03`（PreviewEditor）、`R_FE_04`（无 fields 时 undefined），共 2 条 |

---

## 6. 测试与验收

### 6.1 单元测试

- **测试对象**：`ServiceConfigSchema::fromJsonObject`、`ServiceConfigSchema::fromJsObject`、`ServiceConfigValidator::rejectUnknownFields`、`PreviewEditor.nodesToFieldMeta`、`ArrayField`
- **用例分层**：正常路径、边界值（空 items.fields、items 为 null）、异常输入（items.fields 不是对象）、回归（旧类型 string/int/bool/enum/object）
- **断言要点**：返回值字段数量与内容、错误路径字符串精确匹配、错误 valid 布尔值
- **桩替身**：前端测试使用 `@testing-library/react` 渲染；C++ 测试直接构造 JSON 字符串，无需进程通信 Mock

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `parseFieldMeta`: `items` 字段存在且 `type=object` | 递归解析 items.fields | `R_CPP_01` |
| `parseFieldMeta`: `items` 字段存在，含 `description` | description 正确透传 | `R_CPP_02` |
| `parseFieldMeta`: `items.constraints` 存在 | constraints 正确解析 | `R_CPP_03` |
| `parseFieldMeta`: `fields` 字段不是 JSON object | 返回 error，schema 为空 | `R_CPP_ERR01`（见下方详情） |
| `fromJsObject`: 通过 parseObject 路径 | items.fields 完整 | `R_CPP_04` |
| `rejectUnknownFields`: array 类型，items.fields 非空，数组项为 object | 递归校验每个数组项 | `R_CPP_05`、`R_CPP_06` |
| `rejectUnknownFields`: array 类型，items.fields 为空 | 跳过递归 | `R_CPP_07`（array<string>）|
| `rejectUnknownFields`: array 类型，数组项不是 object | continue，跳过该项 | `R_CPP_08`（见下方）|
| `parseFieldMeta`: items.items 嵌套（array<array<object>>）| 递归不崩溃，items.items 正确解析 | `R_CPP_09` |
| `rejectUnknownFields`: 外层 items 为 array 类型（不是 object）| 不递归进入内层 array，不崩溃 | `R_CPP_10` |
| `ArrayField.handleAdd`: items.type=object，fields 非空 | 深度初始化：每子字段按 getDefaultItem 设置默认值 | `R_FE_01` |
| `ArrayField.handleAdd`: items.type=string（非 object）| 浅层初始化：`''` | `R_FE_01_NONOBJ` |
| `ArrayField.getDefaultItem`: type=int | 返回 `0` | `R_FE_01_INT`（见下方）|
| `ArrayField.getDefaultItem`: type=string/undefined | 返回 `''` | 现有行为保持 |
| `nodesToFieldMeta`: items.fields 存在 | 递归转换 fields 数组 | `R_FE_03` |
| `nodesToFieldMeta`: items.fields 不存在 | fields 为 undefined | `R_FE_04`（见下方）|
| `ArrayField` label | name 为 `"${items.name} ${i+1}"` 格式 | `R_FE_02` |

- 覆盖要求（硬性）：所有可达路径 100% 有用例。
  - `parseFieldMeta` 的 items.items 嵌套路径现已通过 `R_CPP_09` 覆盖，不再标注为"可按需扩展"。

#### 用例详情

**R_CPP_01 — parseObject 解析 array<object>，items.fields 完整**
- 前置条件：无外部依赖，直接调用解析函数
- 输入：`kArrayObjectSchema`（含 radars 字段，items.fields 有 3 个子字段）
- 预期：`schema.fields[0].items->fields.size() == 3`，port 子字段 type 为 Int，min=1，max=65535
- 断言：`EXPECT_EQ(radars.items->fields.size(), 3)` / `EXPECT_EQ(portField.constraints.min, 1)`

**R_CPP_02 — items.description 正确透传**
- 前置条件：同上
- 输入：`kArrayObjectSchema`（items 含 `"description": "单个雷达配置"`）
- 预期：`schema.fields[0].items->description == "单个雷达配置"`
- 断言：`EXPECT_EQ(schema.fields[0].items->description, QString("单个雷达配置"))`

**R_CPP_03 — items.constraints.minItems 正确解析**
- 前置条件：同上
- 输入：`kArrayObjectSchema`（顶层 radars constraints 含 `"minItems": 1`）
- 预期：`schema.fields[0].constraints.minItems == 1`
- 断言：`EXPECT_EQ(schema.fields[0].constraints.minItems, 1)`

**R_CPP_04 — fromJsObject 路径 items.fields 完整（防回归）**
- 前置条件：无
- 输入：`ServiceConfigSchema::fromJsObject(kArrayObjectSchemaJson)`
- 预期：`schema.fields[0].items->fields.size() == 3`，`items->description` 正确
- 断言：`EXPECT_EQ(schema.fields[0].items->fields.size(), 3)`

**R_CPP_ERR01 — items.fields 不是 JSON object 时返回错误**
- 前置条件：无
- 输入：`{"f": {"type": "array", "items": {"type": "object", "fields": "invalid_string"}}}`
- 预期：`error` 非空，`schema.fields` 为空
- 断言：`EXPECT_FALSE(error.isEmpty())`

**R_CPP_05 — radars[0] 含未知字段，errorField 含索引**
- 前置条件：使用 `kArrayObjectSchema` 初始化 m_schema
- 输入：radars[0] 含 `"bad_key": "value"`
- 预期：`result.valid == false`，`result.errorField == "radars[0].bad_key"`
- 断言：`EXPECT_EQ(result.errorField, QString("radars[0].bad_key"))`

**R_CPP_06 — radars[1] 含未知字段，errorField 索引为 1**
- 前置条件：同上
- 输入：radars[0] 合法，radars[1] 含 `"bad_key": "x"`
- 预期：`result.valid == false`，`result.errorField == "radars[1].bad_key"`
- 断言：`EXPECT_EQ(result.errorField, QString("radars[1].bad_key"))`

**R_CPP_07 — array<string> 不触发 items 递归**
- 前置条件：构造 tags 字段（array of string，items.fields 为空）
- 输入：`{"tags": ["a", "b"]}`
- 预期：`result.valid == true`
- 断言：`EXPECT_TRUE(result.valid)`

**R_CPP_08 — array 项不是 object 时跳过该项**
- 前置条件：使用 `kArrayObjectSchema`（items.fields 非空）
- 输入：`{"radars": ["not-an-object"]}`（字符串而非 object）
- 预期：跳过非 object 项，校验通过（`result.valid == true`）
- 断言：`EXPECT_TRUE(result.valid)`

**R_CPP_09 — parseFieldMeta 递归解析 array<array<object>>，不崩溃**
- 前置条件：构造含两层 array 嵌套的 schema JSON
- 输入：`{ "matrix": { "type": "array", "items": { "type": "array", "items": { "type": "object", "fields": { "value": { "type": "int" } } } } } }`
- 预期：`error` 为空；`schema.fields[0].items` 非空；`schema.fields[0].items->items` 非空且 `type == Object`
- 断言：`ASSERT_NE(schema.fields[0].items->items, nullptr)` / `EXPECT_EQ(schema.fields[0].items->items->type, FieldType::Object)`

**R_CPP_10 — rejectUnknownFields 对 array<array<object>> 外层 items 为 array 时不递归、不崩溃**
- 前置条件：构造外层 array，items 为 array 类型（非 object），外层不应触发 rejectUnknownFields 递归
- 输入：`{"matrix": [[{"value":1}],[{"value":2,"bad_key":"x"}]]}`
- 预期：外层 array 的 items 类型为 Array（非 Object），`rejectUnknownFields` 不递归进入内层，`bad_key` 不被检测，`result.valid == true`（验证不崩溃且不误报）
- 断言：`EXPECT_TRUE(result.valid)`

**R_FE_01 — ArrayField handleAdd 对 array<object> 执行深度初始化**
- 前置条件：render `<ArrayField field={radarSchema} value={[]} onChange={onChange} />`（radarSchema.items.fields 含 id:string、port:int）
- 输入：点击 Add 按钮
- 预期：`onChange` 被调用，参数为 `[{ id: '', port: 0 }]`（string→`''`，int→`0`）
- 断言：`expect(onChange).toHaveBeenCalledWith([{ id: '', port: 0 }])`

**R_FE_01_NONOBJ — array<string> handleAdd 浅层初始化不受影响**
- 前置条件：render `<ArrayField field={stringArrayField} value={[]} onChange={onChange} />`
- 输入：点击 Add 按钮
- 预期：`onChange` 被调用，参数为 `['']`
- 断言：`expect(onChange).toHaveBeenCalledWith([''])`

**R_FE_01_INT — getDefaultItem 对 int 类型返回 0**
- 前置条件：直接调用 `getDefaultItem('int')`
- 输入：`'int'`
- 预期：返回 `0`
- 断言：`expect(getDefaultItem('int')).toBe(0)`

**R_FE_02 — 数组项 label 显示 "radar 1" 而非 "0"**
- 前置条件：render `<ArrayField field={radarSchema} value={[{}]} onChange={jest.fn()} />`
- 输入：初始渲染一个已有项
- 预期：存在包含 "radar 1" 文本的元素
- 断言：`expect(screen.getByText(/radar 1/i)).toBeInTheDocument()`

**R_FE_03 — PreviewEditor 正确渲染 array<object> 子字段**
- 前置条件：构造含 `descriptor.items.fields` 的 SchemaNode
- 输入：调用 `nodesToFieldMeta` 转换
- 预期：返回的 FieldMeta 中 `items.fields.length == 2`（id, port）
- 断言：`expect(meta[0].items?.fields?.length).toBe(2)`

**R_FE_04 — nodesToFieldMeta items 无 fields 时输出 undefined**
- 前置条件：`descriptor.items` 存在但无 `fields` 属性
- 输入：`{ name: 'tags', descriptor: { type: 'array', items: { type: 'string' } } }`
- 预期：`meta.items.fields == undefined`
- 断言：`expect(result[0].items?.fields).toBeUndefined()`

#### 测试代码框架

```cpp
// C++ — gtest（service_config_schema 相关用例已在 4.1 给出完整代码）
// 此处给出 validator 补充用例框架：

TEST_F(ValidatorArrayObjectTest, RejectUnknownField_ArrayItem_NotObject_Skipped) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": ["not-an-object"]
    })").object();
    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_TRUE(result.valid);  // 非 object 项跳过，不报错
}

// R_CPP_09：array<array<object>> 嵌套解析鲁棒性
TEST(ServiceConfigSchemaTest, ParseNestedArrayObject_TwoLevels_NocrashFieldsReachable) {
    const char* schema = R"({
      "matrix": {
        "type": "array",
        "items": {
          "type": "array",
          "items": {
            "type": "object",
            "fields": { "value": { "type": "int" } }
          }
        }
      }
    })";
    QString error;
    auto s = ServiceConfigSchema::fromJsonObject(
        QJsonDocument::fromJson(schema).object(), error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_EQ(s.fields.size(), 1);
    ASSERT_NE(s.fields[0].items, nullptr);                        // 外层 items
    ASSERT_NE(s.fields[0].items->items, nullptr);                 // 内层 items
    EXPECT_EQ(s.fields[0].items->items->type, FieldType::Object); // 内层 items 为 object
    EXPECT_EQ(s.fields[0].items->items->fields.size(), 1);        // 含 value 字段
}

// R_CPP_10：rejectUnknownFields 对 array<array<object>> 外层 items 为 array 时不递归
TEST(ValidatorTest, RejectUnknownFields_OuterArrayItemsIsArray_NoRecurseNoCrash) {
    ServiceConfigSchema schema;
    FieldMeta outerArr;
    outerArr.name = "matrix"; outerArr.type = FieldType::Array;
    auto innerArr = std::make_shared<FieldMeta>();
    innerArr->type = FieldType::Array;                             // items 是 Array，非 Object
    outerArr.items = innerArr;
    schema.fields.append(outerArr);

    // 外层 items 类型为 Array（不是 Object），不应触发 rejectUnknownFields 递归
    // bad_key 在内层，不应被检测（外层递归条件不满足）
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "matrix": [ [{"value": 1}], [{"value": 2, "bad_key": "x"}] ]
    })").object();
    auto result = ServiceConfigValidator::rejectUnknownFields(schema, config, "");
    EXPECT_TRUE(result.valid);  // 不崩溃，不误报
}
```

```typescript
// TypeScript — Vitest/Jest（前端测试）
describe('getDefaultItem', () => {
  it('returns {} for object', () => expect(getDefaultItem('object')).toEqual({}));
  it('returns [] for array',  () => expect(getDefaultItem('array')).toEqual([]));
  it('returns false for bool', () => expect(getDefaultItem('bool')).toBe(false));
  it('returns 0 for int',      () => expect(getDefaultItem('int')).toBe(0));
  it('returns 0 for double',   () => expect(getDefaultItem('double')).toBe(0));
  it('returns "" for string',  () => expect(getDefaultItem('string')).toBe(''));
  it('returns "" for undefined', () => expect(getDefaultItem(undefined)).toBe(''));
});
```

### 6.2 集成 / API 测试

文件：`src/tests/test_api_router.cpp`

**`GET_ServiceDetail_ArrayObject_HasItemsFields`**
- 注册含 `array<object>` 的 schema，调用 `GET /api/services/:id`
- 断言：响应 JSON 中 `configSchemaFields[0].items.fields` 非空，长度为 3

**`POST_ValidateConfig_ArrayObject_Valid`**
- 合法 `array<object>` 配置（radars 含 id/host/port）
- 断言：响应 `{"valid": true}`

**`POST_ValidateConfig_ArrayObject_UnknownSubfield`**
- radars[0] 含未知字段 `bad_key`
- 断言：响应 `{"valid": false}`，`errorField` 含 `"radars[0].bad_key"`，HTTP 状态码 200（validation 结果而非服务器错误）

### 6.3 验收标准

- [ ] `GET /api/services/:id` 返回的 `configSchemaFields` 中 `array<object>` 字段 `items.fields` 完整，包含正确子字段列表（对应 `R_CPP_01`）
- [ ] `POST /api/services/:id/validate-config` 对 `array<object>` 项内未知字段返回 `errorField` 格式为 `"field[i].subkey"`（对应 `R_CPP_05`、`R_CPP_06`）
- [ ] 项目配置页点击 Add Item 后渲染出 `array<object>` 子字段表单，新增项按类型深度初始化（int 字段为 `0`，不为 `''`）（对应 `R_FE_01`、`R_FE_03`）
- [ ] Schema Editor Preview 模式展示 `array<object>` 子字段，与 JSON Editor 模式内容一致（对应 `R_FE_03`）
- [ ] 数组项 label 格式为 `"${itemName} ${i+1}"`，不再显示纯数字索引（对应 `R_FE_02`）
- [ ] `fromJsObject` 路径与 `fromJsonObject` 路径对相同 schema 的解析结果完全一致（对应 `R_CPP_04`）
- [ ] `array<array<object>>` 两层嵌套 schema 解析不崩溃，`rejectUnknownFields` 对外层 items 为 array 类型时不误递归（对应 `R_CPP_09`、`R_CPP_10`）
- [ ] 新增 20 条测试（C++ 11 + API 3 + 前端 6）全部通过，现有测试套件无回归

---

## 7. 风险与控制

- 风险：`fromJsObject` 改为调用 `parseObject` 后，JS API 旧路径某些字段行为出现差异（如 `enumValues`/`enum` 命名转换）
  - 控制：`parseObject` 已有 `enumValues → enum` 重命名逻辑，确认覆盖；补充 `fromJsObject` 的基础类型回归测试（string/int/bool/enum/object/array 各一条），在 Green 阶段完成前执行
  - 控制：Refactor 阶段专项检查旧 JS API 用例，确认无新增失败
  - 测试覆盖：`R_CPP_04` 含 description 断言，若命名转换出现问题会在此暴露

- 风险：`rejectUnknownFields` 新增 array 递归，若 `items` 为 null 指针导致崩溃
  - 控制：递归分支已有 `field->items` 空指针检查（`field->items && !field->items->fields.isEmpty()`），守护条件覆盖 null 情况
  - 测试覆盖：`R_CPP_07`（array<string>，items.fields 为空）覆盖此边界

- 风险：`PreviewEditor` `nodesToFieldMeta` 改动影响非 array<object> 场景（如 array<string>）的 items 转换
  - 控制：改动仅在 `itemDesc.fields` 存在时才设置 `fields` 属性（使用三元表达式返回 undefined），不影响 `itemDesc.fields` 不存在的情况
  - 测试覆盖：`R_FE_04` 验证 items 无 fields 时返回 undefined

- 风险：`ArrayField` label 改动使现有快照测试失败
  - 控制：检查 `SchemaForm.__tests__` 下是否存在快照用例（snapshot tests），若存在则同步更新快照；快照更新应在 Refactor 阶段单独提交
  - 控制：label 改动向后兼容（`field.items?.name ?? field.name` 保留 fallback），无 items 的场景行为不变

---

## 8. 里程碑完成定义（DoD）

- [ ] 代码实现完成（4 个文件改动），经代码评审通过
- [ ] 文档同步完成（本里程碑文档入库 `doc/milestone/`）
- [ ] 向后兼容确认：`fromJsObject`、`ArrayField` 旧用法测试无回归
- [ ] 【问题修复类】Red 阶段：`R_CPP_01`–`R_CPP_06`、`R_FE_01`–`R_FE_03` 共 9 条失败测试有执行记录（含失败输出摘要），确认在修复代码提交前为 Red
- [ ] 【问题修复类】Green 阶段：上述 9 条测试在最小修复后全部通过，现有测试套件（stdiolink_tests + webui vitest）无回归，有运行记录
- [ ] 【问题修复类】Refactor 阶段：清除 `parseObject` 内旧内联代码、补充 `fromJsObject` 基础类型回归测试、`getDefaultItem` JSDoc；重构后全量测试仍通过
- [ ] API 集成测试 3 条（`test_api_router.cpp` 新增）全部通过
- [ ] E2E 手工验收：在开发环境部署后，使用含 `radars` 字段（array<object>，3 个子字段）的 service，完成"新增配置项 → 填写子字段 → 保存 → 再次打开表单"完整流程，子字段渲染正确，保存值与输入一致
