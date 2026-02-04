# 里程碑 11：Host 侧元数据查询

> **前置条件**: 里程碑 7、8 已完成
> **目标**: Host 能查询和使用 Driver 元数据

---

## 1. 目标

- 扩展 `Driver` 类添加 `queryMeta()` 方法
- 实现 `MetaCache` 元数据缓存
- 实现 `UiGenerator` UI 描述生成器
- 支持元数据变更检测

---

## 2. 技术要点

### 2.1 元数据查询策略

1. 优先调用 `meta.describe` 命令
2. 失败时尝试兼容命令（`meta.get`、`meta.introspect`）
3. 兼容 `info` 与 `driver` 两种字段名
4. 缓存查询结果，避免重复请求

### 2.2 缓存策略

- 按 Driver ID 缓存元数据
- 支持 `metaHash` 变更检测
- 提供手动刷新接口
- 进程重启后自动失效

### 2.3 UI 描述模型

```cpp
struct FormDesc {
    QString title;
    QString description;
    QJsonArray widgets;  // 控件描述数组
};
```

控件描述格式：
```json
{
  "name": "timeout",
  "type": "int",
  "widget": "slider",
  "label": "超时时间",
  "default": 5000,
  "min": 100,
  "max": 60000,
  "unit": "ms"
}
```

---

## 3. 实现步骤

### 3.1 扩展 Driver 类

```cpp
// host/driver.h 新增

class Driver {
public:
    // ... 现有方法

    /**
     * 查询 Driver 元数据
     * @param timeoutMs 超时时间
     * @return 元数据指针，失败返回 nullptr
     */
    const meta::DriverMeta* queryMeta(int timeoutMs = 5000);

    /**
     * 是否已有元数据
     */
    bool hasMeta() const;

    /**
     * 刷新元数据缓存
     */
    void refreshMeta();

    /**
     * 带验证的请求
     * 发送前自动验证参数
     */
    Task requestWithValidation(const QString& cmd,
                               const QJsonObject& data = {});

private:
    std::shared_ptr<meta::DriverMeta> m_meta;
};
```

### 3.2 创建 host/meta_cache.h

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"
#include <QHash>
#include <QMutex>
#include <memory>

namespace stdiolink {

class MetaCache {
public:
    static MetaCache& instance();

    void store(const QString& driverId,
               std::shared_ptr<meta::DriverMeta> meta);

    std::shared_ptr<meta::DriverMeta> get(const QString& driverId) const;

    void invalidate(const QString& driverId);

    void clear();

    bool hasChanged(const QString& driverId,
                    const QString& metaHash) const;

private:
    MetaCache() = default;
    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<meta::DriverMeta>> m_cache;
    QHash<QString, QString> m_hashCache;
};

} // namespace stdiolink
```

### 3.3 创建 host/ui_generator.h

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

struct FormDesc {
    QString title;
    QString description;
    QJsonArray widgets;
};

class UiGenerator {
public:
    static FormDesc generateCommandForm(const meta::CommandMeta& cmd);
    static FormDesc generateConfigForm(const meta::ConfigSchema& config);
    static QJsonObject toJson(const FormDesc& form);

private:
    static QJsonObject fieldToWidget(const meta::FieldMeta& field);
    static QString defaultWidget(meta::FieldType type);
};

} // namespace stdiolink
```

### 3.4 queryMeta 实现

```cpp
// host/driver.cpp

const meta::DriverMeta* Driver::queryMeta(int timeoutMs) {
    if (m_meta) {
        return m_meta.get();
    }

    // 发送 meta.describe 请求
    Task task = request("meta.describe", QJsonObject{});
    Message msg;
    if (!task.waitNext(msg, timeoutMs)) {
        return nullptr;
    }

    if (msg.status != "done") {
        return nullptr;
    }

    // 解析元数据
    m_meta = std::make_shared<meta::DriverMeta>(
        meta::DriverMeta::fromJson(msg.data.toObject()));

    // 存入缓存
    MetaCache::instance().store(m_meta->info.id, m_meta);

    return m_meta.get();
}
```

### 3.5 UiGenerator 实现

```cpp
// host/ui_generator.cpp

FormDesc UiGenerator::generateCommandForm(const meta::CommandMeta& cmd) {
    FormDesc form;
    form.title = cmd.title.isEmpty() ? cmd.name : cmd.title;
    form.description = cmd.description;

    for (const auto& param : cmd.params) {
        form.widgets.append(fieldToWidget(param));
    }

    return form;
}

QJsonObject UiGenerator::fieldToWidget(const meta::FieldMeta& field) {
    QJsonObject widget;
    widget["name"] = field.name;
    widget["type"] = meta::fieldTypeToString(field.type);
    widget["label"] = field.description;
    widget["required"] = field.required;

    // 设置控件类型
    QString w = field.ui.widget;
    if (w.isEmpty()) {
        w = defaultWidget(field.type);
    }
    widget["widget"] = w;

    // 默认值
    if (!field.defaultValue.isNull()) {
        widget["default"] = field.defaultValue;
    }

    // 约束条件
    if (field.constraints.min.has_value())
        widget["min"] = *field.constraints.min;
    if (field.constraints.max.has_value())
        widget["max"] = *field.constraints.max;
    if (!field.constraints.enumValues.isEmpty())
        widget["options"] = field.constraints.enumValues;

    return widget;
}
```

---

## 4. 验收标准

1. Host 能通过 `queryMeta()` 获取 Driver 元数据
2. 元数据正确缓存，重复查询不发送请求
3. 能生成命令调用的 UI 描述 JSON
4. 能生成配置界面的 UI 描述 JSON
5. 不支持 meta 的 Driver 返回 nullptr，不崩溃
6. 缓存失效后能重新获取

---

## 5. 单元测试用例

### 5.1 测试文件：tests/meta_host_test.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/host/meta_cache.h"
#include "stdiolink/host/ui_generator.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class MetaHostTest : public ::testing::Test {};

// 测试 MetaCache 存取
TEST_F(MetaHostTest, CacheStoreAndGet) {
    auto meta = std::make_shared<DriverMeta>();
    meta->info.id = "test.driver";
    meta->info.name = "Test";

    MetaCache::instance().store("test.driver", meta);
    auto retrieved = MetaCache::instance().get("test.driver");

    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->info.id, "test.driver");
}

// 测试缓存失效
TEST_F(MetaHostTest, CacheInvalidate) {
    auto meta = std::make_shared<DriverMeta>();
    meta->info.id = "test.driver2";

    MetaCache::instance().store("test.driver2", meta);
    MetaCache::instance().invalidate("test.driver2");

    auto retrieved = MetaCache::instance().get("test.driver2");
    EXPECT_EQ(retrieved, nullptr);
}

// 测试 UI 生成
TEST_F(MetaHostTest, UiGeneratorCommand) {
    CommandMeta cmd;
    cmd.name = "scan";
    cmd.description = "执行扫描";

    FieldMeta param;
    param.name = "fps";
    param.type = FieldType::Int;
    param.description = "帧率";
    param.defaultValue = 10;
    param.constraints.min = 1;
    param.constraints.max = 60;
    cmd.params.append(param);

    FormDesc form = UiGenerator::generateCommandForm(cmd);

    EXPECT_EQ(form.title, "scan");
    EXPECT_EQ(form.widgets.size(), 1);

    QJsonObject widget = form.widgets[0].toObject();
    EXPECT_EQ(widget["name"].toString(), "fps");
    EXPECT_EQ(widget["default"].toInt(), 10);
}
```

---

## 6. 依赖关系

- **前置依赖**:
  - 里程碑 7（元数据类型定义）
  - 里程碑 8（meta.describe 协议）
- **可选依赖**: 里程碑 10（参数验证器）用于 requestWithValidation

---

## 7. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/stdiolink/host/driver.h` | 修改 | 添加 queryMeta() |
| `src/stdiolink/host/driver.cpp` | 修改 | 实现元数据查询 |
| `src/stdiolink/host/meta_cache.h` | 新增 | 元数据缓存 |
| `src/stdiolink/host/meta_cache.cpp` | 新增 | 缓存实现 |
| `src/stdiolink/host/ui_generator.h` | 新增 | UI 生成器 |
| `src/stdiolink/host/ui_generator.cpp` | 新增 | UI 生成器实现 |
| `src/tests/meta_host_test.cpp` | 新增 | 单元测试 |
```
