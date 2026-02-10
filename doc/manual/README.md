# stdiolink 用户手册

stdiolink 是一个基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 进行进程间通信。

## 目录

### 入门指南

- [项目介绍](01-introduction.md) - 核心概念、设计理念、适用场景
- [快速入门](02-quick-start.md) - 环境要求、安装、5分钟入门示例
- [架构概述](03-architecture.md) - 分层设计、数据流、组件关系

### 协议层

- [协议层概述](04-protocol/README.md) - 协议层设计与核心类型
- [JSONL 协议格式](04-protocol/jsonl-format.md) - 请求/响应格式、帧结构
- [元数据类型](04-protocol/meta-types.md) - FieldType、FieldMeta、CommandMeta 等
- [参数验证](04-protocol/validation.md) - MetaValidator、DefaultFiller

### Driver 端开发

- [Driver 端概述](05-driver/README.md) - Driver 端架构与开发流程
- [DriverCore 核心类](05-driver/driver-core.md) - 核心类 API 参考
- [命令处理器接口](05-driver/command-handler.md) - ICommandHandler、IMetaCommandHandler
- [响应器接口](05-driver/responder.md) - IResponder、StdioResponder
- [元数据 Builder API](05-driver/meta-builder.md) - FieldBuilder、CommandBuilder、DriverMetaBuilder

### Host 端开发

- [Host 端概述](06-host/README.md) - Host 端架构与开发流程
- [Driver 类](06-host/driver-class.md) - Driver 类 API 参考
- [Task 句柄](06-host/task.md) - Task 类 API 参考
- [多任务并行等待](06-host/wait-any.md) - waitAnyNext 函数
- [UI 表单生成器](06-host/form-generator.md) - UiGenerator、FormDesc

### Console 模式

- [Console 模式使用指南](07-console/README.md) - 命令行参数、交互模式

### JS Service 运行时

- [JS Service 概述](10-js-service/README.md) - JS 运行时架构与 API 总览
- [快速入门](10-js-service/getting-started.md) - 第一个 JS Service 脚本
- [模块系统](10-js-service/module-system.md) - ES Module 加载与内置模块
- [Driver/Task 绑定](10-js-service/driver-binding.md) - 底层 Driver 和 Task API
- [进程调用](10-js-service/process-binding.md) - exec() 外部进程执行
- [Proxy 代理与并发调度](10-js-service/proxy-and-scheduler.md) - openDriver() 与异步调用
- [多路事件监听](10-js-service/wait-any-binding.md) - waitAny() 异步多 Task 监听
- [配置系统](10-js-service/config-schema.md) - config.schema.json 与 getConfig 配置管理

### 参考资料

- [最佳实践](08-best-practices.md) - 设计模式、性能优化、安全建议
- [故障排除](09-troubleshooting.md) - 常见问题与解决方案
- [错误码参考](appendix/error-codes.md) - 标准错误码定义
- [术语表](appendix/glossary.md) - 专业术语解释

## 版本信息

- 文档版本：1.0
- 适用于 stdiolink 1.0.x

## 相关链接

- [CLAUDE.md](../../CLAUDE.md) - 项目开发指南
- [示例代码](../../src/demo/) - 完整示例程序
