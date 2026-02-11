# 术语表

## A

**AnyItem**
`waitAnyNext` 返回的结果结构，包含任务索引和消息。

## C

**CommandMeta**
命令元数据，描述一个命令的名称、参数、返回值等。

**Console 模式**
Driver 的命令行交互模式，用于调试和独立使用。

**Constraints**
字段约束条件，如数值范围、字符串长度等。

**ConsoleBridge**
JS 引擎的 console 对象桥接，将 `console.log` 等调用映射到 Qt 日志系统。

## D

**Driver**
工作进程，接收 Host 的请求并执行任务。

**DriverCore**
Driver 端的核心类，处理通信和命令分发。

**DriverMeta**
驱动元数据，描述 Driver 的完整能力。

**DriverManagerScanner**
Manager 层的 Driver 扫描器，在核心库 `DriverScanner` 基础上补充 `.failed` 隔离策略、meta 自动导出和手动重扫能力。

**config.schema.json**
服务目录中的配置 schema 文件，声明配置字段的类型和约束，复用 FieldMeta 类型系统。

## F

**FieldMeta**
字段元数据，描述单个参数的类型和约束。

**FieldType**
字段类型枚举，如 String、Int、Bool 等。

## H

**Host**
主控进程，负责启动和管理 Driver。

## I

**ICommandHandler**
命令处理器接口。

**IMetaCommandHandler**
支持元数据的命令处理器接口。

**IResponder**
响应输出接口。

**Instance**
一个正在运行的 `stdiolink_service` 子进程，由 `InstanceManager` 创建和管理。

**InstanceManager**
管理 Instance 生命周期的组件，负责子进程的创建、监控、终止和日志重定向。

## J

**JSONL**
JSON Lines 格式，每行一个 JSON 对象。

**JsEngine**
QuickJS-NG 引擎的 RAII 封装，管理 JS 运行时和上下文的生命周期。

**JsTaskScheduler**
JS Service 的并发调度器，基于 `waitAnyNext` 实现单线程多任务调度。

## M

**Message**
响应消息结构。

**MetaValidator**
参数验证器。

**ModuleLoader**
ES Module 加载器，处理内置模块拦截和文件模块的路径解析与加载。

## O

**openDriver**
JS Service 的 Driver 工厂函数，启动进程、获取元数据并返回 Proxy 代理对象。

## P

**Project**
对某个 Service 的一次实例化配置，包含业务参数（config）和调度策略（schedule）。存储为 `projects/{id}.json` 文件。

**ProjectManager**
管理 Project 配置文件的 CRUD 和 Schema 验证的组件。

**Proxy（Driver Proxy）**
`openDriver()` 返回的代理对象，将 Driver 命令映射为异步方法调用。

## S

**Schedule**
Project 的调度策略配置，支持 manual（手动）、fixed_rate（定时）、daemon（守护）三种类型。

**ScheduleEngine**
调度引擎，根据 Project 的 Schedule 配置自动编排 Instance 的启停。

**ServerManager**
`stdiolink_server` 的编排层，统一持有所有子系统引用，协调初始化、调度和关闭流程。

**Service**
一个 JS Service 目录模板，包含 `manifest.json`、`config.schema.json`、`index.js`。由 `ServiceScanner` 自动发现。

**ServiceScanner**
扫描 `services/` 目录，发现并加载所有可用 Service 模板的组件。

**ServiceConfigSchema**
JS Service 的配置 schema 结构，复用 FieldMeta 描述配置字段。

**Stdio 模式**
标准 IPC 模式，通过 stdin/stdout 通信。

**stdiolink_server**
服务管理器，提供 Service 编排、Project 管理、Instance 生命周期管理、调度引擎和 HTTP API。

**stdiolink_service**
基于 QuickJS-NG 的 JS Service 运行时，使用 JS 脚本编排 Driver 调用。

## T

**Task**
请求句柄，用于获取响应。

## U

**UIHint**
UI 渲染提示。

**UiGenerator**
UI 表单生成器。
