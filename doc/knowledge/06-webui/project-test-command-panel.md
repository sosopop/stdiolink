# Project Test Command Panel

## Purpose

说明 Projects 配置页“测试命令”面板由谁生成、依赖哪些数据，以及改哪里。

## User-Facing Behavior

- 面板显示在 Project 配置页顶部，当前展示展开参数和 `--config-file` 两种命令。
- 命令片段直接显示 `stdiolink_service ...`，不额外拼接前置 `cd`。

## Source Of Truth

- `currentProject.config`：Project 已保存配置。
- `currentService.serviceDir`：Service 目录。
- `serverStatus.dataRoot`：Server `data_root` 目录。

## Runtime Chain

`useDashboardStore.fetchServerStatus()` -> `serverStatus.dataRoot`

`useServicesStore.fetchServiceDetail()` -> `currentService.serviceDir`

`ProjectDetail` -> `ProjectConfig` -> `buildProjectCommandLines()`

## Modify Entry

- 页面入口：`src/webui/src/pages/Projects/Detail.tsx`
- 配置页 UI：`src/webui/src/pages/Projects/components/ProjectConfig.tsx`
- 命令生成：`src/webui/src/utils/projectCommandLine.ts`
- CLI 参数展开：`src/webui/src/utils/cliArgs.ts`
- store：`src/webui/src/stores/useDashboardStore.ts`、`src/webui/src/stores/useServicesStore.ts`

## Tests

- `src/webui/src/utils/__tests__/projectCommandLine.test.ts`
- `src/webui/src/pages/Projects/__tests__/ProjectConfig.test.tsx`
- `src/webui/src/pages/Projects/__tests__/ProjectDetail.test.tsx`

## Related

- `project-command-path-rules.md`
- `webui-structure.md`
- `../05-server/server-api-realtime.md`
