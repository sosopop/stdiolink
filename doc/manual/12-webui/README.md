# WebUI 概述

stdiolink WebUI 是基于 React 18 的管理前端，提供 Service 编排、Project 管理、Instance 监控和 Driver 交互调试的可视化界面。

## 运行前提

- 需要可访问的 `stdiolink_server`（REST API: `/api/*`）
- 实时功能依赖：
  - SSE：`/api/events/stream`
  - WebSocket：`/api/driverlab/:id`
- 生产部署建议将前端构建产物托管到 server 的 WebUI 静态目录（默认 `<data_root>/webui`）

## 技术栈

| 类别 | 技术 |
|------|------|
| 框架 | React 18 + TypeScript |
| 构建 | Vite |
| UI 组件 | Ant Design 6 |
| 状态管理 | Zustand |
| 路由 | React Router v6 |
| 图表 | Recharts |
| 代码编辑 | Monaco Editor |
| 国际化 | i18next（9 种语言） |
| 测试 | Vitest + Playwright |

## 设计风格

WebUI 采用 **Style 06（Premium Glassmorphism）** 设计规范：

- 深色/浅色双主题，默认深色
- 毛玻璃（Glassmorphism）卡片效果，`backdrop-filter: blur`
- 品牌色 `#6366F1`（Indigo），圆角 12px
- Bento Grid 布局
- 动态状态指示器（脉冲动画）

## 页面结构

```
/ (AppLayout)
├── /dashboard        Dashboard 仪表盘
├── /services         Service 列表
├── /services/:id     Service 详情
├── /projects         Project 列表
├── /projects/create  创建 Project
├── /projects/:id     Project 详情
├── /instances        Instance 列表
├── /instances/:id    Instance 详情
├── /drivers          Driver 列表
├── /drivers/:id      Driver 详情
└── /driverlab        DriverLab 交互调试
```

## 架构概览

```
┌─────────────────────────────────────────────┐
│  React Components (Pages / Shared)          │
├─────────────────────────────────────────────┤
│  Zustand Stores (状态管理)                   │
├──────────────────┬──────────────────────────┤
│  REST API Client │  SSE / WebSocket Client  │
├──────────────────┴──────────────────────────┤
│  stdiolink_server (HTTP :6200)             │
└─────────────────────────────────────────────┘
```

- **REST API**：通过 Axios 调用 `/api/*` 接口，完成 CRUD 操作
- **SSE**：订阅 `/api/events/stream`，接收实例启停、调度触发等实时事件
- **WebSocket**：DriverLab 通过 `/api/driverlab/:id` 与 Driver 进程双向通信

## 本章目录

- [开发与构建](getting-started.md) — 环境搭建、开发服务器、生产构建
- [页面功能](pages.md) — 各页面功能详解
- [DriverLab 交互调试](driverlab.md) — WebSocket 驱动的实时调试工具
- [实时通信](realtime.md) — SSE 事件流与 WebSocket 协议
- [主题与国际化](theme-and-i18n.md) — 双主题系统与多语言支持
