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
- [进程调用](10-js-service/process-binding.md) - exec() 同步外部进程执行
- [Proxy 代理与并发调度](10-js-service/proxy-and-scheduler.md) - openDriver() 与异步调用
- [多路事件监听](10-js-service/wait-any-binding.md) - waitAny() 异步多 Task 监听
- [配置系统](10-js-service/config-schema.md) - config.schema.json 与 getConfig 配置管理
- [常量模块](10-js-service/constants-binding.md) - SYSTEM 系统信息与 APP_PATHS 路径常量
- [路径操作](10-js-service/path-binding.md) - join/resolve/dirname/basename 等路径函数
- [文件系统](10-js-service/fs-binding.md) - 文件读写、目录操作、JSON 读写
- [时间模块](10-js-service/time-binding.md) - 时间获取与非阻塞 sleep
- [HTTP 客户端](10-js-service/http-binding.md) - 异步 HTTP request/get/post
- [结构化日志](10-js-service/log-binding.md) - createLogger 与 Logger API
- [异步进程](10-js-service/process-async-binding.md) - execAsync/spawn 异步进程执行

### Server 管理器（stdiolink_server）

- [Server 概述](11-server/README.md) - 管控面架构、生命周期模型、子系统总览
- [快速入门](11-server/getting-started.md) - 启动配置、目录结构、命令行参数
- [Service 扫描](11-server/service-scanner.md) - Service 目录约定与扫描机制
- [Driver 扫描](11-server/driver-scanner.md) - Driver 元数据导出与失败隔离
- [Project 管理](11-server/project-management.md) - Project 配置格式、验证、调度参数
- [Instance 与调度](11-server/instance-and-schedule.md) - 实例生命周期与三种调度策略
- [HTTP API 参考](11-server/http-api.md) - 完整的 REST API 接口文档

### WebUI 管理前端

- [WebUI 概述](12-webui/README.md) - 技术栈、设计风格、架构概览
- [开发与构建](12-webui/getting-started.md) - 环境搭建、开发服务器、生产构建与 Server 集成部署
- [页面功能](12-webui/pages.md) - Dashboard、Services、Projects、Instances、Drivers
- [DriverLab 交互调试](12-webui/driverlab.md) - WebSocket 驱动的实时调试工具
- [实时通信](12-webui/realtime.md) - SSE 事件流与 WebSocket 协议
- [主题与国际化](12-webui/theme-and-i18n.md) - 双主题系统与多语言支持

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
