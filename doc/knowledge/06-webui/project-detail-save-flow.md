# Project Detail Save Flow

## Purpose

说明 Project 详情页保存 `config` / `schedule` 时，前端为什么必须提交完整 Project payload。

## Runtime Chain

`ProjectDetail` -> `useProjectsStore.updateProject()` -> `projectsApi.update()` -> `PUT /api/projects/:id`

`ProjectOverview` -> `useProjectsStore.setEnabled()` -> `projectsApi.setEnabled()` -> `PATCH /api/projects/:id/enabled`

## Key Rule

- `PUT /api/projects/:id` 按全量 `Project` 校验
- 后端要求 `name`、`serviceId`
- 前端保存 `config` 或 `schedule` 时不能只传局部字段，必须带上当前 Project 的完整更新体
- `enabled` 切换走独立 `PATCH`，不要复用 `PUT`
- `setEnabled` 成功后前端还要刷新 `runtime`，否则详情页状态胶囊和列表页运行态会滞后

## Affected UI

- Project 详情页 `Config` tab 保存
- Project 详情页 `Schedule` tab 保存
- Project 详情页 `Overview` tab 启用/禁用按钮
- `setEnabled` 不走全量保存；它走独立的 `PATCH /api/projects/:id/enabled`

## Modify Entry

- 页面入口：`src/webui/src/pages/Projects/Detail.tsx`
- Store：`src/webui/src/stores/useProjectsStore.ts`
- API：`src/webui/src/api/projects.ts`
- 后端校验：`src/stdiolink_server/model/project.cpp`

## Tests

- `src/webui/src/pages/Projects/__tests__/ProjectDetail.test.tsx`
- `src/tests/test_api_router.cpp`
