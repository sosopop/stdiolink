# 里程碑 16：高级 UI 生成策略

## 1. 目标

UiGenerator 从"基础表单"进化为"结构化交互引擎"。

## 2. 对应需求

- **需求4**: 复杂 UI 自动映射策略 (Advanced UI Generation)

## 3. 支持特性

| 特性 | 说明 |
|------|------|
| 嵌套对象 | Object 类型递归渲染 |
| 动态数组 | Array 可增删元素 |
| 条件显示 | visibleIf 依赖逻辑 |
| 分组排序 | group + order 属性 |

## 4. 条件显示语法

```cpp
struct UIHint {
    QString visibleIf;  // "mode == 'advanced'"
};
```

## 5. 验收标准

1. 嵌套 Object 递归生成表单
2. Array 支持动态增删
3. visibleIf 条件正确解析
4. 字段按 group/order 排序

## 6. 依赖关系

- **前置**: M15 (强类型事件流)
- **后续**: M17 (配置注入闭环)
