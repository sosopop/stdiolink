# 里程碑 99：CLI 示例统一与多入口渲染收敛

> **前置条件**: M98（JSON/CLI 参数规范化核心链路完成）
> **后续里程碑**: M100（手册与迁移说明）
> **目标**: 在 M98 核心解析链路稳定后，收敛 help/doc/WebUI 中现有三套 JSON->CLI 示例渲染逻辑，确保 `meta.examples`、CLI 帮助、导出文档、DriverLab 命令行示例遵循同一套规范。

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `stdiolink/driver` | `example_auto_fill`、`help_generator` 示例统一 |
| `stdiolink/doc` | 导出文档中的 CLI 示例统一 |
| `webui/driverlab` | DriverLab 命令行示例与示例选择逻辑统一 |
| 测试与验证 | 现有 help/doc/UI 测试补 delta 用例，新增示例一致性 smoke |

- 统一 `meta.examples` 的 console/stdio mode 语义，不在 auto-fill 阶段提前拼命令行字符串
- `HelpGenerator`、`DocGenerator`、DriverLab 复用同一套 CLI 规则，不再各自拼接复杂对象/数组参数
- 前端保留独立纯函数渲染，不新增后端“示例渲染 API”
- 通过共享 fixture `src/tests/data/cli_render_cases.json` 保证 C++ 与 TypeScript 渲染结果一致
- M99 不修改 runtime 解析主链路，不重新定义 M98 的路径/值语义
- M99 不更新用户手册和迁移说明，这部分顺延到 M100

## 2. 背景与问题

- 当前仓库已存在三套 JSON->CLI 渲染逻辑：`HelpGenerator`、`DocGenerator`、DriverLab `CommandLineExample.tsx`
- `example_auto_fill`、help、doc、WebUI 已有测试，但它们目前只各自验证局部格式，没有验证“所有入口输出相同规则”
- 若 M98 只修 runtime，不收敛这些示例链路，用户将继续从帮助、导出文档或 DriverLab 复制到旧格式
- 示例展示与真实执行链路不同：DriverLab 实际执行走 JSON 协议，命令行示例只是展示/复制辅助，因此 M99 的目标是“规则一致”，不是“把前端执行链路改成命令行”

**范围**:
- `src/stdiolink/driver/example_auto_fill.*`
- `src/stdiolink/driver/help_generator.*`
- `src/stdiolink/doc/doc_generator.*`
- `src/webui/src/components/driverlab/CommandLineExample.tsx`
- `src/webui/src/components/driverlab/CommandExamples.tsx`
- `src/webui/src/components/driverlab/exampleMeta.ts`
- `src/webui/src/components/driverlab/CommandPanel.tsx`
- 对应 C++ / WebUI 测试与 M99 smoke 脚本

**非目标**:
- 不修改 `JsonCliCodec`、`ConsoleArgs`、`DriverCore` 的运行时行为
- 不变更 DriverLab 的实际执行协议
- 不更新用户手册、最佳实践、故障排除和发布说明
- 不新增后端“渲染 CLI 示例”API

## 3. 技术方案

### 3.1 统一链路与规则来源

统一链路：

```text
meta / params
  -> M98 规范化 CLI 规则
  -> example_auto_fill 保留结构化 params + mode
  -> HelpGenerator CLI 示例
  -> DocGenerator CLI 示例
  -> DriverLab 命令行示例
```

规则来源：
- C++ 侧唯一规范入口是 `JsonCliCodec::renderArgs()`；`HelpGenerator` 与 `DocGenerator` 必须直接复用它
- 前端侧：`renderCliArgs()` / `buildArgsLine()` 独立实现，但必须对齐 M98 路径语法、稳定排序、字符串/对象/数组输出规则
- 一致性靠共享 fixture `src/tests/data/cli_render_cases.json` 维护，不引入运行期 RPC 查询后端渲染结果

### 3.2 C++ 侧统一策略

关键接口：

```cpp
// src/stdiolink/driver/help_generator.h
static QString formatExampleCli(const meta::CommandMeta& cmd, const QJsonObject& ex);

// src/stdiolink/driver/example_auto_fill.h
void ensureCommandExamples(DriverMeta& meta, bool addConsoleExamples = true);
```

约束：
- `example_auto_fill` 负责补齐结构化 `params` 和 `mode`，不在这里拼命令行字符串
- `HelpGenerator::formatExampleCli()` 内部必须直接调用 `JsonCliCodec::renderArgs()`；`DocGenerator` 必须复用同一 helper，不允许新增 `renderNormalizedCliArgs()` 之类第二套 C++ 渲染函数
- `JsonCliCodec::renderArgs()` 只负责渲染普通 data 参数；`mode` 与 `profile` 仍由 help/doc 包装层处理
- `mode=console` 与 `profile=oneshot` 的默认省略行为必须保持与现有 `HelpGenerator` 一致；非默认 `mode/profile` 仍由包装层显式输出
- `stdio` 示例继续保留 `echo '<jsonl>' | <program> --mode=stdio` 这一展示语义

### 3.3 前端同步策略

关键接口：

```ts
export function renderCliArgs(params: Record<string, unknown>): string[];

export function buildArgsLine(
  command: string | null,
  params: Record<string, unknown>,
): string;
```

约束：
- 前端不得继续使用 `JSON.stringify(value)` + 手工引号拼接作为复杂对象/数组的默认展示策略
- `CommandExamples` / `exampleMeta` / `CommandPanel` 必须保留 `mode` 语义，不再只取“第一个合法示例”
- DriverLab UX 契约固定为：命令行预览永远反映当前表单 `commandParams`；示例只通过 `Apply` 写回表单后才影响预览
- `CommandPanel` 不得在进入命令时自动把 console 示例灌进表单，也不得把“示例预览”静默替换成“当前参数预览”

### 3.4 向后兼容与边界

- M99 只改变示例文本，不改变命令真实执行语义
- `meta.examples` 里现有 `stdio` 示例仍保留，不会被 console 示例覆盖掉
- 旧示例文本若依赖整段 JSON 引号写法，在 M99 中会被规范路径形式替换
- 用户手册和迁移说明不在本里程碑处理，避免边做边改文档

## 4. 实现步骤

### 4.1 统一 `example_auto_fill`

涉及文件：
- `src/stdiolink/driver/example_auto_fill.cpp`

关键代码片段：

```cpp
void ensureCommandExamples(DriverMeta& meta, bool addConsoleExamples) {
    // 保留现有 stdio 示例
    // 仅补齐缺失的 console 示例结构化 params
    // 不在此处拼 CLI 字符串
}
```

改动理由：先把示例元数据本身收敛成统一源头，再让 help/doc/UI 消费。
验收方式：T01。

### 4.2 统一 `HelpGenerator` 与 `DocGenerator`

涉及文件：
- `src/stdiolink/driver/help_generator.cpp`
- `src/stdiolink/doc/doc_generator.cpp`

关键代码片段：

```cpp
QString HelpGenerator::formatExampleCli(const meta::CommandMeta& cmd, const QJsonObject& ex) {
    const QString mode = ex.value("mode").toString();
    const QJsonObject params = ex.value("params").toObject();
    QJsonObject dataParams = params;
    dataParams.remove("mode");
    dataParams.remove("profile");
    const QStringList argv = JsonCliCodec::renderArgs(dataParams, CliRenderOptions{});
    QStringList parts;
    parts << "<program>" << ("--cmd=" + cmd.name);
    if (!mode.isEmpty() && mode != "console") {
        parts << ("--mode=" + mode);
    }
    const QString profile = params.value("profile").toString();
    if (!profile.isEmpty() && profile != "oneshot") {
        parts << ("--profile=" + profile);
    }
    parts.append(argv);
    return parts.join(' ');
}
```

改动理由：帮助输出和导出文档是用户最常复制的文本入口，必须直接复用 M98 renderer，避免重复开发。
验收方式：T02-T04、S01-S02。

### 4.3 统一 DriverLab 示例渲染与示例选择

涉及文件：
- `src/webui/src/components/driverlab/CommandLineExample.tsx`
- `src/webui/src/components/driverlab/CommandExamples.tsx`
- `src/webui/src/components/driverlab/exampleMeta.ts`
- `src/webui/src/components/driverlab/CommandPanel.tsx`

关键代码片段：

```ts
export function buildArgsLine(command: string | null, params: Record<string, unknown>): string {
  if (!command) return '';
  return ['--cmd=' + command, ...renderCliArgs(params)].join(' ');
}
```

改动理由：DriverLab 是用户直接可见的入口，若继续保留旧写法，M98 的 runtime 改动就没有真实落地价值。
验收方式：T05-T07。

### 4.4 新增 smoke 脚本并接入统一入口

涉及文件：
- `src/smoke_tests/m99_cli_example_unification.py`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`

改动理由：需要在命令级验证帮助输出和导出文档的 CLI 示例片段已经统一；前端一致性继续由单元测试承担。
验收方式：S01-S03。

## 5. 文件变更清单

### 5.1 新增文件
- `src/smoke_tests/m99_cli_example_unification.py`
- `src/tests/data/cli_render_cases.json`

### 5.2 修改文件
- `src/stdiolink/driver/example_auto_fill.cpp`
- `src/stdiolink/driver/help_generator.cpp`
- `src/stdiolink/doc/doc_generator.cpp`
- `src/webui/src/components/driverlab/CommandLineExample.tsx`
- `src/webui/src/components/driverlab/CommandExamples.tsx`
- `src/webui/src/components/driverlab/exampleMeta.ts`
- `src/webui/src/components/driverlab/CommandPanel.tsx`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`

### 5.3 测试文件
- `src/tests/test_example_auto_fill.cpp`
- `src/tests/test_system_help.cpp`
- `src/tests/test_doc_generator.cpp`
- `src/webui/src/components/driverlab/__tests__/CommandLineExample.test.tsx`
- `src/webui/src/components/driverlab/__tests__/CommandExamples.test.tsx`
- `src/webui/src/components/driverlab/__tests__/exampleMeta.test.ts`
- `src/webui/src/components/driverlab/__tests__/CommandPanel.test.tsx`

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `example_auto_fill`、`HelpGenerator`、`DocGenerator`、DriverLab 示例组件
- 测试策略: 优先扩展现有 help/doc/UI 测试文件，不新增重复测试套件
- 共享 fixture: C++ 测试与 Vitest 必须共同消费 `src/tests/data/cli_render_cases.json`，禁止各自维护独立的 case 常量

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `example_auto_fill` | 保留 stdio 示例并补齐 console 示例 | T01 |
| `HelpGenerator` | console 示例遵循 M98 规则 | T02 |
| `DocGenerator` | Markdown/HTML/TS 示例片段一致 | T03 |
| `HelpGenerator` 与 `DocGenerator` | 同一命令示例输出相同 CLI 片段 | T04 |
| `CommandLineExample` | 前端示例遵循 M98 规则 | T05 |
| `exampleMeta` / `CommandExamples` | 保留 console/stdio mode 语义 | T06 |
| `CommandPanel` | 预览永远跟随当前表单值，示例仅通过 Apply 生效 | T07 |
| fixture 共享 | C++ 与 Vitest 消费同一 `cli_render_cases.json` | T08 |

#### 用例详情

**T01 — auto-fill 生成的示例保留 mode 语义**
- 前置条件: 构造只含 stdio 示例或空示例的命令 meta
- 输入: 调用 `ensureCommandExamples(meta, true)`
- 预期: stdio/console 示例均存在且 `mode` 字段不丢失
- 断言: `examples.size() == 2` 且能分别按 `mode` 找到 stdio/console 示例

**T02 — 命令帮助中的 CLI 示例遵循统一规则**
- 前置条件: 命令示例包含 object/array/string 参数
- 输入: 调用 `HelpGenerator::generateCommandHelp(cmd)`
- 预期: 输出中的 CLI 示例与 M98 渲染规则一致
- 断言: 输出包含 `--password="123456"`、`--units[0].id=1`，且不包含旧式 `--units="{"`

**T03 — 文档导出的 CLI 示例统一**
- 前置条件: meta 含 console 与 stdio 示例
- 输入: 调用 `DocGenerator::toMarkdown()` / `toHtml()` / `toTypeScript()`
- 预期: 三类导出中的 CLI 示例规则一致
- 断言: 三种导出都包含相同的规范 CLI 片段

**T04 — 帮助输出与导出文档使用同一 CLI 片段**
- 前置条件: 同一命令 meta
- 输入: `generateCommandHelp()` 与 `DocGenerator::toMarkdown()`
- 预期: 同一示例的 console CLI 片段完全一致
- 断言: 提取后的 CLI 行文本相等

**T05 — DriverLab 命令行示例组件输出规范 CLI**
- 前置条件: React 测试环境可渲染 `CommandLineExample`
- 输入: object/array/string 参数组合
- 预期: 渲染出的文本符合 M98 CLI 规则
- 断言: `cmdline-text` 精确等于规范路径形式，不包含旧式 JSON 整体引号格式

**T06 — 示例列表保留 mode 语义**
- 前置条件: command `examples` 同时包含 `stdio` 与 `console`
- 输入: `normalizeCommandExamples()`、`CommandExamples` 渲染
- 预期: 不再只取“第一个合法示例”并丢失模式差异
- 断言: 返回结果同时包含 `mode == "console"` 与 `mode == "stdio"`

**T07 — CommandPanel 预览始终跟随当前表单值**
- 前置条件: 同时存在 console 与 stdio 示例，且当前表单值与示例不同
- 输入: 通过带 state 的 wrapper 渲染 `CommandPanel`，点击 `Apply`
- 预期: 命令行示例区显示当前 `commandParams`，只有点击 `Apply` 后才切换到示例值
- 断言: 初始 `cmdline-text` 反映当前表单值；点击 `Apply` 后才变为示例片段

**T08 — C++ 与前端消费同一 fixture**
- 前置条件: 新增 `src/tests/data/cli_render_cases.json`
- 输入: C++ 测试与 Vitest 都加载同一 fixture
- 预期: 两边对同一 case 生成相同 CLI 片段
- 断言: 共享 case 的期望输出在 C++ 与 TypeScript 测试中均通过

#### 测试代码

```cpp
TEST(HelpGeneratorCommandHelp, T02_UsesNormalizedCliExampleExactText) {
    const QString output = HelpGenerator::generateCommandHelp(cmd);
    EXPECT_TRUE(output.contains("--password=\"123456\""));
    EXPECT_TRUE(output.contains("--units[0].id=1"));
    EXPECT_FALSE(output.contains("--units=\"{"));
}
```

```ts
it('T07 preview follows current form values until Apply', () => {
  function PanelHarness() {
    const [params, setParams] = React.useState({ password: 'manual' });
    return (
      <CommandPanel
        commands={[{ ...cmd, examples }]}
        selectedCommand="run"
        commandParams={params}
        executing={false}
        connected={true}
        driverId="drv"
        onSelectCommand={() => {}}
        onParamsChange={setParams}
        onExec={() => {}}
        onCancel={() => {}}
      />
    );
  }

  render(<PanelHarness />);
  expect(screen.getByTestId('cmdline-text').textContent).toContain('--password="manual"');
  fireEvent.click(screen.getByTestId('apply-example-0'));
  expect(screen.getByTestId('cmdline-text').textContent).toContain('--password="123456"');
});
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m99_cli_example_unification.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m99_cli_example_unification`
- CTest 接入: `smoke_m99_cli_example_unification`
- 覆盖范围: help 输出、文档导出、现有驱动元数据示例的统一性
- 用例清单:
  - `S01`: `--cmd=... --help` 中的 console 示例使用规范路径格式
  - `S02`: 文档导出中的 CLI 示例使用与 help 相同的规范片段
  - `S03`: 对同一命令，从 help 与 doc 抽取出的 console CLI 片段完全一致
- 失败输出规范: 输出 stdout、stderr、退出码、关键片段 diff
- 环境约束与跳过策略: 不依赖外部服务、原则上不允许 `skip`
- 产物定位契约:
  - 优先使用 `build/runtime_debug/data_root/drivers/<target_driver>/<target_driver>.exe` 或 `build/runtime_release/...`
  - 可执行文件不存在时必须判定 `FAIL` 并输出候选路径
- 跨平台运行契约: 使用 `subprocess` 直接传 argv 数组，不拼 shell 字符串
- 说明: DriverLab 一致性由前端单元测试覆盖，不纳入 Python smoke

### 6.3 集成/端到端测试

- 帮助输出与导出文档共享同一渲染规则
- DriverLab 示例元数据、示例列表、命令行示例区的联动验证

### 6.4 验收标准

- [ ] `meta.examples` 补齐后仍保留 console/stdio mode 语义（T01, T06）
- [ ] 帮助输出中的 CLI 示例统一为 M98 规范（T02, S01）
- [ ] 导出文档中的 CLI 示例统一为 M98 规范（T03, S02）
- [ ] 帮助输出与导出文档对同一示例生成相同 CLI 片段（T04, S03）
- [ ] DriverLab 命令行示例与示例列表遵循同一套 CLI 规则，且预览/Apply 语义明确（T05-T07）
- [ ] C++ 与前端通过共享 fixture 约束关键示例片段一致（T08）

## 7. 风险与控制

- 风险: C++ 与 TypeScript 各自维护渲染逻辑，后续再次分叉
  - 控制: 用同一份 `src/tests/data/cli_render_cases.json` 约束 help/doc/WebUI 的关键片段
  - 控制: 前端和 C++ 均补对应回归测试
  - 测试覆盖: T02-T08, S01-S03

- 风险: 为统一示例而误改 DriverLab 真实执行链路
  - 控制: 明确限制改动只落在展示/复制逻辑，不改执行协议
  - 控制: `CommandPanel` 测试断言“预览跟随表单值、Apply 才写回表单”，不触碰实际 exec 流程
  - 测试覆盖: T05-T07

- 风险: auto-fill 过程中丢失原有 stdio 示例信息
  - 控制: `ensureCommandExamples()` 只补齐缺失示例，不覆盖现有 mode 和 description
  - 测试覆盖: T01

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] 冒烟测试脚本已新增并接入统一入口（`run_smoke.py`）与 CTest
- [ ] 冒烟测试在目标环境执行通过（或有明确 skip 记录）
- [ ] M99 不修改 runtime 解析主链路，只统一示例文本与展示逻辑
- [ ] M100 需要消费的规范示例片段已在 M99 中稳定产出
