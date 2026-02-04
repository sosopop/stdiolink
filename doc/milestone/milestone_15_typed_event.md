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
    // 新增：带事件名的 event 方法
    virtual void event(const QString& eventName, int code, const QJsonValue& data) = 0;
};
```

## 5. 验收标准

1. EventMeta 结构完整定义
2. IResponder 支持带事件名的 event
3. UI 可根据 Schema 解析事件流

## 6. 依赖关系

- **前置**: M12 (双模式集成)
- **后续**: M16 (高级UI生成)
