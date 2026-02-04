下面是一份“下一阶段：Driver 自描述 / 自文档 / UI 自动生成”的开发需求与详细设计（面向你现有的 **StdioLink** 代码结构：`driver/ host/ protocol/ console/`，仍保持 **stdio + JSONL** 通讯风格）。

---

## 1. 阶段目标

让每个 driver **先用标准模板声明“我支持哪些命令/参数/类型/默认值/约束/示例/配置项”**，框架自动导出为 **manifest**（自描述清单）：

* Host 启动 driver 后，可自动拉取 manifest 并注册到 Host 的命令中心
* Host/UI 可根据 manifest：

  * 自动生成“配置界面”（driver 的可配置项）
  * 自动生成“命令调用界面”（表单字段 = 命令参数模板）
  * 自动生成“调用请求”（把 UI 输入按类型/默认值/约束打包成 data）
* driver 开发流程变成：**先写模板描述 → 再实现命令代码**（可强制校验模板与实现一致）

---

## 2. 范围与非目标

### 2.1 本阶段交付范围

1. **Manifest 数据结构**（命令、参数、配置、能力、版本、示例等）
2. Driver 侧：**元信息声明（meta 模板） + 导出 + 内置 meta 命令响应**
3. Host 侧：**拉取 manifest、缓存、注册、查询接口**
4. **参数校验/默认值填充**：Host/UI 生成请求时、Driver 收到请求时都能用同一套规则校验（至少一侧必须有）
5. 最小可用示例：提供一个 demo driver 定义 2~3 个命令 + 3~5 个配置项

### 2.2 明确非目标（避免膨胀）

* 不做完整 GUI（你可以后续接 Qt/QML/ImGui/网页都行），本阶段只输出“UI 生成所需的数据模型”
* 不做 TCP/localSocket（与你前面一致，仍是 stdio）
* 不做复杂权限体系（先留字段，后续扩展）

---

## 3. 关键概念与术语

* **Manifest**：driver 的“自描述清单”，包含 commands/config/capabilities/version/schemaVersion 等
* **CommandSpec**：单条命令的模板描述（名称、描述、参数、返回、示例）
* **ArgSpec / FieldSpec**：参数/配置字段描述（类型、默认值、范围、枚举、是否必填、UI 提示等）
* **SchemaVersion**：manifest 结构版本（用于 Host/Driver 协商兼容）
* **Capability**：driver 能力特性开关，如支持 KeepAlive / streaming / progress / cancel 等

---

## 4. 协议设计（JSONL + 系统保留命令）

### 4.1 保留命令命名

约定所有元命令使用 `meta.*` 前缀（避免与业务命令冲突）：

* `meta.describe`：获取完整 manifest（最重要）
* `meta.ping`：健康检查（可选）
* `meta.getConfig`：读取当前 config（可选）
* `meta.setConfig`：写入 config（可选，后续可加）

> 你当前协议是 `cmd + data`，非常适合直接加这些命令，不用改 framing。

### 4.2 meta.describe 响应格式

`meta.describe` 的 `finalPayload()` 返回：

```json
{
  "schemaVersion": 1,
  "driver": {
    "id": "com.xxx.mydriver",
    "name": "My Driver",
    "version": "1.2.0",
    "protocol": "stdiolink-jsonl",
    "minHostSchemaVersion": 1
  },
  "capabilities": {
    "keepAlive": true,
    "streamingMessages": true,
    "cancel": false
  },
  "commands": [
    {
      "name": "scan.run",
      "title": "执行扫描",
      "description": "启动一次扫描任务并持续输出进度",
      "profile": "oneshot|keepalive|both",
      "args": [
        {"name":"path","type":"string","required":true,"ui":{"widget":"file"},"description":"输入路径"},
        {"name":"maxDepth","type":"int","default":3,"min":0,"max":99},
        {"name":"mode","type":"enum","enum":["fast","full"],"default":"fast"}
      ],
      "returns": {"type":"object","description":"最终结果"},
      "examples": [
        {"title":"快速扫描","data":{"path":"D:/data","maxDepth":2,"mode":"fast"}}
      ]
    }
  ],
  "config": {
    "title": "运行配置",
    "fields": [
      {"name":"threads","type":"int","default":4,"min":1,"max":64,"ui":{"group":"性能"}},
      {"name":"logLevel","type":"enum","enum":["trace","debug","info","warn","error"],"default":"info","ui":{"group":"日志"}}
    ]
  }
}
```

---

## 5. Driver 侧设计：meta 模板声明 + 自动导出 + 自动注册

### 5.1 设计目标

* 业务 driver 通过“声明式模板”注册命令与配置字段
* 框架将声明汇总为 `Manifest`
* `DriverCore` 内置处理 `meta.describe`（业务 handler 无需重复写）
* 可选：如果业务也想自定义 meta 输出，允许覆盖

### 5.2 建议的 C++ API（Qt 风格）

在 `stdiolink/driver` 增加一个轻量 registry：

* `Manifest`, `CommandSpec`, `FieldSpec`, `TypeSpec`
* `DriverRegistry`：存放命令模板与 config 模板
* `ManifestBuilder`：链式构建（对你这种写驱动的人体验最好）

#### 5.2.1 典型用法（driver 作者视角）

```cpp
using namespace stdiolink;

static DriverRegistry gReg = ManifestBuilder()
  .driver("com.xxx.mydriver", "My Driver", "1.2.0")
  .capKeepAlive(true)
  .command("scan.run")
    .title("执行扫描")
    .desc("启动一次扫描任务并持续输出进度")
    .arg("path", Type::String).required().uiFile()
    .arg("maxDepth", Type::Int).def(3).range(0, 99)
    .arg("mode", Type::Enum).enumOf({"fast","full"}).def("fast")
    .example("快速扫描", QJsonObject{{"path","D:/data"},{"maxDepth",2},{"mode","fast"}})
  .end()
  .config()
    .field("threads", Type::Int).def(4).range(1,64).group("性能")
    .field("logLevel", Type::Enum).enumOf({"trace","debug","info","warn","error"}).def("info").group("日志")
  .endConfig()
  .build();
```

然后业务 handler 只关心命令实现：

```cpp
class MyHandler : public ICommandHandler {
public:
  void handle(const QString& cmd, const QJsonValue& data, IResponder& r) override {
    if (cmd == "scan.run") { ... }
    else r.error(...);
  }
};
```

### 5.3 DriverCore 与 meta 命令的关系（框架内置）

在 `DriverCore::processOneLine()` 中：

1. parse 出 `cmd` 和 `data`
2. 如果 `cmd` 以 `meta.` 开头：

   * `meta.describe`：直接用 registry 导出 manifest JSON（无需业务 handler）
   * `meta.getConfig` / `meta.setConfig`：如果 registry 开启 config 存储则处理；否则报“不支持”
3. 否则交给业务 `ICommandHandler`

> 这样 driver 100% 自动具备自描述能力，业务不会忘。

---

## 6. Host 侧设计：启动即拉取 manifest、注册、对外查询、辅助构造请求

### 6.1 Host Driver 生命周期增强

你现有 `host::Driver::start()` 成功后，增加一个可选流程：

* `Driver::loadManifest()`：发送一次 `meta.describe` 请求
* 缓存到 `Driver` 内部：`std::shared_ptr<DriverManifest> m_manifest;`
* 提供接口：

  * `const DriverManifest* manifest() const;`
  * `QJsonObject buildRequestData(cmd, uiInputs)`（可选：默认值填充/类型转换/校验）

### 6.2 “注册到 host 中”的含义（建议抽象）

Host 增加一个 `CommandRegistry`（可能在 host 模块里）：

* key = `{driverId}:{commandName}` 或者 `{processInstance}:{commandName}`
* value = `CommandSpec + driver handle`
* UI 层/上层业务可通过 registry 获取：

  * 命令列表、分组、描述、参数模板
  * 自动生成表单
  * 点击“执行” → 框架自动打包 `data` → 调 `Driver::request(cmd, data)`

---

## 7. 参数类型系统（最小闭环）

### 7.1 类型集合（先做够用的）

建议先支持：

* `bool`
* `int`（32 位）
* `double`
* `string`
* `enum`（本质 string + enum list）
* `object`（允许嵌套字段：可后续扩展）
* `array<T>`（后续扩展）

### 7.2 校验规则（Manifest 里表达）

* required / optional
* default
* min/max（数值）
* minLen/maxLen（字符串）
* enum list
* regex（可选）
* ui hint（widget、group、order、placeholder、help）

### 7.3 校验发生位置（建议策略）

为了稳健，建议 **Host 与 Driver 都能校验**，但本阶段可以先做到：

* **Driver 必须校验**（安全边界在被调用方）
* Host 也做“友好校验”（尽早提示 UI 用户）

---

## 8. 配置模板（用于“自动生成配置界面”）

### 8.1 ConfigSpec

* `title`
* `fields[]`：FieldSpec（同参数结构）
* 每个 field 可以带：

  * `group`：用于 UI 分组
  * `order`：用于 UI 排序
  * `sensitive`：是否敏感（UI 可隐藏/掩码）
  * `restartRequired`：修改后是否提示重启 driver

### 8.2 配置读写（最小实现建议）

给 driver 增加一个简单 config 存储（内存）：

* `meta.getConfig` → 返回当前值（未设置则返回默认值合成结果）
* `meta.setConfig` → 校验后写入（并返回 diff / 生效说明）

> 后续你可以把它落盘到 `xxx.json`，或接你自己的项目配置系统。

---

## 9. 兼容性与版本协商

Manifest 顶层必须有：

* `schemaVersion`：manifest 结构版本（从 1 开始）
* `driver.version`：driver 自身版本
* `driver.minHostSchemaVersion`：要求 Host 至少支持的 schemaVersion

Host 行为：

* 若 `schemaVersion` 高于 Host 支持范围：提示“不兼容”，但仍允许基本命令调用（可选）
* 若 Host 不理解某些字段：忽略即可（向后兼容）

---

## 10. 开发里程碑拆分（下一阶段可执行计划）

### M1：Manifest 数据结构与 JSON 序列化

* `protocol/` 或新增 `meta/` 子模块
* 定义 `Manifest / CommandSpec / FieldSpec`
* 实现 `toJson()`（Qt `QJsonObject/QJsonArray`）

验收：

* 单元测试：构建 manifest → JSON 输出稳定、字段齐全

### M2：Driver 侧 registry + meta.describe

* `driver/driver_registry.*`
* `DriverCore` 内置处理 `meta.describe`
* Demo driver：声明 2~3 命令模板

验收：

* Host 用 `request("meta.describe")` 能拿到完整 manifest

### M3：Host 侧 manifest 拉取 + 缓存 + registry

* `host/driver` 增加 `loadManifest()`
* `host/command_registry`（可选）
* 提供查询 API：列出命令/参数模板

验收：

* Host 能打印 driver 命令列表与参数说明（console 演示即可）

### M4：参数校验与默认值填充（最小闭环）

* Driver 收到业务命令前：用 manifest 做校验（至少 required/default/enum/min/max）
* 返回结构化错误（错误码 + 哪个字段错）

验收：

* 给缺失必填字段/非法枚举值，driver 返回明确错误信息

### M5：ConfigSpec + meta.getConfig/meta.setConfig（可选但强烈建议）

* 能让 UI 自动生成配置表单的字段结构完整
* setConfig 具备校验

验收：

* host 可写入 config，再读取确认生效

---

## 11. 对现有 StdioLink 代码的最小侵入改动点

结合你 zip 里的结构（`driver_core`, `ICommandHandler`, `host::Driver`, `Task`）：

1. `driver/` 新增：

   * `driver_manifest.h/.cpp`
   * `driver_registry.h/.cpp`
   * （可选）`validator.h/.cpp`

2. 修改 `DriverCore::processOneLine()`：

   * 在调用 `m_handler->handle()` 之前拦截 `meta.*`

3. `host/Driver` 新增：

   * `std::shared_ptr<DriverManifest> m_manifest;`
   * `bool loadManifest(int timeoutMs=...)`

4. `protocol/` 不一定要改 framing（你现成 JSONL 就够），最多新增一些错误码/字段约定

---

## 12. 验收标准（写得更“可测”）

* ✅ driver 不写任何额外业务代码，也能响应 `meta.describe`
* ✅ manifest 中至少包含：driver id/name/version、commands（含 args）、config（含 fields）
* ✅ host 能拉取 manifest 并缓存，能枚举命令与参数信息
* ✅ 对于缺失必填参数 / 类型不匹配 / enum 非法，driver 返回结构化错误并指出字段名
* ✅ demo：基于 manifest 自动构造一个合法请求 data（含默认值填充）并成功调用业务命令

---

如果你愿意把“模板声明方式”再推进一步（更像你说的 *meta 声明导出*），下一步我建议做成两种声明风格并存：

1. **链式 Builder（最快落地）**
2. **宏 + 静态注册（更像反射）**：例如 `STDIOLINK_COMMAND(scan_run, ...)` 自动注册到 registry

你回复我一句你更偏好哪种（Builder / 宏 / 两者都要），我就按你的偏好把 **API 头文件草案 + JSON 字段规范 + 错误码** 直接写到“可落地”的级别（基本等于你下一步就能开工）。
