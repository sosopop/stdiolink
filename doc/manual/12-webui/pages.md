# 页面功能

## Dashboard 仪表盘

路径：`/dashboard`

Mission Control 风格的总览页面，展示系统运行状态：

- **KPI 卡片**：Service 数量、Project 数量、运行中 Instance 数量、Driver 数量
- **活跃项目网格**：展示前 4 个项目，支持一键启动/停止
- **实例列表**：当前运行中的 Instance 表格，支持终止操作
- **事件流**：通过 SSE 实时接收的系统事件（实例启停、调度触发等），最多保留 50 条
- **服务器状态指示器**：显示与后端的连接状态

数据通过 30 秒轮询 + SSE 实时推送双通道更新。

## Services 页面

路径：`/services`、`/services/:id`

管理 JS Service 模板：

- **列表页**：搜索过滤、扫描发现新 Service、创建/删除 Service
- **详情页**：
  - 基本信息（manifest.json 内容）
  - 配置 Schema 查看与编辑
  - 关联的 Project 列表
  - 文件管理（查看、编辑、创建、删除 Service 目录下的文件）

## Projects 页面

路径：`/projects`、`/projects/create`、`/projects/:id`

管理 Service 的实例化配置：

- **列表页**：搜索、按 Service 过滤、按状态过滤，支持启动/停止/删除/启用切换
- **创建向导**：多步表单，选择 Service → 填写配置 → 设置调度策略
- **详情页**：
  - 项目配置（config）查看与编辑
  - 调度策略（schedule）配置
  - 运行时状态与日志

## Instances 页面

路径：`/instances`、`/instances/:id`

监控运行中的实例：

- **列表页**：搜索、按 Project 过滤、按状态过滤，支持终止操作
- **详情页**：
  - 进程树信息
  - 资源指标（CPU、内存、线程），保留最近 60 个采样点
  - 实时日志输出

## Drivers 页面

路径：`/drivers`、`/drivers/:id`

查看已注册的 Driver：

- **列表页**：搜索、扫描发现新 Driver
- **详情页**：
  - 元数据信息（DriverMeta）
  - 命令列表与参数定义
  - 自动生成的文档（Markdown 导出）

## DriverLab 页面

路径：`/driverlab`

交互式 Driver 调试工具，详见 [DriverLab 交互调试](driverlab.md)。
