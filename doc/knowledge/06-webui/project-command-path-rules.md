# Project Command Path Rules

## Purpose

说明 Projects 配置页测试命令里的路径为何显示为相对路径，以及 `--config-file` 指向什么。

## What Frontend Knows

- 前端能拿到 `serverStatus.dataRoot` 和 `currentService.serviceDir` 的绝对路径。
- 前端不能直接拿到“发布根目录”；这个概念由 `dataRoot` 的父目录推导。

## Relative Path Rule

- 只有 `dataRoot` 末段是 `data_root` 时，才尝试做相对化。
- 相对化基准是 `dataRoot` 的父目录；这只影响展示和复制命令，不改变后端运行行为。

## Config File Rule

- `--config-file` 始终指向项目目录里已保存的 `param.json`。
- 发布目录布局下显示为 `data_root/projects/<id>/param.json`；否则显示为 `projects/<id>/param.json`。
- 未保存的表单编辑不会改变面板里 `--config-file` 的展示内容。

## Decision Rule

- 当前没有专门的后端 API 直接返回最终命令字符串；命令由前端根据现有字段拼装。

## Related

- `project-test-command-panel.md`
- `webui-structure.md`
