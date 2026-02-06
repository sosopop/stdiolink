# StdioLink 服务管理平台研发设计方案

## 1. 项目概述

### 1.1 设计目标
基于现有的 StdioLink 框架，构建一个完整的类操作系统级服务管理平台，提供Web界面进行服务编排、配置管理、运行监控等功能。

### 1.2 核心特性
- **服务生命周期管理**：服务注册、启动、停止、重启、卸载
- **Driver管理**：Driver注册、配置、版本控制
- **Web配置界面**：在线代码编辑、可视化配置、工作流编排
- **文件系统隔离**：私有沙盒 + 共享目录
- **进程树可视化**：完整的服务依赖关系展示
- **计划任务调度**：Cron风格的定时任务
- **日志聚合**：统一日志收集与查询
- **插件化架构**：Driver自带Web配置插件

---

## 2. 系统架构设计

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                     Web Management UI                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │ Dashboard│ │  Service │ │  Driver  │ │   Jobs   │       │
│  │          │ │  Manager │ │  Manager │ │ Scheduler│       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │Code Edit │ │   Logs   │ │  Monitor │ │  Config  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
└─────────────────────────────────────────────────────────────┘
                            ↕ REST API / WebSocket
┌─────────────────────────────────────────────────────────────┐
│                  Service Manager Core                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Service Registry & Lifecycle Manager                │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Driver Registry & Configuration Manager             │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Task Scheduler (Cron Engine)                        │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Process Monitor & Tree Builder                      │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Log Aggregator & Query Engine                       │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  File System Manager (Sandbox/Shared)                │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            ↕
┌─────────────────────────────────────────────────────────────┐
│                    Runtime Layer                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ Service  │  │ Service  │  │ Service  │  │ Service  │    │
│  │    A     │  │    B     │  │    C     │  │    D     │    │
│  │  ┌────┐  │  │  ┌────┐  │  │  ┌────┐  │  │  ┌────┐  │    │
│  │  │Drv1│  │  │  │Drv2│  │  │  │Drv3│  │  │  │Drv4│  │    │
│  │  └────┘  │  │  └────┘  │  │  └────┘  │  │  └────┘  │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 技术栈选型

#### 后端
- **核心框架**：Qt 6 (C++)
- **脚本引擎**：QuickJS (已集成)
- **HTTP服务器**：QHttpServer (Qt官方)
- **数据库**：SQLite (服务/Driver元数据) + JSON配置文件
- **进程管理**：QProcess + 自定义进程树跟踪
- **定时任务**：基于 QTimer 的 Cron 解析器

#### 前端
- **框架**：Vue 3 + TypeScript
- **UI组件库**：Element Plus
- **代码编辑器**：Monaco Editor (VS Code核心)
- **可视化**：D3.js (进程树), ECharts (监控图表)
- **状态管理**：Pinia
- **实时通信**：WebSocket (日志流、状态更新)

---

## 3. 数据模型设计

### 3.1 服务定义 (Service)

```cpp
// service_meta.h
namespace platform {

struct ServiceMeta {
    QString id;              // 唯一标识，如 "cloud-filter-service"
    QString name;            // 显示名称
    QString description;     // 服务描述
    QString version;         // 版本号
    QString jsEntryPoint;    // JS入口文件路径
    QStringList dependencies; // 依赖的其他服务ID
    QJsonObject config;      // 服务配置（用户可通过Web修改）
    
    enum State {
        Stopped,
        Starting,
        Running,
        Stopping,
        Error
    };
    State state = Stopped;
    
    // 文件系统路径
    QString sandboxPath;     // 私有目录: /var/services/{id}/private
    QString sharedPath;      // 共享目录: /var/services/{id}/shared
    QString configPath;      // 配置文件: /var/services/{id}/config.json
    QString logPath;         // 日志目录: /var/services/{id}/logs
    
    // 进程信息
    qint64 pid = 0;          // 主进程ID
    QDateTime startTime;     // 启动时间
    
    QJsonObject toJson() const;
    static ServiceMeta fromJson(const QJsonObject& json);
};

} // namespace platform
```

### 3.2 Driver定义 (Driver)

```cpp
// driver_meta.h
namespace platform {

struct DriverMeta {
    QString id;              // 唯一标识，如 "point-cloud-filter"
    QString name;            // 显示名称
    QString executable;      // 可执行文件路径
    QString description;     // 描述
    QString version;         // 版本号
    
    // Driver元数据（从 driver.queryMeta() 获取）
    QJsonArray commands;     // 支持的命令列表
    QJsonObject schema;      // 参数 schema
    
    // 配置
    QJsonObject defaultConfig;  // 默认配置
    
    // Web插件（可选）
    struct WebPlugin {
        QString type;        // "workflow" | "form" | "custom"
        QString htmlPath;    // 插件HTML文件路径
        QString jsPath;      // 插件JS文件路径
        QString cssPath;     // 插件CSS文件路径
        QJsonObject meta;    // 插件元数据
    };
    std::optional<WebPlugin> webPlugin;
    
    // 资源要求
    int maxInstances = -1;   // -1表示无限制
    
    QJsonObject toJson() const;
    static DriverMeta fromJson(const QJsonObject& json);
};

} // namespace platform
```

### 3.3 计划任务定义 (ScheduledJob)

```cpp
// job_meta.h
namespace platform {

struct ScheduledJob {
    QString id;              // 任务ID
    QString name;            // 任务名称
    QString serviceId;       // 所属服务ID
    QString cronExpression;  // Cron表达式："0 */5 * * *"
    
    enum Type {
        ServiceCommand,      // 调用服务的某个命令
        Script,              // 执行脚本
        Http                 // HTTP请求
    };
    Type type;
    
    QJsonObject params;      // 任务参数
    bool enabled = true;     // 是否启用
    
    QDateTime lastRun;       // 上次执行时间
    QDateTime nextRun;       // 下次执行时间
    QString lastStatus;      // 上次执行状态
    
    QJsonObject toJson() const;
    static ScheduledJob fromJson(const QJsonObject& json);
};

} // namespace platform
```

### 3.4 数据库Schema (SQLite)

```sql
-- services.db

CREATE TABLE services (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT,
    version TEXT,
    js_entry_point TEXT NOT NULL,
    dependencies TEXT,  -- JSON array
    config TEXT,        -- JSON object
    state INTEGER DEFAULT 0,
    sandbox_path TEXT,
    shared_path TEXT,
    config_path TEXT,
    log_path TEXT,
    pid INTEGER DEFAULT 0,
    start_time TEXT,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE drivers (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    executable TEXT NOT NULL,
    description TEXT,
    version TEXT,
    commands TEXT,      -- JSON array
    schema TEXT,        -- JSON object
    default_config TEXT, -- JSON object
    web_plugin TEXT,    -- JSON object (nullable)
    max_instances INTEGER DEFAULT -1,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE scheduled_jobs (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    service_id TEXT NOT NULL,
    cron_expression TEXT NOT NULL,
    type INTEGER NOT NULL,
    params TEXT,        -- JSON object
    enabled INTEGER DEFAULT 1,
    last_run TEXT,
    next_run TEXT,
    last_status TEXT,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (service_id) REFERENCES services(id) ON DELETE CASCADE
);

CREATE TABLE service_driver_instances (
    id TEXT PRIMARY KEY,
    service_id TEXT NOT NULL,
    driver_id TEXT NOT NULL,
    config TEXT,        -- JSON object，实例特定配置
    pid INTEGER DEFAULT 0,
    state INTEGER DEFAULT 0,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (service_id) REFERENCES services(id) ON DELETE CASCADE,
    FOREIGN KEY (driver_id) REFERENCES drivers(id) ON DELETE CASCADE
);

CREATE INDEX idx_services_state ON services(state);
CREATE INDEX idx_jobs_service ON scheduled_jobs(service_id);
CREATE INDEX idx_jobs_next_run ON scheduled_jobs(next_run, enabled);
CREATE INDEX idx_driver_instances_service ON service_driver_instances(service_id);
```

---

## 4. 核心模块设计

### 4.1 服务管理器 (ServiceManager)

```cpp
// service_manager.h
namespace platform {

class ServiceManager : public QObject {
    Q_OBJECT
public:
    explicit ServiceManager(QObject* parent = nullptr);
    
    // 服务注册
    bool registerService(const ServiceMeta& meta);
    bool unregisterService(const QString& serviceId);
    
    // 服务生命周期
    bool startService(const QString& serviceId);
    bool stopService(const QString& serviceId);
    bool restartService(const QString& serviceId);
    
    // 服务查询
    QVector<ServiceMeta> listServices() const;
    std::optional<ServiceMeta> getService(const QString& serviceId) const;
    
    // 配置管理
    bool updateServiceConfig(const QString& serviceId, const QJsonObject& config);
    QJsonObject getServiceConfig(const QString& serviceId) const;
    
    // 依赖解析
    QStringList resolveDependencies(const QString& serviceId) const;
    bool checkDependencies(const QString& serviceId) const;
    
signals:
    void serviceStateChanged(const QString& serviceId, ServiceMeta::State newState);
    void serviceRegistered(const QString& serviceId);
    void serviceUnregistered(const QString& serviceId);
    
private:
    struct ServiceInstance {
        ServiceMeta meta;
        std::unique_ptr<JsEngine> engine;
        QProcess* process = nullptr;
        QHash<QString, stdiolink::Driver*> drivers; // driverId -> Driver*
    };
    
    QHash<QString, ServiceInstance> m_services;
    class Database* m_db;
    
    bool createServiceDirectories(const ServiceMeta& meta);
    void loadServiceConfig(ServiceInstance& instance);
    void saveServiceConfig(const ServiceInstance& instance);
};

} // namespace platform
```

### 4.2 Driver管理器 (DriverManager)

```cpp
// driver_manager.h
namespace platform {

class DriverManager : public QObject {
    Q_OBJECT
public:
    explicit DriverManager(QObject* parent = nullptr);
    
    // Driver注册
    bool registerDriver(const DriverMeta& meta);
    bool unregisterDriver(const QString& driverId);
    
    // Driver查询
    QVector<DriverMeta> listDrivers() const;
    std::optional<DriverMeta> getDriver(const QString& driverId) const;
    
    // Driver实例化（为服务创建Driver实例）
    stdiolink::Driver* createDriverInstance(
        const QString& serviceId,
        const QString& driverId,
        const QJsonObject& config
    );
    
    bool destroyDriverInstance(const QString& serviceId, const QString& instanceId);
    
    // Web插件管理
    bool installWebPlugin(const QString& driverId, const DriverMeta::WebPlugin& plugin);
    std::optional<DriverMeta::WebPlugin> getWebPlugin(const QString& driverId) const;
    
signals:
    void driverRegistered(const QString& driverId);
    void driverUnregistered(const QString& driverId);
    
private:
    QHash<QString, DriverMeta> m_drivers;
    class Database* m_db;
    
    bool validateDriver(const DriverMeta& meta);
    QJsonObject queryDriverMeta(const QString& executable);
};

} // namespace platform
```

### 4.3 任务调度器 (JobScheduler)

```cpp
// job_scheduler.h
namespace platform {

class JobScheduler : public QObject {
    Q_OBJECT
public:
    explicit JobScheduler(ServiceManager* serviceMgr, QObject* parent = nullptr);
    
    // 任务管理
    bool addJob(const ScheduledJob& job);
    bool removeJob(const QString& jobId);
    bool updateJob(const ScheduledJob& job);
    bool enableJob(const QString& jobId, bool enabled);
    
    // 任务查询
    QVector<ScheduledJob> listJobs(const QString& serviceId = QString()) const;
    std::optional<ScheduledJob> getJob(const QString& jobId) const;
    
    // 调度控制
    void start();
    void stop();
    bool isRunning() const;
    
signals:
    void jobExecuted(const QString& jobId, bool success, const QString& message);
    void jobAdded(const QString& jobId);
    void jobRemoved(const QString& jobId);
    
private slots:
    void checkPendingJobs();
    
private:
    struct CronParser {
        static QDateTime calculateNext(const QString& cronExpr, const QDateTime& from);
        static bool matches(const QString& cronExpr, const QDateTime& time);
    };
    
    ServiceManager* m_serviceMgr;
    QHash<QString, ScheduledJob> m_jobs;
    QTimer* m_timer;
    class Database* m_db;
    
    void executeJob(const ScheduledJob& job);
    void updateNextRunTime(ScheduledJob& job);
};

} // namespace platform
```

### 4.4 进程监控器 (ProcessMonitor)

```cpp
// process_monitor.h
namespace platform {

struct ProcessInfo {
    qint64 pid;
    QString name;
    QString serviceId;
    QString driverId;
    qint64 parentPid;
    QVector<qint64> childPids;
    
    // 资源使用
    double cpuPercent;
    qint64 memoryKB;
    qint64 startTime;
    
    QJsonObject toJson() const;
};

struct ProcessTree {
    ProcessInfo root;
    QVector<ProcessTree> children;
    
    QJsonObject toJson() const;
};

class ProcessMonitor : public QObject {
    Q_OBJECT
public:
    explicit ProcessMonitor(QObject* parent = nullptr);
    
    // 进程注册
    void registerProcess(qint64 pid, const QString& serviceId, const QString& driverId = QString());
    void unregisterProcess(qint64 pid);
    
    // 进程查询
    QVector<ProcessInfo> listProcesses() const;
    std::optional<ProcessInfo> getProcess(qint64 pid) const;
    ProcessTree buildProcessTree(const QString& serviceId) const;
    
    // 监控控制
    void startMonitoring(int intervalMs = 5000);
    void stopMonitoring();
    
signals:
    void processStarted(qint64 pid);
    void processTerminated(qint64 pid);
    void processStatsUpdated(qint64 pid, double cpu, qint64 memory);
    
private slots:
    void updateProcessStats();
    
private:
    QHash<qint64, ProcessInfo> m_processes;
    QTimer* m_timer;
    
#ifdef Q_OS_WIN
    void updateStatsWindows();
#else
    void updateStatsLinux();
#endif
};

} // namespace platform
```

### 4.5 日志聚合器 (LogAggregator)

```cpp
// log_aggregator.h
namespace platform {

struct LogEntry {
    QString id;              // 日志ID
    QString serviceId;       // 服务ID
    QString level;           // "debug" | "info" | "warn" | "error"
    QString message;         // 日志内容
    QDateTime timestamp;     // 时间戳
    QJsonObject metadata;    // 额外元数据
    
    QJsonObject toJson() const;
};

class LogAggregator : public QObject {
    Q_OBJECT
public:
    explicit LogAggregator(QObject* parent = nullptr);
    
    // 日志写入
    void log(const QString& serviceId, const QString& level, const QString& message,
             const QJsonObject& metadata = {});
    
    // 日志查询
    struct QueryOptions {
        QString serviceId;
        QStringList levels;
        QDateTime startTime;
        QDateTime endTime;
        QString searchText;
        int limit = 100;
        int offset = 0;
    };
    QVector<LogEntry> query(const QueryOptions& options) const;
    
    // 日志流（WebSocket）
    void startStreaming(const QString& serviceId = QString());
    void stopStreaming();
    
    // 日志清理
    void clearLogs(const QString& serviceId, const QDateTime& before = QDateTime());
    
signals:
    void logReceived(const LogEntry& entry);
    
private:
    QHash<QString, QFile*> m_logFiles;  // serviceId -> log file
    QVector<LogEntry> m_recentLogs;     // 内存缓存（用于实时流）
    
    void writeToFile(const LogEntry& entry);
    void rotateLogFile(const QString& serviceId);
};

} // namespace platform
```

### 4.6 文件系统管理器 (FileSystemManager)

```cpp
// filesystem_manager.h
namespace platform {

class FileSystemManager {
public:
    static constexpr const char* BASE_PATH = "/var/services";
    
    struct ServicePaths {
        QString sandbox;     // /var/services/{id}/private
        QString shared;      // /var/services/{id}/shared
        QString config;      // /var/services/{id}/config.json
        QString logs;        // /var/services/{id}/logs
    };
    
    // 创建服务目录结构
    static bool createServiceDirectories(const QString& serviceId);
    
    // 删除服务目录
    static bool removeServiceDirectories(const QString& serviceId);
    
    // 获取路径
    static ServicePaths getServicePaths(const QString& serviceId);
    
    // 文件操作（带权限检查）
    static QByteArray readFile(const QString& serviceId, const QString& relativePath, bool isShared = false);
    static bool writeFile(const QString& serviceId, const QString& relativePath, 
                         const QByteArray& data, bool isShared = false);
    static bool deleteFile(const QString& serviceId, const QString& relativePath, bool isShared = false);
    
    // 目录操作
    static QStringList listFiles(const QString& serviceId, const QString& relativePath = QString(),
                                 bool isShared = false);
    
    // 共享目录权限
    static bool grantSharedAccess(const QString& serviceId, const QString& targetServiceId);
    static bool revokeSharedAccess(const QString& serviceId, const QString& targetServiceId);
    
private:
    static QString resolveServicePath(const QString& serviceId, const QString& relativePath, bool isShared);
};

} // namespace platform
```

---

## 5. Web API设计

### 5.1 RESTful API规范

#### 基础路径
```
/api/v1/services
/api/v1/drivers
/api/v1/jobs
/api/v1/logs
/api/v1/monitor
/api/v1/files
```

#### 服务管理API

```cpp
// api_service.h
namespace platform::api {

class ServiceApiHandler : public QObject {
    Q_OBJECT
public:
    void setupRoutes(QHttpServer* server);
    
private:
    // GET /api/v1/services
    QHttpServerResponse listServices(const QHttpServerRequest& request);
    
    // POST /api/v1/services
    QHttpServerResponse createService(const QHttpServerRequest& request);
    
    // GET /api/v1/services/{id}
    QHttpServerResponse getService(const QString& id);
    
    // PUT /api/v1/services/{id}
    QHttpServerResponse updateService(const QString& id, const QHttpServerRequest& request);
    
    // DELETE /api/v1/services/{id}
    QHttpServerResponse deleteService(const QString& id);
    
    // POST /api/v1/services/{id}/start
    QHttpServerResponse startService(const QString& id);
    
    // POST /api/v1/services/{id}/stop
    QHttpServerResponse stopService(const QString& id);
    
    // POST /api/v1/services/{id}/restart
    QHttpServerResponse restartService(const QString& id);
    
    // GET /api/v1/services/{id}/config
    QHttpServerResponse getServiceConfig(const QString& id);
    
    // PUT /api/v1/services/{id}/config
    QHttpServerResponse updateServiceConfig(const QString& id, const QHttpServerRequest& request);
    
    // GET /api/v1/services/{id}/logs
    QHttpServerResponse getServiceLogs(const QString& id);
    
    ServiceManager* m_serviceMgr;
};

} // namespace platform::api
```

#### Driver管理API

```http
GET    /api/v1/drivers                    # 列出所有Driver
POST   /api/v1/drivers                    # 注册Driver
GET    /api/v1/drivers/{id}               # 获取Driver信息
PUT    /api/v1/drivers/{id}               # 更新Driver
DELETE /api/v1/drivers/{id}               # 删除Driver
GET    /api/v1/drivers/{id}/plugin        # 获取Web插件信息
POST   /api/v1/drivers/{id}/plugin        # 安装Web插件
GET    /api/v1/drivers/{id}/schema        # 获取配置Schema
```

#### 任务调度API

```http
GET    /api/v1/jobs                       # 列出所有任务
POST   /api/v1/jobs                       # 创建任务
GET    /api/v1/jobs/{id}                  # 获取任务信息
PUT    /api/v1/jobs/{id}                  # 更新任务
DELETE /api/v1/jobs/{id}                  # 删除任务
POST   /api/v1/jobs/{id}/enable           # 启用任务
POST   /api/v1/jobs/{id}/disable          # 禁用任务
POST   /api/v1/jobs/{id}/run              # 立即执行任务
```

#### 监控API

```http
GET    /api/v1/monitor/processes          # 获取进程列表
GET    /api/v1/monitor/processes/{pid}    # 获取进程详情
GET    /api/v1/monitor/tree/{serviceId}   # 获取服务进程树
GET    /api/v1/monitor/stats              # 获取系统统计信息
```

#### 日志API

```http
GET    /api/v1/logs                       # 查询日志
GET    /api/v1/logs/stream                # WebSocket日志流
POST   /api/v1/logs                       # 写入日志
DELETE /api/v1/logs/{serviceId}           # 清理日志
```

#### 文件管理API

```http
GET    /api/v1/files/{serviceId}          # 列出文件
GET    /api/v1/files/{serviceId}/*        # 读取文件
POST   /api/v1/files/{serviceId}/*        # 创建/上传文件
PUT    /api/v1/files/{serviceId}/*        # 更新文件
DELETE /api/v1/files/{serviceId}/*        # 删除文件
```

### 5.2 WebSocket协议

```javascript
// WebSocket端点
ws://localhost:8080/api/v1/ws

// 消息格式
{
  "type": "subscribe" | "unsubscribe" | "log" | "event" | "stats",
  "channel": "logs" | "service.{id}.state" | "monitor.stats",
  "data": {...}
}

// 订阅示例
{
  "type": "subscribe",
  "channel": "logs",
  "data": {
    "serviceId": "cloud-filter-service",
    "levels": ["info", "warn", "error"]
  }
}

// 服务器推送
{
  "type": "log",
  "channel": "logs",
  "data": {
    "serviceId": "cloud-filter-service",
    "level": "info",
    "message": "Processing started",
    "timestamp": "2025-02-06T10:30:00Z"
  }
}
```

---

## 6. Web前端设计

### 6.1 页面结构

```
src/
├── views/
│   ├── Dashboard.vue          # 仪表盘（总览）
│   ├── Services/
│   │   ├── ServiceList.vue    # 服务列表
│   │   ├── ServiceDetail.vue  # 服务详情
│   │   ├── ServiceEditor.vue  # 服务代码编辑器
│   │   └── ServiceConfig.vue  # 服务配置
│   ├── Drivers/
│   │   ├── DriverList.vue     # Driver列表
│   │   ├── DriverDetail.vue   # Driver详情
│   │   └── DriverConfig.vue   # Driver配置
│   ├── Jobs/
│   │   ├── JobList.vue        # 任务列表
│   │   ├── JobEditor.vue      # 任务编辑器
│   │   └── JobHistory.vue     # 任务历史
│   ├── Monitor/
│   │   ├── ProcessTree.vue    # 进程树可视化
│   │   ├── SystemStats.vue    # 系统统计
│   │   └── ResourceUsage.vue  # 资源使用图表
│   └── Logs/
│       ├── LogViewer.vue      # 日志查看器
│       └── LogStream.vue      # 实时日志流
├── components/
│   ├── CodeEditor/
│   │   ├── MonacoEditor.vue   # Monaco编辑器封装
│   │   └── FileTree.vue       # 文件树
│   ├── PluginHost/
│   │   └── PluginIframe.vue   # 插件加载容器
│   └── Common/
│       ├── StateIndicator.vue # 状态指示器
│       └── ConfirmDialog.vue  # 确认对话框
├── stores/
│   ├── service.ts             # 服务状态管理
│   ├── driver.ts              # Driver状态管理
│   ├── job.ts                 # 任务状态管理
│   └── log.ts                 # 日志状态管理
└── api/
    ├── service.ts             # 服务API封装
    ├── driver.ts              # Driver API封装
    ├── job.ts                 # 任务API封装
    └── websocket.ts           # WebSocket封装
```

### 6.2 核心组件设计

#### 代码编辑器组件

```vue
<!-- CodeEditor/MonacoEditor.vue -->
<template>
  <div class="code-editor">
    <div class="toolbar">
      <el-button @click="save" :loading="saving">保存</el-button>
      <el-button @click="format">格式化</el-button>
      <el-select v-model="language" size="small">
        <el-option label="JavaScript" value="javascript" />
        <el-option label="JSON" value="json" />
      </el-select>
    </div>
    <div ref="editorContainer" class="editor-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import * as monaco from 'monaco-editor'

const props = defineProps<{
  modelValue: string
  language?: string
  readonly?: boolean
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string]
  'save': [value: string]
}>()

const editorContainer = ref<HTMLElement>()
let editor: monaco.editor.IStandaloneCodeEditor

onMounted(() => {
  if (!editorContainer.value) return
  
  editor = monaco.editor.create(editorContainer.value, {
    value: props.modelValue,
    language: props.language || 'javascript',
    theme: 'vs-dark',
    automaticLayout: true,
    readOnly: props.readonly,
    minimap: { enabled: true }
  })
  
  editor.onDidChangeModelContent(() => {
    emit('update:modelValue', editor.getValue())
  })
})

const save = () => {
  emit('save', editor.getValue())
}

const format = () => {
  editor.getAction('editor.action.formatDocument')?.run()
}

onUnmounted(() => {
  editor?.dispose()
})
</script>
```

#### 进程树可视化

```vue
<!-- Monitor/ProcessTree.vue -->
<template>
  <div class="process-tree">
    <div ref="treeContainer" class="tree-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as d3 from 'd3'

interface ProcessNode {
  pid: number
  name: string
  serviceId: string
  cpuPercent: number
  memoryKB: number
  children?: ProcessNode[]
}

const props = defineProps<{
  data: ProcessNode
}>()

const treeContainer = ref<HTMLElement>()

const renderTree = () => {
  if (!treeContainer.value) return
  
  const width = treeContainer.value.clientWidth
  const height = 600
  
  const svg = d3.select(treeContainer.value)
    .append('svg')
    .attr('width', width)
    .attr('height', height)
  
  const treeLayout = d3.tree<ProcessNode>()
    .size([width - 200, height - 200])
  
  const root = d3.hierarchy(props.data)
  const treeData = treeLayout(root)
  
  const g = svg.append('g')
    .attr('transform', 'translate(100, 100)')
  
  // 绘制连线
  g.selectAll('.link')
    .data(treeData.links())
    .enter()
    .append('path')
    .attr('class', 'link')
    .attr('d', d3.linkHorizontal()
      .x((d: any) => d.y)
      .y((d: any) => d.x))
    .attr('fill', 'none')
    .attr('stroke', '#ccc')
    .attr('stroke-width', 2)
  
  // 绘制节点
  const node = g.selectAll('.node')
    .data(treeData.descendants())
    .enter()
    .append('g')
    .attr('class', 'node')
    .attr('transform', d => `translate(${d.y}, ${d.x})`)
  
  node.append('circle')
    .attr('r', 8)
    .attr('fill', d => d.data.cpuPercent > 50 ? '#f56c6c' : '#67c23a')
  
  node.append('text')
    .attr('dx', 12)
    .attr('dy', 4)
    .text(d => `${d.data.name} (PID: ${d.data.pid})`)
    .style('font-size', '12px')
  
  node.append('text')
    .attr('dx', 12)
    .attr('dy', 18)
    .text(d => `CPU: ${d.data.cpuPercent.toFixed(1)}%, Mem: ${(d.data.memoryKB / 1024).toFixed(0)}MB`)
    .style('font-size', '10px')
    .style('fill', '#999')
}

onMounted(() => {
  renderTree()
})

watch(() => props.data, () => {
  if (treeContainer.value) {
    treeContainer.value.innerHTML = ''
    renderTree()
  }
})
</script>
```

#### Driver插件加载器

```vue
<!-- PluginHost/PluginIframe.vue -->
<template>
  <div class="plugin-host">
    <iframe
      ref="iframe"
      :src="pluginUrl"
      sandbox="allow-scripts allow-same-origin"
      @load="onLoad"
    ></iframe>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'

const props = defineProps<{
  driverId: string
  config: Record<string, any>
}>()

const emit = defineEmits<{
  'update:config': [config: Record<string, any>]
}>()

const iframe = ref<HTMLIFrameElement>()

const pluginUrl = computed(() => {
  return `/api/v1/drivers/${props.driverId}/plugin/index.html`
})

const onLoad = () => {
  if (!iframe.value?.contentWindow) return
  
  // 建立双向通信
  const channel = new MessageChannel()
  
  // 监听插件消息
  channel.port1.onmessage = (event) => {
    if (event.data.type === 'configChanged') {
      emit('update:config', event.data.config)
    }
  }
  
  // 发送初始配置到插件
  iframe.value.contentWindow.postMessage(
    {
      type: 'init',
      config: props.config,
      port: channel.port2
    },
    '*',
    [channel.port2]
  )
}
</script>
```

### 6.3 Driver Web插件规范

Driver可以提供自定义的Web配置界面，插件需要遵循以下规范：

#### 插件目录结构
```
driver-plugin/
├── manifest.json      # 插件元数据
├── index.html         # 主页面
├── config.js          # 配置逻辑
└── style.css          # 样式
```

#### manifest.json
```json
{
  "id": "point-cloud-workflow",
  "name": "Point Cloud Workflow Editor",
  "version": "1.0.0",
  "type": "workflow",
  "main": "index.html",
  "configSchema": {
    "type": "object",
    "properties": {
      "pipeline": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "step": { "type": "string" },
            "params": { "type": "object" }
          }
        }
      }
    }
  }
}
```

#### 插件API (config.js)
```javascript
// 插件接收平台消息
window.addEventListener('message', (event) => {
  if (event.data.type === 'init') {
    const { config, port } = event.data
    
    // 保存通信端口
    window.platformPort = port
    
    // 初始化插件UI
    initWorkflowEditor(config)
    
    // 监听配置变化
    port.onmessage = (e) => {
      if (e.data.type === 'configUpdate') {
        updateWorkflowEditor(e.data.config)
      }
    }
  }
})

// 插件发送配置到平台
function saveConfig(newConfig) {
  if (window.platformPort) {
    window.platformPort.postMessage({
      type: 'configChanged',
      config: newConfig
    })
  }
}

// 工作流编辑器示例
function initWorkflowEditor(config) {
  // 使用流程图库（如 jsPlumb）渲染工作流
  const editor = new WorkflowEditor({
    container: document.getElementById('workflow-container'),
    initialData: config.pipeline || []
  })
  
  editor.on('change', (pipeline) => {
    saveConfig({ pipeline })
  })
}
```

---

## 7. 服务开发示例

### 7.1 服务JS脚本模板

```javascript
// service-template.js
import { openDriver } from 'stdiolink'

// 服务元数据（可被平台读取）
export const meta = {
  id: 'my-cloud-service',
  name: 'Point Cloud Processing Service',
  version: '1.0.0',
  description: 'Service for processing point cloud data',
  
  // 配置Schema（Web界面根据此生成表单）
  configSchema: {
    type: 'object',
    properties: {
      inputPath: {
        type: 'string',
        title: 'Input Directory',
        description: 'Path to input point cloud files'
      },
      outputPath: {
        type: 'string',
        title: 'Output Directory'
      },
      filterParams: {
        type: 'object',
        title: 'Filter Parameters',
        properties: {
          voxelSize: { type: 'number', default: 0.05 },
          outlierThreshold: { type: 'number', default: 2.0 }
        }
      }
    },
    required: ['inputPath', 'outputPath']
  },
  
  // 依赖的Driver
  drivers: [
    {
      id: 'point-cloud-filter',
      executable: './drivers/point-cloud-filter',
      required: true
    }
  ],
  
  // 对外提供的命令
  commands: [
    {
      name: 'process',
      description: 'Process point cloud files',
      params: {
        type: 'object',
        properties: {
          files: {
            type: 'array',
            items: { type: 'string' }
          }
        }
      }
    },
    {
      name: 'status',
      description: 'Get processing status'
    }
  ]
}

// 全局Driver实例
let filterDriver

// 服务初始化
export async function initialize(config) {
  console.log('Initializing service with config:', config)
  
  // 启动Driver
  filterDriver = await openDriver(
    config.drivers.find(d => d.id === 'point-cloud-filter').executable,
    ['--profile=keepalive']
  )
  
  console.log('Filter driver started:', filterDriver.$meta)
  
  return { success: true }
}

// 处理 process 命令
export async function process(params, responder) {
  const { files } = params
  
  responder.event('progress', 0, { message: 'Starting processing' })
  
  for (let i = 0; i < files.length; i++) {
    const file = files[i]
    
    try {
      // 调用Driver的filter命令
      const result = await filterDriver.filter({
        input: file,
        output: file.replace('.pcd', '_filtered.pcd'),
        voxelSize: config.filterParams.voxelSize,
        outlierThreshold: config.filterParams.outlierThreshold
      })
      
      responder.event('progress', ((i + 1) / files.length) * 100, {
        file,
        status: 'completed'
      })
      
    } catch (error) {
      responder.event('error', 0, {
        file,
        error: error.message
      })
    }
  }
  
  responder.done(0, {
    processed: files.length,
    success: true
  })
}

// 处理 status 命令
export async function status(params, responder) {
  const stats = {
    uptime: process.uptime(),
    driverStatus: filterDriver ? 'running' : 'stopped',
    memoryUsage: process.memoryUsage()
  }
  
  responder.done(0, stats)
}

// 服务清理
export async function cleanup() {
  if (filterDriver) {
    filterDriver.$close()
  }
}
```

### 7.2 服务配置文件示例

```json
// /var/services/my-cloud-service/config.json
{
  "inputPath": "/shared/input",
  "outputPath": "/shared/output",
  "filterParams": {
    "voxelSize": 0.05,
    "outlierThreshold": 2.0
  },
  "drivers": [
    {
      "id": "point-cloud-filter",
      "executable": "/usr/local/bin/point-cloud-filter",
      "config": {
        "maxMemory": "2GB",
        "threads": 4
      }
    }
  ]
}
```

---

## 8. 部署与配置

### 8.1 系统要求

- **操作系统**：Linux (Ubuntu 22.04+), Windows 10+, macOS 12+
- **Qt**：6.5+
- **Node.js**：18+ (前端构建)
- **磁盘**：至少10GB空闲空间（用于服务和日志）
- **内存**：推荐8GB+

### 8.2 目录结构

```
/opt/stdiolink-platform/
├── bin/
│   └── platform-manager           # 主程序
├── lib/
│   └── libstdiolink.so            # 核心库
├── web/
│   └── dist/                      # 前端构建产物
├── drivers/
│   ├── point-cloud-filter/
│   └── image-processor/
└── etc/
    └── platform.conf              # 平台配置

/var/services/                     # 服务数据目录
├── service-a/
│   ├── private/                   # 私有沙盒
│   ├── shared/                    # 共享目录
│   ├── config.json                # 配置
│   └── logs/                      # 日志
└── service-b/
    └── ...

/var/lib/stdiolink/
└── platform.db                    # 元数据数据库
```

### 8.3 配置文件

```ini
# /opt/stdiolink-platform/etc/platform.conf

[Server]
HttpPort=8080
WebSocketPort=8081
EnableTLS=false
TLSCert=/path/to/cert.pem
TLSKey=/path/to/key.pem

[Database]
Path=/var/lib/stdiolink/platform.db

[Services]
BasePath=/var/services
MaxServices=100
DefaultProfile=keepalive

[Drivers]
SearchPaths=/opt/stdiolink-platform/drivers;/usr/local/drivers
MaxInstancesPerDriver=10

[Monitoring]
ProcessMonitorInterval=5000
LogRetentionDays=30
MaxLogFileSize=100MB

[Security]
EnableAuthentication=true
SessionTimeout=3600
AllowedOrigins=*
```

---

## 9. 安全性设计

### 9.1 服务隔离

- **文件系统隔离**：每个服务只能访问自己的 sandbox 和被授权的 shared 目录
- **进程隔离**：使用 QProcess 的独立进程空间
- **资源限制**：可配置CPU/内存限制（通过 cgroups on Linux）

### 9.2 权限管理

```cpp
// security/permission_manager.h
namespace platform::security {

enum Permission {
    ServiceStart    = 1 << 0,
    ServiceStop     = 1 << 1,
    ServiceConfig   = 1 << 2,
    DriverInstall   = 1 << 3,
    DriverUninstall = 1 << 4,
    JobCreate       = 1 << 5,
    JobDelete       = 1 << 6,
    LogView         = 1 << 7,
    LogDelete       = 1 << 8,
    FileRead        = 1 << 9,
    FileWrite       = 1 << 10,
    Admin           = 0xFFFFFFFF
};

struct User {
    QString id;
    QString username;
    QString passwordHash;
    uint32_t permissions;
};

class PermissionManager {
public:
    bool hasPermission(const QString& userId, Permission perm) const;
    bool grantPermission(const QString& userId, Permission perm);
    bool revokePermission(const QString& userId, Permission perm);
};

} // namespace platform::security
```

### 9.3 API认证

```cpp
// security/auth_middleware.h
namespace platform::api {

class AuthMiddleware {
public:
    static QHttpServerResponse authenticate(
        const QHttpServerRequest& request,
        std::function<QHttpServerResponse()> next
    ) {
        QString token = request.headers()["Authorization"];
        if (!validateToken(token)) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
        }
        return next();
    }
    
private:
    static bool validateToken(const QString& token);
};

} // namespace platform::api
```

---

## 10. 测试计划

### 10.1 单元测试

```cpp
// tests/test_service_manager.cpp
#include <QtTest>
#include "service_manager.h"

class TestServiceManager : public QObject {
    Q_OBJECT
    
private slots:
    void initTestCase() {
        m_mgr = new ServiceManager(this);
    }
    
    void testRegisterService() {
        ServiceMeta meta;
        meta.id = "test-service";
        meta.name = "Test Service";
        meta.jsEntryPoint = "/path/to/service.js";
        
        QVERIFY(m_mgr->registerService(meta));
        QVERIFY(m_mgr->getService("test-service").has_value());
    }
    
    void testStartService() {
        QVERIFY(m_mgr->startService("test-service"));
        auto service = m_mgr->getService("test-service");
        QVERIFY(service.has_value());
        QCOMPARE(service->state, ServiceMeta::Running);
    }
    
    void cleanupTestCase() {
        delete m_mgr;
    }
    
private:
    ServiceManager* m_mgr = nullptr;
};

QTEST_MAIN(TestServiceManager)
#include "test_service_manager.moc"
```

### 10.2 集成测试

```javascript
// tests/integration/test_api.spec.ts
import { describe, it, expect } from 'vitest'
import axios from 'axios'

describe('Service API', () => {
  const baseURL = 'http://localhost:8080/api/v1'
  
  it('should list services', async () => {
    const response = await axios.get(`${baseURL}/services`)
    expect(response.status).toBe(200)
    expect(Array.isArray(response.data)).toBe(true)
  })
  
  it('should create service', async () => {
    const service = {
      id: 'test-service',
      name: 'Test Service',
      jsEntryPoint: '/services/test/main.js'
    }
    const response = await axios.post(`${baseURL}/services`, service)
    expect(response.status).toBe(201)
    expect(response.data.id).toBe('test-service')
  })
  
  it('should start service', async () => {
    const response = await axios.post(`${baseURL}/services/test-service/start`)
    expect(response.status).toBe(200)
    expect(response.data.state).toBe('running')
  })
})
```

### 10.3 E2E测试

```javascript
// tests/e2e/service-workflow.spec.ts
import { test, expect } from '@playwright/test'

test('complete service workflow', async ({ page }) => {
  // 访问平台
  await page.goto('http://localhost:8080')
  
  // 登录
  await page.fill('input[name="username"]', 'admin')
  await page.fill('input[name="password"]', 'admin')
  await page.click('button[type="submit"]')
  
  // 创建服务
  await page.click('text=Services')
  await page.click('text=Create Service')
  await page.fill('input[name="id"]', 'e2e-test')
  await page.fill('input[name="name"]', 'E2E Test Service')
  await page.click('button:has-text("Create")')
  
  // 启动服务
  await page.click('text=Start')
  await expect(page.locator('.state-indicator')).toHaveText('Running')
  
  // 查看日志
  await page.click('text=Logs')
  await expect(page.locator('.log-entry')).toContainText('Service started')
  
  // 停止服务
  await page.click('text=Stop')
  await expect(page.locator('.state-indicator')).toHaveText('Stopped')
})
```

---

## 11. 实施路线图

### Phase 1: 核心基础设施 (4周)
- [ ] 数据模型定义与数据库Schema
- [ ] ServiceManager 基础实现
- [ ] DriverManager 基础实现
- [ ] FileSystemManager 实现
- [ ] HTTP Server 搭建
- [ ] 基础REST API

### Phase 2: 服务生命周期 (3周)
- [ ] 服务注册/注销
- [ ] 服务启动/停止/重启
- [ ] 依赖解析
- [ ] 配置管理
- [ ] 进程监控基础

### Phase 3: Web界面基础 (4周)
- [ ] 前端项目初始化
- [ ] 服务列表页面
- [ ] 服务详情页面
- [ ] Driver管理页面
- [ ] 基础状态监控

### Phase 4: 高级功能 (5周)
- [ ] 代码编辑器集成
- [ ] 任务调度器实现
- [ ] 日志聚合系统
- [ ] WebSocket实时通信
- [ ] 进程树可视化

### Phase 5: 插件系统 (3周)
- [ ] Driver Web插件规范
- [ ] 插件加载器
- [ ] 示例插件（点云工作流）
- [ ] 插件市场（可选）

### Phase 6: 安全与优化 (3周)
- [ ] 用户认证系统
- [ ] 权限管理
- [ ] API限流
- [ ] 性能优化
- [ ] 压力测试

### Phase 7: 文档与发布 (2周)
- [ ] API文档
- [ ] 用户手册
- [ ] 开发者指南
- [ ] 示例服务
- [ ] 打包发布

**总计：24周（约6个月）**

---

## 12. 参考架构

### 类似系统参考
- **Kubernetes**：服务编排理念
- **systemd**：服务生命周期管理
- **PM2**：进程管理
- **Airflow**：任务调度
- **Portainer**：容器管理Web界面

### 技术文档
- Qt Documentation: https://doc.qt.io/
- QuickJS: https://bellard.org/quickjs/
- Monaco Editor: https://microsoft.github.io/monaco-editor/
- D3.js: https://d3js.org/
- WebSocket Protocol: https://tools.ietf.org/html/rfc6455

---

## 附录A：数据流示例

### 创建并启动服务的完整流程

```
1. 用户在Web界面创建服务
   ↓
2. 前端发送 POST /api/v1/services
   {
     "id": "my-service",
     "name": "My Service",
     "jsEntryPoint": "/uploaded/service.js"
   }
   ↓
3. ServiceApiHandler 接收请求
   ↓
4. ServiceManager::registerService()
   - 验证服务元数据
   - 创建服务目录（sandbox, shared, logs）
   - 写入数据库
   - 保存到内存映射
   ↓
5. 返回成功响应
   ↓
6. 用户点击"启动"按钮
   ↓
7. 前端发送 POST /api/v1/services/my-service/start
   ↓
8. ServiceManager::startService()
   - 检查依赖
   - 创建 JsEngine 实例
   - 加载服务 JS 文件
   - 执行 initialize(config)
   - 为服务启动所需的 Driver
   - 更新服务状态为 Running
   - 注册进程到 ProcessMonitor
   ↓
9. WebSocket 推送状态变更事件
   {
     "type": "event",
     "channel": "service.my-service.state",
     "data": { "state": "running" }
   }
   ↓
10. 前端接收事件，更新UI状态指示器
```

---

## 附录B：关键代码片段

### HTTP Server 初始化

```cpp
// main.cpp
#include <QHttpServer>
#include "api/service_handler.h"
#include "api/driver_handler.h"
#include "api/job_handler.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    // 初始化管理器
    auto serviceMgr = new ServiceManager(&app);
    auto driverMgr = new DriverManager(&app);
    auto jobScheduler = new JobScheduler(serviceMgr, &app);
    auto processMon = new ProcessMonitor(&app);
    auto logAggr = new LogAggregator(&app);
    
    // 创建HTTP服务器
    QHttpServer httpServer;
    
    // 注册API路由
    ServiceApiHandler serviceApi(serviceMgr);
    serviceApi.setupRoutes(&httpServer);
    
    DriverApiHandler driverApi(driverMgr);
    driverApi.setupRoutes(&httpServer);
    
    JobApiHandler jobApi(jobScheduler);
    jobApi.setupRoutes(&httpServer);
    
    // 静态文件服务
    httpServer.route("/", []() {
        return QHttpServerResponse::fromFile(":/web/dist/index.html");
    });
    
    // 启动服务器
    const quint16 port = 8080;
    if (!httpServer.listen(QHostAddress::Any, port)) {
        qCritical() << "Failed to start HTTP server on port" << port;
        return 1;
    }
    
    qInfo() << "Platform Manager started on port" << port;
    return app.exec();
}
```

---

## 总结

本设计方案提供了一个完整的、类操作系统级的服务管理平台架构，基于现有的 StdioLink 框架进行扩展。核心特点包括：

1. **完善的服务生命周期管理**：从注册、配置、启动到监控的全流程管理
2. **插件化架构**：Driver 可携带自定义 Web 配置界面
3. **沙盒文件系统**：保证服务隔离与安全
4. **强大的Web管理界面**：在线代码编辑、可视化监控、实时日志
5. **灵活的任务调度**：支持 Cron 表达式的定时任务
6. **完整的进程树追踪**：可视化服务依赖关系

该平台可以作为点云处理、图像处理等领域的统一服务编排引擎，大大简化复杂系统的开发与运维工作。
