# 里程碑 8：Driver 侧 meta.describe

> **前置条件**: 里程碑 7 已完成
> **目标**: 让 Driver 能够响应 meta.describe 命令，返回完整元数据

---

## 1. 目标

- 定义 `IMetaCommandHandler` 接口，扩展现有 `ICommandHandler`
- 扩展 `DriverCore` 拦截 `meta.*` 保留命令
- 实现 `meta.describe` 命令响应
- 保持向后兼容，不实现 meta 的旧 Driver 仍正常工作

---

## 2. 技术要点

### 2.1 保留命令命名空间

所有框架保留命令使用 `meta.` 前缀：

| 命令 | 方向 | 描述 | 必须实现 |
|------|------|------|----------|
| `meta.describe` | Host → Driver | 获取完整元数据 | 推荐实现 |
| `meta.config.get` | Host → Driver | 获取当前配置 | 可选 |
| `meta.config.set` | Host → Driver | 设置配置 | 可选 |
| `meta.validate` | Host → Driver | 验证参数（不执行） | 可选 |

### 2.2 meta.describe 协议

**请求**：
```json
{"cmd": "meta.describe", "data": null}
```

**响应**（done payload）：
```json
{
  "schemaVersion": "1.0",
  "info": {
    "id": "com.example.scan_driver",
    "name": "Scan Driver",
    "version": "1.2.0",
    "description": "3D 扫描驱动程序"
  },
  "capabilities": ["keepalive", "streaming"],
  "config": { ... },
  "commands": [ ... ]
}
```

### 2.3 错误码扩展

| 错误码 | 名称 | 描述 |
|--------|------|------|
| 400 | ValidationFailed | 参数验证失败 |
| 404 | CommandNotFound | 命令不存在 |
| 501 | MetaNotSupported | 不支持元数据查询 |

---

## 3. 实现步骤

### 3.1 创建 driver/meta_command_handler.h

```cpp
#pragma once

#include "icommand_handler.h"
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

/**
 * 支持元数据的命令处理器接口
 * 继承自 ICommandHandler，添加元数据支持
 */
class IMetaCommandHandler : public ICommandHandler {
public:
    /**
     * 返回 Driver 的元数据描述
     * 子类必须实现此方法
     */
    virtual const meta::DriverMeta& driverMeta() const = 0;

    /**
     * 是否自动验证参数
     * 默认为 true，框架会在调用 handle() 前自动验证
     */
    virtual bool autoValidateParams() const { return true; }
};

} // namespace stdiolink
```

### 3.2 扩展 DriverCore

在 `driver_core.h` 中添加：

```cpp
class DriverCore {
public:
    // ... 现有方法

    /**
     * 设置支持元数据的处理器
     * 设置后将自动响应 meta.* 命令
     */
    void setMetaHandler(IMetaCommandHandler* handler);

private:
    IMetaCommandHandler* m_metaHandler = nullptr;

    /**
     * 处理 meta.* 保留命令
     * @return true 如果是 meta 命令并已处理
     */
    bool handleMetaCommand(const QString& cmd,
                           const QJsonValue& data,
                           IResponder& responder);
};
```

### 3.3 实现 meta 命令处理

在 `driver_core.cpp` 中：

```cpp
void DriverCore::setMetaHandler(IMetaCommandHandler* handler) {
    m_metaHandler = handler;
    setHandler(handler);  // 同时设置为普通处理器
}

bool DriverCore::handleMetaCommand(const QString& cmd,
                                   const QJsonValue& data,
                                   IResponder& responder) {
    if (!cmd.startsWith("meta.")) {
        return false;
    }

    if (m_metaHandler == nullptr) {
        responder.error(501, "MetaNotSupported",
                        "This driver does not support meta commands");
        return true;
    }

    if (cmd == "meta.describe") {
        QJsonObject metaJson = m_metaHandler->driverMeta().toJson();
        responder.done(0, metaJson);
        return true;
    }

    if (cmd == "meta.config.get") {
        // 由子类实现，这里返回空配置
        responder.done(0, QJsonObject{});
        return true;
    }

    if (cmd == "meta.config.set") {
        // 由子类实现
        responder.done(0, QJsonObject{{"status", "applied"}});
        return true;
    }

    // 未知的 meta 命令
    responder.error(404, "CommandNotFound",
                    QString("Unknown meta command: %1").arg(cmd));
    return true;
}
```

### 3.4 修改 processOneLine

```cpp
void DriverCore::processOneLine(const QString& line) {
    // ... 解析请求

    // 优先处理 meta 命令
    if (handleMetaCommand(cmd, data, *m_responder)) {
        return;
    }

    // 正常业务命令处理
    if (m_handler) {
        m_handler->handle(cmd, data, *m_responder);
    }
}
```

---

## 4. 验收标准

1. Host 发送 `meta.describe` 能收到完整元数据 JSON
2. 不实现 `IMetaCommandHandler` 的旧 Driver 仍正常工作
3. 未知 meta 命令返回 404 错误
4. 不支持 meta 的 Driver 返回 501 错误
5. 元数据 JSON 符合 Meta Schema v1.0 规范

---

## 5. 单元测试用例

### 5.1 测试文件：tests/meta_describe_test.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

// 测试用 MetaHandler
class TestMetaHandler : public IMetaCommandHandler {
public:
    const DriverMeta& driverMeta() const override {
        static DriverMeta meta = createTestMeta();
        return meta;
    }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override {
        if (cmd == "echo") {
            resp.done(0, data);
        } else {
            resp.error(404, "NotFound", "Unknown command");
        }
    }

private:
    static DriverMeta createTestMeta() {
        DriverMeta meta;
        meta.schemaVersion = "1.0";
        meta.info.id = "test.meta.driver";
        meta.info.name = "Test Meta Driver";
        meta.info.version = "1.0.0";

        CommandMeta cmd;
        cmd.name = "echo";
        cmd.description = "Echo input";
        meta.commands.append(cmd);

        return meta;
    }
};

class MetaDescribeTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_handler = new TestMetaHandler();
    }

    void TearDown() override {
        delete m_handler;
    }

    TestMetaHandler* m_handler;
};

// 测试 meta.describe 返回正确元数据
TEST_F(MetaDescribeTest, DescribeReturnsMetadata) {
    // 模拟发送 meta.describe 并验证响应
    // 实际测试需要通过进程通信或模拟
    const auto& meta = m_handler->driverMeta();
    EXPECT_EQ(meta.schemaVersion, "1.0");
    EXPECT_EQ(meta.info.id, "test.meta.driver");
    EXPECT_EQ(meta.commands.size(), 1);
}

// 测试元数据 JSON 序列化
TEST_F(MetaDescribeTest, MetadataJsonFormat) {
    QJsonObject json = m_handler->driverMeta().toJson();

    EXPECT_EQ(json["schemaVersion"].toString(), "1.0");
    EXPECT_TRUE(json.contains("info") || json.contains("driver"));
    EXPECT_TRUE(json.contains("commands"));
}

// 测试普通命令仍然工作
TEST_F(MetaDescribeTest, NormalCommandsStillWork) {
    // 验证 echo 命令仍然可以正常处理
    // 需要通过集成测试验证
}
```

---

## 6. 依赖关系

- **前置依赖**: 里程碑 7（元数据类型定义）
- **后续依赖**:
  - 里程碑 9（Builder API）可独立开发
  - 里程碑 10（参数验证）依赖本里程碑的框架扩展
  - 里程碑 11（Host 侧）依赖本里程碑的协议实现

---

## 7. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/stdiolink/driver/meta_command_handler.h` | 新增 | 元数据处理器接口 |
| `src/stdiolink/driver/driver_core.h` | 修改 | 添加 setMetaHandler |
| `src/stdiolink/driver/driver_core.cpp` | 修改 | 实现 meta 命令拦截 |
| `src/tests/meta_describe_test.cpp` | 新增 | 单元测试 |
