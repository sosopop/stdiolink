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
├── stdiolink_service/  # JS 脚本驱动的 Service 运行时
│   ├── bindings/       # QuickJS 绑定 (Driver、Task、Process、Config)
│   ├── config/         # 配置系统 (Schema、Args、Validator)
│   ├── engine/         # JS 引擎 (QuickJS 封装、ES Module 加载、Console)
│   ├── proxy/          # Driver 代理层
│   └── utils/          # 工具函数 (JS↔JSON 转换)
├── demo/               # 示例程序
│   ├── calculator_driver/        # 计算器 Driver 示例
│   ├── device_simulator_driver/  # 设备模拟器示例
│   ├── file_processor_driver/    # 文件处理器示例
│   ├── js_runtime_demo/          # JS 运行时示例
│   └── demo_host/                # Host 端使用示例
├── drivers/            # 实际 Driver 实现
│   ├── driver_3dvision/          # 3D 视觉 Driver
│   ├── driver_modbusrtu/         # Modbus RTU Driver
│   └── driver_modbustcp/         # Modbus TCP Driver
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
├── milestone/          # 里程碑文档 (M1-M28)
│   ├── milestone_01-06   # 基础功能 (协议、Driver、Host、Task、Console)
│   ├── milestone_07-11   # 元数据系统 (类型、描述、构建、验证、Host集成)
│   ├── milestone_12-20   # 高级功能 (双模式、CLI、文档、事件、UI、配置、版本、注册、自省)
│   └── milestone_21-28   # JS Service 运行时 (引擎、模块、绑定、代理、TS导出、集成测试、配置Schema)
├── other/              # 参考设计文档
├── design_review.md            # 设计评审
├── meta_system_design_v2.md    # 元数据系统设计
└── stdiolink_ipc_design.md     # IPC 设计文档
```

## 工具脚本

- `tools/pack_single_header.py`: 将 stdiolink 打包为单头文件。
- `tools/create_driver.py`: 创建新的 Driver 项目脚手架。
- `tools/run-clang-tidy.py`: 运行静态分析。
- `tools/tcp_bridge.py`: TCP 桥接工具。
- `tools/udp_bridge.py`: UDP 桥接工具。
