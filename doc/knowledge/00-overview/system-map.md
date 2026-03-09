# System Map

## Purpose

统一理解 `stdiolink` 的层次、关键对象和模块边界。

## Key Conclusions

- 通信底座是 `stdin/stdout + JSONL`，协议在 `src/stdiolink/protocol/`。
- Driver 是独立进程，Host 用 `QProcess` 管理，隔离崩溃和阻塞风险。
- JS Service 不是协议层扩展，而是基于 Host 能力的 QuickJS 编排层。
- Server 管理的是 `Service -> Project -> Instance` 三级生命周期。
- WebUI 主要消费 Server 的 REST/SSE/WS，不直接碰 Driver 进程。

## Core Objects

- `DriverCore`：Driver 端运行时，负责解析请求、校验、调 handler、输出响应。
- `Driver`：Host 端子进程包装，负责启动、发请求、收消息、处理早退。
- `Task`：单次请求句柄，支持事件流和最终完成态。
- `DriverMeta`：命令、参数、返回、配置 schema 的统一自描述模型。
- `ServerManager`：组织扫描器、Project 管理、实例管理、调度引擎和 API。

## Main Source Entry

- Core: `src/stdiolink/{protocol,driver,host,console}/`
- Service runtime: `src/stdiolink_service/{bindings,config}/`
- Server: `src/stdiolink_server/{scanner,manager,http,model}/`
- WebUI: `src/webui/src/{api,pages,stores,components}/`

## Typical Flow

用户/API -> Host/Service -> `Driver` -> Driver 进程 -> `DriverCore` -> handler -> `event|done|error` -> Task/Proxy -> Server/WebUI

## Related

- `runtime-layout.md`
- `../01-protocol/message-model.md`
- `../05-server/server-lifecycle.md`
