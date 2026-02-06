ServiceOS：服务管理平台架构与详细设计文档版本: 1.0状态: 设计阶段核心框架: stdiolink (C++ / QuickJS)文档语言: 中文 (Chinese)1. 概述 (Executive Summary)ServiceOS 是一个轻量级、面向嵌入式或边缘计算场景的“服务操作系统”。它不仅仅是一个库，而是一个完整的运行时环境。该平台基于您现有的 stdiolink (C++ Host + QuickJS + JSONL 协议) 技术构建，将底层驱动能力与上层业务逻辑分离。用户可以通过一个类似桌面的 WebOS 界面，在线编写 JavaScript 业务逻辑（服务），安装和配置高性能 C++ 驱动（Drivers），并对系统资源、任务调度和运行日志进行全方位管理。核心目标是将 stdiolink 从一个通信库转变为一个可管理、可编排、可视化的应用平台。2. 系统架构 (System Architecture)系统采用 B/S (浏览器/服务器) 架构，但在服务端内部采用了微内核思想，C++ Host 作为内核，JS Service 作为用户态进程，Driver 作为硬件/能力抽象层。2.1 高层架构图graph TD
    subgraph "浏览器端 (Client - WebOS)"
        UI[<b>WebOS 前端</b><br>Vue3 + TypeScript + Element Plus]
        Editor[<b>在线代码编辑器</b><br>Monaco Editor]
        PluginUI[<b>动态插件容器</b><br>自定义驱动 UI]
    end

    subgraph "ServiceOS 主机 (Server C++)"
        WebServer[<b>HTTP/WS 服务器</b><br>API 网关 & 静态资源]
        Auth[<b>认证中心</b><br>JWT / RBAC 权限控制]
        
        subgraph "内核层 (Kernel)"
            SvcMgr[<b>服务管理器</b><br>生命周期 & 进程树]
            VFS[<b>虚拟文件系统 (VFS)</b><br>沙盒路径隔离]
            Scheduler[<b>任务调度器</b><br>Cron 计划任务]
            Registry[<b>驱动注册表</b><br>元数据缓存]
        end

        subgraph "运行时 (Runtime)"
            QJS[<b>QuickJS 引擎</b><br>执行 JS 服务逻辑]
            StdioHost[<b>Stdiolink Host</b><br>驱动通信总线]
        end
    end

    subgraph "执行层 (Process Layer)"
        Driver1[<b>驱动 A</b><br>FFmpeg 推流]
        Driver2[<b>驱动 B</b><br>点云滤波算法]
        Driver3[<b>驱动 C</b><br>硬件 GPIO 控制]
    end

    subgraph "持久化层 (Storage)"
        DB[(<b>SQLite 数据库</b><br>用户/配置/日志)]
        FS[<b>物理文件系统</b><br>代码文件 & 数据]
    end

    UI <-->|REST API / WebSocket| WebServer
    WebServer --> Auth
    WebServer --> SvcMgr
    SvcMgr --> QJS
    SvcMgr --> Scheduler
    QJS --> StdioHost
    StdioHost <-->|JSONL 协议| Driver1
    StdioHost <-->|JSONL 协议| Driver2
    StdioHost <-->|JSONL 协议| Driver3
    SvcMgr --> DB
    VFS --> FS
3. 核心子系统详细设计3.1 服务内核 (Service Kernel)这是对 stdiolink::DriverCore 的扩展，充当整个平台的“内核”。服务 (Service) 与 驱动 (Driver) 的分离：驱动 (Driver): 也就是您现在的 C++ 程序，负责高性能计算或硬件交互（如点云处理）。它们是无状态或低状态的工具。服务 (Service): 是运行在 Host 内存中的 JavaScript 脚本。它负责编排逻辑（例如：“每隔5秒调用一次点云驱动，如果结果异常则报警”）。进程树管理 (Process Tree):内核维护全局进程树：Host (PID X) -> Service (JS Context) -> Driver Subprocess (PID Y)。提供 API 获取完整树结构，包括 CPU/内存占用率。沙盒文件系统 (VFS):为了安全，每个服务只能访问其私有目录。私有沙盒: ./services/<service_id>/data/。JS 中的 fs.write('log.txt') 会被自动映射到此路径。共享目录: ./shared/。只有在服务的 manifest.json 中声明了权限才可访问。3.2 WebOS 交互界面 (The Shell)前端采用 Vue 3 + TypeScript + Vite 构建，是一个单页应用 (SPA)。仪表盘 (Dashboard): 实时显示 Host 系统负载，以及各服务/驱动的资源占用。服务管理器:列表展示所有服务状态 (Running, Stopped, Error)。控制台：通过 WebSocket 实时流式传输服务的 stdout/stderr 日志。在线 IDE (Code Editor):集成 Monaco Editor (VS Code 的核心编辑器)。支持 JavaScript 语法高亮和智能提示。功能：在线修改 service.js 和 config.json，保存后一键热重载服务。元数据表单引擎 (Meta-Form Engine):核心功能: 自动解析 driver.meta.json 中的 config 和 ui 字段。渲染: 将 JSON Schema 渲染为 Element Plus 表单组件（输入框、滑动条、下拉选单）。用途: 用户在安装驱动或配置服务时，无需手写 JSON，通过图形界面即可配置参数。3.3 插件系统 (针对特定驱动的 UI)为了支持您提到的“点云滤波”等复杂场景，系统支持前端插件化。工作流:定义: 驱动开发者在 driver.meta.json 中添加字段 "web_plugin": "./plugins/pointcloud-viewer.umd.js"。加载: 当用户在 WebOS 中打开该驱动的详情页时，前端动态加载这个 .js 组件。交互: 插件组件会自动获得一个 driverProxy 对象。插件可以直接调用 driverProxy.request('getPointCloud')，数据通过 WebSocket 从 C++ 后端透传到前端插件，最终在 Canvas/WebGL 中渲染。3.4 任务调度与日志Cron 调度器:内置 C++ Cron 解析器。用户可以在 Web 界面设置计划任务（例如 0 0 * * *），绑定到某个 Service 的特定函数（如 dailyCleanUp()）。审计日志:记录系统级操作：用户登录、服务启停、配置修改。记录驱动级日志：所有 stdiolink 的 JSONL 消息均可选择性落盘存储到 SQLite 或文本文件。4. 数据设计 (Data Design)4.1 数据库设计 (SQLite)系统使用嵌入式 SQLite 数据库 system.db 存储元数据。表名关键字段描述sys_usersid, username, password_hash, role, avatar用户管理 (RBAC: Admin, Dev, Viewer)sys_driversid, name, exec_path, meta_json, status已注册的驱动程序信息sys_servicesid, alias, script_path, auto_start, permissions用户创建的 JS 服务实例sys_tasksid, service_id, cron_expression, action_payload计划任务表sys_logsid, level, category, message, created_at系统操作审计日志4.2 文件系统目录结构建议采用 Linux 标准目录风格进行管理：/opt/serviceos/
├── bin/
│   └── serviceos_host          # 主程序 (C++ 编译产物)
├── www/                        # 前端静态资源 (Vue dist)
├── plugins/                    # 前端插件 (自定义驱动UI)
├── sys/
│   └── drivers/                # 已安装的驱动程序 (Driver Executables)
│       ├── ffmpeg_driver/
│       └── pcl_filter_driver/
├── data/
│   ├── system.db               # SQLite 数据库
│   └── system.log              # 系统运行日志
├── services/                   # 用户服务根目录
│   ├── svc_monitor_01/         # [服务实例 ID]
│   │   ├── service.js          # 服务逻辑代码 (可在线编辑)
│   │   ├── config.json         # 服务配置文件
│   │   └── sandbox/            # [私有沙盒] 服务产生的数据存于此
│   └── svc_backup/
└── shared/                     # [共享目录] 服务间交换数据
5. API 接口设计 (Interface Design)Host 提供 RESTful API 供前端调用。5.1 系统管理POST /api/auth/login - 用户登录获取 TokenGET /api/sys/status - 获取 Host CPU/内存及运行时间GET /api/sys/process_tree - 获取完整的服务进程树 (JSON 树状结构)5.2 服务管理GET /api/services - 列出所有服务POST /api/services - 创建新服务 (上传 JS 代码)POST /api/services/:id/action - 动作: start | stop | restartGET /api/services/:id/files - 获取服务沙盒文件列表POST /api/services/:id/files/save - 在线保存代码文件5.3 驱动管理GET /api/drivers - 列出已注册驱动GET /api/drivers/:id/meta - 获取驱动的 driver.meta.json (用于生成配置表单)5.4 WebSocket 实时通道 (/ws/events)订阅日志: {"cmd": "subscribe_log", "service_id": "svc_monitor_01"}接收事件:{
  "type": "log",
  "service": "svc_monitor_01",
  "level": "info",
  "message": "Connected to camera...",
  "timestamp": 167888888
}
