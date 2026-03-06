# JSON 到 CLI 参数映射规范（草案）

**版本**：1.0
**状态**：Draft

## 1. 目标

本规范定义一种通用的、可逆的 JSON 与命令行参数之间的映射方式。

适用场景：

* 命令行工具接收结构化输入
* SDK、脚本、CI/CD、容器启动参数
* 需要让用户直接在终端输入 JSON 对应参数
* 需要尽量避免 shell 不兼容、特殊编码和外部文件依赖

本规范**明确不采用**：

* base64 编码承载整段 JSON
* `@file`、`--file config.json` 等文件输入作为规范主体

本规范**允许**：

* 在单个参数值中直接写合法 JSON 文本，作为特殊值或局部子树输入方式

---

## 2. 设计原则

### 2.1 易输入

常见场景应尽量接近直觉，例如：

```bash
--name alice --age 18 --enabled
```

### 2.2 兼容性优先

优先采用多数 CLI 解析器、POSIX shell、PowerShell、Windows cmd 都容易接受的形式：

* 长参数优先：`--key`
* 推荐同时支持：

  * `--key value`
  * `--key=value`

### 2.3 可逆

在没有外部 schema 的情况下，也应尽量能稳定还原 JSON。

### 2.4 分层表达

简单值用简单写法，复杂值允许局部使用内联 JSON。

### 2.5 最小特殊语法

只引入少量路径语法：

* `.` 表示对象层级
* `[N]` 表示数组下标
* `[]` 表示数组追加
* `["..."]` 表示带特殊字符的键名

---

## 3. 术语

### 3.1 路径（Path）

用于表示 JSON 中某个节点的位置。

示例：

* `user.name`
* `db.port`
* `items[0].id`
* `labels["app.kubernetes.io/name"]`

### 3.2 值（Value）

CLI 中某个参数对应的值，可表示：

* string
* number
* boolean
* null
* object
* array

### 3.3 规范模式（Canonical Mode）

值总是按 **JSON 字面量** 解析，歧义最小，最利于跨实现互通。

### 3.4 友好模式（Friendly Mode）

为便于手输，允许未加引号的普通字符串；解析器按规则推断类型。

---

## 4. 参数基本形式

本规范只定义**长参数**形式，不定义短参数（如 `-p`）。

标准写法：

```bash
--<path> <value>
```

或：

```bash
--<path>=<value>
```

例如：

```bash
--name alice
--age=18
--db.host 127.0.0.1
```

### 4.1 推荐

实现方应当：

* **MUST** 支持 `--key value`
* **SHOULD** 支持 `--key=value`

### 4.2 值以 `-` 开头时

为避免被误识别为新参数，**SHOULD** 使用 `=` 形式：

```bash
--delta=-1
--name=--prod
```

或在参数解析器支持时使用 `--` 截断选项解析：

```bash
cmd --name -- --prod
```

但跨实现更推荐前者。

---

## 5. 路径语法

## 5.1 对象字段

对象层级使用 `.` 连接。

JSON：

```json
{
  "db": {
    "host": "localhost",
    "port": 5432
  }
}
```

CLI：

```bash
--db.host localhost --db.port 5432
```

---

## 5.2 数组下标

数组元素使用 `[N]` 表示，`N` 从 `0` 开始。

JSON：

```json
{
  "servers": [
    {"host": "a"},
    {"host": "b"}
  ]
}
```

CLI：

```bash
--servers[0].host a --servers[1].host b
```

---

## 5.3 数组追加

对数组追加元素，使用终止标记 `[]`。

JSON：

```json
{
  "tags": ["api", "beta"]
}
```

CLI：

```bash
--tags[] api --tags[] beta
```

追加顺序即参数出现顺序。

### 约束

`[]` **SHOULD** 只出现在路径末尾。

合法：

```bash
--tags[] api
--users[] '{"name":"alice"}'
```

不推荐：

```bash
--users[].name alice
```

对于对象数组，推荐两种方式：

1. 追加整个对象元素
2. 使用显式下标

例如：

```bash
--users[] '{"name":"alice","age":18}'
--users[] '{"name":"bob","age":20}'
```

或：

```bash
--users[0].name alice --users[0].age 18
--users[1].name bob   --users[1].age 20
```

---

## 5.4 特殊键名

当键名包含以下字符时，不能直接用裸字段名：

* `.`
* `[`
* `]`
* `"`
* 空格
* `/`
* 其他容易与路径语法冲突的字符

此时使用：

```text
["<json-string>"]
```

作为路径段。

例如 JSON：

```json
{
  "labels": {
    "app.kubernetes.io/name": "demo"
  }
}
```

CLI：

```bash
'--labels["app.kubernetes.io/name"]' demo
```

再例如：

```json
{
  "x y": {
    "a.b": 1
  }
}
```

CLI：

```bash
'--["x y"]["a.b"]' 1
```

### 建议

为兼容性和可读性，业务键名 **SHOULD** 优先使用：

* 英文字母
* 数字
* `_`
* `-`

---

## 6. 值语法

本规范定义两种值解析模式。

# 6.1 规范模式（Canonical Mode）

在规范模式下，`value` **MUST** 是一个合法 JSON 文本，且表示一个完整 JSON 值：

* string：`"alice"`
* number：`18`
* boolean：`true`
* null：`null`
* object：`{"k":"v"}`
* array：`["a","b"]`

示例：

```bash
--name '"alice"'
--age 18
--enabled true
--tags '["api","beta"]'
```

### 特点

* 无歧义
* 最利于实现互通
* 适合自动生成参数
* 人手输入略繁琐

---

# 6.2 友好模式（Friendly Mode）

在友好模式下，值按以下顺序解释：

1. 若值是合法 JSON 对象文本，解析为 object
2. 若值是合法 JSON 数组文本，解析为 array
3. 若值是 `true` / `false` / `null`，解析为对应 JSON 值
4. 若值是合法 JSON number，解析为 number
5. 否则解析为 string

例如：

```bash
--name alice
--age 18
--enabled true
--tags[] api
--meta '{"region":"us-east-1"}'
```

得到：

```json
{
  "name": "alice",
  "age": 18,
  "enabled": true,
  "tags": ["api"],
  "meta": {"region": "us-east-1"}
}
```

### 歧义处理

若希望把这些内容当作字符串而不是其他 JSON 类型，必须写成 JSON string：

```bash
--v '"true"'
--x '"18"'
--empty '""'
--raw '"[1,2]"'
```

对应：

```json
{
  "v": "true",
  "x": "18",
  "empty": "",
  "raw": "[1,2]"
}
```

---

## 7. 布尔值简写

为了便于手输，建议支持以下布尔简写，但它们不是最强约束的互通形式。

## 7.1 `--flag`

当某个参数不带值时，可解释为：

```json
true
```

例如：

```bash
--enabled
```

等价于：

```bash
--enabled true
```

## 7.2 `--no-flag`

可选支持 `--no-<path>`，表示：

```json
false
```

例如：

```bash
--no-enabled
```

等价于：

```bash
--enabled false
```

### 兼容性建议

为了避免与真实键名 `no-*` 冲突：

* 生产端 **SHOULD NOT** 把 `--no-...` 作为唯一互通写法
* 规范互通写法仍推荐：

```bash
--enabled false
```

---

## 8. JSON 到 CLI 的映射规则

## 8.1 根对象

当 JSON 根节点为 object 时，按以下规则递归展开。

### object

对象的每个字段映射为一个子路径。

例如：

```json
{
  "user": {
    "name": "alice",
    "age": 18
  }
}
```

映射为：

```bash
--user.name alice --user.age 18
```

### array

数组有两种标准映射方式：

#### 方式 A：追加形式

适用于顺序输入、人工输入更方便的场景。

```json
{
  "tags": ["a", "b"]
}
```

映射为：

```bash
--tags[] a --tags[] b
```

#### 方式 B：下标形式

适用于部分更新、对象数组、需要稳定定位的场景。

```bash
--tags[0] a --tags[1] b
```

### scalar

标量值直接映射为：

```bash
--path value
```

---

## 8.2 根数组和根标量

CLI 参数天然更适合承载根对象。为了支持任意 JSON 根类型，本规范保留一个特殊根路径：

```text
@
```

### 根数组

```json
[1, 2, 3]
```

CLI：

```bash
--@[] 1 --@[] 2 --@[] 3
```

或：

```bash
--@ '[1,2,3]'
```

### 根对象

```json
{"a":1}
```

CLI：

```bash
--@ '{"a":1}'
```

### 根字符串

```json
"hello"
```

CLI：

```bash
--@ '"hello"'
```

### 保留规则

路径段 `@` 为保留根标识。
若业务键名真的需要使用 `@`，应写为：

```bash
'--["@"]' value
```

---

## 9. 反向解析规则（CLI → JSON）

解析器按**从左到右**顺序处理参数。

## 9.1 容器创建规则

当路径包含对象段时，自动创建 object。
当路径包含数组段时，自动创建 array。

例如：

```bash
--db.host localhost
```

会创建：

```json
{"db":{"host":"localhost"}}
```

例如：

```bash
--items[0].id 1
```

会创建：

```json
{"items":[{"id":1}]}
```

---

## 9.2 数组扩容

当使用显式下标时，如果下标超出当前数组长度，解析器 **MUST** 自动扩容。

未赋值的位置用 `null` 填充。

例如：

```bash
--items[2] x
```

结果：

```json
{
  "items": [null, null, "x"]
}
```

---

## 9.3 追加语义

`--path[] value` 表示向数组尾部追加一个元素。

例如：

```bash
--tags[] a --tags[] b
```

结果：

```json
{"tags":["a","b"]}
```

---

## 9.4 覆盖与合并

当同一路径被多次设置时，按**后出现覆盖先出现**处理。

例如：

```bash
--name alice --name bob
```

结果：

```json
{"name":"bob"}
```

当先设置子树，再设置其后代，后代覆盖局部字段：

```bash
--db '{"host":"a","port":5432}' --db.port 5433
```

结果：

```json
{"db":{"host":"a","port":5433}}
```

反之，若后面再次给整个子树赋值，则整个子树替换之前内容：

```bash
--db.host a --db.port 5432 --db '{"x":1}'
```

结果：

```json
{"db":{"x":1}}
```

### 原则

* 后写优先
* 路径精确生效
* 冲突时以后者替换前者

---

## 10. 空值表示

## 10.1 null

```bash
--deleted null
```

对应：

```json
{"deleted":null}
```

## 10.2 空字符串

必须显式写成 JSON string：

```bash
--name '""'
```

对应：

```json
{"name":""}
```

## 10.3 空数组

推荐直接写内联 JSON：

```bash
--tags '[]'
```

对应：

```json
{"tags":[]}
```

## 10.4 空对象

```bash
--meta '{}'
```

对应：

```json
{"meta":{}}
```

---

## 11. 推荐的互通级别

为了兼顾“好输入”和“兼容性”，推荐把实现分成两个层级。

## 11.1 最低互通要求

解析器 **MUST** 支持：

* 路径语法：`.`、`[N]`、`[]`、`["..."]`
* 规范模式值解析
* `--key value`
* 后写覆盖前写
* 数组自动扩容
* 内联 JSON 子树值

## 11.2 用户友好增强

解析器 **SHOULD** 支持：

* `--key=value`
* 友好模式值解析
* `--flag` → `true`
* `--no-flag` → `false`
* 对追加数组和值类型的宽容解析

---

## 12. 推荐生成规则（机器输出）

当工具、SDK、脚本自动把 JSON 输出为 CLI 参数时，建议：

### 12.1 默认策略

* 简单字符串：可用友好模式
* 有歧义的字符串：用 JSON string
* 复杂对象/数组：优先拆分；必要时用局部内联 JSON
* 值以 `-` 开头：优先 `--key=value`

### 12.2 例子

JSON：

```json
{
  "name": "alice",
  "debug": true,
  "count": -1,
  "note": "true",
  "tags": ["a", "b"],
  "db": {"host": "127.0.0.1", "port": 5432}
}
```

推荐输出：

```bash
--name alice \
--debug true \
--count=-1 \
--note '"true"' \
--tags[] a \
--tags[] b \
--db.host 127.0.0.1 \
--db.port 5432
```

---

## 13. 兼容性建议

## 13.1 本规范定义的是 argv 语义，不是 shell 转义语义

shell 的引号规则不同：

* POSIX sh/bash/zsh
* PowerShell
* Windows cmd

并不一致。

因此本规范建议：

* 文档层描述 **参数 token 的语义**
* 各语言实现或 CLI 文档再分别给出 shell 示例

### 例子

下面两行在语义上都表示把 JSON string `"true"` 传给参数 `note`：

POSIX 风格：

```bash
--note '"true"'
```

PowerShell 中可能也可写：

```powershell
--note '"true"'
```

但具体外层转义仍应由实现文档单独说明。

---

## 13.2 不建议把整段 JSON 永远塞进一个参数

虽然下面是合法的：

```bash
--payload '{"a":1,"b":[2,3]}'
```

但作为通用交互形式，不如拆分更友好：

```bash
--payload.a 1 --payload.b[] 2 --payload.b[] 3
```

因此建议：

* **局部复杂值** 可用内联 JSON
* **整体输入** 优先使用展开路径

---

## 13.3 不依赖 schema

本规范在无 schema 下可工作。
若实现方有 schema，可以进一步增强：

* 更精确的类型推断
* 更好的错误提示
* 更严格的布尔、数字、枚举校验

但 schema 不是本规范的前提。

---

## 14. 错误处理建议

解析器遇到以下情况时，应报错：

### 14.1 非法路径

例如：

```bash
--items[abc] x
```

### 14.2 路径冲突无法兼容

例如先把某路径当标量，后又要求其为容器，且实现不支持后写替换时，应报错。
更推荐的做法是支持“后写替换”。

### 14.3 非法 JSON 字面量

在规范模式下，值不是合法 JSON。

### 14.4 `[]` 用法不合法

例如：

```bash
--users[].name alice
```

实现可拒绝，也可作为扩展支持；规范层面不推荐依赖该形式。

---

## 15. 规范示例

## 15.1 基本对象

JSON：

```json
{
  "name": "alice",
  "age": 18
}
```

CLI：

```bash
--name alice --age 18
```

---

## 15.2 嵌套对象

JSON：

```json
{
  "db": {
    "host": "localhost",
    "port": 5432
  }
}
```

CLI：

```bash
--db.host localhost --db.port 5432
```

---

## 15.3 布尔值

JSON：

```json
{
  "enabled": true,
  "dryRun": false
}
```

CLI 推荐互通写法：

```bash
--enabled true --dryRun false
```

友好写法：

```bash
--enabled --no-dryRun
```

---

## 15.4 标量数组

JSON：

```json
{
  "tags": ["api", "beta"]
}
```

CLI：

```bash
--tags[] api --tags[] beta
```

---

## 15.5 对象数组

JSON：

```json
{
  "users": [
    {"name": "alice", "age": 18},
    {"name": "bob", "age": 20}
  ]
}
```

CLI 写法 A：

```bash
--users[] '{"name":"alice","age":18}' \
--users[] '{"name":"bob","age":20}'
```

CLI 写法 B：

```bash
--users[0].name alice --users[0].age 18 \
--users[1].name bob   --users[1].age 20
```

---

## 15.6 特殊键名

JSON：

```json
{
  "labels": {
    "app.kubernetes.io/name": "demo"
  }
}
```

CLI：

```bash
'--labels["app.kubernetes.io/name"]' demo
```

---

## 15.7 字符串 `"true"` 与布尔 `true`

JSON：

```json
{
  "a": true,
  "b": "true"
}
```

CLI：

```bash
--a true --b '"true"'
```

---

## 15.8 空值

JSON：

```json
{
  "s": "",
  "a": [],
  "o": {},
  "n": null
}
```

CLI：

```bash
--s '""' --a '[]' --o '{}' --n null
```

---

## 16. 推荐摘要

这个规范最终可以压缩成下面几条：

1. **对象用点号**：`--db.host localhost`
2. **数组用下标或追加**：

   * `--tags[] api`
   * `--users[0].name alice`
3. **复杂键名用 `["..."]`**
4. **值优先支持两种模式**：

   * 规范模式：值是 JSON 字面量
   * 友好模式：常见字符串可直接手输
5. **复杂子树允许直接写内联 JSON**
6. **后写覆盖前写**
7. **不使用 base64，不依赖文件**

---

## 17. 一版更短的“推荐落地规则”

要真正落地，最推荐直接采用这一组：

* 参数名：只用长参数 `--path`
* 路径：

  * 对象：`.`
  * 数组下标：`[N]`
  * 数组追加：`[]`
  * 特殊键：`["..."]`
* 值：

  * 解析器同时支持规范模式与友好模式
  * 文档默认用友好模式举例
  * 自动生成参数时，歧义值输出成 JSON string
* 布尔：

  * 规范互通：`--flag true|false`
  * 友好增强：支持 `--flag` / `--no-flag`
* 空数组/空对象/空串：

  * `[]`
  * `{}`
  * `""`
