# Update Knowledge Base

## Purpose

约束功能开发后何时必须更新知识库，以及最小更新顺序。

## Must Update When

- 公共协议、消息状态、元数据字段、配置 schema、HTTP API、实时事件、调试路径发生变化
- 新增或调整 Driver、Service、Project、调度策略、联调方式
- 新增高频实现路径、联动规则、排障经验或测试入口
- 原有知识库入口已不能准确指向当前实现

## Update Order

1. 更新受影响的主题文档，优先补“修改入口、约束、联动点、测试入口”
2. 更新对应目录 `README.md`，确保索引能找到新内容
3. 如果新增了新的高频需求路径，更新根索引 `doc/knowledge/README.md`

## Minimum Checklist

- 是否影响实现路径判断
- 是否影响前后端/多子系统联动
- 是否影响测试或调试入口
- 是否影响现有 Query Routing

## Placement Rule

- 子系统内规则放对应一级目录
- 跨子系统实现路径放 `08-workflows/`
- 详细背景说明留在 `doc/manual/`，不要把知识库写回成长手册
