# 里程碑 64：Instances 实例监控模块

> **前置条件**: 里程碑 60 已完成（布局框架已就绪），里程碑 63 已完成（Projects 模块可用，Instance 从 Project 启动）
> **目标**: 实现 Instances 模块的完整 UI：列表页（搜索/筛选/批量终止）、详情页（概览/进程树可视化/资源监控图表/日志查看器），支持实时资源轮询和进程树递归渲染

---

## 1. 目标

- 实现 Instances 列表页：表格展示（含实时 CPU/内存指标）、搜索、按 Project/状态筛选、终止操作
- 实现 Instance 详情页：概览 Tab、进程树 Tab、资源监控 Tab、日志 Tab
- 实现进程树可视化组件：递归树形渲染、CPU 颜色编码、可折叠节点、汇总统计
- 实现资源监控图表：CPU/内存趋势图（前端本地聚合，Recharts）、实时指标卡片
- 实现日志查看器：文本规则过滤（级别关键字）、搜索高亮、自动滚动、导出
- 实现 Zustand Store：`useInstancesStore`
- 资源数据 5s 轮询刷新
- 新增依赖：`recharts`（趋势图渲染）

---

## 2. 背景与问题

Instances 是运行中的进程实体，用户需要实时监控其资源占用、进程树结构和日志输出。设计文档将其定位为"实时监控 / Live Monitor"，强调高频遥测和可视化拓扑。

**范围**：Instances 列表 + 详情（概览/进程树/资源/日志）。

**非目标**：React Flow 力导向拓扑图（复杂度高，后续按需补充）。进程树使用树形列表渲染。

### 新增依赖

```json
{
  "dependencies": {
    "recharts": "^2.x"
  }
}
```

> **说明**：M59 初始化工程时未包含 `recharts`，本里程碑首次引入图表库，需在 `package.json` 中添加。

---

## 3. 技术要点

### 3.1 列表页功能

| 功能 | 实现 |
|------|------|
| 表格列 | ID、Project、Service、状态、PID、CPU%、内存、运行时长、操作 |
| 搜索 | 前端过滤（ID/Project 模糊匹配） |
| 筛选 | Project 下拉、状态下拉 |
| 终止 | 调用 `POST /api/instances/{id}/terminate`，需确认对话框 |
| 刷新 | 手动刷新按钮 + 30s 自动轮询 |

### 3.2 详情页 Tab 结构

| Tab | 内容 | API | 刷新策略 |
|-----|------|-----|----------|
| 概览 | 基本信息 + 资源快照 | `GET /instances/{id}` + `GET /instances/{id}/resources` | 5s 轮询 |
| 进程树 | 递归进程树 + 汇总统计 | `GET /instances/{id}/process-tree` | 5s 轮询 |
| 资源 | CPU/内存趋势图 + 实时指标 | `GET /instances/{id}/resources` | 5s 轮询 |
| 日志 | 日志查看器 | `GET /instances/{id}/logs` | 手动刷新 |

### 3.3 进程树组件

```typescript
// src/components/Instances/ProcessTree.tsx
interface ProcessTreeProps {
  tree: ProcessTreeNode;
  summary: ProcessTreeSummary;
}

interface ProcessTreeNode {
  pid: number;
  name: string;
  commandLine: string;
  status: string;
  startedAt?: string;
  resources: {
    cpuPercent: number;
    memoryRssBytes: number;
    memoryVmsBytes: number;
    threadCount: number;
    uptimeSeconds: number;
  };
  children: ProcessTreeNode[];
}

interface ProcessTreeSummary {
  totalProcesses: number;
  totalCpuPercent: number;
  totalMemoryRssBytes: number;
  totalThreads: number;
}
```

视觉要求：
- 树形缩进渲染，每层缩进 24px
- 可折叠/展开节点（有子进程时显示 ▶/▼）
- CPU 颜色编码：< 50% 绿色、50-80% 橙色、> 80% 红色
- 内存使用 `formatBytes()` 格式化
- 底部汇总统计卡片

### 3.4 资源监控

**趋势图**：前端维护环形缓冲区（最近 60 个采样点，5s 间隔 = 5 分钟窗口）：

```typescript
interface ResourceSample {
  timestamp: number;
  cpuPercent: number;
  memoryRssBytes: number;
  threadCount: number;
}
```

使用 Recharts `AreaChart` 渲染 CPU 和内存两条曲线。

**实时指标卡片**：CPU%、内存、线程数、I/O 读写、运行时长。

### 3.5 日志查看器

复用 M63 中创建的 `LogViewer` 组件，增强功能：

| 功能 | 实现 |
|------|------|
| 级别过滤 | 文本规则过滤（按前缀/关键字匹配 INFO/DEBUG/WARN/ERROR） |
| 搜索 | 全文搜索，匹配行高亮 |
| 自动滚动 | 默认开启，可关闭 |
| 颜色编码 | 基于文本规则：匹配 ERROR 红色、WARN 橙色、INFO 默认、DEBUG 灰色 |
| 导出 | 导出为 TXT 文件 |
| 行数限制 | 前端最多显示 1000 行，超出后截断旧行 |

> **数据契约说明**：`GET /api/instances/{id}/logs` 返回 `lines: string[]`，无结构化日志级别字段。级别筛选和颜色编码必须基于文本匹配实现。

### 3.6 Zustand Store

```typescript
// src/stores/useInstancesStore.ts
interface InstancesState {
  instances: Instance[];
  currentInstance: Instance | null;
  processTree: { tree: ProcessTreeNode; summary: ProcessTreeSummary } | null;
  resources: ProcessInfo[];
  resourceHistory: ResourceSample[];
  logs: string[];
  loading: boolean;
  error: string | null;

  fetchInstances: () => Promise<void>;
  fetchInstanceDetail: (id: string) => Promise<void>;
  fetchProcessTree: (id: string) => Promise<void>;
  fetchResources: (id: string) => Promise<void>;
  fetchLogs: (id: string, params?: { lines?: number }) => Promise<void>;
  terminateInstance: (id: string) => Promise<void>;
  appendResourceSample: (sample: ResourceSample) => void;
}
```

---

## 4. 实现方案

### 4.1 组件树

```
InstancesPage (列表)
├── PageHeader (标题 + 刷新按钮)
├── FilterBar (搜索 + Project/状态筛选)
└── InstancesTable
    ├── StatusDot
    ├── ResourceBadge (CPU/内存小指标)
    └── ActionMenu (详情/终止)

InstanceDetailPage (详情)
├── PageHeader (返回 + 标题 + 终止按钮)
├── Tabs
│   ├── OverviewTab
│   │   ├── InstanceInfoCard
│   │   └── ResourceSnapshotCard
│   ├── ProcessTreeTab
│   │   ├── ProcessTree (递归树)
│   │   └── ProcessTreeSummary
│   ├── ResourceTab
│   │   ├── ResourceChart (CPU/内存趋势)
│   │   └── ResourceMetricCards
│   └── LogsTab
│       └── LogViewer (增强版)
└── TerminateModal
```

### 4.2 ProcessTreeNode 组件

```tsx
// src/components/Instances/ProcessTreeNode.tsx
const ProcessTreeNode: React.FC<{ node: ProcessTreeNode; level: number }> = ({
  node, level
}) => {
  const [collapsed, setCollapsed] = useState(false);
  const cpuColor = node.resources.cpuPercent > 80 ? 'var(--color-error)' :
                   node.resources.cpuPercent > 50 ? 'var(--color-warning)' :
                   'var(--color-success)';

  return (
    <div style={{ marginLeft: level * 24 }}>
      <div className="process-node">
        {node.children.length > 0 && (
          <button onClick={() => setCollapsed(!collapsed)}>
            {collapsed ? '▶' : '▼'}
          </button>
        )}
        <span className="pid">PID {node.pid}</span>
        <span className="name">{node.name}</span>
        <span style={{ color: cpuColor }}>CPU: {node.resources.cpuPercent.toFixed(1)}%</span>
        <span>内存: {formatBytes(node.resources.memoryRssBytes)}</span>
        <span>线程: {node.resources.threadCount}</span>
      </div>
      {!collapsed && node.children.map(child => (
        <ProcessTreeNode key={child.pid} node={child} level={level + 1} />
      ))}
    </div>
  );
};
```

---

## 5. 文件变更清单

### 5.1 新增文件

**页面**：
- `src/webui/src/pages/Instances/index.tsx` — 列表页（替换占位）
- `src/webui/src/pages/Instances/Detail.tsx` — 详情页（替换占位）

**组件**：
- `src/webui/src/components/Instances/InstancesTable.tsx`
- `src/webui/src/components/Instances/InstanceInfoCard.tsx`
- `src/webui/src/components/Instances/ProcessTree.tsx`
- `src/webui/src/components/Instances/ProcessTreeNode.tsx`
- `src/webui/src/components/Instances/ProcessTreeSummary.tsx`
- `src/webui/src/components/Instances/ResourceChart.tsx`
- `src/webui/src/components/Instances/ResourceMetricCards.tsx`
- `src/webui/src/components/Instances/ResourceSnapshotCard.tsx`
- `src/webui/src/components/Instances/TerminateModal.tsx`
- `src/webui/src/components/Common/ResourceBadge.tsx`

**Store**：
- `src/webui/src/stores/useInstancesStore.ts`

**测试**：
- `src/webui/src/__tests__/pages/Instances.test.tsx`
- `src/webui/src/__tests__/pages/InstanceDetail.test.tsx`
- `src/webui/src/__tests__/components/InstancesTable.test.tsx`
- `src/webui/src/__tests__/components/ProcessTree.test.tsx`
- `src/webui/src/__tests__/components/ProcessTreeNode.test.tsx`
- `src/webui/src/__tests__/components/ResourceChart.test.tsx`
- `src/webui/src/__tests__/components/ResourceMetricCards.test.tsx`
- `src/webui/src/__tests__/stores/useInstancesStore.test.ts`

---

## 6. 测试与验收

### 6.1 单元测试场景

**InstancesTable（InstancesTable.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染实例列表 | 显示 ID、Project、PID、状态 |
| 2 | 空列表 | 显示"暂无运行中的实例" |
| 3 | 搜索过滤 | 输入关键词后列表过滤 |
| 4 | 终止操作 | 显示确认对话框 |
| 5 | 确认终止 | 调用 `terminateInstance` API |

**ProcessTree（ProcessTree.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 6 | 渲染单节点树 | 显示根进程信息 |
| 7 | 渲染多层树 | 子进程正确缩进 |
| 8 | 折叠节点 | 点击后子节点隐藏 |
| 9 | 展开节点 | 再次点击后子节点显示 |
| 10 | CPU 颜色编码 | >80% 红色、50-80% 橙色、<50% 绿色 |
| 11 | 内存格式化 | 显示 `256 MB` 而非原始字节数 |

**ProcessTreeSummary（ProcessTreeSummary.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 12 | 渲染汇总 | 显示总进程数、总 CPU、总内存、总线程 |

**ResourceChart（ResourceChart.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 13 | 渲染图表 | Recharts AreaChart 存在 |
| 14 | 无数据 | 显示空状态 |
| 15 | 数据更新 | 图表平滑更新 |

**ResourceMetricCards（ResourceMetricCards.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 16 | 渲染指标卡片 | CPU、内存、线程、I/O、运行时长 |
| 17 | 数据为空 | 显示 `--` 占位 |

**LogViewer 增强（LogViewer.test.tsx 补充）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 18 | 级别过滤 | 选择 ERROR 后仅显示匹配 ERROR 关键字的行 |
| 19 | 搜索高亮 | 匹配文本高亮显示 |
| 20 | 颜色编码 | 匹配 ERROR 的行红色、匹配 WARN 的行橙色 |
| 21 | 导出 | 触发文件下载 |
| 22 | 行数限制 | 超过 1000 行后旧行被截断 |

**useInstancesStore（useInstancesStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 23 | `fetchInstances()` 成功 | instances 列表更新 |
| 24 | `fetchProcessTree(id)` | processTree 更新 |
| 25 | `fetchResources(id)` | resources 更新 |
| 26 | `fetchLogs(id)` | logs 更新 |
| 27 | `terminateInstance(id)` | 调用 API 并从列表移除 |
| 28 | `appendResourceSample()` | resourceHistory 追加 |
| 29 | resourceHistory 超过 60 条 | 旧采样点被移除 |

### 6.2 验收标准

- Instances 列表页正确展示、搜索、筛选
- Instance 详情页四个 Tab 正常工作
- 进程树递归渲染正确，折叠/展开正常
- CPU 颜色编码正确
- 资源趋势图正常渲染和更新
- 日志查看器功能完整（文本规则过滤/搜索/导出）
- 终止操作正常
- 5s 轮询刷新正常
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：5s 轮询频率对服务端压力
  - 控制：仅在详情页激活时轮询；页面不可见时暂停；列表页使用 30s 间隔
- **风险 2**：进程树层级过深导致渲染性能问题
  - 控制：递归深度限制为 10 层；超出后显示"..."省略
- **风险 3**：日志量大导致前端内存压力
  - 控制：前端最多保留 1000 行；使用虚拟滚动（react-window）优化长列表

---

## 6. 风险与控制

- **风险 1**：5s 轮询频率对服务端压力
  - 控制：仅在详情页激活时轮询；页面不可见时暂停；列表页使用 30s 间隔
- **风险 2**：进程树层级过深导致渲染性能问题
  - 控制：递归深度限制为 10 层；超出后显示"..."省略
- **风险 3**：日志量大导致前端内存压力
  - 控制：前端最多保留 1000 行；使用虚拟滚动（react-window）优化长列表

---

## 7. UI/UX 设计师建议 (针对 Style 06)

Instances 模块是系统的“心跳”，界面应传达出实时、动态且可控的感觉，同时符合 **Style 06** 的高级质感：

1.  **状态呼吸灯 (Breathing Status)**：
    *   **雷达脉冲**：废弃简单的透明度闪烁，全面采用 M60 定义的 `pulse` 动画（光圈扩散消散），模拟雷达扫描效果。
    *   **色彩语义**：严格遵循 Semantic 颜色规范，Success (Emerald), Warning (Amber), Error (Red)。光晕颜色应使用 `rgba` 版本（如 `rgba(16, 185, 129, 0.2)`）。

2.  **资源图表 (Resource Charts)**：
    *   **极简无框**：图表背景网格线应完全移除 (`stroke="none"`)，坐标轴线隐藏，只保留刻度文字。
    *   **渐变填充**：面积图 (Area Chart) 必须使用 `<defs>` 定义的线性渐变（如 `stop-color="#6366F1"` 从 30% 不透明度过渡到 0%），营造“数据流动”的视觉隐喻，严禁使用纯色块填充。
    *   **动态平滑**：开启 `isAnimationActive`，确保曲线平滑推进。

3.  **进程树 (Process Tree)**：
    *   **连接线**：树形结构的连接线应使用 `var(--text-tertiary)` 或极低透明度的边框色，线条宽度保持 `1px`，起到引导视线的作用即可，不要喧宾夺主。
    *   **告警视觉**：CPU 占用过高时的红色文字应谨慎使用。建议仅高亮数值部分，或在该行左侧添加细微的红色竖条标记 (`border-left`)。

---

## 8. 里程碑完成定义（DoD）

- Instances 列表页完整实现
- Instance 详情页（概览/进程树/资源/日志）完整实现
- 进程树可视化组件完整实现
- 资源监控图表完整实现
- 日志查看器增强功能完整实现（基于文本规则）
- 对应单元测试完成并通过
- 本里程碑文档入库
