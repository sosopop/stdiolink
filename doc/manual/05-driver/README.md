# Driver 端概述

Driver 是 stdiolink 中的工作进程，负责接收请求、执行任务并返回结果。

## 核心组件

| 组件 | 文件 | 说明 |
|------|------|------|
| DriverCore | `driver_core.h` | 核心运行时 |
| ICommandHandler | `icommand_handler.h` | 命令处理器接口 |
| IMetaCommandHandler | `meta_command_handler.h` | 带元数据的处理器 |
| IResponder | `iresponder.h` | 响应输出接口 |
| MetaBuilder | `meta_builder.h` | 元数据构建器 |

## 开发流程

1. 实现 `ICommandHandler` 或 `IMetaCommandHandler`
2. 创建 `DriverCore` 实例
3. 设置处理器和运行模式
4. 调用 `run()` 启动

## 详细文档

- [DriverCore 核心类](driver-core.md)
- [命令处理器接口](command-handler.md)
- [响应器接口](responder.md)
- [元数据 Builder API](meta-builder.md)
