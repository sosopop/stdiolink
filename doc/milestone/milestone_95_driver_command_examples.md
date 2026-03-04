# 里程碑 95：驱动命令示例（含 WebUI 展示与文档/帮助输出）

> **前置条件**: M9（DriverMetaBuilder）、M89（统一运行时布局）、M94（测试入口统一）
> **目标**: 为 8 个驱动共 130 个命令补齐 `examples` 元数据，并在 WebUI、`--export-doc`、`--help` 中完成一致展示

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `src/stdiolink/driver` | `CommandBuilder` 新增 `example()` 链式 API |
| `src/stdiolink/doc` | `DocGenerator` 多格式输出示例区（markdown/openapi/html/ts） |
| `src/stdiolink/driver` | `console --help` 与 `--cmd=<x> --help` 展示示例与参数解释 |
| `src/drivers/*` | 8 个驱动命令元数据补齐示例 |
| `src/webui` | DriverLab 渲染示例列表并支持一键填充参数 |
| `src/tests` | `example()` 单元测试补齐 |
| `src/webui/src/**/__tests__` | 示例渲染与填充交互测试 |
| `src/smoke_tests` | M95 冒烟脚本接入 `run_smoke.py --plan` 与 CTest |

- 扩展 `CommandBuilder`，支持声明命令示例
- 为 8 个驱动共 130 个命令补齐示例，且每个命令至少 1 条 `stdio` 示例
- 尽量为可执行命令补齐 `mode=console` 示例（不适配命令允许例外）
- WebUI DriverLab 在命令面板展示示例，并可将示例参数填充到参数表单
- `--export-doc` 的 markdown/openapi/html/ts 均包含示例内容
- `console --help` 和 `--cmd=<command> --help` 增强为包含命令解释与示例
- `meta.describe` 输出稳定包含 `examples`
- 冒烟测试覆盖示例存在性与结构完整性

## 2. 背景与问题

- 当前驱动虽然有参数定义，但缺少“可直接复制/填充”的命令样例
- `CommandMeta` 已有 `examples` 字段，但构建器层无法写入，导致长期为空
- WebUI DriverLab 目前未消费 `command.examples`，用户仍需手动逐项填参
- `--export-doc` 生成文档目前未系统展示 `examples`，文档与真实可执行样例脱节
- `--cmd=<command> --help` 当前只有参数列表，缺少可直接执行的示例命令解释

**范围**:
- 新增 `CommandBuilder::example(...)`
- 为以下 8 个驱动命令元数据补齐示例：
  - `driver_plc_crane`
  - `driver_modbustcp`
  - `driver_modbustcp_server`
  - `driver_modbusrtu`
  - `driver_modbusrtu_serial`
  - `driver_modbusrtu_server`
  - `driver_modbusrtu_serial_server`
  - `driver_3dvision`
- WebUI DriverLab 解析并渲染示例，支持“应用示例参数”
- 更新 `DocGenerator`（markdown/openapi/html/ts）以输出命令示例
- 更新 `HelpGenerator`/`DriverCore::printCommandHelp` 输出示例与说明
- 新增 M95 冒烟测试计划

**非目标**:
- 不修改任何命令运行逻辑、参数校验逻辑、事件语义
- 不实现“自动执行所有示例参数”的能力
- 不修改 Drivers 列表页/详情页的布局结构（仅 DriverLab 交互增强）
- 不新增新的文档导出格式（仅增强现有 markdown/openapi/html/ts 内容）

## 3. 技术要点

### 3.1 `CommandBuilder` 扩展

`CommandMeta` 已有 `examples` 字段，本次补齐 Builder 写入能力。

```cpp
// src/stdiolink/driver/meta_builder.h
CommandBuilder& example(const QString& description,
                        const QString& mode,
                        const QJsonObject& params,
                        const QJsonValue& expectedOutput = QJsonValue());
```

```cpp
// src/stdiolink/driver/meta_builder.cpp
CommandBuilder& CommandBuilder::example(const QString& description,
                                        const QString& mode,
                                        const QJsonObject& params,
                                        const QJsonValue& expectedOutput) {
    QJsonObject ex;
    ex["description"] = description;
    ex["mode"] = mode;
    ex["params"] = params;
    if (!expectedOutput.isNull() && !expectedOutput.isUndefined()) {
        ex["expectedOutput"] = expectedOutput;
    }
    m_cmd.examples.append(ex);
    return *this;
}
```

### 3.2 示例元数据契约（后端与前端）

后端输出契约：

```json
{
  "description": "读取 5 个线圈",
  "mode": "stdio",
  "params": { "host": "127.0.0.1", "port": 502, "unit_id": 1, "address": 0, "count": 5 },
  "expectedOutput": { "values": [true, false, true] }
}
```

前端类型约束（新增显式类型，避免 `Record<string, unknown>[]` 弱类型）：

```ts
export interface CommandExampleMeta {
  description: string;
  mode: 'stdio' | 'console' | string;
  params: Record<string, unknown>;
  expectedOutput?: unknown;
}
```

### 3.3 示例命令行规范（统一用于 WebUI / 帮助 / 文档）

示例中的命令行参数规范：

- `mode` 与 `profile` 为默认值时不写入示例参数
- 默认值定义：
  - `mode=console` 为 console 示例默认模式
  - `profile=oneshot` 为默认 profile
- 仅当示例确实依赖非默认值时才写：
  - `mode=stdio`
  - `profile=keepalive`

示例参数最小化伪代码：

```text
if example.mode == "console":
  omit mode unless explicitly required != console
  omit profile unless explicitly required != oneshot
else:
  keep only semantically required mode/profile
```

### 3.4 WebUI 交互流程（DriverLab）

```text
接收 meta(ws: type=meta)
  -> commands[] 写入 store
  -> CommandPanel 选择命令
  -> 渲染该命令 examples 列表
  -> 用户点击“应用示例”
  -> commandParams = example.params
  -> ParamForm 刷新字段值
  -> CommandLineExample 同步刷新 CLI 参数串
```

关键行为约束：
- 示例为空时不渲染示例区
- 示例存在但 `params` 非对象时忽略该条（防御式解析）
- 应用示例后，用户仍可继续手动修改参数

### 3.5 `DocGenerator` 与 `--help` 输出对齐策略

`DocGenerator` 输出策略：
- markdown：每个命令新增 `#### Examples`，列出示例模式、参数、可选期望输出
- openapi：在 operation 下新增 `x-stdiolink-examples`
- html：命令卡片新增 `Examples` 区块
- ts：命令方法 JSDoc 增加 `@example`（使用 console 示例优先）

`--help` 输出策略：
- 全局帮助：保留命令列表，并明确提示 `--cmd=<command> --help` 可查看示例
- 子命令帮助：新增 `Examples:` 段，优先展示 console 示例；无 console 时展示 stdio 示例
- 帮助文本中的示例参数遵循“默认 mode/profile 省略”规则

### 3.6 向后兼容

- 无改动时：`examples` 常为空，WebUI 不展示示例区
- 有改动时：`examples` 被填充，DriverLab 多出示例列表和“应用示例”交互
- 兼容性：旧驱动（无 `examples`）仍可正常使用，UI 自动降级为原行为

## 4. 实现步骤

### 4.1 扩展 `CommandBuilder`

- 修改 `src/stdiolink/driver/meta_builder.h`
  - 新增 `example(...)` 声明
- 修改 `src/stdiolink/driver/meta_builder.cpp`
  - 实现 `example(...)` 并写入 `m_cmd.examples`

改动理由：
- 将示例写入能力收敛到 Builder，避免各驱动手写 JSON 结构

验收方式：
- `test_meta_builder.cpp` 用例验证单条/多条/可选 `expectedOutput`

### 4.2 驱动命令元数据补齐示例

按驱动补齐 `.example(...)`：

- `src/drivers/driver_plc_crane/handler.cpp`（6 命令）
- `src/drivers/driver_modbustcp/main.cpp`（10 命令）
- `src/drivers/driver_modbusrtu/main.cpp`（10 命令）
- `src/drivers/driver_modbusrtu_serial/handler.cpp`（10 命令）
- `src/drivers/driver_modbustcp_server/handler.cpp`（17 命令）
- `src/drivers/driver_modbusrtu_server/handler.cpp`（17 命令）
- `src/drivers/driver_modbusrtu_serial_server/handler.cpp`（17 命令）
- `src/drivers/driver_3dvision/main.cpp`（43 命令）

示例规则：
- 每个命令至少 1 条 `mode=stdio`
- 在命令语义允许的前提下尽量补齐 `mode=console`
- `mode/profile` 若为默认值，不写入示例参数
- `expectedOutput` 仅在返回结构稳定时填写

验收方式：
- 冒烟脚本断言每个命令存在示例且包含一条 `stdio`
- 冒烟脚本统计 `mode=console` 覆盖率并输出报告（允许少量例外但必须可解释）

### 4.3 WebUI 类型与解析

- 修改 `src/webui/src/types/driver.ts`
  - 新增 `CommandExampleMeta`
  - 将 `CommandMeta.examples` 改为 `CommandExampleMeta[]`
- 新增 `src/webui/src/components/DriverLab/exampleMeta.ts`
  - 提供 `normalizeCommandExamples(input)`，过滤非法示例并返回标准结构

改动理由：
- 降低运行时 `any/unknown` 风险，避免渲染层散落防御代码

验收方式：
- `exampleMeta` 单元测试覆盖空值、非法结构、合法结构

### 4.4 WebUI DriverLab 示例渲染与填充

- 新增 `src/webui/src/components/DriverLab/CommandExamples.tsx`
  - 渲染当前命令示例列表（描述、模式、简短参数预览）
  - 提供“应用示例”按钮
- 修改 `src/webui/src/components/DriverLab/CommandPanel.tsx`
  - 集成 `CommandExamples`
  - 点击应用后调用 `onParamsChange(example.params)`
  - 示例为空时不渲染示例区
- 修改 `src/webui/src/locales/en.json`、`src/webui/src/locales/zh.json`
  - 新增 DriverLab 示例区文案键

改动理由：
- 将示例消费入口放在命令执行面板，减少用户在“阅读示例”和“填写参数”间来回切换

验收方式：
- 组件测试断言示例区可见性、应用示例行为、空示例降级行为

### 4.5 `DocGenerator` 多格式示例输出改造

- 修改 `src/stdiolink/doc/doc_generator.cpp`、`src/stdiolink/doc/doc_generator.h`
  - markdown：命令段落新增 `Examples`
  - openapi：operation 增加 `x-stdiolink-examples`
  - html：命令详情新增示例模块
  - ts：方法注释新增 `@example`

改动理由：
- 文档输出必须与运行时元数据同源，避免“有示例但文档看不到”

验收方式：
- `test_doc_generator.cpp` 补充 4 类格式对 `examples` 的断言

### 4.6 `console --help` 子命令解释改造

- 修改 `src/stdiolink/driver/help_generator.cpp`
  - `generateCommandHelp()` 增加 `Examples:` 输出
  - 示例文案应用默认 `mode/profile` 省略规则
- 修改 `src/stdiolink/driver/driver_core.cpp`
  - `printCommandHelp()` 调整为展示增强后的帮助文本
  - `printHelp()` 增加子命令帮助引导说明

改动理由：
- 命令帮助应直接回答“这个命令怎么调用”，而不是只列参数定义

验收方式：
- `test_system_help.cpp` 新增命令帮助示例断言
- `test_console.cpp` 新增 `--cmd=<x> --help` 输出断言

### 4.7 冒烟测试接入统一入口

- 新增 `src/smoke_tests/m95_driver_examples.py`
  - 解析驱动 `meta.describe`，校验示例覆盖与结构
  - 调用 `--export-doc` 四种格式验证示例可见
  - 调用 `--cmd=<x> --help` 验证示例段存在
- 修改 `src/smoke_tests/run_smoke.py`
  - 注册 `--plan m95_driver_examples`
- 修改 `src/smoke_tests/CMakeLists.txt`
  - 注册 `smoke_m95_driver_examples`

验收方式：
- `python src/smoke_tests/run_smoke.py --plan m95_driver_examples`
- `ctest -R smoke_m95_driver_examples`

## 5. 文件变更清单

### 5.1 新增文件

- `src/webui/src/components/DriverLab/CommandExamples.tsx` - DriverLab 示例列表组件
- `src/webui/src/components/DriverLab/exampleMeta.ts` - 示例解析/归一化工具
- `src/webui/src/components/DriverLab/__tests__/CommandExamples.test.tsx` - 示例组件测试
- `src/webui/src/components/DriverLab/__tests__/exampleMeta.test.ts` - 示例解析测试
- `src/smoke_tests/m95_driver_examples.py` - M95 冒烟测试

### 5.2 修改文件

- `src/stdiolink/driver/meta_builder.h` - 新增 `example()` 声明
- `src/stdiolink/driver/meta_builder.cpp` - 新增 `example()` 实现
- `src/stdiolink/doc/doc_generator.h` - 增加示例渲染辅助接口
- `src/stdiolink/doc/doc_generator.cpp` - 四种导出格式补齐示例输出
- `src/stdiolink/driver/help_generator.cpp` - 子命令帮助增加示例与解释
- `src/stdiolink/driver/driver_core.cpp` - 帮助输出入口对齐
- `src/drivers/driver_plc_crane/handler.cpp` - 6 命令示例
- `src/drivers/driver_modbustcp/main.cpp` - 10 命令示例
- `src/drivers/driver_modbusrtu/main.cpp` - 10 命令示例
- `src/drivers/driver_modbusrtu_serial/handler.cpp` - 10 命令示例
- `src/drivers/driver_modbustcp_server/handler.cpp` - 17 命令示例
- `src/drivers/driver_modbusrtu_server/handler.cpp` - 17 命令示例
- `src/drivers/driver_modbusrtu_serial_server/handler.cpp` - 17 命令示例
- `src/drivers/driver_3dvision/main.cpp` - 43 命令示例
- `src/webui/src/types/driver.ts` - `CommandExampleMeta` 类型与 `CommandMeta.examples` 类型收敛
- `src/webui/src/components/DriverLab/CommandPanel.tsx` - 集成示例渲染/填充交互
- `src/webui/src/locales/en.json` - 示例区 i18n 文案
- `src/webui/src/locales/zh.json` - 示例区 i18n 文案
- `src/smoke_tests/run_smoke.py` - 注册 M95 计划
- `src/smoke_tests/CMakeLists.txt` - 注册 M95 CTest

### 5.3 测试文件

- `src/tests/test_meta_builder.cpp` - 新增/扩展 `example()` 单元测试
- `src/tests/test_doc_generator.cpp` - 扩展示例导出断言
- `src/tests/test_system_help.cpp` - 扩展子命令帮助示例断言
- `src/tests/test_console.cpp` - 扩展 `--cmd=<x> --help` 解析/输出断言
- `src/webui/src/components/DriverLab/__tests__/CommandPanel.test.tsx` - 扩展示例相关断言
- `src/webui/src/components/DriverLab/__tests__/CommandExamples.test.tsx` - 新增示例组件测试
- `src/webui/src/components/DriverLab/__tests__/exampleMeta.test.ts` - 新增示例解析测试

## 6. 测试与验收

### 6.1 单元测试

#### 6.1.1 C++（Builder 与序列化）

测试对象：
- `CommandBuilder::example()`

测试文件：
- `src/tests/test_meta_builder.cpp`

路径矩阵：

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `example()` 添加示例 | 首次添加 | T01 |
| `example()` 多次添加 | 顺序与数量保持 | T02 |
| `expectedOutput` 处理 | 提供值时写入字段 | T03 |
| `expectedOutput` 处理 | 未提供时不写入字段 | T04 |
| 链式调用兼容 | 与 `param()/returns()` 组合 | T05 |
| 示例参数规范 | 默认 `mode/profile` 省略 | T06 |

#### 6.1.2 C++（文档与帮助输出）

测试对象：
- `DocGenerator::toMarkdown/toOpenAPI/toHtml/toTypeScript`
- `HelpGenerator::generateCommandHelp`

测试文件：
- `src/tests/test_doc_generator.cpp`
- `src/tests/test_system_help.cpp`
- `src/tests/test_console.cpp`

路径矩阵：

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| markdown 示例区 | 命令有示例时渲染 `Examples` | D01 |
| openapi 扩展字段 | 写入 `x-stdiolink-examples` | D02 |
| html 示例区 | 命令卡片渲染示例 | D03 |
| ts 注释示例 | 生成 `@example` | D04 |
| 命令帮助 | `--cmd=<x> --help` 展示示例段 | H01 |
| 默认参数省略 | 帮助示例不包含默认 `mode/profile` | H02 |

#### 6.1.3 WebUI（解析、渲染、交互）

测试对象：
- `normalizeCommandExamples`
- `CommandExamples`
- `CommandPanel` 示例应用流程

测试文件：
- `src/webui/src/components/DriverLab/__tests__/exampleMeta.test.ts`
- `src/webui/src/components/DriverLab/__tests__/CommandExamples.test.tsx`
- `src/webui/src/components/DriverLab/__tests__/CommandPanel.test.tsx`

路径矩阵：

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `normalizeCommandExamples` 输入为空 | 返回空数组 | W01 |
| `normalizeCommandExamples` 非法项 | 过滤非法项 | W02 |
| `CommandExamples` 渲染 | 有示例时渲染列表 | W03 |
| `CommandExamples` 渲染 | 无示例时不渲染内容 | W04 |
| `CommandPanel` 交互 | 点击应用示例触发 `onParamsChange` | W05 |
| `CommandPanel` 兼容 | 命令无示例时维持原有表单流程 | W06 |

测试代码骨架：

```ts
it('apply example should update params', () => {
  const onParamsChange = vi.fn();
  render(<CommandPanel ... onParamsChange={onParamsChange} />);
  fireEvent.click(screen.getByTestId('apply-example-0'));
  expect(onParamsChange).toHaveBeenCalledWith({ host: '127.0.0.1', port: 502 });
});
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m95_driver_examples.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m95_driver_examples`
- CTest: `smoke_m95_driver_examples`

覆盖范围：
- 成功链路：8 个驱动 `meta.describe` 均可解析并包含示例
- 成功链路：`--export-doc` 四种格式均可导出且包含示例段
- 成功链路：`--cmd=<x> --help` 展示命令解释与示例
- 关键失败链路：任一命令缺示例/缺 `stdio` 示例/字段不完整时脚本非 0 失败

用例清单：
- `S01`: 驱动可执行，`meta.describe` 返回 `status=done`
- `S02`: 每个命令 `examples` 非空
- `S03`: 每个命令至少 1 条 `mode=stdio`
- `S04`: 每条示例包含 `description/mode/params`
- `S05`: `mode=console` 覆盖率报告输出（低于 80% 且不在脚本例外清单内则失败）
- `S06`: `--export-doc=markdown/openapi/html/ts` 均含示例片段
- `S07`: `--cmd=<x> --help` 包含 `Examples:`
- `S08`: 结构不合规时输出驱动名+命令名并失败

环境约束与 skip：
- 驱动二进制不存在时 `SKIP` 并输出原因
- 脚本不依赖外部硬件或真实网络设备

### 6.3 集成测试

WebUI 侧通过已有 Vitest 组件测试链路验证“元数据->渲染->参数填充”流程；本里程碑不新增 Playwright 用例。

### 6.4 验收标准

- [ ] `CommandBuilder::example()` 实现完成，T01-T06 通过
- [ ] 8 个驱动共 130 个命令全部具备 `examples`
- [ ] 每个命令至少 1 条 `mode=stdio` 示例
- [ ] 命令示例遵循默认参数省略规则（默认 `mode/profile` 不写）
- [ ] `mode=console` 在可适配命令中尽量覆盖，并有例外清单
- [ ] DriverLab 可展示示例并一键填充参数（W03-W06 全通过）
- [ ] `--export-doc` 四种格式均包含示例段（D01-D04 通过）
- [ ] `--cmd=<x> --help` 可输出命令解释与示例（H01-H02 通过）
- [ ] `python src/smoke_tests/run_smoke.py --plan m95_driver_examples` 通过
- [ ] `ctest -R smoke_m95_driver_examples` 通过

## 7. 风险与控制

- 风险: 示例参数与命令参数定义不匹配，误导用户
  - 控制: 示例与参数定义同文件维护，Code Review 强制对照
  - 控制: 冒烟脚本校验命令示例存在性与字段完整性
  - 测试覆盖: S02/S03/S04/S08

- 风险: 前端直接使用弱类型 `unknown` 导致运行时异常
  - 控制: 新增 `CommandExampleMeta` 与 `normalizeCommandExamples`
  - 控制: 对非法示例执行过滤降级，不阻断其他命令使用
  - 测试覆盖: W01/W02/W06

- 风险: 示例区交互影响现有执行流程
  - 控制: 示例功能仅附加，不改 `onExec` 与 `ParamForm` 主流程
  - 控制: 命令无示例时渲染路径与旧版本一致
  - 测试覆盖: W04/W06

- 风险: 文档导出与帮助输出使用不同格式规则，导致示例不一致
  - 控制: 在 `doc_generator` 与 `help_generator` 共享示例格式化规则
  - 控制: 新增 D/H/S 组测试同时校验文档与帮助
  - 测试覆盖: D01-D04/H01-H02/S06-S07

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成（C++ + WebUI）
- [ ] 冒烟脚本已新增并接入 `run_smoke.py --plan` 与 CTest
- [ ] 冒烟脚本在目标环境可运行并通过（或有明确 `SKIP` 记录）
- [ ] WebUI 示例渲染与参数填充功能可用，并有对应单元测试
- [ ] doc_generator 四格式文档均包含命令示例
- [ ] console 子命令帮助包含示例与参数解释（默认参数省略）
- [ ] 向后兼容确认：旧驱动（无 `examples`）不受影响
