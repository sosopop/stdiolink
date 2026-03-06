# 里程碑 98：JSON/CLI 参数规范化核心链路

> **前置条件**: M12（console/stdio 双模式）、M20（系统参数统一注册）、M10（meta 自动校验）
> **后续里程碑**: M99（示例统一）、M100（手册与迁移）
> **目标**: 在不破坏 `stdio` JSONL 协议的前提下，完成 console 模式 JSON/CLI 参数规范化的核心解析链路，彻底解决字符串/枚举误判、复杂路径表达不稳定、容器冲突未定义等运行时问题。

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `stdiolink/console` | JSON/CLI 路径解析、值解析、冲突检测、唯一规范 C++ renderer（`renderArgs()`） |
| `stdiolink/driver` | console 模式改为“保留原始参数 -> 按 meta 定向转换 -> 默认值填充 -> 校验” |
| 测试与验证 | stub driver、单元测试、冒烟测试、兼容与回归矩阵 |

- 新增正式 CLI 路径语法，支持 `.`、`[N]`、`[]`、`["..."]`
- 明确定义 `Canonical` 与 `Friendly` 值语义，并要求默认 parse 可无损消费默认 render 输出
- console 模式改为“先保留原始参数，再按 meta 定向转换”，不在拿到 meta 前做最终类型推断
- `String`/`Enum` 字段不再把 `123456`、`true`、`[1,2]` 等原始输入误判为其他类型
- `Int64` 明确遵循 `QJsonValue` / JS safe integer 边界，不承诺任意 64 位整数
- 明确“容器字面量 + 子路径”混用时的冲突规则，避免运行时隐式合并
- 通过最小 stub driver 与 smoke 覆盖核心成功链路和关键失败链路
- M98 不负责 help/doc/WebUI 示例统一，也不负责用户手册更新；但 `JsonCliCodec::renderArgs()` 作为唯一规范 C++ renderer 在 M98 定义，M99 只能复用它，不得再发明第二套 C++ 规则

## 2. 背景与问题

- 当前 `ConsoleArgs::parse()` 在没有命令 meta 的阶段就调用 `inferType()`，导致 `--password=123456` 被提前解析为 number，后续遇到 `FieldType::String` 时报 `expected string`
- 当前 `FieldType::Enum` 在校验器中要求 string，若 console 入口仍用启发式推断，则 `--mode_code=1` 仍会落成 number，问题与 string 字段本质相同
- 当前复杂 JSON 参数主要依赖 `--units=[{"id":1}]` 这类整段内联 JSON；在 `PowerShell 5.1`、`cmd` 等 shell 中，内部双引号极易失真
- 当前 `setNestedValue()` 仅支持 `a.b`，不支持数组下标、数组追加、特殊键名，也没有定义容器字面量与子路径混用的冲突行为
- 当前 `Int64` 最终仍由 `QJsonValue` 承载，实际能力边界由 safe integer 决定；若文档不写死这条边界，后续实现和验收会产生错误预期
- 当前仓库已经有 help/doc/WebUI 各自的示例渲染实现，但 M98 的主目标不是统一这些输出，而是先把 runtime 主链路做对；示例统一顺延到 M99

**范围**:
- `src/stdiolink/console/`：新增 `JsonCliCodec`，扩展路径与值语法
- `src/stdiolink/driver/driver_core.*`：调整 console 模式参数构造顺序
- `src/stdiolink/console/console_args.*`：降级为“保留原始参数”
- `src/tests/`：新增 codec / DriverCore console stub 测试，扩展现有 console 测试
- `src/smoke_tests/`：新增 M98 冒烟脚本并接入统一入口

**非目标**:
- 不修改 `stdio` JSONL 协议格式
- 不在 M98 中直接修改 help/doc/WebUI 的 CLI 示例输出
- 不更新用户手册、排障文档与迁移说明
- 不支持 `--flag` / `--no-flag` / 无前缀参数
- 不为所有 shell 生成一条“可直接复制且完全等价”的命令行文本
- 不引入运行期 `legacy` 解析回退开关

## 3. 技术方案

### 3.1 JSON/CLI codec 公共模型

```cpp
// src/stdiolink/console/json_cli_codec.h
enum class CliValueMode {
    Canonical,
    Friendly
};

struct RawCliArg {
    QString path;
    QString rawValue;
};

struct CliParseOptions {
    CliValueMode mode = CliValueMode::Friendly;
};

struct CliRenderOptions {
    CliValueMode mode = CliValueMode::Canonical;
    bool useEquals = true;
};

class JsonCliCodec {
public:
    static bool parseArgs(const QList<RawCliArg>& args,
                          const CliParseOptions& opts,
                          QJsonObject& out,
                          QString* error);

    static QStringList renderArgs(const QJsonObject& data,
                                  const CliRenderOptions& opts);
};
```

约束：
- 首版 API 仅覆盖本里程碑实际需要的能力，不暴露 `allowBoolFlag`、`allowNoPrefix` 之类已延期分支
- 对外标准输出物是 argv token 数组，而不是单条 shell 字符串
- 默认 `renderArgs()` 输出 `Canonical`，默认 `parseArgs()` 使用 `Friendly`
- 默认 `Friendly` 必须先识别完整 JSON string/object/array literal，再回退到 `bool/null/number/raw string`
- `JsonCliCodec::renderArgs()` 是 M98 产出的唯一规范 C++ renderer；M99 的 `HelpGenerator` / `DocGenerator` 必须直接复用它，而不是再抽一套新的 C++ 渲染规则

### 3.2 路径语法与冲突规则

支持的路径：

```text
db.host
units[0].id
tags[]
labels["app.kubernetes.io/name"]
```

路径 AST：

```cpp
struct CliPathSegment {
    enum class Kind {
        Key,
        Index,
        Append
    };
    Kind kind = Kind::Key;
    QString key;
    int index = -1;
};
```

结构冲突策略：

| 场景 | 行为 | 错误语义 |
|------|------|----------|
| `--a=1 --a.b=2` | 失败 | `path conflict: scalar vs object` |
| `--tags[]=x --tags[0]=y` | 失败 | `path conflict: append vs explicit index` |
| `--a={"b":1} --a.c=2` | 失败 | `path conflict: container literal vs child path` |
| `--units=[{"id":1}] --units[0].size=2` | 失败 | `path conflict: container literal vs child path` |
| `--users[].name=alice` | 失败 | `append path must be terminal` |
| `--a[foo]=1` | 失败 | `invalid array index` |
| `--a[1` | 失败 | `invalid path syntax` |
| 重复标量路径 | 最后一次覆盖 | 无错误 |
| 多次 `[]` 追加 | 追加顺序保留 | 无错误 |

### 3.3 meta 感知转换与字段查找

关键流程：

```text
argv
  -> ConsoleArgs.parse()
     -> 框架参数: mode/profile/cmd/help/version/export*
     -> 数据参数: RawCliArg(path, rawValue)
  -> DriverCore.runConsoleMode()
     -> findCommand(args.cmd)
     -> buildConsoleData(rawArgs, cmdMeta)
     -> DefaultFiller::fillDefaults()
     -> MetaValidator::validateParams()
     -> m_handler->handle()
```

关键接口：

```cpp
static bool buildConsoleData(const QList<RawCliArg>& rawArgs,
                             const CommandMeta* cmdMeta,
                             QJsonObject& out,
                             QString* error);

static const FieldMeta* resolveFieldMetaByPath(const CommandMeta* cmdMeta,
                                               const CliPath& path,
                                               QString* error);

static bool resolveFieldValue(const QString& raw,
                              const FieldMeta* fieldMeta,
                              QJsonValue& out,
                              QString* error);
```

字段元数据查找规则：
- 首段必须是顶层参数名，从 `cmdMeta->params` 中按名称定位
- 命中 `Object` 后，后续 `Key` 段从 `field.fields` 中继续查找
- 命中 `Array` 后，`Index`/`Append` 段只负责消费数组层级，后续解析进入 `field.items`
- `units[0].id` 这类路径必须按 `Array.items -> Object.fields` 逐段下钻
- `resolveFieldValue()` 只处理“已定位到的叶子字段”，路径遍历与值解析严格分离

字段类型解析规则：

| meta 类型 | 解析规则 |
|-----------|----------|
| `String` | 优先按字符串处理；若输入是合法 JSON string 字面量，则解码成其字符串值 |
| `Enum` | 与 `String` 相同，之后再由 `MetaValidator` 校验枚举集合 |
| `Bool` | 接受 `true/false`；其余失败 |
| `Int` | 按 32 位整数解析；非法失败 |
| `Int64` | 按整数解析，但值域限定在 `[-2^53, 2^53]`；超界直接失败 |
| `Double` | 按浮点数解析；非法失败 |
| `Object/Array` | 要求原始值是合法 JSON object/array，或通过路径聚合得到 |
| `Any/未知字段` | 回退到 `Friendly` 规则 |

关键伪代码：

```cpp
bool resolveFieldValue(const QString& raw,
                       const FieldMeta* fieldMeta,
                       QJsonValue& out,
                       QString* error) {
    if (fieldMeta == nullptr) {
        out = inferType(raw);
        return true;
    }

    switch (fieldMeta->type) {
    case FieldType::String:
    case FieldType::Enum:
        out = decodeStringOrKeepRaw(raw);
        return true;
    case FieldType::Bool:
        return parseBool(raw, out, error);
    case FieldType::Int:
        return parseInt(raw, out, error);
    case FieldType::Int64:
        return parseSafeInt64(raw, out, error);
    case FieldType::Double:
        return parseDouble(raw, out, error);
    case FieldType::Object:
        return parseExpectedJsonContainer(raw, QJsonValue::Object, out, error);
    case FieldType::Array:
        return parseExpectedJsonContainer(raw, QJsonValue::Array, out, error);
    case FieldType::Any:
        out = inferType(raw);
        return true;
    }
    return false;
}
```

### 3.4 JSON -> CLI roundtrip 契约

默认渲染策略：
- 根对象递归展开
- 对象 key 按字典序稳定输出，数组按索引顺序输出
- 标量值输出为 JSON 字面量
- 对象数组优先使用显式下标展开，避免 `[]` 追加在自动生成场景下不可稳定定位
- 特殊键名自动输出为 `[...]` 形式

Roundtrip 契约：
- `renderArgs()` 默认输出 `Canonical`
- `parseArgs()` 默认仍是 `Friendly`
- 但默认 `Friendly` 必须可无损消费默认 `Canonical` 输出
- 若实现无法满足这一点，`T14/S05` 不得宣称通过
- M99 允许复用 `renderArgs()` 作为 help/doc 的规范 C++ 渲染入口，但不得改写其语义

### 3.5 向后兼容与边界

- `stdio` 模式不变：仍由请求 JSON 自身决定类型，不做额外修正
- 简单 console 参数如 `--fps=10`、`--roi.x=10`、`--enable=true` 继续有效
- `--arg-mode=frame` 这类规避框架参数冲突的写法继续保留
- 新增路径语法后，过去未定义但偶尔成功的冲突写法将转为显式失败
- 不新增 `--strict-path`、`--legacy-console-parse` 或环境变量回退开关，避免长期维护两套语义

## 4. 实现步骤

实施顺序：
- `P0`（本里程碑全部内容）: 4.1-4.5，必须先完成 runtime 主链路、stub 和 smoke
- help/doc/WebUI 示例统一与手册更新不属于 M98，实现上不得并行掺入本里程碑代码改动

### 4.1 新增 `JsonCliCodec`

涉及文件：
- `src/stdiolink/console/json_cli_codec.h`
- `src/stdiolink/console/json_cli_codec.cpp`

关键代码片段：

```cpp
bool JsonCliCodec::parseArgs(const QList<RawCliArg>& args,
                             const CliParseOptions& opts,
                             QJsonObject& out,
                             QString* error) {
    for (const auto& arg : args) {
        CliPath path;
        if (!parsePath(arg.path, path, error)) {
            return false;
        }
        QJsonValue value;
        if (!parseFriendlyValue(arg.rawValue, opts.mode, value, error)) {
            return false;
        }
        if (!writePath(out, path, value, error)) {
            return false;
        }
    }
    return true;
}
```

改动理由：先把纯逻辑的路径/值/冲突规则固化为可单测模块，避免把路径语义散落到 `DriverCore`。
验收方式：T01-T08、T14、T16、T18。

### 4.2 改造 `ConsoleArgs` 为原始参数收集

涉及文件：
- `src/stdiolink/console/console_args.h`
- `src/stdiolink/console/console_args.cpp`

关键代码片段：

```cpp
if (isFrameworkArg(key)) {
    applyFrameworkArg(key, value);
} else {
    rawDataArgs.push_back(RawCliArg{key, value});
}
```

改动理由：问题根因是过早 `inferType()`，必须先把 console 参数层降级成“仅收 token”。
验收方式：T12、T15。

### 4.3 在 `DriverCore` 接入 meta 感知转换

涉及文件：
- `src/stdiolink/driver/driver_core.h`
- `src/stdiolink/driver/driver_core.cpp`

关键代码片段：

```cpp
QJsonObject typedData;
QString buildError;
if (!buildConsoleData(args.rawDataArgs, cmdMeta, typedData, &buildError)) {
    responder.error(400, QJsonObject{{"name", "CliParseFailed"}, {"message", buildError}});
    return;
}

QJsonObject filledData = meta::DefaultFiller::fillDefaults(typedData, *cmdMeta);
auto result = meta::MetaValidator::validateParams(filledData, *cmdMeta);
```

改动理由：console 和 stdio 的语义分界必须落在 `DriverCore`，而不是让校验器承担纠错职责。
验收方式：T09-T13、T17-T18。

### 4.4 新增最小 stub driver 与测试接入

涉及文件：
- `src/tests/test_json_cli_codec.cpp`
- `src/tests/test_console.cpp`
- `src/tests/test_driver_core_console_stub.cpp`

推荐的最小 stub meta：

```cpp
CommandBuilder("echo")
    .param(FieldBuilder("password", FieldType::String).required())
    .param(FieldBuilder("mode_code", FieldType::Enum)
               .enumValues({"1", "2"})
               .required())
    .param(FieldBuilder("count64", FieldType::Int64))
    .param(FieldBuilder("units", FieldType::Array)
               .items(FieldBuilder("", FieldType::Object)
                          .field(FieldBuilder("id", FieldType::Int).required())
                          .field(FieldBuilder("size", FieldType::Int64))))
    .param(FieldBuilder("labels", FieldType::Object)
               .field(FieldBuilder("app.kubernetes.io/name", FieldType::String)))
    .returns(FieldType::Object);
```

改动理由：避免依赖真实网络服务或第三方驱动，把所有失败路径都收敛成可本地复现的输入。
验收方式：全部单元测试与 S01-S05。

### 4.5 新增冒烟测试脚本并接入统一入口

涉及文件：
- `src/smoke_tests/m98_json_cli_normalization.py`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`

关键代码片段：

```python
def run_case(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=10,
    )
```

改动理由：M98 覆盖 console 参数入口和跨进程调用，必须有命令级 smoke 兜底。
验收方式：S01-S05。

## 5. 文件变更清单

### 5.1 新增文件
- `src/stdiolink/console/json_cli_codec.h`
- `src/stdiolink/console/json_cli_codec.cpp`
- `src/tests/test_json_cli_codec.cpp`
- `src/tests/test_driver_core_console_stub.cpp`
- `src/smoke_tests/m98_json_cli_normalization.py`

### 5.2 修改文件
- `src/stdiolink/console/console_args.h`
- `src/stdiolink/console/console_args.cpp`
- `src/stdiolink/driver/driver_core.h`
- `src/stdiolink/driver/driver_core.cpp`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`

### 5.3 测试文件
- `src/tests/test_console.cpp`
- `src/tests/test_json_cli_codec.cpp`
- `src/tests/test_driver_core_console_stub.cpp`

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `JsonCliCodec`、`ConsoleArgs`、`DriverCore::runConsoleMode()`
- 用例分层: 正常路径、边界值、异常输入、错误传播、兼容回归
- 桩替身策略:
  - 使用本地 stub driver 提供 `String/Enum/Int64/Array<Object>/Object` 参数 meta
  - 不依赖真实网络服务、不依赖外部端口抢占、不依赖公网
- 测试文件: `src/tests/test_json_cli_codec.cpp`、`src/tests/test_console.cpp`、`src/tests/test_driver_core_console_stub.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `parsePath()`：普通对象段 | `a.b` 正常拆分 | T01 |
| `parsePath()`：显式数组下标 | `units[0].id` 正常拆分 | T02 |
| `parsePath()`：数组追加 | `tags[]` 正常识别追加段 | T03 |
| `parsePath()`：特殊键名 | `labels["app.kubernetes.io/name"]` 正常识别 | T04 |
| `parsePath()`：语法非法 | 缺失 `]` / 非法 index | T05 |
| `writePath()`：重复标量 | 后写覆盖前写 | T06 |
| `writePath()`：结构冲突 | 标量/对象冲突时报错 | T07 |
| `writePath()`：追加与显式下标冲突 | 明确失败 | T08 |
| `resolveFieldValue()`：meta=string/enum | `123456`、`1` 等保持 string 语义 | T09 |
| `resolveFieldMetaByPath()`：array.items.object.fields | `units[0].id` 正确命中叶子字段元数据 | T10 |
| `resolveFieldValue()`：unknown field | Friendly fallback | T11 |
| `runConsoleMode()`：兼容旧路径 | `roi.x/roi.y` 不回归 | T12 |
| `runConsoleMode()`：stdio 不变 | number 传到 string 字段仍报错 | T13 |
| `renderArgs()+parseArgs()`：默认模式兼容 | `Canonical` 默认输出可被默认 parse 无损吃回 | T14 |
| `--arg-` 冲突规避 | `--arg-mode` 不影响框架 `--mode` | T15 |
| `append must be terminal` | `users[].name` 明确失败 | T16 |
| `resolveFieldValue()`：meta=Int64 | 超出 safe integer 直接失败 | T17 |
| `writePath()/buildConsoleData()`：容器字面量与子路径混用 | `--a={...} --a.b=...` / `--units=[...] --units[0].x=...` 明确失败 | T18 |

覆盖要求（硬性）: 上表已列出本里程碑所有可达核心决策路径；未列分支仅包括已明确延期的 `--flag/--no-flag` 与 `no-prefix` 解析分支，本里程碑实现中明确不存在。

#### 用例详情

**T01 — 普通对象路径解析**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--db.host=localhost`
- 预期: 生成 `{"db":{"host":"localhost"}}`
- 断言: `obj["db"].toObject()["host"].toString() == "localhost"`

**T02 — 显式数组下标路径解析**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--units[0].id=1`
- 预期: 生成 `{"units":[{"id":1}]}`
- 断言: `obj["units"].toArray()[0].toObject()["id"].toInt() == 1`

**T03 — 数组追加顺序保持**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--tags[]=api --tags[]=beta`
- 预期: `tags` 顺序为 `["api","beta"]`
- 断言: `arr[0] == "api" && arr[1] == "beta"`

**T04 — 特殊键名路径解析**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--labels["app.kubernetes.io/name"]=demo`
- 预期: `labels` 下生成带点号的真实键名
- 断言: `obj["labels"].toObject()["app.kubernetes.io/name"].toString() == "demo"`

**T05 — 非法路径语法报错**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--units[abc]=1` 或 `--units[1=1`
- 预期: parse 失败
- 断言: `parseArgs()` 返回 `false`，且 `error.startsWith("invalid path syntax:")` 或 `error.startsWith("invalid array index:")`

**T06 — 重复标量路径后写覆盖**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--name=alice --name=bob`
- 预期: 最终 `name == "bob"`
- 断言: `obj["name"].toString() == "bob"`

**T07 — 标量与对象冲突失败**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--a=1 --a.b=2`
- 预期: 构造失败
- 断言: `parseArgs()` 返回 `false`，且 `error.startsWith("path conflict: scalar vs object")`

**T08 — 追加与显式下标冲突失败**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--tags[]=x --tags[0]=y`
- 预期: 构造失败
- 断言: `parseArgs()` 返回 `false`，且 `error.startsWith("path conflict: append vs explicit index")`

**T09 — meta=string/enum 时数字形态输入保持字符串**
- 前置条件: stub driver 暴露 `password: string` 与 `mode_code: enum{"1","2"}`
- 输入: `--cmd=echo --password=123456 --mode_code=1`
- 预期: `password` 与 `mode_code` 都以字符串语义进入 handler，`mode_code` 通过枚举校验
- 断言: 返回 payload 中 `password` 与 `mode_code` 都是 string，且不出现 `ValidationFailed`

**T10 — array.items.object.fields 路径可命中叶子字段并构造对象数组**
- 前置条件: stub driver 暴露 `units: array<object{id:int,size:int64}>`
- 输入: `--cmd=echo --units[0].id=1 --units[0].size=10000`
- 预期: `units` 为对象数组，叶子字段按各自 meta 解析
- 断言: `units[0].id == 1` 且 `units[0].size == 10000`

**T11 — unknown field 继续 Friendly fallback**
- 前置条件: 命令 meta 中不存在 `x`
- 输入: `--cmd=echo --x=18`
- 预期: `x` 作为 number 进入 data
- 断言: `data["x"].isDouble() == true`

**T12 — 旧的 `a.b` 写法不回归**
- 前置条件: 使用现有 `ConsoleArgs` 路径场景
- 输入: `--cmd=scan --roi.x=10 --roi.y=20`
- 预期: 仍生成 `{"roi":{"x":10,"y":20}}`
- 断言: `roi.x == 10 && roi.y == 20`

**T13 — stdio 严格语义保持不变**
- 前置条件: stub driver 暴露 `password: string`
- 输入: stdio JSON 请求 `{"cmd":"echo","data":{"password":123456}}`
- 预期: 仍报类型错误
- 断言: 错误名为 `ValidationFailed`，消息包含 `password: expected string`

**T14 — JSON->CLI->JSON roundtrip**
- 前置条件: 构造包含对象、数组、特殊键名的 JSON
- 输入: 默认 `renderArgs()` 后再用默认 `parseArgs()` 回读
- 预期: 结果与原对象一致，证明默认 `Friendly` 可无损消费默认 `Canonical` 输出
- 断言: 两个 `QJsonDocument::toJson(QJsonDocument::Compact)` 相等

**T15 — `--arg-` 前缀冲突规避不回归**
- 前置条件: 现有框架参数 `--mode` 保持有效
- 输入: `--mode=console --cmd=echo --arg-mode=frame`
- 预期: 框架 mode 为 `console`，data.mode 为 `"frame"`
- 断言: `args.mode == "console"` 且 `data["mode"] == "frame"`

**T16 — `[]` 非末尾路径失败**
- 前置条件: 创建空 `QJsonObject`
- 输入: `--users[].name=alice`
- 预期: parse 失败
- 断言: `parseArgs()` 返回 `false`，且 `error.startsWith("append path must be terminal:")`

**T17 — Int64 超出 safe integer 边界时显式失败**
- 前置条件: stub driver 暴露 `count64: int64`
- 输入: `--cmd=echo --count64=9007199254740993`
- 预期: console 解析阶段直接失败，不把超界值交给 handler
- 断言: 错误名为 `CliParseFailed`，消息包含 `integer out of safe range`

**T18 — 容器字面量与子路径混用显式失败**
- 前置条件: stub driver 暴露 `a: object`、`units: array<object>`
- 输入: `--cmd=echo --a={\"b\":1} --a.c=2` 或 `--cmd=echo --units=[{\"id\":1}] --units[0].size=2`
- 预期: 不允许把“整段 JSON 字面量”与“子路径写入”隐式合并
- 断言: 错误名为 `CliParseFailed`，消息包含 `path conflict: container literal vs child path`

#### 测试代码

```cpp
TEST(DriverCoreConsoleStub, T09_StringAndEnumMetaKeepNumericLikeStrings) {
    const auto resp = runStubDriver({
        "--mode=console",
        "--cmd=echo",
        "--password=123456",
        "--mode_code=1"
    });

    ASSERT_EQ(resp.status, "done");
    ASSERT_TRUE(resp.data.value("password").isString());
    ASSERT_TRUE(resp.data.value("mode_code").isString());
    EXPECT_EQ(resp.data.value("password").toString(), "123456");
    EXPECT_EQ(resp.data.value("mode_code").toString(), "1");
}

TEST(DriverCoreConsoleStub, T17_Int64OutOfSafeRangeFails) {
    const auto resp = runStubDriver({"--mode=console", "--cmd=echo", "--count64=9007199254740993"});
    ASSERT_EQ(resp.status, "error");
    EXPECT_EQ(resp.data["name"].toString(), "CliParseFailed");
    EXPECT_TRUE(resp.data["message"].toString().contains("integer out of safe range"));
}
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m98_json_cli_normalization.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m98_json_cli_normalization`
- CTest 接入: `smoke_m98_json_cli_normalization`
- 覆盖范围: console 主成功链路、结构冲突失败链路、roundtrip 主链路
- 用例清单:
  - `S01`: `meta=string/enum` 且输入 `123456`、`1` -> 返回 string，不能报 `ValidationFailed`
  - `S02`: `units[0].id/units[0].size` -> 返回对象数组
  - `S03`: 特殊键名路径 -> 返回正确键名
  - `S04`: `a=1` 与 `a.b=2`、`--units=[...]` 与 `--units[0].x=...` 冲突 -> 返回失败且错误文本稳定
  - `S05`: 默认 `renderArgs()` 结果再执行 -> 输出与原 JSON 一致
- 失败输出规范: 输出 stdout、stderr、退出码、超时信息
- 环境约束与跳过策略: 不依赖外部服务、原则上不允许 `skip`
- 产物定位契约:
  - 优先使用 `build/runtime_debug/data_root/drivers/stdio.drv.m98_stub/stdio.drv.m98_stub.exe` 或 `build/runtime_release/data_root/drivers/stdio.drv.m98_stub/stdio.drv.m98_stub.exe`
  - 原始构建目录仅作为兼容兜底：`build/debug/stdio.drv.m98_stub.exe`、`build/release/stdio.drv.m98_stub.exe`
  - 可执行文件不存在时必须判定 `FAIL`，并打印全部已尝试候选路径，禁止静默通过
- 跨平台运行契约: 使用 `subprocess` 直接传 argv 数组，不拼 shell 字符串

### 6.3 集成/端到端测试

- console 参数 -> DriverCore -> handler 参数接收 的跨模块联动
- JSON->CLI 渲染结果被同一 stub driver 消费并还原为原 JSON
- `stdio` 与 `console` 两条链路对同一 meta 的差异化行为验证

### 6.4 验收标准

- [ ] `--password=123456`、`--mode_code=1` 等在 `meta=string/enum` 的 console 命令中不再被误判（T09, S01）
- [ ] 对象数组参数可通过 `units[0].id=1` 等正式路径稳定表达（T02, T10, S02）
- [ ] 特殊键名可通过 `[...]` 路径正确映射（T04, S03）
- [ ] 结构冲突写法会显式失败，不再静默退化（T07, T08, T16, T18, S04）
- [ ] JSON->CLI->JSON roundtrip 在规范支持范围内成立（T14, S05）
- [ ] 旧的简单 console 参数写法不回归（T12, T15）
- [ ] `stdio` 严格类型语义保持不变（T13）
- [ ] `Int64` 在 safe integer 边界内外的行为与现有 `MetaValidator` 一致（T17）

## 7. 风险与控制

- 风险: `ConsoleArgs` 与 codec 之间职责分割不清，产生双重解析
  - 控制: `ConsoleArgs` 仅做原始 token 收集，不再决定最终值类型
  - 控制: 所有路径和值解析统一下沉到 `JsonCliCodec`
  - 测试覆盖: T01-T16

- 风险: 结构冲突规则不清会导致历史“偶然成功”的命令在升级后失败
  - 控制: 在 CI 中新增兼容性回归测试，覆盖旧 `a.b` 路径和冲突路径
  - 控制: 对 `CliParseFailed` 输出明确迁移提示
  - 控制: 发布前灰度验证若发现历史脚本大面积失败，则阻断发布并回退本里程碑提交集；不提供运行期 `legacy` 解析开关
  - 测试覆盖: T07, T08, T16, T18, S04

- 风险: `Int64` 能力边界与 `MetaValidator` 不一致
  - 控制: 在 M98 文档和实现中统一写死 safe integer 边界
  - 控制: `parseSafeInt64()` 与 `MetaValidator::checkType(FieldType::Int64)` 采用同一错误语义
  - 测试覆盖: T17

- 风险: 为修复 console 宽松转换而误伤 `stdio` 严格语义
  - 控制: `stdio` 路径不改代码流程
  - 控制: 专门增加 stdio 回归测试
  - 测试覆盖: T13

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] 冒烟测试脚本已新增并接入统一入口（`run_smoke.py`）与 CTest
- [ ] 冒烟测试在目标环境执行通过（或有明确 skip 记录）
- [ ] 向后兼容/迁移策略确认
- [ ] M99/M100 的前置依赖边界已写清，不在 M98 中夹带示例统一和手册更新改动
