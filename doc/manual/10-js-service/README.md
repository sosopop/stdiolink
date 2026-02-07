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
│            stdiolink 内置模块                 │
│  Driver / Task / openDriver / exec          │
│  defineConfig / getConfig                   │
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
stdiolink_service <script.js> [options]
```

脚本以 ES Module 模式执行，支持 `import`/`export` 语法。

## 命令行选项

| 选项 | 说明 |
|------|------|
| `-h, --help` | 显示帮助（有脚本时同时显示配置项帮助） |
| `-v, --version` | 显示版本 |
| `--config.key=value` | 设置配置值 |
| `--config-file=<path>` | 从 JSON 文件加载配置 |
| `--dump-config-schema` | 导出配置 schema 并退出 |

## stdiolink 模块导出

```js
import {
    Driver,         // Driver 类（底层 API）
    openDriver,     // Proxy 工厂函数（推荐）
    exec,           // 外部进程执行
    defineConfig,   // 声明配置 schema
    getConfig       // 读取配置值
} from 'stdiolink';
```

## 本章内容

- [快速入门](getting-started.md) - 第一个 JS Service 脚本
- [模块系统](module-system.md) - ES Module 加载与内置模块
- [Driver/Task 绑定](driver-binding.md) - 底层 Driver 和 Task API
- [进程调用](process-binding.md) - exec() 外部进程执行
- [Proxy 代理与并发调度](proxy-and-scheduler.md) - openDriver() 与异步调用
- [配置系统](config-schema.md) - defineConfig/getConfig 配置管理
