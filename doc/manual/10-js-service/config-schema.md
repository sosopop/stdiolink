# 配置系统

stdiolink_service 提供类型安全的配置参数机制，通过 `config.schema.json` 声明 schema，通过 `getConfig()` 读取配置值。

## 基本用法

在服务目录中创建 `config.schema.json`：

```json
{
    "port": {
        "type": "int",
        "required": true,
        "description": "监听端口",
        "constraints": { "min": 1, "max": 65535 }
    },
    "debug": {
        "type": "bool",
        "default": false,
        "description": "启用调试模式"
    }
}
```

在 `index.js` 中读取配置：

```js
import { getConfig } from 'stdiolink';

const config = getConfig();
console.log(config.port);   // 8080（命令行传入）
console.log(config.debug);  // false（默认值）
```

## 字段描述

每个配置字段支持以下属性：

| 属性 | 类型 | 说明 |
|------|------|------|
| `type` | `string` | 字段类型（必填） |
| `required` | `boolean` | 是否必填 |
| `default` | `any` | 默认值 |
| `description` | `string` | 字段描述 |
| `constraints` | `object` | 约束条件 |
| `items` | `object` | 数组元素 schema（仅 array 类型） |

## 支持的字段类型

| type 值 | 命令行格式 | 示例 |
|---------|-----------|------|
| `string` | 原始字符串 | `--config.name=hello` |
| `int` | 整数字面量 | `--config.port=8080` |
| `int64` | 64位整数 | `--config.id=9007199254740991` |
| `double` | 浮点字面量 | `--config.ratio=0.5` |
| `bool` | `true`/`false` | `--config.debug=true` |
| `enum` | 枚举值字符串 | `--config.mode=fast` |
| `array` | JSON 数组 | `--config.tags=[1,2,3]` |
| `object` | JSON 对象 | `--config.opts={"a":1}` |
| `any` | 任意 JSON | `--config.extra={"k":1}` |

## 约束条件

`constraints` 对象支持以下属性：

| 属性 | 适用类型 | 说明 |
|------|---------|------|
| `min` | int/int64/double | 最小值 |
| `max` | int/int64/double | 最大值 |
| `minLength` | string | 最小长度 |
| `maxLength` | string | 最大长度 |
| `pattern` | string | 正则表达式 |
| `enumValues` | enum | 允许的枚举值列表 |
| `minItems` | array | 最小元素数 |
| `maxItems` | array | 最大元素数 |

## 配置来源与优先级

配置值按以下优先级合并（高到低）：

| 优先级 | 来源 | 示例 |
|--------|------|------|
| 1 | 命令行参数 | `--config.port=8080` |
| 2 | 配置文件 | `--config-file=config.json` |
| 3 | Schema 默认值 | `config.schema.json` 中的 `default` |

合并规则：object 类型深合并，array/scalar 整值覆盖。

## 命令行用法

```bash
# 直接传入配置
stdiolink_service ./my_service --config.port=8080 --config.name=myService

# 使用配置文件
stdiolink_service ./my_service --config-file=config.json

# 配置文件 + 命令行覆盖
stdiolink_service ./my_service --config-file=config.json --config.debug=true
```

### 嵌套路径

支持点号分隔的嵌套路径：

```bash
--config.server.host=localhost
--config.server.port=3000
```

对应 `config.schema.json`：

```json
{
    "server": {
        "type": "object",
        "fields": {
            "host": { "type": "string", "default": "localhost" },
            "port": { "type": "int", "required": true }
        }
    }
}
```

## Schema 导出

导出配置 schema 为 JSON 格式：

```bash
stdiolink_service ./my_service --dump-config-schema
```

输出示例：

```json
{
    "fields": [
        {
            "name": "port",
            "type": "int",
            "required": true,
            "description": "监听端口",
            "constraints": { "min": 1, "max": 65535 }
        }
    ]
}
```

## 配置帮助

当服务目录包含配置 schema 时，`--help` 会自动展示配置项帮助：

```bash
stdiolink_service ./my_service --help
```

输出示例：

```
Options:
  -h, --help                Show this help
  -v, --version             Show version
      --config.key=value    Set config value
      --config-file=<path>  Load config from JSON file
      --dump-config-schema  Dump config schema and exit

Config:
  --config.port <int> [required]
      监听端口
      Range: 1-65535
  --config.debug <bool>
      启用调试模式
      Default: false
```

## 错误处理

| 场景 | 行为 |
|------|------|
| 必填字段缺失 | stderr 报错，退出码 1 |
| 类型不匹配 | stderr 报错，含字段名和期望类型，退出码 1 |
| 约束校验失败 | stderr 报错，含字段名和约束描述，退出码 1 |
| 配置文件不存在 | stderr 报错，退出码 2 |
| JSON 格式错误 | stderr 报错，退出码 2 |
| `config.schema.json` 缺失 | stderr 报错，退出码 2 |

## getConfig() 行为

- `getConfig()` 返回只读（冻结）的合并后配置对象
- 多次调用 `getConfig()` 返回同一对象
- 未注入配置时返回空对象 `{}`
