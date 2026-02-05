# stdiolink 项目指南

基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 通信。

## 构建

```bash
build_ninja.bat                              # 首次构建
cmake --build build_ninja --parallel 8       # 增量构建
./build_ninja/bin/stdiolink_tests.exe        # 运行测试
```

## 代码规范

### Qt 类优先

- 文件 I/O：`QFile`（非 fopen/fread）
- 字符串：`QString`/`QByteArray`
- JSON：`QJsonDocument`/`QJsonObject`

### 命名

- 类：`CamelCase`，函数：`camelBack`，成员：`m_camelBack`

### 输出规范

| 内容 | 输出目标 |
|------|---------|
| JSONL 响应、导出数据 | stdout |
| 帮助、版本、错误信息 | stderr |
| Qt 日志 (qDebug等) | stderr 或 `--log=<file>` |

### Windows 注意

`fread()` 与 QProcess 管道不兼容，必须用 `QTextStream::readLine()` 逐行读取。

## 项目结构

### src 源码目录

```
src/
├── stdiolink/          # 核心库 (DLL)
│   ├── protocol/       # JSONL 协议层 - 序列化、解析、元数据类型
│   ├── driver/         # Driver 端 - DriverCore、响应器、元数据构建
│   ├── host/           # Host 端 - Driver 进程管理、Task、表单生成
│   ├── console/        # Console 模式 - 命令行参数、交互式响应
│   └── doc/            # 文档生成器 - Markdown/HTML/OpenAPI 导出
├── demo/               # 示例程序
│   ├── calculator_driver/        # 计算器 Driver 示例
│   ├── device_simulator_driver/  # 设备模拟器示例
│   ├── file_processor_driver/    # 文件处理器示例
│   └── demo_host/                # Host 端使用示例
├── driverlab/          # GUI 测试工具
│   ├── ui/             # 界面组件 (MainWindow, TestPage, Form...)
│   ├── models/         # 数据模型 (DriverSession, CommandHistory)
│   └── resources/      # 资源文件
└── tests/              # 单元测试
```

### doc 文档目录

```
doc/
├── manual/             # 用户手册
│   ├── 01-introduction.md      # 简介
│   ├── 02-quick-start.md       # 快速入门
│   ├── 03-architecture.md      # 架构概述
│   ├── 04-protocol/            # 协议详解
│   ├── 05-driver/              # Driver 开发指南
│   ├── 06-host/                # Host 开发指南
│   ├── 07-console/             # Console 模式
│   ├── 08-best-practices.md    # 最佳实践
│   ├── 09-troubleshooting.md   # 故障排除
│   └── appendix/               # 附录
├── milestone/          # 里程碑文档 (M1-M20)
│   ├── milestone_01-06   # 基础功能 (协议、Driver、Host、Task)
│   ├── milestone_07-11   # 元数据系统 (类型、描述、构建、验证)
│   └── milestone_12-20   # 高级功能 (双模式、CLI、文档、配置)
├── other/              # 参考设计文档
├── design_review.md            # 设计评审
├── meta_system_design_v2.md    # 元数据系统设计
└── stdiolink_ipc_design.md     # IPC 设计文档
```
