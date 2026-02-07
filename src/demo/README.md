# stdiolink Demo 程序

本目录包含 stdiolink 库的完整功能演示程序。

## 目录结构

```
demo/
├── demo_host/              # 综合 Host 演示程序
├── calculator_driver/      # 计算器 Driver
├── file_processor_driver/  # 文件处理 Driver
├── device_simulator_driver/# 设备模拟 Driver
└── js_runtime_demo/         # JS runtime (M21-M27) 演示脚本
```

## Demo 说明

### demo_host

综合 Host 演示程序，展示以下功能：

1. **基本使用** - Driver 启动、请求、响应
2. **事件流** - 批量计算与进度反馈
3. **多 Driver 并发** - waitAnyNext 使用
4. **元数据查询** - queryMeta 使用
5. **UI 表单生成** - UiGenerator 使用
6. **配置注入** - ConfigInjector 使用
7. **版本检查** - MetaVersionChecker 使用

### calculator_driver

计算器 Driver，演示：

- 多种数学运算命令 (add, subtract, multiply, divide, power)
- 数值约束验证 (range)
- 批量计算与事件流 (batch)
- 统计计算 (statistics)
- 数组参数

### file_processor_driver

文件处理 Driver，演示：

- 字符串约束 (minLength, maxLength)
- 枚举类型参数
- 数组参数
- 事件流 (search 命令)
- UI 提示 (placeholder, group)

### device_simulator_driver

设备模拟 Driver，演示：

- 正则表达式验证 (IP 地址)
- 枚举类型参数
- 配置模式 (ConfigSchema)
- 配置注入 (startupArgs)
- 事件流 (scan 命令)

### js_runtime_demo

`stdiolink_service` 的综合脚本演示，覆盖 M21-M27：

- JS 引擎与 `console.*` 输出桥接
- ES Module 相对/父级导入
- `Driver` / `Task` 绑定
- `openDriver` 代理与调度行为
- `exec` 进程调用
- `--export-doc=ts` 类型声明导出

## 运行方式

```bash
# 运行综合演示
./build_ninja/src/demo/demo_host/demo_host.exe

# 单独测试 Driver (Console 模式)
./build_ninja/src/demo/calculator_driver/calculator_driver.exe --help
./build_ninja/src/demo/calculator_driver/calculator_driver.exe add --a=10 --b=20

# 运行 JS runtime 综合演示（构建后服务目录会复制到 bin/js_runtime_demo）
./build_ninja/bin/stdiolink_service.exe ./build_ninja/bin/js_runtime_demo/services/basic_demo
```

## 功能覆盖

| 功能 | 演示位置 |
|------|----------|
| DriverCore | 所有 Driver |
| IMetaCommandHandler | 所有 Driver |
| MetaBuilder API | 所有 Driver |
| Driver 类 | demo_host |
| Task 句柄 | demo_host |
| waitAnyNext | demo_host |
| queryMeta | demo_host |
| UiGenerator | demo_host |
| ConfigInjector | demo_host |
| MetaVersionChecker | demo_host |
| 事件流 | calculator, file_processor, device_simulator |
| 参数验证 | 所有 Driver |
| Console 模式 | 所有 Driver |
