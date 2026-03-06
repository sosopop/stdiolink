# 里程碑 100：CLI 手册、排障与迁移说明收尾

> **前置条件**: M99（CLI 示例统一与多入口渲染收敛完成）
> **目标**: 在 runtime 行为和示例文本都稳定后，完成 Console、DriverLab、最佳实践、故障排除手册以及迁移说明的同步更新，使 `doc/manual` 下的用户手册与排障文档和 M98/M99 的最终行为保持一致。

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `doc/manual` | Console、DriverLab、最佳实践、故障排除章节更新 |
| 迁移说明 | 旧写法 -> 新写法迁移表、兼容边界、常见失败路径说明 |
| 测试与验证 | 文档内容校验测试、手册 smoke、自检清单 |

- 更新 `07-console`，明确路径语法、Canonical/Friendly、shell 兼容建议
- 更新 `12-webui/driverlab`，说明 DriverLab 命令行示例来源、限制与复制建议
- 更新 `08-best-practices`，明确推荐显式路径写法，不依赖整段 JSON shell 转义
- 更新 `09-troubleshooting`，补充 `expected string`、`expected array/object`、shell 转义、字符串误判排障
- 新增迁移说明，列出旧写法、失败症状、推荐替代写法
- 通过文档测试和 smoke 保证关键片段存在且与 M99 统一示例一致

## 2. 背景与问题

- M98 解决了 runtime 核心解析链路，M99 统一了 help/doc/WebUI 示例，但用户手册仍可能保留旧写法
- 对用户来说，真正的升级成本主要来自“旧脚本为什么失败”“该改成什么写法”“哪些 shell 更脆弱”
- 如果手册和排障文档不同步，runtime 与示例统一的收益会被抵消，第三方集成仍会反复踩旧坑

**范围**:
- `doc/manual/07-console/README.md`
- `doc/manual/12-webui/driverlab.md`
- `doc/manual/08-best-practices.md`
- `doc/manual/09-troubleshooting.md`
- `src/tests/test_manual_docs.cpp`
- `src/smoke_tests/m100_cli_docs_and_migration.py`

**非目标**:
- 不再修改 runtime 解析逻辑
- 不再修改 help/doc/WebUI 的示例渲染实现
- 不引入新的 CLI 语法或兼容模式
- 不替代 M98/M99 的单元测试，只做文档与迁移层收尾

## 3. 技术方案

### 3.1 文档同步矩阵

| 文档 | 更新重点 |
|------|----------|
| `doc/manual/07-console/README.md` | 路径语法、Canonical/Friendly、shell 兼容建议 |
| `doc/manual/12-webui/driverlab.md` | DriverLab 示例来源、展示与执行边界、复制建议 |
| `doc/manual/08-best-practices.md` | 推荐使用显式路径，不依赖整段 JSON shell 转义 |
| `doc/manual/09-troubleshooting.md` | `expected string`/`expected array/object`、PowerShell 5.1/cmd 典型问题、替代写法 |

### 3.2 迁移说明结构

迁移说明至少包含三类信息：
- 旧写法 -> 新写法对照表
- 失败症状 -> 根因 -> 推荐替代写法
- 兼容边界说明：哪些行为收紧、哪些行为保持不变

迁移说明落点固定为：
- `doc/manual/07-console/README.md`：推荐写法与旧写法对照
- `doc/manual/09-troubleshooting.md`：失败症状、根因、替代写法

示例：

| 旧写法 | 问题 | 新写法 |
|--------|------|--------|
| `--units=[{"id":1}]` | shell 转义脆弱 | `--units[0].id=1` |
| `--password=123456` | 旧实现会误判为 number | M98 后可直接使用；stdio 仍应传 JSON string |
| `--a={"b":1} --a.c=2` | 结构冲突 | 改为统一使用子路径或统一使用完整 JSON，不混用 |

### 3.3 文档验收原则

- 手册主体写“推荐写法”和“为何推荐”
- 排障文档写“症状 -> 根因 -> 替代写法”
- DriverLab 文档写明“示例字符串用于展示，实际执行仍走 JSON 协议”
- 文档中的 CLI 示例必须与 M99 产出的规范片段一致，禁止再次回落到旧式整体 JSON 引号写法

## 4. 实现步骤

### 4.1 更新 Console 手册

涉及文件：
- `doc/manual/07-console/README.md`

关键片段：

```md
### 推荐：对象数组参数
--units[0].id=1 --units[0].size=10000

### 不推荐：依赖 shell 保留整段 JSON
--units=[{"id":1,"size":10000}]
```

改动理由：Console 是命令行规范的第一入口，必须先把推荐写法讲清楚。
验收方式：T01。

### 4.2 更新 DriverLab、最佳实践与故障排除手册

涉及文件：
- `doc/manual/12-webui/driverlab.md`
- `doc/manual/08-best-practices.md`
- `doc/manual/09-troubleshooting.md`

关键片段：

```md
DriverLab 中显示的命令行示例用于展示和复制；实际执行仍走 WebSocket/JSON 请求，不以该字符串作为内部协议。
```

改动理由：这三份文档分别对应展示边界、推荐写法和排障路径，必须一起更新才能形成闭环。
验收方式：T02-T04。

### 4.3 增加迁移说明与文档校验测试

涉及文件：
- `src/tests/test_manual_docs.cpp`
- `doc/manual/07-console/README.md`
- `doc/manual/09-troubleshooting.md`

关键代码片段：

```cpp
TEST(ManualDocs, T04_TroubleshootingContainsShellCompatibilityNotes) {
    const QString doc = readTextFile("doc/manual/09-troubleshooting.md");
    EXPECT_TRUE(doc.contains("PowerShell 5.1"));
    EXPECT_TRUE(doc.contains("expected string"));
}
```

改动理由：M100 交付的是“文档可用性”，必须有自动化校验兜底。
验收方式：T01-T05、S01-S02。

### 4.4 新增 smoke 脚本并接入统一入口

涉及文件：
- `src/smoke_tests/m100_cli_docs_and_migration.py`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`

改动理由：需要在命令行级验证关键文档文件存在且包含关键迁移片段，避免只靠人工检查。
验收方式：S01-S03。

## 5. 文件变更清单

### 5.1 新增文件
- `src/smoke_tests/m100_cli_docs_and_migration.py`

### 5.2 修改文件
- `doc/manual/07-console/README.md`
- `doc/manual/12-webui/driverlab.md`
- `doc/manual/08-best-practices.md`
- `doc/manual/09-troubleshooting.md`
- `src/tests/test_manual_docs.cpp`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`

### 5.3 测试文件
- `src/tests/test_manual_docs.cpp`

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: 文档内容与迁移说明
- 测试文件: `src/tests/test_manual_docs.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `07-console` | 包含路径语法与推荐写法 | T01 |
| `12-webui/driverlab` | 说明示例来源与执行边界 | T02 |
| `08-best-practices` | 包含推荐显式路径示例 | T03 |
| `09-troubleshooting` | 包含 shell 兼容与错误排障 | T04 |
| 迁移说明 | 包含旧写法 -> 新写法对照 | T05 |

#### 用例详情

**T01 — Console 手册包含路径语法与推荐写法**
- 前置条件: 更新 `07-console/README.md`
- 输入: 读取手册文本
- 预期: 手册包含 `servers[0].host`、`tags[]`、`--units[0].id=1`
- 断言: `consoleDoc.contains(...) == true`

**T02 — DriverLab 文档说明展示/执行边界**
- 前置条件: 更新 `12-webui/driverlab.md`
- 输入: 读取手册文本
- 预期: 文档明确写出 `argv token` 或等价表述，并说明实际执行仍走 JSON 协议
- 断言: `driverlabDoc.contains("argv token")` 且 `driverlabDoc.contains("JSON")`

**T03 — 最佳实践文档包含推荐路径写法**
- 前置条件: 更新 `08-best-practices.md`
- 输入: 读取手册文本
- 预期: 包含 `--units[0].id=1` 等推荐写法
- 断言: `practicesDoc.contains("--units[0].id=1")`

**T04 — 故障排除文档包含 shell 兼容与错误排障**
- 前置条件: 更新 `09-troubleshooting.md`
- 输入: 读取手册文本
- 预期: 包含 `PowerShell 5.1`、`expected string`、`expected array` 或 `expected object`
- 断言: `troubleshootDoc.contains(...) == true`

**T05 — 迁移说明包含旧写法 -> 新写法对照**
- 前置条件: 迁移说明已落在 `07-console/README.md` 或 `09-troubleshooting.md`
- 输入: 读取上述手册文本
- 预期: 至少包含 `--units=[{"id":1}] -> --units[0].id=1` 这类对照
- 断言: 文本中同时出现旧写法和替代写法

#### 测试代码

```cpp
TEST(ManualDocs, T01_ConsoleManualContainsNormalizedExamples) {
    const QString consoleDoc = readTextFile("doc/manual/07-console/README.md");
    EXPECT_TRUE(consoleDoc.contains("servers[0].host"));
    EXPECT_TRUE(consoleDoc.contains("tags[]"));
    EXPECT_TRUE(consoleDoc.contains("--units[0].id=1"));
}
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m100_cli_docs_and_migration.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m100_cli_docs_and_migration`
- CTest 接入: `smoke_m100_cli_docs_and_migration`
- 覆盖范围: 手册文件存在性、关键片段存在性、迁移说明存在性
- 用例清单:
  - `S01`: 四份手册文件存在，且包含关键规范片段
  - `S02`: 故障排除文档包含 `PowerShell 5.1` 与典型错误文本
  - `S03`: `07-console/README.md` 或 `09-troubleshooting.md` 中存在旧写法 -> 新写法对照表
- 失败输出规范: 输出缺失文件名、缺失关键片段、退出码
- 环境约束与跳过策略: 不依赖外部服务、原则上不允许 `skip`
- 产物定位契约: 文档文件路径固定在 `doc/manual/`；文件不存在或关键片段缺失时必须判定 `FAIL`
- 跨平台运行契约: 使用 UTF-8 读取文本，Windows/Linux/macOS 一致按路径访问仓库文件

### 6.3 集成/端到端测试

- 文档内容与 M99 统一示例片段的一致性检查
- 迁移说明覆盖 M98/M99 的关键行为变化

### 6.4 验收标准

- [ ] `07-console` 手册已同步路径语法、值语义和推荐写法（T01, S01）
- [ ] `12-webui/driverlab` 已同步示例来源与执行边界说明（T02, S01）
- [ ] `08-best-practices` 已同步推荐路径写法（T03, S01）
- [ ] `09-troubleshooting` 已同步 shell 兼容与典型错误排障（T04, S02）
- [ ] 迁移说明已覆盖旧写法 -> 新写法对照（T05, S03）

## 7. 风险与控制

- 风险: M100 文档仍引用旧示例文本，与 M99 规则不一致
  - 控制: 文档测试直接断言规范片段存在
  - 控制: smoke 读取真实 Markdown 文件做关键片段校验
  - 测试覆盖: T01-T05, S01-S03

- 风险: 只更新手册正文，遗漏迁移说明和排障路径
  - 控制: M100 验收必须同时包含最佳实践、排障和迁移三类文档内容
  - 测试覆盖: T03-T05, S02-S03

- 风险: DriverLab 文档把展示字符串误写成执行协议
  - 控制: 明确要求写出“展示/执行边界”说明，并用测试检查关键表述
  - 测试覆盖: T02

## 8. 里程碑完成定义（DoD）

- [ ] 文档与测试完成
- [ ] 冒烟测试脚本已新增并接入统一入口（`run_smoke.py`）与 CTest
- [ ] 冒烟测试在目标环境执行通过（或有明确 skip 记录）
- [ ] 手册、排障、迁移说明三类内容均已同步
- [ ] M98/M99 的用户可见变化已在 M100 中形成完整说明闭环
