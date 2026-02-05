# 协议层概述

协议层是 stdiolink 的基础，定义了 Host 与 Driver 之间的通信格式。

## 核心组件

| 组件 | 文件 | 说明 |
|------|------|------|
| JsonlParser | `jsonl_parser.h` | JSONL 请求解析器 |
| JsonlSerializer | `jsonl_serializer.h` | JSONL 响应序列化器 |
| Request/Message | `jsonl_types.h` | 请求和响应数据结构 |
| MetaTypes | `meta_types.h` | 元数据类型定义 |
| MetaValidator | `meta_validator.h` | 参数验证器 |

## 通信流程

```
Host                           Driver
  │                               │
  │   Request (JSONL)             │
  │  ─────────────────────────▶   │
  │                               │ JsonlParser
  │                               │
  │   Response Header (JSONL)     │
  │  ◀─────────────────────────   │ JsonlSerializer
  │   Response Payload (JSONL)    │
  │  ◀─────────────────────────   │
  │                               │
```

## 详细文档

- [JSONL 协议格式](jsonl-format.md) - 请求/响应的具体格式
- [元数据类型](meta-types.md) - FieldMeta、CommandMeta 等类型定义
- [参数验证](validation.md) - MetaValidator 和 DefaultFiller
