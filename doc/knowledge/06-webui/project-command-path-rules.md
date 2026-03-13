# Project Command Path Rules

## Purpose

说明 WebUI 中需要展示路径时，何时显示相对 `data_root` 的路径，以及 Projects 测试命令里的 `--config-file` 指向什么。

## What Frontend Knows

- 前端能拿到 `serverStatus.dataRoot` 和 `currentService.serviceDir` 的绝对路径。
- 前端不能直接拿到“发布根目录”；这个概念由 `dataRoot` 的父目录推导。
- Drivers / Services 列表与详情页优先消费后端返回的 `programDisplay` / `serviceDirDisplay`，不再直接展示绝对路径。

## Relative Path Rule

- 只有 `dataRoot` 末段是 `data_root` 时，才尝试做相对化。
- 相对化基准是 `dataRoot` 的父目录；这只影响展示和复制命令，不改变后端运行行为。
- Drivers / Services 的目录展示由后端按 `data_root` 根目录相对化；路径不在 `data_root` 下时统一显示 `--`。

## Config File Rule

- `--config-file` 始终指向项目目录里已保存的 `param.json`。
- 发布目录布局下显示为 `data_root/projects/<id>/param.json`；否则显示为 `projects/<id>/param.json`。
- 未保存的表单编辑不会改变面板里 `--config-file` 的展示内容。

## Decision Rule

- 当前没有专门的后端 API 直接返回最终命令字符串；命令由前端根据现有字段拼装。

## Related

- `project-test-command-panel.md`
- `webui-structure.md`
