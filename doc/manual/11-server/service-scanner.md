# Service 扫描

`ServiceScanner` 在 `stdiolink_server` 启动时自动扫描 `services/` 目录，发现并加载所有可用的 Service 模板。

## Service 目录结构

每个 Service 是 `services/` 下的一个子目录，必须包含以下文件：

```
services/
└── my-service/
    ├── manifest.json          # Service 描述文件（必需）
    ├── config.schema.json     # 配置 Schema 定义（必需）
    ├── index.js               # 入口脚本（必需）
    └── lib/                   # 可选的模块目录
        └── utils.js
```

三个必需文件缺少任何一个，该 Service 将被标记为加载失败并跳过。

## manifest.json

Service 的描述文件，定义基本信息：

```json
{
  "manifestVersion": "1",
  "id": "data-collector",
  "name": "数据采集服务",
  "version": "1.0.0"
}
```

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `manifestVersion` | string | 是 | 固定为 `"1"` |
| `id` | string | 是 | Service 唯一标识符 |
| `name` | string | 是 | 显示名称 |
| `version` | string | 是 | 语义化版本号 |

`id` 用于 Project 配置中的 `serviceId` 字段关联。

## config.schema.json

定义该 Service 接受的配置参数及其类型约束。Server 使用此 Schema 对 Project 的 `config` 字段进行验证。

Schema 文件为 JSON 对象，每个键是字段名，值是字段的类型定义：

```json
{
  "device": {
    "type": "object",
    "required": true,
    "description": "设备连接参数",
    "fields": {
      "host": { "type": "string", "required": true },
      "port": { "type": "int", "default": 502 }
    }
  },
  "polling": {
    "type": "object",
    "fields": {
      "intervalMs": { "type": "int", "default": 1000 },
      "registers": { "type": "array", "default": [] }
    }
  }
}
```

Schema 格式复用 stdiolink 核心库的 `FieldMeta` 类型系统，支持嵌套对象、数组、枚举、正则约束等。详见 [元数据类型](../04-protocol/meta-types.md)。

## 扫描机制

启动时 `ServiceScanner` 遍历 `services/` 下的每个子目录，依次执行：

1. 校验目录结构（`manifest.json` + `index.js` + `config.schema.json` 均存在）
2. 解析 `manifest.json`，提取 `id`、`name`、`version`
3. 解析 `config.schema.json`，构建内部 Schema 对象
4. 缓存原始 Schema JSON（供 HTTP API 原样返回）

扫描结果汇总为 `ServiceInfo` 集合，记录每个 Service 的加载状态。

### 扫描统计

扫描完成后输出统计信息：

```
Services: 3 loaded, 1 failed
```

| 计数器 | 说明 |
|--------|------|
| `scannedDirs` | 扫描的子目录总数 |
| `loadedServices` | 成功加载的 Service 数 |
| `failedServices` | 加载失败的 Service 数 |

### 错误处理

加载失败的 Service 会被跳过，不影响其他 Service 的正常加载。常见失败原因：

- 缺少 `manifest.json` 或格式非法
- `manifest.json` 缺少必需字段（`id`、`name`、`version`）
- 缺少 `index.js` 入口文件
- 缺少 `config.schema.json` 或 JSON 解析失败

失败信息通过 `qWarning` 输出到日志。

## 通过 API 查看

扫描结果可通过 HTTP API 查询：

```bash
# 列出所有 Service
curl http://127.0.0.1:8080/api/services

# 查看单个 Service 详情（含 Schema）
curl http://127.0.0.1:8080/api/services/data-collector
```

详见 [HTTP API 参考](http-api.md)。
