# 里程碑 92：`stdio.drv.multiscan` 演示 Driver 与 DriverLab 端到端验证

> **前置条件**: 里程碑 90（`array<object>` 解析与渲染修复，含 `ArrayField` 深度初始化）、里程碑 91（Demo Service 资产）已完成；里程碑 66（DriverLab 模块）已实现
> **目标**: 新增含 `array<object>` 参数命令的演示 Driver `stdio.drv.multiscan`，作为 DriverLab 对 `array<object>` 参数内联表单能力的端到端验证载体；不引入任何新的 WebUI React 组件

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| 演示 Driver（C++） | 新增 `stdio.drv.multiscan`，含两条使用 `array<object>` 参数的命令：`scan_targets`、`configure_channels` |
| 演示资产（静态文件） | 通过 `STDIOLINK_EXECUTABLE_TARGETS` 注册，`assemble_runtime` 自动将 Driver 放入 `runtime_*/drivers/`，DriverLab 的 Driver 选择列表中出现 `stdio.drv.multiscan` |
| 测试（C++） | `test_multiscan_driver.cpp`：6 条命令行为与元数据单元测试 |
| 端到端验证（手工） | DriverLab 连接 `stdio.drv.multiscan`，确认 `scan_targets` 参数以内联表单渲染，可正常执行 |

**为什么无需新增 WebUI React 组件**：`DriverLab/ParamForm.tsx` 已通过 `<FieldRenderer />` 将参数渲染委托给 `SchemaForm` 字段组件体系（`ArrayField`、`ObjectField` 等）。M90 完成后 `ArrayField` 已支持 `array<object>` 内联表单及子字段深度初始化，DriverLab 天然继承该能力，无需单独开发 `ArrayParamField` 或 `InlineObjectField`。

**非目标**：
- 不新增任何 React 组件
- 不修改 `ParamForm.tsx`、`ArrayField.tsx` 或任何现有 WebUI 文件
- 不支持通过 DriverLab UI 编辑三层以上嵌套（`array<array<object>>`），降级 AnyField 合理

---

## 2. 背景

DriverLab（M66）的命令参数表单通过以下调用链渲染参数：

```
ParamForm → FieldRenderer → {StringField | NumberField | BoolField | EnumField | ArrayField | ...}
```

M90 前，`ArrayField` 缺少对 `array<object>` 的子字段递归渲染和深度初始化支持，DriverLab 因此无法正确展示含 `array<object>` 参数的命令。M90 修复后，该调用链对 `array<object>` 已完全畅通。

M92 的唯一任务是提供一个真实的 C++ Driver，让 DriverLab 用于端到端演示和验收，并作为后续开发者的参考实现。

---

## 3. 技术要点

### 3.1 `stdio.drv.multiscan` Driver 设计

Driver 提供两条命令，均含 `array<object>` 类型参数，覆盖不同的子字段类型组合：

**命令 1：`scan_targets`**

```cpp
CommandMeta scanTargets;
scanTargets.name = "scan_targets";
scanTargets.description = "对多个目标地址并发扫描，返回各目标响应结果";

// targets 参数（array<object>）
FieldMeta targetsParam;
targetsParam.name = "targets";
targetsParam.type = FieldType::Array;
targetsParam.required = true;
targetsParam.description = "扫描目标列表（至少 1 个，最多 16 个）";
targetsParam.constraints.minItems = 1;
targetsParam.constraints.maxItems = 16;

auto targetItem = std::make_shared<FieldMeta>();
targetItem->name = "target";
targetItem->type = FieldType::Object;
targetItem->description = "单个扫描目标";

FieldMeta hostField;
hostField.name = "host";
hostField.type = FieldType::String;
hostField.required = true;
hostField.description = "目标 IP 或主机名";

FieldMeta portField;
portField.name = "port";
portField.type = FieldType::Int;
portField.required = true;
portField.constraints.min = 1;
portField.constraints.max = 65535;
portField.description = "端口";

FieldMeta timeoutField;
timeoutField.name = "timeout_ms";
timeoutField.type = FieldType::Int;
timeoutField.required = false;
timeoutField.constraints.min = 100;
timeoutField.constraints.max = 10000;
timeoutField.description = "超时（ms）";
timeoutField.defaultValue = QJsonValue(2000);

targetItem->fields = { hostField, portField, timeoutField };
targetsParam.items = targetItem;

FieldMeta modeParam;
modeParam.name = "mode";
modeParam.type = FieldType::Enum;
modeParam.required = false;
modeParam.constraints.enumValues = QJsonArray{"quick", "full", "deep"};
modeParam.description = "扫描模式";
modeParam.defaultValue = QJsonValue("quick");

scanTargets.params = { targetsParam, modeParam };
```

**命令 2：`configure_channels`**

```cpp
CommandMeta configureChannels;
configureChannels.name = "configure_channels";
configureChannels.description = "批量配置数据采集通道";

FieldMeta channelsParam;
channelsParam.name = "channels";
channelsParam.type = FieldType::Array;
channelsParam.required = true;
channelsParam.description = "通道配置列表（1–8 条）";
channelsParam.constraints.minItems = 1;
channelsParam.constraints.maxItems = 8;

auto channelItem = std::make_shared<FieldMeta>();
channelItem->name = "channel";
channelItem->type = FieldType::Object;

FieldMeta chIdField;
chIdField.name = "id";
chIdField.type = FieldType::Int;
chIdField.required = true;
chIdField.constraints.min = 0;
chIdField.constraints.max = 7;
chIdField.description = "通道 ID（0-7）";

FieldMeta chLabelField;
chLabelField.name = "label";
chLabelField.type = FieldType::String;
chLabelField.required = true;
chLabelField.description = "通道名称";

FieldMeta chEnabledField;
chEnabledField.name = "enabled";
chEnabledField.type = FieldType::Bool;
chEnabledField.required = false;
chEnabledField.defaultValue = QJsonValue(true);

FieldMeta chSampleField;
chSampleField.name = "sample_hz";
chSampleField.type = FieldType::Int;
chSampleField.required = false;
chSampleField.constraints.min = 1;
chSampleField.constraints.max = 1000;
chSampleField.description = "采样频率（Hz）";
chSampleField.defaultValue = QJsonValue(100);

channelItem->fields = { chIdField, chLabelField, chEnabledField, chSampleField };
channelsParam.items = channelItem;

configureChannels.params = { channelsParam };
```

**子字段类型覆盖矩阵**（确保 DriverLab 各基础控件在 `array<object>` 上下文中均被激活）：

| 命令 | 字段 | 类型 | 激活控件 |
|------|------|------|---------|
| `scan_targets` | host | string | StringField |
| `scan_targets` | port | int + min/max | NumberField |
| `scan_targets` | timeout_ms | int（可选）| NumberField |
| `scan_targets` | mode | enum | EnumField |
| `configure_channels` | id | int + min/max | NumberField |
| `configure_channels` | label | string | StringField |
| `configure_channels` | enabled | bool | BoolField |
| `configure_channels` | sample_hz | int（可选）| NumberField |

### 3.2 命令实现（模拟行为）

**`handleScanTargets`**：

```cpp
void MultiscanDriver::handleScanTargets(const QJsonValue& data, IResponder& resp) {
    const QJsonObject obj = data.toObject();
    const QJsonArray targets = obj.value("targets").toArray();
    const QString mode = obj.value("mode").toString("quick");

    if (targets.isEmpty()) {
        resp.error(400, QJsonObject{{"message", "targets must not be empty"}});
        return;
    }

    QJsonArray results;
    for (const auto& t : targets) {
        const QJsonObject target = t.toObject();
        const QString host = target.value("host").toString();
        const int port = target.value("port").toInt();
        const int timeout = target.value("timeout_ms").toInt(2000);

        // 模拟扫描：无实际阻塞等待，latency 仅为数值模拟。
        // 所有 resp.event() 和 resp.done() 在同一调用栈内同步发出，
        // DriverLab 端会看到消息批量到达而非逐条流式到达。
        // 如需演示流式效果，需改为 QTimer/事件循环异步化。
        const int latency = 100;  // 固定模拟延迟数值（ms），非阻塞
        const bool reachable = (latency < timeout / 4);

        QJsonObject result;
        result["host"] = host;
        result["port"] = port;
        result["status"] = reachable ? "reachable" : "timeout";
        result["latency_ms"] = latency;
        results.append(result);

        // 发送中间事件，便于 DriverLab 消息流展示
        resp.event(0, QJsonObject{
            {"target", host}, {"port", port}, {"status", result["status"]}
        });
    }

    resp.done(0, QJsonObject{
        {"mode", mode},
        {"results", results},
        {"total", static_cast<int>(targets.size())}
    });
}
```

**`handleConfigureChannels`**：

```cpp
void MultiscanDriver::handleConfigureChannels(const QJsonValue& data, IResponder& resp) {
    const QJsonArray channels = data.toObject().value("channels").toArray();

    if (channels.isEmpty()) {
        resp.error(400, QJsonObject{{"message", "channels must not be empty"}});
        return;
    }

    int configured = 0;
    for (const auto& c : channels) {
        const QJsonObject ch = c.toObject();
        if (!ch.contains("id") || !ch.contains("label")) continue;
        ++configured;
    }

    resp.done(0, QJsonObject{
        {"configured", configured},
        {"skipped", static_cast<int>(channels.size()) - configured}
    });
}
```

### 3.3 文件布局

```
src/demo/multiscan_driver/
├── CMakeLists.txt
├── main.cpp
├── multiscan_driver.h
└── multiscan_driver.cpp
```

编译目标：`stdio.drv.multiscan`，依赖 `stdiolink`，与现有 demo driver 结构完全一致。

---

## 4. 实现步骤

### 4.1 新增 Driver 源文件

**`main.cpp`**：

```cpp
#include "multiscan_driver.h"
#include <stdiolink/driver/driver_core.h>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    stdiolink::DriverCore core;
    MultiscanDriver driver;
    core.setMetaHandler(&driver);
    return core.run(argc, argv);
}
```

**`multiscan_driver.h`**：

```cpp
#pragma once
#include <stdiolink/driver/meta_command_handler.h>
using namespace stdiolink;

class MultiscanDriver : public IMetaCommandHandler {
public:
    MultiscanDriver();
    const meta::DriverMeta& driverMeta() const override;
    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override;

private:
    void handleScanTargets(const QJsonValue& data, IResponder& resp);
    void handleConfigureChannels(const QJsonValue& data, IResponder& resp);

    meta::DriverMeta m_meta;
};
```

**`CMakeLists.txt`**（参照 `device_simulator_driver` 现有结构）：

```cmake
add_executable(multiscan_driver main.cpp multiscan_driver.cpp)
target_link_libraries(multiscan_driver PRIVATE stdiolink)
set_target_properties(multiscan_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.multiscan"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS multiscan_driver)
```

**改动理由**：提供真实的 `array<object>` 参数声明，触发 DriverLab 对 `ArrayField` 的渲染路径，是 M90 修复后端到端验收的必要载体。

**验收方式**：`stdio.drv.multiscan --help` 正常输出；Console 模式带参数执行返回 ok。

---

### 4.2 运行时自动组装

CMakeLists.txt 中已通过 `set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS multiscan_driver)` 注册。构建后 `assemble_runtime` 目标会自动将编译产物（`stdio.drv.multiscan`）复制到 `runtime_*/drivers/` 目录，无需手动修改 `run_demo.sh`。

**验收方式**：构建完成后，`runtime_debug/drivers/` 下出现 `stdio.drv.multiscan`；启动 demo server 后 `GET /api/drivers` 返回列表含 `stdio.drv.multiscan`；DriverLab 页面 Driver 选择列表中出现该 Driver。

---

### 4.3 注册 CMake 编译目标

在顶层 `src/demo/CMakeLists.txt` 中追加：

```cmake
add_subdirectory(multiscan_driver)
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/demo/multiscan_driver/CMakeLists.txt`
- `src/demo/multiscan_driver/main.cpp`
- `src/demo/multiscan_driver/multiscan_driver.h`
- `src/demo/multiscan_driver/multiscan_driver.cpp`
- `src/tests/test_multiscan_driver.cpp`

### 5.2 修改文件

| 文件 | 改动内容 |
|------|---------|
| `src/demo/CMakeLists.txt` | 追加 `add_subdirectory(multiscan_driver)` |

### 5.3 不变文件（明确声明）

以下文件**不在本里程碑改动范围内**：

| 文件 | 说明 |
|------|------|
| `src/webui/src/components/DriverLab/ParamForm.tsx` | 已通过 FieldRenderer 复用 ArrayField，M90 修复后无需改动 |
| `src/webui/src/components/SchemaForm/fields/ArrayField.tsx` | M90 已完成深度初始化修复 |
| 任何其他 `.tsx` / `.ts` 文件 | 本里程碑不引入任何 WebUI 组件改动 |

---

## 6. 测试与验收

### 6.1 C++ 单元测试

- **测试对象**：`MultiscanDriver::handleScanTargets`、`MultiscanDriver::handleConfigureChannels`
- **用例分层**：正常路径、边界（空数组）、鲁棒性（含未知子字段）
- **断言要点**：响应类型（ok / error）、结果结构（数组长度、字段存在性）、错误码
- **桩替身**：`MockResponder`（与现有 driver 测试框架一致，无需新增 mock 基础设施）
- **测试文件**：`src/tests/test_multiscan_driver.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `scan_targets`：targets 非空，合法输入 | 返回 ok，results 长度 == targets 长度 | `M92_CPP_01` |
| `scan_targets`：targets 为空数组 | 返回 error 400 | `M92_CPP_02` |
| `scan_targets`：目标含未知字段 | 忽略未知字段，正常处理 | `M92_CPP_03` |
| `configure_channels`：channels 合法 | 返回 ok，configured 字段正确 | `M92_CPP_04` |
| `configure_channels`：channels 缺少必填子字段（id/label）| 跳过该项，configured 不计入 | `M92_CPP_05` |
| `driverMeta()`：`scan_targets` 命令的 `targets` 参数元数据 | `items->fields.size() == 3`，子字段名/类型正确 | `M92_CPP_06` |

- 覆盖要求：6 条路径全部覆盖

#### 用例详情

**M92_CPP_01 — scan_targets 合法输入返回结果**
- 前置条件：构造 `MockResponder`
- 输入：`targets=[{host:"127.0.0.1",port:2368},{host:"127.0.0.2",port:2369}]`，`mode:"quick"`
- 预期：`isDone() == true`；`result["results"].toArray().size() == 2`；第一项含 `host`、`port`、`status`、`latency_ms`
- 断言：
  ```cpp
  EXPECT_TRUE(responder.isDone());
  EXPECT_EQ(responder.result()["results"].toArray().size(), 2);
  const auto first = responder.result()["results"].toArray()[0].toObject();
  EXPECT_EQ(first["host"].toString(), QString("127.0.0.1"));
  EXPECT_TRUE(first.contains("status"));
  EXPECT_TRUE(first.contains("latency_ms"));
  ```

**M92_CPP_02 — scan_targets targets 为空数组**
- 输入：`{"targets": []}`
- 预期：`isError() == true`；`errorCode() == 400`
- 断言：`EXPECT_EQ(responder.errorCode(), 400)`

**M92_CPP_03 — scan_targets 目标含未知字段**
- 输入：`{"targets": [{"host":"127.0.0.1","port":2368,"unknown":"x"}]}`
- 预期：未知字段被忽略，正常返回 ok
- 断言：`EXPECT_TRUE(responder.isDone())`

**M92_CPP_04 — configure_channels 合法输入**
- 输入：`{"channels": [{"id":0,"label":"ch0","enabled":true,"sample_hz":100},{"id":1,"label":"ch1"}]}`
- 预期：`isDone() == true`；`result["configured"].toInt() == 2`
- 断言：`EXPECT_EQ(responder.result()["configured"].toInt(), 2)`

**M92_CPP_05 — configure_channels 缺少必填子字段**
- 输入：`{"channels": [{"id":0,"label":"ch0"},{"only_sample_hz":100}]}`（第二项缺 id 和 label）
- 预期：第二项被跳过，`configured == 1`，`skipped == 1`
- 断言：
  ```cpp
  EXPECT_EQ(responder.result()["configured"].toInt(), 1);
  EXPECT_EQ(responder.result()["skipped"].toInt(), 1);
  ```

**M92_CPP_06 — driverMeta() 元数据声明正确性**
- 输入：调用 `driver.driverMeta()`
- 预期：`scan_targets` 命令的 `targets` 参数 `items->fields.size() == 3`，子字段分别为 host(String)、port(Int)、timeout_ms(Int)
- 断言：
  ```cpp
  const auto& meta = driver.driverMeta();
  const auto* scanCmd = meta.findCommand("scan_targets");
  ASSERT_NE(scanCmd, nullptr);
  const auto& targetsParam = scanCmd->params[0];
  ASSERT_NE(targetsParam.items, nullptr);
  EXPECT_EQ(targetsParam.items->fields.size(), 3);
  EXPECT_EQ(targetsParam.items->fields[0].name, QString("host"));
  EXPECT_EQ(targetsParam.items->fields[0].type, FieldType::String);
  ```

#### 测试代码框架

```cpp
// src/tests/test_multiscan_driver.cpp
#include <gtest/gtest.h>
#include <stdiolink/driver/mock_responder.h>
#include "multiscan_driver.h"

using stdiolink::MockResponder;

class MultiscanDriverTest : public ::testing::Test {
protected:
    MultiscanDriver driver;
    MockResponder responder;
};

TEST_F(MultiscanDriverTest, ScanTargets_ValidInput_ReturnsResults) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "targets": [
            {"host": "127.0.0.1", "port": 2368},
            {"host": "127.0.0.2", "port": 2369}
        ],
        "mode": "quick"
    })").object();
    driver.handle("scan_targets", data, responder);

    // 2 个 event + 1 个 done = 3 条响应
    ASSERT_EQ(responder.responses.size(), 3u);
    EXPECT_EQ(responder.responses.back().status, QString("done"));

    const auto& done = responder.responses.back();
    const auto results = done.payload.toObject()["results"].toArray();
    EXPECT_EQ(results.size(), 2);
    const auto first = results[0].toObject();
    EXPECT_EQ(first["host"].toString(), QString("127.0.0.1"));
    EXPECT_TRUE(first.contains("status"));
    EXPECT_TRUE(first.contains("latency_ms"));
}

TEST_F(MultiscanDriverTest, ScanTargets_EmptyTargets_Returns400) {
    const QJsonValue data = QJsonDocument::fromJson(R"({"targets":[]})").object();
    driver.handle("scan_targets", data, responder);
    ASSERT_EQ(responder.responses.size(), 1u);
    EXPECT_EQ(responder.responses.back().status, QString("error"));
    EXPECT_EQ(responder.responses.back().code, 400);
}

TEST_F(MultiscanDriverTest, ScanTargets_UnknownSubfield_Ignored) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "targets": [{"host":"127.0.0.1","port":2368,"unknown":"x"}]
    })").object();
    driver.handle("scan_targets", data, responder);
    // 1 个 event + 1 个 done = 2 条响应
    ASSERT_EQ(responder.responses.size(), 2u);
    EXPECT_EQ(responder.responses.back().status, QString("done"));
}

TEST_F(MultiscanDriverTest, ConfigureChannels_ValidInput_ReturnsConfigured) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "channels": [
            {"id":0,"label":"ch0","enabled":true,"sample_hz":100},
            {"id":1,"label":"ch1"}
        ]
    })").object();
    driver.handle("configure_channels", data, responder);
    ASSERT_EQ(responder.responses.size(), 1u);
    EXPECT_EQ(responder.responses.back().status, QString("done"));
    EXPECT_EQ(responder.responses.back().payload.toObject()["configured"].toInt(), 2);
}

TEST_F(MultiscanDriverTest, ConfigureChannels_MissingRequiredSubfields_Skipped) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "channels": [{"id":0,"label":"ch0"}, {"only_sample_hz":100}]
    })").object();
    driver.handle("configure_channels", data, responder);
    ASSERT_EQ(responder.responses.size(), 1u);
    const auto& done = responder.responses.back();
    EXPECT_EQ(done.status, QString("done"));
    EXPECT_EQ(done.payload.toObject()["configured"].toInt(), 1);
    EXPECT_EQ(done.payload.toObject()["skipped"].toInt(), 1);
}

TEST_F(MultiscanDriverTest, DriverMeta_ScanTargets_ItemsFieldsCorrect) {
    const auto& meta = driver.driverMeta();
    const auto* scanCmd = meta.findCommand("scan_targets");
    ASSERT_NE(scanCmd, nullptr);
    ASSERT_FALSE(scanCmd->params.isEmpty());
    const auto& targetsParam = scanCmd->params[0];
    EXPECT_EQ(targetsParam.name, QString("targets"));
    ASSERT_NE(targetsParam.items, nullptr);
    EXPECT_EQ(targetsParam.items->fields.size(), 3);
    EXPECT_EQ(targetsParam.items->fields[0].name, QString("host"));
    EXPECT_EQ(targetsParam.items->fields[0].type, FieldType::String);
}
```

### 6.2 CLI 冒烟验证

```bash
# Console 模式快速验证
./stdio.drv.multiscan --help

./stdio.drv.multiscan --cmd=scan_targets \
  --targets='[{"host":"127.0.0.1","port":2368},{"host":"127.0.0.2","port":2369}]' \
  --mode=quick
# 期望：输出 ok JSON，results 数组含两项，每项含 host/port/status/latency_ms

./stdio.drv.multiscan --cmd=configure_channels \
  --channels='[{"id":0,"label":"ch0","enabled":true}]'
# 期望：输出 ok JSON，configured=1，skipped=0
```

### 6.3 DriverLab 端到端手工验收

M90 修复后，`ParamForm → FieldRenderer → ArrayField` 调用链对 `array<object>` 已畅通。本验收项确认两个里程碑的修复在真实 Driver 场景下协同工作：

1. 构建项目（`assemble_runtime` 自动将 `stdio.drv.multiscan` 放入 `runtime_*/drivers/`），启动 demo server
2. 打开 WebUI → DriverLab 页面，Driver 选择列表中出现 `stdio.drv.multiscan`
3. 连接（oneshot 模式），等待收到 `meta` 消息
4. 选择 `scan_targets` 命令：
   - **期望**：`targets` 参数渲染为**内联列表控件**（Add/Remove + 子字段表单），不是 JSON textarea
   - **期望**：点击 Add Item，新增一行，host 输入框默认值为 `""`，port 输入框默认值为 `0`（int 深度初始化，M90 `R_FE_01` 验收点）
5. 填写 `host=127.0.0.1`、`port=2368`，点击执行
6. 消息流面板收到：`driver.started`、`meta`、若干 event（每个 target 一条）、`stdout(ok)`
7. 切换到 `configure_channels` 命令：
   - **期望**：`channels` 参数同样渲染为内联列表，子字段含 id（int）、label（string）、enabled（bool switch）、sample_hz（int）

### 6.4 验收标准

- [ ] `stdio.drv.multiscan --help` 正常输出并退出 0
- [ ] `M92_CPP_01`–`M92_CPP_06` 全部通过
- [ ] `GET /api/drivers` 返回列表含 `stdio.drv.multiscan`（demo server 启动后）
- [ ] DriverLab 中 `scan_targets` 参数 `targets` 以内联列表渲染（不降级 JSON textarea）
- [ ] 点击 Add Item 后，int 子字段 `port` 默认值为 `0`（验证 M90 深度初始化生效）
- [ ] 执行 `scan_targets` 后消息流面板可见 event 和 ok 消息
- [ ] 现有测试套件（stdiolink_tests + webui vitest）无回归
- [ ] **无新增 WebUI React 组件**（对照不变文件列表确认）

**测试执行入口**：

```powershell
# 全量回归（GTest + Vitest + Playwright）
tools/run_tests.ps1

# 仅 C++ 单元测试（含 test_multiscan_driver M92_CPP_01–M92_CPP_06）
tools/run_tests.ps1 --gtest
```

---

## 7. 风险与控制

- 风险：`stdio.drv.multiscan` 的模拟延迟影响测试确定性
  - 控制：使用固定延迟（`const int latency = 100`）替代随机数，确保测试结果完全确定
  - 测试覆盖：所有 6 条用例均不依赖随机分支结果

- 风险：`ParamForm → FieldRenderer → ArrayField` 在 DriverLab 暗色主题下样式异常
  - 控制：样式问题通过 DriverLab 顶层容器 CSS override 或 Ant Design `<ConfigProvider theme={...}>` 局部处理，不在本里程碑内解决；若手工验收发现问题，作为独立 UI 调整提单，不阻塞合入

- 风险：`assemble_runtime` 组装时产物尚未编译
  - 控制：`assemble_runtime` 已通过 `add_dependencies` 依赖所有 `STDIOLINK_EXECUTABLE_TARGETS`，确保编译完成后再组装

---

## 8. 里程碑完成定义（DoD）

- [ ] `stdio.drv.multiscan` 编译通过，4 个源文件入库
- [ ] `test_multiscan_driver.cpp` 的 `M92_CPP_01`–`M92_CPP_06` 全部通过
- [ ] 构建后 `assemble_runtime` 自动将 `stdio.drv.multiscan` 放入 `runtime_*/drivers/`
- [ ] `stdio.drv.multiscan --help` 正常输出并退出 0
- [ ] `GET /api/drivers` 返回列表含 `stdio.drv.multiscan`（demo server 启动后）
- [ ] DriverLab 手工验收：`scan_targets` 参数以内联列表渲染，int 子字段默认值为 `0`，执行后消息流可见
- [ ] 现有测试套件（stdiolink_tests + webui vitest）无回归（`tools/run_tests.ps1`）
- [ ] **无新增 WebUI React 组件**（对照不变文件列表逐项确认）
- [ ] 本里程碑文档入库 `doc/milestone/`
