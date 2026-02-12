# 里程碑 54：Schema 校验与配置工具 API

> **前置条件**: 里程碑 52 已完成（Service CRUD 已就绪）
> **目标**: 实现 Schema 格式校验、默认配置生成、配置预校验三个工具 API，并增强 `GET /api/services/{id}` 返回 `configSchemaFields`

---

## 1. 目标

- 实现 `POST /api/services/{id}/validate-schema` — 校验 Schema 格式合法性
- 实现 `POST /api/services/{id}/generate-defaults` — 根据 Schema 生成默认配置
- 实现 `POST /api/services/{id}/validate-config` — 创建 Project 前的配置预校验
- 增强 `GET /api/services/{id}` — 新增 `configSchemaFields` 字段（FieldMeta 数组格式）
- 为 `ServiceConfigSchema` 新增 `fromJsonObject()` public 静态方法

---

## 2. 背景与问题

WebUI 需要在 Schema 编辑器中实时校验 schema 格式、在 Project 创建界面自动填充默认值并预校验配置。当前后端缺少这三个工具 API。此外，Service 的 `configSchema` 是 key-value 格式，而 Driver Meta 的 `params` 是数组格式，前端需要统一的 FieldMeta 数组格式来复用表单生成逻辑。

---

## 3. 技术要点

### 3.1 `POST /api/services/{id}/validate-schema`

校验一份 config schema JSON 是否合法（不写入文件）。

请求体：

```json
{
  "schema": {
    "port": { "type": "int", "required": true, "default": 8080 },
    "name": { "type": "string", "required": true }
  }
}
```

成功响应（200 OK）：

```json
{
  "valid": true,
  "fields": [
    { "name": "port", "type": "int", "required": true, "defaultValue": 8080, "description": "" },
    { "name": "name", "type": "string", "required": true, "description": "" }
  ]
}
```

失败响应（200 OK）：

```json
{
  "valid": false,
  "error": "unknown field type \"datetime\" for field \"createdAt\""
}
```

实现要点：

- 需为 `ServiceConfigSchema` 新增 `fromJsonObject(const QJsonObject& obj, QString& error)` public 方法
- 复用已有的 `parseObject()` 内部校验逻辑
- 注意：`integer`（→ int）、`number`（→ double）、`boolean`（→ bool）是合法的类型别名
- 不可使用 `fromJsObject()`，它不返回错误信息

### 3.2 `POST /api/services/{id}/generate-defaults`

根据 Service 的 config schema 生成默认配置。

响应（200 OK）：

```json
{
  "serviceId": "my-service",
  "config": {
    "port": 8080,
    "debug": false,
    "ratio": 0.5
  },
  "requiredFields": ["name", "port"],
  "optionalFields": ["ratio", "debug"]
}
```

实现：遍历 `ServiceInfo.configSchema.fields`，有 `defaultValue` 的字段填入默认值。同时分类返回必填字段和可选字段列表。

### 3.3 `POST /api/services/{id}/validate-config`

在创建 Project 之前预校验配置。

请求体：

```json
{
  "config": {
    "port": 8080,
    "name": "test"
  }
}
```

成功响应（200 OK）：

```json
{
  "valid": true
}
```

失败响应（200 OK）：

```json
{
  "valid": false,
  "errors": [
    { "field": "name", "message": "required field missing" }
  ]
}
```

实现要点：

- 调用 `ServiceConfigValidator::validate()` 针对 Service schema 校验
- **当前限制**：`validate()` 采用 fail-fast 策略，遇到第一个错误即返回
- **阶段性方案**：先返回单一错误（`errors` 数组长度为 1），保持向前兼容格式
- 后续可增强为 `validateAll()` 遍历所有字段收集全部错误

### 3.4 `GET /api/services/{id}` 增强

在现有响应中新增 `configSchemaFields` 字段：

```json
{
  "id": "my-service",
  "name": "My Service",
  "configSchema": { "port": { "type": "int", ... } },
  "configSchemaFields": [
    { "name": "port", "type": "int", "required": true, ... }
  ]
}
```

实现：`ServiceInfo` 已持有解析后的 `ServiceConfigSchema`（含 `QVector<FieldMeta> fields`）。在 `handleServiceDetail()` 中新增 `configSchemaFields` 时，建议通过 `FieldMeta::toJson()` 逐个构造数组。

注意：保留现有 `ServiceConfigSchema::toJson()` 的返回类型（`QJsonObject`），避免破坏现有调用方；新增独立方法导出 FieldMeta 数组。

---

## 4. 实现方案

### 4.1 ServiceConfigSchema 扩展

```cpp
// 新增 public 静态方法
class ServiceConfigSchema {
public:
    // ... existing methods

    /// 从 JSON 对象解析 schema，带错误检查
    static ServiceConfigSchema fromJsonObject(const QJsonObject& obj,
                                               QString& error);

    /// 保持现有接口：导出为 {"fields":[...]} 对象
    QJsonObject toJson() const;

    /// 新增：直接导出 FieldMeta 数组，供 API 返回 configSchemaFields 使用
    QJsonArray toFieldMetaArray() const;

    /// 生成默认配置
    QJsonObject generateDefaults() const;

    /// 获取必填字段名列表
    QStringList requiredFieldNames() const;

    /// 获取可选字段名列表
    QStringList optionalFieldNames() const;
};
```

### 4.2 `fromJsonObject()` 实现

```cpp
ServiceConfigSchema ServiceConfigSchema::fromJsonObject(const QJsonObject& obj,
                                                          QString& error) {
    ServiceConfigSchema schema;
    // 复用已有 parseObject() 逻辑
    // parseObject 内部校验类型合法性、约束完整性等
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString fieldName = it.key();
        const QJsonObject fieldObj = it.value().toObject();

        FieldMeta meta;
        if (!parseFieldMeta(fieldName, fieldObj, meta, error)) {
            return {}; // error 已填充
        }
        schema.fields.append(meta);
    }
    return schema;
}
```

### 4.3 `generateDefaults()` 实现

```cpp
QJsonObject ServiceConfigSchema::generateDefaults() const {
    QJsonObject config;
    for (const auto& field : fields) {
        if (!field.defaultValue.isNull() && !field.defaultValue.isUndefined()) {
            config[field.name] = field.defaultValue;
        }
    }
    return config;
}
```

### 4.4 ApiRouter handler

```cpp
QHttpServerResponse ApiRouter::handleValidateSchema(const QString& id,
                                                     const QHttpServerRequest& req) {
    // 1. 查找 Service
    auto it = m_manager->services().find(id);
    if (it == m_manager->services().end())
        return errorResponse(StatusCode::NotFound, "service not found");

    // 2. 解析请求
    auto body = QJsonDocument::fromJson(req.body()).object();
    if (!body.contains("schema") || !body["schema"].isObject())
        return errorResponse(StatusCode::BadRequest, "schema field required");
    auto schemaObj = body["schema"].toObject();

    // 3. 校验
    QString error;
    auto schema = ServiceConfigSchema::fromJsonObject(schemaObj, error);
    if (!error.isEmpty()) {
        return jsonResponse(QJsonObject{
            {"valid", false},
            {"error", error}
        });
    }

    // 4. 返回解析后的 fields
    return jsonResponse(QJsonObject{
        {"valid", true},
        {"fields", schema.toFieldMetaArray()}
    });
}
```

---

## 5. 文件变更清单

### 5.1 修改文件

- `src/stdiolink_service/config/service_config_schema.h` — 新增 `fromJsonObject()`/`toFieldMetaArray()`/`generateDefaults()`/`requiredFieldNames()`/`optionalFieldNames()`，保留 `toJson()`
- `src/stdiolink_service/config/service_config_schema.cpp` — 实现新增方法
- `src/stdiolink_server/http/api_router.h` — 新增 `handleValidateSchema`/`handleGenerateDefaults`/`handleValidateConfig` handler 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现三个 handler + 路由注册 + `handleServiceDetail` 增加 `configSchemaFields`

### 5.2 测试文件

- 修改 `src/tests/test_service_config_schema.cpp` — 新增 `fromJsonObject`/`toFieldMetaArray`/`generateDefaults` 测试
- 修改 `src/tests/test_api_router.cpp` — 新增三个工具 API 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**ServiceConfigSchema::fromJsonObject（test_service_config_schema.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 合法 schema（含 int/string/bool/double） | 返回正确 fields，error 为空 |
| 2 | 类型别名 `integer` → `int` | 合法通过 |
| 3 | 类型别名 `number` → `double` | 合法通过 |
| 4 | 类型别名 `boolean` → `bool` | 合法通过 |
| 5 | 未知类型 `"datetime"` | error 非空，包含字段名 |
| 6 | 空 schema `{}` | 合法，fields 为空 |
| 7 | 含 constraints（min/max） | constraints 正确解析 |
| 8 | 含 ui hints（widget/group） | ui 信息正确解析 |
| 9 | 含 enum 类型 + enumValues | 合法通过 |
| 10 | 含 array 类型 + items | 合法通过 |
| 11 | 含 object 类型 + fields | 嵌套结构正确 |

**ServiceConfigSchema::toFieldMetaArray**：

| # | 场景 | 验证点 |
|---|------|--------|
| 12 | 转换为 FieldMeta 数组 | 每个元素含 `name`/`type`/`required` |
| 13 | 与 fromJsonObject 往返一致 | `toFieldMetaArray` 后结构与原始 fields 对应 |

**generateDefaults**：

| # | 场景 | 验证点 |
|---|------|--------|
| 14 | 含 default 的字段 | 填入默认值 |
| 15 | 无 default 的字段 | 不出现在结果中 |
| 16 | 不同类型的默认值（int/string/bool/double） | 类型正确 |

**API 测试（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 17 | `POST validate-schema` 合法 schema | 200 + `valid: true` + `fields` 数组 |
| 18 | `POST validate-schema` 非法类型 | 200 + `valid: false` + `error` |
| 19 | `POST validate-schema` 缺少 `schema` 字段 | 400 |
| 20 | `POST validate-schema` Service 不存在 | 404 |
| 21 | `POST generate-defaults` | 200 + `config` 含默认值 + `requiredFields`/`optionalFields` |
| 22 | `POST generate-defaults` schema 无 default | 200 + `config` 为空对象 |
| 23 | `POST generate-defaults` Service 不存在 | 404 |
| 24 | `POST validate-config` 合法配置 | 200 + `valid: true` |
| 25 | `POST validate-config` 必填字段缺失 | 200 + `valid: false` + `errors` 数组 |
| 26 | `POST validate-config` 值超出范围 | 200 + `valid: false` |
| 27 | `POST validate-config` Service 不存在 | 404 |
| 28 | `GET /api/services/{id}` 含 `configSchemaFields` | 响应中有 FieldMeta 数组 |
| 29 | `configSchemaFields` 与 `configSchema` 字段一致 | 数量和字段名对应 |

### 6.2 验收标准

- validate-schema 正确识别合法/非法 schema，返回解析后的 FieldMeta
- generate-defaults 正确生成默认配置和字段分类
- validate-config 正确校验配置并返回错误列表（阶段性单错误）
- `GET /api/services/{id}` 包含 `configSchemaFields` 数组
- `ServiceConfigSchema::fromJsonObject()` 可供其他模块复用
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`fromJsonObject()` 与已有 `fromJsonFile()` 解析逻辑不一致
  - 控制：`fromJsonObject()` 内部调用相同的 `parseObject()` / 字段解析逻辑
- **风险 2**：validate-config 的 fail-fast 限制导致用户体验差
  - 控制：阶段性方案先返回单一错误，`errors` 数组格式保持向前兼容；后续增强为 `validateAll()`
- **风险 3**：直接改 `toJson()` 签名引发兼容性破坏
  - 控制：保留现有 `toJson()`，新增 `toFieldMetaArray()` 承载 API 新需求

---

## 8. 里程碑完成定义（DoD）

- `ServiceConfigSchema::fromJsonObject()` 实现并可被 API 调用
- `ServiceConfigSchema::toFieldMetaArray()`/`generateDefaults()` 实现（`toJson()` 保持兼容）
- 三个工具 API 实现并注册路由
- `GET /api/services/{id}` 返回 `configSchemaFields`
- 对应单元测试完成并通过
- 本里程碑文档入库
