# 里程碑 51：Project 列表增强与运维操作 API

> **前置条件**: 里程碑 49 已完成（CORS 中间件已就绪）
> **目标**: 增强 Project 列表查询（过滤 + 分页），新增启停开关、Project 级日志、批量运行态查询

---

## 1. 目标

- 增强 `GET /api/projects` — 支持 `serviceId`/`status`/`enabled` 过滤 + `page`/`pageSize` 分页
- 新增 `PATCH /api/projects/{id}/enabled` — 切换 Project 启用状态
- 新增 `GET /api/projects/{id}/logs` — Project 级日志（不依赖 Instance 存活）
- 新增 `GET /api/projects/runtime` — 批量运行态查询（避免 N+1 请求）
- 响应格式从纯数组变为 `{ projects: [...], total, page, pageSize }` 对象，这是**破坏性变更**。WebUI 是唯一消费方且尚未上线，直接切换新格式
- 路由注册时 `/api/projects/runtime` **必须**在 `/api/projects/<arg>` 之前注册。QHttpServer Router 按注册顺序遍历、首次命中返回，若动态路由先注册，`runtime` 会被当作 `id=runtime` 吞掉

---

## 2. 背景与问题

当前 `GET /api/projects` 返回全量列表，无过滤和分页，Project 数量较多时效率低下。WebUI Dashboard 需要按条件筛选和分页展示。启停开关避免了 start/stop 的重量级操作。Project 级日志支持 Instance 退出后仍可查看历史日志。批量运行态接口避免逐个查询 N+1 问题。

---

## 3. 技术要点

### 3.1 `GET /api/projects` 增强

新增查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `serviceId` | string | 按 Service ID 过滤 |
| `status` | string | 按状态过滤：`running`/`stopped`/`disabled`/`invalid` |
| `enabled` | bool | 按启用状态过滤 |
| `page` | int | 页码（从 1 开始，默认 1） |
| `pageSize` | int | 每页数量（默认 20，最大 100） |

响应增加分页元信息：

```json
{
  "projects": [...],
  "total": 42,
  "page": 1,
  "pageSize": 20
}
```

实现：在内存中过滤 `m_projects`，计算分页偏移，无需数据库查询。

**状态判定逻辑**：

- `running`：`project.valid && project.enabled` 且有运行中 Instance
- `stopped`：`project.valid && project.enabled` 且无运行中 Instance
- `disabled`：`project.valid && !project.enabled`
- `invalid`：`!project.valid`

### 3.2 `PATCH /api/projects/{id}/enabled`

请求体：

```json
{
  "enabled": false
}
```

响应（200 OK）：返回更新后的 Project JSON。

实现要点：

- 仅修改 `enabled` 字段，通过 `ProjectManager::updateProject()` 写入文件
- 如 `enabled: false`，通过 `ScheduleEngine` 停止该 Project 的调度，终止运行中 Instance
- 如 `enabled: true`，恢复调度（如 daemon 模式自动拉起）

### 3.3 `GET /api/projects/{id}/logs?lines=N`

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `lines` | int | 返回最后 N 行（默认 100，范围 1-5000） |

响应（200 OK）：

```json
{
  "projectId": "silo-a",
  "lines": ["line1", "line2", "..."],
  "logPath": "/path/to/logs/silo-a.log",
  "totalLines": 1500
}
```

实现：直接读取 `logs/{projectId}.log` 文件的最后 N 行。采用从文件尾部反向读取的策略，避免加载整个文件。

日志文件不存在时返回空数组（非 404），因为 Project 可能从未运行过。

### 3.4 `GET /api/projects/runtime`

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `ids` | string | 逗号分隔的 Project ID 列表（为空则返回所有） |

响应（200 OK）：

```json
{
  "runtimes": [
    {
      "id": "silo-a",
      "enabled": true,
      "valid": true,
      "status": "running",
      "runningInstances": 1,
      "instances": [...],
      "schedule": {
        "type": "daemon",
        "timerActive": false,
        "restartSuppressed": false,
        "consecutiveFailures": 0,
        "shuttingDown": false,
        "autoRestarting": true
      }
    }
  ]
}
```

实现：复用 `handleProjectRuntime()` 中已有的单 Project 运行态逻辑，批量遍历。

---

## 4. 实现方案

### 4.1 `handleProjectList` 改造

```cpp
QHttpServerResponse ApiRouter::handleProjectList(const QHttpServerRequest& req) {
    const QUrlQuery query(req.url());

    // 解析过滤参数
    const QString filterServiceId = query.queryItemValue("serviceId");
    const QString filterStatus = query.queryItemValue("status");
    const QString filterEnabled = query.queryItemValue("enabled");

    // 解析分页参数
    int page = qMax(1, query.queryItemValue("page").toInt());
    int pageSize = qBound(1, query.queryItemValue("pageSize").toInt(), 100);
    if (!query.hasQueryItem("page")) page = 1;
    if (!query.hasQueryItem("pageSize")) pageSize = 20;

    // 过滤
    QVector<const Project*> filtered;
    for (const auto& p : m_manager->projects()) {
        if (!filterServiceId.isEmpty() && p.serviceId != filterServiceId) continue;
        if (!filterEnabled.isEmpty()) {
            bool en = (filterEnabled == "true");
            if (p.enabled != en) continue;
        }
        if (!filterStatus.isEmpty()) {
            QString st = computeProjectStatus(p);
            if (st != filterStatus) continue;
        }
        filtered.append(&p);
    }

    // 分页
    int total = filtered.size();
    int offset = (page - 1) * pageSize;
    // ... 构建响应
}
```

### 4.2 日志文件尾部读取

> **注意**：`api_router.cpp` 中已有 `readTailLines()` 实现（用于 `GET /api/instances/{id}/logs`），但当前实现是加载整个文件后取尾部行。本里程碑应将其重构为从文件尾部反向读取的高效实现，并在两处复用。

```cpp
QStringList readLastLines(const QString& filePath, int count) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    // 从文件尾部向前读取块，逐块查找换行符
    const qint64 fileSize = file.size();
    const qint64 chunkSize = 4096;
    QStringList lines;
    QByteArray buffer;
    qint64 pos = fileSize;

    while (pos > 0 && lines.size() < count) {
        qint64 readSize = qMin(chunkSize, pos);
        pos -= readSize;
        file.seek(pos);
        buffer.prepend(file.read(readSize));

        // 从 buffer 中提取完整行
        // ...
    }

    return lines;
}
```

### 4.3 enabled 切换与调度联动

```cpp
QHttpServerResponse ApiRouter::handleProjectEnabled(const QString& id,
                                                     const QHttpServerRequest& req) {
    auto it = m_manager->projects().find(id);
    if (it == m_manager->projects().end())
        return errorResponse(StatusCode::NotFound, "project not found");

    auto body = QJsonDocument::fromJson(req.body()).object();
    if (!body.contains("enabled") || !body["enabled"].isBool())
        return errorResponse(StatusCode::BadRequest, "enabled field required (bool)");

    bool newEnabled = body["enabled"].toBool();
    it->enabled = newEnabled;

    // 持久化
    QString error;
    if (!ProjectManager::saveProject(m_manager->dataRoot() + "/projects", *it, error))
        return errorResponse(StatusCode::InternalServerError, error);

    // 调度联动
    if (!newEnabled) {
        m_manager->scheduleEngine()->stopProject(id);
        m_manager->instanceManager()->terminateByProject(id);
    } else {
        // ScheduleEngine 已有 resumeProject() 方法（见 schedule_engine.h），
        // 仅恢复单个 Project 的调度状态，无需调用重量级的 startScheduling()
        m_manager->scheduleEngine()->resumeProject(id);
    }

    return jsonResponse(projectToJson(*it));
}
```

### 4.4 路由注册（关键）

QHttpServer Router 按注册顺序遍历规则列表，首次命中即返回。`/api/projects/runtime` **必须**在 `/api/projects/<arg>` 之前注册，否则 `runtime` 会被动态占位符当作 `id=runtime` 吞掉：

```cpp
server.route("/api/projects/runtime", Method::Get, ...);   // 必须先注册
server.route("/api/projects/<arg>", Method::Get, ...);     // 后注册
```

---

## 5. 文件变更清单

### 5.1 修改文件

- `src/stdiolink_server/http/api_router.h` — 新增 `handleProjectEnabled`/`handleProjectLogs`/`handleProjectRuntimeBatch` handler 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现四个 handler + 路由注册 + `handleProjectList` 改造
- `src/stdiolink_server/manager/schedule_engine.h` — 复用现有 `stopProject()`/`resumeProject()`；无需新增 `startProject()`
- `src/stdiolink_server/manager/schedule_engine.cpp` — 如需补充细节逻辑再扩展

### 5.2 测试文件

- 修改 `src/tests/test_api_router.cpp` — 新增四个 API 测试组

---

## 6. 测试与验收

### 6.1 单元测试场景

**Project 列表过滤与分页**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 无过滤参数 | 返回所有 Project + 分页元信息 |
| 2 | `serviceId` 过滤 | 仅返回匹配的 Project |
| 3 | `enabled=true` 过滤 | 仅返回启用的 Project |
| 4 | `enabled=false` 过滤 | 仅返回禁用的 Project |
| 5 | `status=running` 过滤 | 仅返回有运行中 Instance 的 Project |
| 6 | `page=1&pageSize=2` | 返回前 2 条 + `total` 为全量 |
| 7 | `page=999` 越界 | 返回空 `projects` 数组 + 正确 `total` |
| 8 | `pageSize=0` 或负数 | 归一化为 1 |
| 9 | `pageSize=200` 超上限 | 归一化为 100 |
| 10 | 组合过滤 + 分页 | 先过滤再分页 |

**启停开关**：

| # | 场景 | 验证点 |
|---|------|--------|
| 11 | `PATCH enabled=false` | Project 标记为 disabled，调度停止 |
| 12 | `PATCH enabled=true` | Project 恢复 enabled，调度恢复 |
| 13 | 请求体缺少 `enabled` | 返回 400 |
| 14 | Project 不存在 | 返回 404 |
| 15 | `enabled` 非 bool 类型 | 返回 400 |

**Project 级日志**：

| # | 场景 | 验证点 |
|---|------|--------|
| 16 | 正常读取日志 | 返回最后 N 行 |
| 17 | `lines=10` | 最多返回 10 行 |
| 18 | 日志文件不存在 | 返回空数组（非 404） |
| 19 | `lines` 超出范围（> 5000） | 归一化为 5000 |
| 20 | `lines=0` 或负数 | 归一化为 100 |
| 21 | Project 不存在 | 返回 404 |

**批量运行态**：

| # | 场景 | 验证点 |
|---|------|--------|
| 22 | 无 `ids` 参数 | 返回所有 Project 运行态 |
| 23 | `ids=p1,p2` | 仅返回 p1 和 p2 的运行态 |
| 24 | `ids` 中包含不存在的 ID | 跳过不存在的，返回存在的 |
| 25 | 运行态含 schedule 信息 | daemon 类型包含 `restartSuppressed`/`consecutiveFailures` |

### 6.2 验收标准

- `GET /api/projects` 支持过滤和分页。响应格式从纯数组变为 `{ projects, total, page, pageSize }` 对象（破坏性变更，WebUI 尚未上线故可直接切换）
- `PATCH /api/projects/{id}/enabled` 切换启停并联动调度
- `GET /api/projects/{id}/logs` 从文件尾部高效读取日志
- `GET /api/projects/runtime` 批量返回运行态
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`handleProjectList` 响应格式变更
  - 控制：响应从纯数组变为 `{ projects, total, page, pageSize }` 对象，是破坏性变更。WebUI 尚未上线，无外部消费方，直接切换新格式。已有测试需同步更新断言
- **风险 2**：enabled 切换与调度引擎的线程安全
  - 控制：所有操作在 Qt 主事件循环中串行执行，无并发问题
- **风险 3**：大日志文件尾部读取性能
  - 控制：从文件尾部按块反向读取，不加载整个文件；`lines` 上限 5000
- **风险 4**：`/api/projects/runtime` 被 `/api/projects/<arg>` 路由吞掉
  - 控制：QHttpServer Router 按注册顺序首次命中返回，静态路由 `/api/projects/runtime` 必须先于动态路由 `/api/projects/<arg>` 注册。增加回归测试覆盖该路径

---

## 8. 里程碑完成定义（DoD）

- `GET /api/projects` 支持过滤 + 分页
- `PATCH /api/projects/{id}/enabled` 实现并联动调度
- `GET /api/projects/{id}/logs` 实现
- `GET /api/projects/runtime` 实现
- 对应单元测试完成并通过
- 本里程碑文档入库
