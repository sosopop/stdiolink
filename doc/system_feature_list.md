# StdioLink 系统完整功能列表

## 1. 平台基础能力

- 基于 `stdin/stdout + JSONL` 的跨进程通信框架
- 使用 UTF-8 文本协议，便于调试、日志采集和跨语言集成
- 统一请求/响应模型：`cmd + data`，`event|done|error`
- 支持流式事件输出与最终完成态分离
- 支持 Driver 进程隔离，降低崩溃和阻塞对主进程的影响
- 支持 `OneShot` / `KeepAlive` 两种 Driver 生命周期
- 支持 Console 模式与 StdIO 模式双运行形态
- 支持跨平台运行，覆盖 Windows 与 Unix 构建/运行链路

## 2. 协议与元数据能力

- Driver 内置 `meta.describe` 自描述导出
- 统一 Driver 元数据模型：命令、参数、返回值、事件、配置 schema
- 支持字段类型、必填约束、默认值、描述信息建模
- 支持参数校验与默认值填充
- 支持配置 schema 校验
- 支持基于元数据生成表单描述
- 支持基于元数据生成帮助信息与文档导出
- 支持命令别名、无参命令、省略 `params` 等元数据消费场景

## 3. Driver 开发与运行能力

- 提供 `DriverCore` 作为 Driver 运行时骨架
- 提供命令处理接口 `ICommandHandler`
- 提供带元数据的命令处理接口 `IMetaCommandHandler`
- 提供响应接口 `IResponder` 与标准输出响应实现
- 提供元数据构建器 `DriverMetaBuilder / CommandBuilder / FieldBuilder`
- 支持 Driver 输出帮助、示例与自动填充值
- 支持 Driver 可执行文件扫描与目录编目
- 支持 Driver 进程启动、停止、退出状态处理
- 支持 Driver 提前退出错误传播
- 支持 Driver 命令请求与异步消息读取

## 4. Host 侧编排能力

- 提供 Host 端 `Driver` 封装，统一管理 Driver 子进程
- 提供 `Task` 句柄，支持事件流与终态混合消费
- 支持单任务等待、轮询与错误传播
- 支持 `waitAny` 多任务并发等待
- 支持 Driver 元数据缓存
- 支持元数据版本检查
- 支持基于元数据的参数表单生成
- 支持配置注入与命令参数辅助拼装
- 支持 Driver 名称解析与运行时查找

## 5. QuickJS Service 运行时能力

- 内置 QuickJS 引擎执行 Service
- 支持 ES Module 形式组织业务脚本
- 支持从 `manifest.json + config.schema.json + index.js` 加载 Service
- 支持 `getConfig()` 读取 Service 配置
- 支持 `resolveDriver()` 查找 Driver
- 支持 `openDriver()` 以 keepalive 方式打开 Driver
- 支持底层 `new Driver()` 控制 Driver 生命周期
- 支持 JS 层 `Task`、调度器与 `waitAny()` 绑定
- 支持 JS 层把 Driver `error` 终态映射为异常

## 6. Service 内置绑定能力

- `stdiolink` 主模块：Driver、Task、调度、配置读取
- `stdiolink/constants` 常量绑定
- `stdiolink/path` 路径处理绑定
- `stdiolink/fs` 文件系统绑定
- `stdiolink/time` 时间绑定
- `stdiolink/http` HTTP 客户端绑定
- `stdiolink/log` 日志绑定
- `stdiolink/process` 进程执行与异步进程绑定
- `stdiolink/driver` Driver 查找扩展

## 7. Server 管控与编排能力

- 提供 `Service -> Project -> Instance` 三级生命周期管理
- 自动扫描 `data_root/services/` 中的 Service 模板
- 自动扫描 `data_root/drivers/` 中的 Driver 与元数据
- 加载、校验并管理 `data_root/projects/` 中的 Project
- 启动、终止、监控 `stdiolink_service` 实例进程
- 管理实例工作目录、日志与运行状态
- 支持手动触发 Project 执行
- 支持 `manual` 调度策略
- 支持 `fixed_rate` 固定频率调度策略
- 支持 `daemon` 常驻自动重启调度策略
- 支持 Project 更新、启停、删除、重载与启用状态切换
- 支持 Project 变更时的保存、停机、回滚与重新接管调度
- 支持同一 Project 的变更互斥控制
- 支持 Windows 单实例保护、托盘运行和 CLI 回退
- 支持按 `data_root` 自动定位运行时目录

## 8. Server HTTP / 实时接口能力

- 提供 Server 状态统计 API
- 提供 Services 列表、详情、创建、删除、扫描 API
- 提供 Projects 列表、详情、创建、更新、删除、启停、重载、启用切换 API
- 提供 Instances 列表、详情、终止、日志查询 API
- 提供 Drivers 列表、详情、元数据查询 API
- 提供静态文件服务
- 提供 Service 文件读取能力
- 提供全局 CORS 支持
- 提供 SSE 事件总线
- 提供 SSE 事件日志回放/推送
- 提供 DriverLab WebSocket 双向调试通道
- 支持浏览器通过 WebSocket 启动 Driver、执行命令、取消执行、接收实时输出

## 9. Web 管理台能力

- 提供 Dashboard 总览页
- 提供 Services 列表页与详情页
- 提供 Projects 列表页、创建页与详情页
- 提供 Instances 列表页与详情页
- 提供 Drivers 列表页与详情页
- 提供 DriverLab 在线调试页
- 提供 NotFound 路由兜底页
- 基于 REST API 获取服务、项目、实例、驱动数据
- 基于 SSE 刷新实例、调度、日志等实时状态
- 基于 WebSocket 驱动 DriverLab 实时交互
- 提供 SchemaForm / SchemaEditor 渲染配置与命令表单
- 提供命令参数校验、默认值回填、最近执行值复用
- 提供消息流查看、清空、自动滚动、取消执行等调试交互
- 提供代码编辑、文件树、日志查看、状态点等复用组件
- 提供多语言国际化支持
- 支持浅色/深色主题

## 10. 内置 Service 能力

- `bin_scan_orchestrator`：PLC 升降装置与智慧仓储扫描编排
- `bin_scan_orchestrator`：驱动 3DVision 登录、WebSocket 连接、事件订阅与扫描错误快速失败
- `exec_runner`：启动任意外部进程并实时输出日志
- `modbustcp_server_service`：提供 Modbus TCP 从站服务
- `modbusrtu_server_service`：提供 Modbus RTU over TCP 从站服务
- `modbusrtu_serial_server_service`：提供 Modbus RTU 串口从站服务
- `plc_crane_sim`：提供 PLC 升降装置仿真业务服务

## 11. 内置 Driver 能力

- `stdio.drv.3d_scan_robot`：三维扫描机器人通信与协议处理
- `stdio.drv.3dvision`：3DVision HTTP / WebSocket 通信
- `stdio.drv.limaco_1_radar`：Limaco 单雷达设备接入
- `stdio.drv.limaco_5_radar`：Limaco 多雷达设备接入
- `stdio.drv.modbusrtu`：Modbus RTU 通信客户端
- `stdio.drv.modbusrtu_serial`：Modbus RTU 串口客户端
- `stdio.drv.modbusrtu_server`：Modbus RTU over TCP 从站
- `stdio.drv.modbusrtu_serial_server`：Modbus RTU 串口从站
- `stdio.drv.modbustcp`：Modbus TCP 客户端
- `stdio.drv.modbustcp_server`：Modbus TCP 从站
- `stdio.drv.plc_crane`：PLC 升降装置控制
- `stdio.drv.plc_crane_sim`：PLC 升降装置仿真与状态机模拟
- `stdio.drv.pqw_analog_output`：品全微模拟量输出模块控制

## 12. 构建、测试与交付能力

- 支持 Windows `build.bat` 与 Unix `build.sh` 构建
- 支持 raw 构建产物与 runtime 运行时目录双层布局
- 支持 runtime 自动组装与发布目录同构
- 支持 GTest 单元/集成测试
- 支持 Smoke Test 端到端测试
- 支持 WebUI Vitest 测试
- 支持 WebUI Playwright E2E 测试
- 覆盖协议、元数据、Driver、Host、Service、Server、WebUI 多层测试
- 支持运行时目录、发布布局、日志与失败归因排查

## 13. 文档与开发支撑能力

- 提供知识库 `doc/knowledge/` 作为 AI/开发检索入口
- 提供 `doc/manual/` 详细手册
- 提供 HTTP API 文档
- 提供设备协议文档与需求文档
- 提供里程碑、待办与开发指南文档

