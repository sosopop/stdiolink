# JS Service 运行时

`stdiolink_service` 是基于 QuickJS-NG 引擎的 JavaScript 运行时，允许使用 JS 脚本编排 Driver 调用、执行外部进程、管理配置参数。

## 概述

JS Service 运行时将 stdiolink 的核心能力暴露为 JS API，使开发者无需编写 C++ 代码即可：

- 启动和管理 Driver 进程
- 通过 Proxy 代理以异步方式调用 Driver 命令
- 执行外部进程并获取输出
- 声明类型安全的配置参数

## 架构

```
┌─────────────────────────────────────────────┐
│              JS 脚本 (ES Module)             │
│  import { openDriver, exec, ... }           │
├─────────────────────────────────────────────┤
│              内置模块                        │
│  stdiolink        主模块（Driver/Task/      │
│                   openDriver/waitAny/exec/  │
│                   getConfig）               │
│  stdiolink/constants  系统信息与路径常量      │
│  stdiolink/path       路径操作              │
│  stdiolink/fs         文件系统              │
│  stdiolink/time       时间与 sleep          │
│  stdiolink/http       HTTP 客户端           │
│  stdiolink/log        结构化日志            │
│  stdiolink/process    异步进程（execAsync/   │
│                       spawn）               │
├─────────────────────────────────────────────┤
│           QuickJS-NG 引擎                    │
│  JsEngine / ModuleLoader / ConsoleBridge    │
├─────────────────────────────────────────────┤
│           stdiolink 核心库 (C++)             │
│  Driver / Task / waitAnyNext / FieldMeta    │
└─────────────────────────────────────────────┘
```

## 运行方式

```bash
stdiolink_service <service_dir> [options]
```

脚本以 ES Module 模式执行，支持 `import`/`export` 语法。

## 命令行选项

| 选项 | 说明 |
|------|------|
| `-h, --help` | 显示帮助（有服务目录时同时显示配置项帮助） |
| `-v, --version` | 显示版本 |
| `--config.key=value` | 设置配置值 |
| `--config-file=<path>` | 从 JSON 文件加载配置（`-` 表示 stdin） |
| `--dump-config-schema` | 导出配置 schema 并退出 |

## 模块总览

### stdiolink（主模块）

```js
import {
    Driver,         // Driver 类（底层 API）
    openDriver,     // Proxy 工厂函数（推荐）
    waitAny,        // 多 Task 多路监听（保留 event）
    exec,           // 同步外部进程执行
    getConfig       // 读取配置值
} from 'stdiolink';
```

### 扩展模块

```js
import { SYSTEM, APP_PATHS } from 'stdiolink/constants';  // 系统信息与路径常量
import { join, resolve, dirname, basename, extname, normalize, isAbsolute } from 'stdiolink/path';  // 路径操作
import { exists, readText, writeText, readJson, writeJson, mkdir, listDir, stat } from 'stdiolink/fs';  // 文件系统
import { nowMs, monotonicMs, sleep } from 'stdiolink/time';  // 时间与非阻塞 sleep
import { request, get, post } from 'stdiolink/http';  // 异步 HTTP 客户端
import { createLogger } from 'stdiolink/log';  // 结构化日志
import { execAsync, spawn } from 'stdiolink/process';  // 异步进程执行
```

## 本章内容

- [快速入门](getting-started.md) - 第一个 JS Service 脚本
- [模块系统](module-system.md) - ES Module 加载与内置模块
- [Driver/Task 绑定](driver-binding.md) - 底层 Driver 和 Task API
- [进程调用](process-binding.md) - exec() 同步外部进程执行
- [Proxy 代理与并发调度](proxy-and-scheduler.md) - openDriver() 与异步调用
- [多路事件监听](wait-any-binding.md) - waitAny() 异步多 Task 监听
- [配置系统](config-schema.md) - config.schema.json 与 getConfig 配置管理
- [常量模块](constants-binding.md) - SYSTEM 系统信息与 APP_PATHS 路径常量
- [路径操作](path-binding.md) - join/resolve/dirname/basename 等路径函数
- [文件系统](fs-binding.md) - 文件读写、目录操作、JSON 读写
- [时间模块](time-binding.md) - 时间获取与非阻塞 sleep
- [HTTP 客户端](http-binding.md) - 异步 HTTP request/get/post
- [结构化日志](log-binding.md) - createLogger 与 Logger API
- [异步进程](process-async-binding.md) - execAsync/spawn 异步进程执行
