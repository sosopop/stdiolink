# Projects Config And Test Commands

## Purpose

说明 Projects 详情页中配置页的“测试命令”面板由谁生成、依赖哪些数据、为什么显示相对路径，以及相关修改入口。

## User-Facing Behavior

- 测试命令面板显示在 Project 配置页顶部
- 面板展示两种命令
  - 展开参数方式：`--config.xxx=...`
  - 配置文件方式：`--config-file=...`
- 配置文件方式直接复用已保存的 `projects/<id>/param.json`
- 命令片段会先切换到一个明确的工作目录，再执行 `stdiolink_service`
- 命令中的 `serviceDir`、`dataRoot`、`--config-file` 路径相对这个工作目录生成

## Source Of Truth

- Project 已保存配置：`currentProject.config`
- Service 目录：`currentService.serviceDir`
- Server `data_root` 目录：`serverStatus.dataRoot`

这些值都来自 Server API，再经由前端 store 和页面 props 传入；前端不直接扫描本地文件系统。

## Runtime Chain

`useDashboardStore.fetchServerStatus()` -> `serverStatus.dataRoot`

`useServicesStore.fetchServiceDetail()` -> `currentService.serviceDir`

`ProjectDetail` -> `ProjectConfig`

`ProjectConfig` -> `buildProjectCommandLines()`

## Modify Entry

- 页面入口：`src/webui/src/pages/Projects/Detail.tsx`
- 配置页 UI：`src/webui/src/pages/Projects/components/ProjectConfig.tsx`
- 命令生成：`src/webui/src/utils/projectCommandLine.ts`
- CLI 参数展开：`src/webui/src/utils/cliArgs.ts`
- Server 状态 store：`src/webui/src/stores/useDashboardStore.ts`
- Service 详情 store：`src/webui/src/stores/useServicesStore.ts`

## Key Rules

### Who Builds The Command

- 测试命令字符串由前端生成
- 后端只返回原始字段，不返回最终命令行字符串
- 当前没有专门的后端 API 来组装 `stdiolink_service` 测试命令

### How Frontend Knows The Paths

- 前端能拿到 `serverStatus.dataRoot` 的绝对路径
- 前端能拿到 `currentService.serviceDir` 的绝对路径
- 前端不能直接获取“软件发布根目录”这个独立字段
- 发布根目录是前端根据 `dataRoot` 的父目录推导出来的展示概念

### Relative Path Policy

- 只有当 `dataRoot` 末段名称为 `data_root` 时，才尝试做相对化
- 相对化基准是 `dataRoot` 的父目录，也就是推导出的发布根目录
- 前端会把这条目录作为命令片段里的 `cd <workingDirectory>`
- 成功相对化时：
  - `D:/pkg/data_root` -> `data_root`
  - `D:/pkg/data_root/services/demo` -> `data_root/services/demo`
- `--config-file` 指向真实项目参数文件：
  - 发布目录布局下显示为 `data_root/projects/<id>/param.json`
  - 非发布目录布局下显示为 `projects/<id>/param.json`
- 非发布目录布局下，工作目录就是 `dataRoot` 本身，因此命令会表现为：
  - `cd /abs/data_root`
  - `stdiolink_service "services/demo" --data-root="." --config-file="projects/<id>/param.json"`
- 这个相对化只影响展示和复制出的示例命令，不改变后端运行行为

### Saved Param File Rule

- 面板不再提供参数文件下载
- `--config-file` 始终指向项目目录下已经存在的 `param.json`
- 命令片段自身保证 `cwd` 正确，不再依赖用户手工切换到特定目录
- 未保存的表单编辑不会改变“测试命令”面板展示的 `--config-file` 内容

## Common Questions

- “整个命令是前端还是后端生成的”
  - 前端生成
- “前端为什么能显示相对路径”
  - 因为后端给了 `dataRoot` 和 `serviceDir` 绝对路径，前端据此推导
- “前端能拿到绝对 `dataRoot` 吗”
  - 能，来自 `serverStatus.dataRoot`
- “为什么不是后端直接给发布根目录”
  - 目前 API 只给 `dataRoot`，相对路径策略在前端实现

## Tests

- `src/webui/src/utils/__tests__/projectCommandLine.test.ts`
- `src/webui/src/pages/Projects/__tests__/ProjectConfig.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectDetail.test.tsx`

## Related

- `webui-structure.md`
- `driverlab-and-events.md`
- `../05-server/server-api-realtime.md`
