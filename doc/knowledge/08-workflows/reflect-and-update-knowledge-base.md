# Reflect And Update Knowledge Base

## Purpose

把一次真实问答、排障、开发或测试过程中的“路径反思”沉淀成通用知识库更新动作，减少下次重复摸索。

## When To Use

- 问题已经解决，但过程中明显绕路
- 回答依赖了临场推断，而不是现成知识库路径
- 找到了高频误区、稳定排障顺序或更优入口
- 用户明确要求“根据这次过程反思并更新知识库”

## Goal

不是重复记录问题答案本身，而是补齐以后更快找到答案的路径。

优先沉淀这几类信息：

- 需求应该先读哪篇文档
- 哪个目录或子系统最先进入
- 哪条测试/调试命令最该先跑
- 哪些误区最容易导致走错路
- 哪些索引和 Query Routing 需要补入口

## Workflow

1. 回看本次过程
   - 用户最初问题是什么
   - 我先去了哪里
   - 中间多花时间确认了什么
   - 最终最快路径其实是什么
2. 提炼“理想最短路径”
   - 需求目标
   - 涉及子系统
   - 修改或排查入口
   - 关键约束/风险
   - 最小测试/验证入口
   - 需要更新哪些索引
3. 判断知识缺口类型
   - 缺主题文档
   - 缺 workflow 文档
   - 缺目录 `README.md` 索引
   - 缺根索引 Query Routing
   - 旧文档已过时或入口不准确
4. 决定落点
   - 子系统内规则：放对应一级目录
   - 跨子系统路径：放 `08-workflows/`
   - 测试/构建/调试通用路径：优先放 `07-testing-build/`
   - 长背景说明：仍放 `doc/manual/`
5. 执行最小更新
   - 先补主题或 workflow 正文
   - 再补对应目录 `README.md`
   - 最后补根索引 `doc/knowledge/README.md`
6. 复核可检索性
   - 用户下次用自然语言提同类问题时，是否能从根索引直接路由到新文档
   - 文档标题是否足够贴近用户问题表述
   - 是否给了可直接执行的命令模板

## Reflection Template

可按下面格式快速整理：

```text
需求目标:
涉及子系统:
本次实际路径:
更优最短路径:
浪费时间的点:
应新增/更新的文档:
需要补的索引入口:
需要补的测试/调试命令模板:
```

## Decision Rules

- 如果反思内容只对单个模块内部有效，先更新对应子系统文档，不要默认塞进 `08-workflows/`
- 如果反思内容可覆盖多个类似提问，优先新增 workflow 文档
- 如果用户的问题是“怎么最快判断/验证/排查”，优先沉淀流程，而不是背景说明
- 如果这次过程里真正卡住的是“命令或入口难找”，优先补命令模板和 Query Routing

## Common Patterns Worth Recording

- 从失败日志确认问题是否当前仍存在
- 不知道先跑哪层测试最省时
- CTest、GTest、runtime 产物关系容易混淆
- 某类需求总要跨多个子系统联查
- 某个高频页面/API/配置入口总是不好定位

## Update Targets Checklist

- 主题文档是否补了“修改入口、约束、测试入口”
- 对应目录 `README.md` 是否出现新入口
- 根索引 `Query Routing` 是否能覆盖用户原始提问方式
- 如果是高频排障路径，`08-workflows/` 是否已有对应文档

## Prompt Pattern

后续可以直接这样触发这条工作流：

- “根据这次过程反思，按知识库反思工作流更新文档”
- “按 `reflect-and-update-knowledge-base.md` 的流程补知识库”
- “把这次排障沉淀成通用知识路径”

## Related

- `update-knowledge-base.md`
- `debug-change-entry.md`
- `../07-testing-build/choose-test-entry.md`
- `../07-testing-build/verify-reported-failure.md`
