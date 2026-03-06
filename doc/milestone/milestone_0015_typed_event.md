# 里程碑 15：强类型事件流规范

## 1. 目标

将事件流语义从"口头约定"强制落地到代码层。

## 2. 对应需求

- **需求6**: 强类型事件流规范 (Typed Event Schema)

## 3. 设计要点

### 3.1 Event Payload 强制结构

```json
{
  "event": "progress",
  "data": { "percent": 50, "message": "Processing..." }
}
```

**协议映射规则**：
1. JSONL 头仍为 `{"status":"event","code":0}`
2. payload 必须包含 `event` 字段
3. `data` 为事件数据主体（可选），未使用 `data` 时允许直接把字段平铺到 payload，但必须保留 `event`

### 3.2 Meta Schema 中定义事件

```cpp
struct EventMeta {
    QString name;        // 事件名称
    QString description;
    QVector<FieldMeta> fields;  // 事件数据结构
};

struct CommandMeta {
    // ...
    QVector<EventMeta> events;  // 命令可能触发的事件
};
```

## 4. IResponder 扩展

```cpp
class IResponder {
public:
    // 旧版（保留，默认实现转发到扩展接口）
    virtual void event(int code, const QJsonValue& payload);
};

// 新版扩展接口（避免破坏 ABI）
class IResponderEx {
public:
    virtual void event(const QString& eventName, int code, const QJsonValue& data) = 0;
};
```

兼容策略：
- 保留旧版 `event(int, payload)`，内部自动封装为 `{event:"default", data:payload}`
- 若 Responder 同时实现 `IResponderEx`，则优先调用带事件名接口
- Host/UI 在解析时若缺失 `event` 字段，则视为 `"default"`（兼容旧驱动）

说明：
- **不直接在 IResponder 上新增纯虚方法**，避免破坏 ABI
- 通过新增扩展接口实现平滑升级
 - 需要同步扩展 `StdioResponder`、`ConsoleResponder`、`MockResponder` 以记录/输出 eventName

## 5. 验收标准

1. EventMeta 结构完整定义
2. IResponder 支持带事件名的 event
3. UI 可根据 Schema 解析事件流
4. 旧版驱动不崩溃，事件按 `default` 处理

## 6. 单元测试用例

### 6.1 测试文件：tests/test_typed_event.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/driver/stdio_responder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class TypedEventTest : public ::testing::Test {};

// 测试 EventMeta 序列化
TEST_F(TypedEventTest, EventMetaSerialization) {
    EventMeta event;
    event.name = "progress";
    event.description = "Progress update";

    FieldMeta field;
    field.name = "percent";
    field.type = FieldType::Int;
    event.fields.append(field);

    QJsonObject json = event.toJson();
    EXPECT_EQ(json["name"].toString(), "progress");
    EXPECT_TRUE(json.contains("fields"));
}

// 测试 EventMeta 反序列化
TEST_F(TypedEventTest, EventMetaDeserialization) {
    QJsonObject json{
        {"name", "progress"},
        {"description", "Progress update"},
        {"fields", QJsonArray{
            QJsonObject{{"name", "percent"}, {"type", "int"}}
        }}
    };

    EventMeta event = EventMeta::fromJson(json);
    EXPECT_EQ(event.name, "progress");
    EXPECT_EQ(event.fields.size(), 1);
}

// 测试命令包含事件定义
TEST_F(TypedEventTest, CommandWithEvents) {
    CommandMeta cmd;
    cmd.name = "scan";

    EventMeta event;
    event.name = "progress";
    cmd.events.append(event);

    QJsonObject json = cmd.toJson();
    EXPECT_TRUE(json.contains("events"));
    EXPECT_EQ(json["events"].toArray().size(), 1);
}
```

### 6.2 IResponder 扩展测试

```cpp
// 测试带事件名的 event 调用
TEST_F(TypedEventTest, ResponderEventWithName) {
    // MockResponder 记录事件
    MockResponder resp;
    resp.event("progress", 50, QJsonObject{{"message", "Processing"}});

    EXPECT_EQ(resp.lastEventName(), "progress");
    EXPECT_EQ(resp.lastEventCode(), 50);
}

// 测试旧版 event 兼容性
TEST_F(TypedEventTest, ResponderEventLegacy) {
    MockResponder resp;
    resp.event(50, QJsonObject{{"percent", 50}});

    // 应自动封装为 default 事件
    EXPECT_EQ(resp.lastEventName(), "default");
}

// 测试事件 payload 结构
TEST_F(TypedEventTest, EventPayloadStructure) {
    // 验证输出格式包含 event 字段
    // {"event": "progress", "data": {...}}
}
```

### 6.3 事件验证测试

```cpp
// 测试事件数据验证
TEST_F(TypedEventTest, ValidateEventData) {
    EventMeta event;
    event.name = "progress";

    FieldMeta field;
    field.name = "percent";
    field.type = FieldType::Int;
    field.constraints.min = 0;
    field.constraints.max = 100;
    event.fields.append(field);

    QJsonObject validData{{"percent", 50}};
    QJsonObject invalidData{{"percent", 150}};

    // 验证有效数据
    // 验证无效数据
}
```

## 7. 依赖关系

- **前置**: M12 (双模式集成)
- **后续**: M16 (高级UI生成)
