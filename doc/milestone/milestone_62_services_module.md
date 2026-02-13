# 里程碑 62：Services 服务工厂模块

> **前置条件**: 里程碑 60 已完成（应用外壳、路由与设计系统已就绪）
> **目标**: 实现 Services 模块的完整 UI，包含列表页、详情页（概览/文件/Schema/项目引用）、创建流程、文件管理器与 Monaco 编辑器集成

---

## 1. 目标

- 实现 Services 列表页（搜索、筛选、分页、扫描目录、创建入口）
- 实现 Service 详情页（概览 Tab、文件 Tab、Schema Tab、项目引用 Tab）
- 实现 Service 创建流程（选择模板 → 基本信息 → 完成）
- 实现 Service 删除（确认对话框，显示关联 Project 数量）
- 实现文件管理器（文件树浏览、新建文件、删除文件）
- 集成 Monaco Editor（JavaScript/JSON 语法高亮、保存、格式化）
- 实现 Schema 表格展示（字段名、类型、必填、默认值）
- 实现 Zustand Services Store
- 单元测试覆盖所有组件和 Store

---

## 2. 背景与问题

Services 是 stdiolink 的服务模板，用户通过 Service 创建 Project。设计文档 §5.2 定义了 Services 模块的完整交互，包括列表视图、详情视图（含文件编辑器和 Schema 展示）、创建流程。

API 依赖（均已实现）：
- `GET /api/services` — 列表
- `GET /api/services/{id}` — 详情
- `POST /api/services` — 创建
- `DELETE /api/services/{id}` — 删除
- `POST /api/services/scan` — 扫描目录
- `GET /api/services/{id}/files` — 文件列表
- `GET/PUT/POST/DELETE /api/services/{id}/files/content` — 文件 CRUD

---

## 3. 技术要点

### 3.1 列表页

功能：
- 搜索框（按 ID/名称模糊搜索，前端过滤）
- 表格列：ID、名称、版本、项目数、操作
- 操作列：查看详情、删除
- 顶部操作：新建 Service、扫描目录
- 分页（前端分页，默认 20 条/页）

### 3.2 详情页 Tab 结构

| Tab | 内容 | API |
|-----|------|-----|
| 概览 | 基本信息（ID/名称/版本/描述/入口脚本/目录） | `GET /api/services/{id}` |
| 文件 | 文件树 + Monaco 编辑器 | `GET /files` + `GET/PUT/POST/DELETE /files/content` |
| Schema | 配置 Schema 表格展示 + JSON 预览 | `GET /api/services/{id}`（schema 字段） |
| 项目引用 | 引用此 Service 的 Project 列表 | `GET /api/projects?serviceId=xxx`（前端过滤） |

### 3.3 Monaco Editor 集成

```typescript
// 安装依赖
// npm install @monaco-editor/react monaco-editor

import Editor from '@monaco-editor/react';

interface FileEditorProps {
  content: string;
  language: string;  // 'javascript' | 'json' | 'markdown'
  onChange: (value: string) => void;
  onSave: () => void;
  readOnly?: boolean;
}
```

编辑器功能：
- 语法高亮（JavaScript、JSON、Markdown）
- 暗色主题（与全局主题一致）
- Ctrl+S 保存快捷键
- 文件修改标记（未保存时 Tab 标题显示 `*`）
- 文件大小限制提示（>1MB 禁止编辑）

Monaco Worker 配置（嵌入式部署必须本地加载，不能依赖 CDN）：

```typescript
// vite.config.ts 中配置 Monaco Worker 本地打包
// 方案 A：使用 vite-plugin-monaco-editor
import monacoEditorPlugin from 'vite-plugin-monaco-editor';

export default defineConfig({
  plugins: [
    react(),
    monacoEditorPlugin({
      languageWorkers: ['editorWorkerService', 'json', 'typescript'],
    }),
  ],
});

// 方案 B：手动配置 Worker（如插件不可用）
// 在入口文件中设置 MonacoEnvironment.getWorkerUrl
```

> **说明**：M58 的嵌入式部署场景下，前端构建产物与后端同目录部署，无法依赖外部 CDN。Monaco Editor 的 Web Worker 必须本地打包。推荐使用 `vite-plugin-monaco-editor` 插件自动处理 Worker 文件的构建和加载。

### 3.4 文件管理器

```typescript
interface FileTreeProps {
  files: ServiceFile[];
  selectedPath: string | null;
  onSelect: (path: string) => void;
  onCreateFile: (path: string) => void;
  onDeleteFile: (path: string) => void;
}
```

核心文件保护：
- `manifest.json`、`index.js`、`config.schema.json` 禁止删除（删除按钮禁用）
- 保存 `manifest.json` 时验证 JSON 格式
- 保存 `config.schema.json` 时验证 JSON 格式

### 3.5 创建流程

步骤 1：选择模板（empty / basic / driver_demo）
步骤 2：填写 Service ID（正则校验 `^[A-Za-z0-9_-]+$`）和名称
步骤 3：确认创建

使用 Ant Design `Steps` + `Modal` 实现向导式创建。

### 3.6 Services Store

```typescript
// src/webui/src/stores/useServicesStore.ts
import { create } from 'zustand';
import { servicesApi } from '@/api/services';
import type { ServiceInfo } from '@/types/service';

interface ServicesState {
  services: ServiceInfo[];
  currentService: ServiceInfo | null;
  loading: boolean;
  error: string | null;

  fetchServices: () => Promise<void>;
  fetchServiceDetail: (id: string) => Promise<void>;
  createService: (data: { id: string; template?: string }) => Promise<boolean>;
  deleteService: (id: string) => Promise<boolean>;
  scanServices: () => Promise<void>;
}
```

---

## 4. 文件变更清单

### 4.1 新增文件

- `src/webui/src/pages/Services/index.tsx` — 列表页（替换占位）
- `src/webui/src/pages/Services/Detail.tsx` — 详情页（替换占位）
- `src/webui/src/pages/Services/components/ServiceTable.tsx` — 服务表格
- `src/webui/src/pages/Services/components/ServiceCreateModal.tsx` — 创建对话框
- `src/webui/src/pages/Services/components/ServiceOverview.tsx` — 概览 Tab
- `src/webui/src/pages/Services/components/ServiceFiles.tsx` — 文件 Tab
- `src/webui/src/pages/Services/components/ServiceSchema.tsx` — Schema Tab
- `src/webui/src/pages/Services/components/ServiceProjects.tsx` — 项目引用 Tab
- `src/webui/src/components/FileTree/FileTree.tsx` — 文件树组件
- `src/webui/src/components/CodeEditor/MonacoEditor.tsx` — Monaco 编辑器封装
- `src/webui/src/stores/useServicesStore.ts` — Services Store

### 4.2 修改文件

- `src/webui/package.json` — 新增 `@monaco-editor/react`、`monaco-editor`、`vite-plugin-monaco-editor` 依赖

### 4.3 测试文件

- `src/webui/src/pages/Services/__tests__/ServiceList.test.tsx`
- `src/webui/src/pages/Services/__tests__/ServiceDetail.test.tsx`
- `src/webui/src/pages/Services/__tests__/ServiceCreateModal.test.tsx`
- `src/webui/src/pages/Services/__tests__/ServiceFiles.test.tsx`
- `src/webui/src/pages/Services/__tests__/ServiceSchema.test.tsx`
- `src/webui/src/components/FileTree/__tests__/FileTree.test.tsx`
- `src/webui/src/components/CodeEditor/__tests__/MonacoEditor.test.tsx`
- `src/webui/src/stores/__tests__/useServicesStore.test.ts`

---

## 5. 测试与验收

### 5.1 单元测试场景

**Services 列表（ServiceList.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染服务列表 | 表格显示所有 Service |
| 2 | 搜索过滤 | 输入关键词后列表过滤 |
| 3 | 空列表 | 显示"暂无服务"提示 |
| 4 | 加载状态 | 显示 loading |
| 5 | 点击行 | 跳转到详情页 |
| 6 | 扫描目录按钮 | 调用 scanServices |
| 7 | 新建按钮 | 打开创建对话框 |

**Service 详情（ServiceDetail.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | 渲染概览 Tab | 显示 ID、名称、版本等信息 |
| 9 | 切换到文件 Tab | 显示文件树和编辑器 |
| 10 | 切换到 Schema Tab | 显示 Schema 表格 |
| 11 | 切换到项目引用 Tab | 显示关联 Project 列表 |
| 12 | Service 不存在 | 显示 404 提示 |

**创建对话框（ServiceCreateModal.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 13 | 选择模板 | 三个模板选项可选 |
| 14 | ID 格式校验 | 非法字符提示错误 |
| 15 | ID 为空 | 提交按钮禁用 |
| 16 | 创建成功 | 对话框关闭，列表刷新 |
| 17 | 创建失败 | 显示错误消息 |

**文件管理（ServiceFiles.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 18 | 渲染文件树 | 显示所有文件 |
| 19 | 点击文件 | 编辑器加载文件内容 |
| 20 | 保存文件 | 调用 fileWrite API |
| 21 | 新建文件 | 调用 fileCreate API |
| 22 | 删除文件 | 确认后调用 fileDelete API |
| 23 | 核心文件禁止删除 | manifest.json 删除按钮禁用 |
| 24 | 大文件提示 | >1MB 文件显示警告 |

**Schema 展示（ServiceSchema.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 25 | 渲染 Schema 表格 | 显示字段名、类型、必填、默认值 |
| 26 | 无 Schema | 显示"未定义配置 Schema" |
| 27 | 嵌套字段 | 正确展示 Object 类型的子字段 |

**FileTree（FileTree.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 28 | 渲染文件列表 | 显示所有文件名 |
| 29 | 选中高亮 | 当前文件有选中样式 |
| 30 | 核心文件标记 | manifest.json 等显示保护标记 |

**MonacoEditor（MonacoEditor.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 31 | 渲染编辑器 | Monaco Editor 容器存在 |
| 32 | 内容变更回调 | onChange 被调用 |
| 33 | 只读模式 | readOnly 时不可编辑 |

**Services Store（useServicesStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 34 | fetchServices 成功 | services 列表更新 |
| 35 | fetchServices 失败 | error 设置 |
| 36 | fetchServiceDetail 成功 | currentService 更新 |
| 37 | createService 成功 | 返回 true，列表刷新 |
| 38 | deleteService 成功 | 从列表移除 |
| 39 | scanServices | 调用 API 后刷新列表 |

### 5.2 验收标准

- Services 列表页正确渲染和搜索
- Service 详情页四个 Tab 正常切换
- 文件管理器可浏览、编辑、创建、删除文件
- Monaco Editor 正确显示和编辑代码
- 核心文件保护机制生效
- 创建流程完整可用
- 全部单元测试通过

---

## 6. 风险与控制

- **风险 1**：Monaco Editor 包体积过大
  - 控制：使用 `vite-plugin-monaco-editor` 本地打包 Worker，配合 Vite `manualChunks` 将 Monaco 拆分为独立 chunk 按需加载；不使用 CDN 加载模式（嵌入式部署不可用）
- **风险 2**：大文件编辑导致浏览器卡顿
  - 控制：>1MB 文件禁止在编辑器中打开，显示提示
- **风险 3**：文件保存与后端 reload 失败
  - 控制：保存 manifest.json/config.schema.json 后检查响应中的 reload 状态，失败时提示用户

---

## 6. 风险与控制

- **风险 1**：Monaco Editor 包体积过大
  - 控制：使用 `vite-plugin-monaco-editor` 本地打包 Worker，配合 Vite `manualChunks` 将 Monaco 拆分为独立 chunk 按需加载；不使用 CDN 加载模式（嵌入式部署不可用）
- **风险 2**：大文件编辑导致浏览器卡顿
  - 控制：>1MB 文件禁止在编辑器中打开，显示提示
- **风险 3**：文件保存与后端 reload 失败
  - 控制：保存 manifest.json/config.schema.json 后检查响应中的 reload 状态，失败时提示用户

---

## 7. UI/UX 设计师建议

为确保 Services 模块符合“现代极简 (Modern Minimalist)”的设计标准，请注意以下细节：

1.  **列表视图 (List View)**：
    *   **呼吸感**：表格单元格的内边距应保持在 `16px` 以上，避免信息过于密集。
    *   **字体**：主列表文字使用 `Inter`，ID 列和版本号建议使用 `JetBrains Mono` 以体现技术感。
    *   **分割线**：表格行分割线颜色应极为低调 (`rgba(255, 255, 255, 0.06)`)，让内容本身构成视觉边界。

2.  **文件管理器 (File Manager)**：
    *   **交互反馈**：文件树节点在悬停时应有微妙的背景色变化 (`Surface-Layer2`)。
    *   **图标区分**：文件夹与不同类型的文件图标颜色应有区分（文件夹通常为暗淡的蓝/黄色，文件为灰白色），但饱和度不宜过高。
    *   **核心文件高亮**：`manifest.json` 等受保护文件可添加微小的锁形图标，而非使用刺眼的红色标记。

3.  **编辑器集成 (Editor Integration)**：
    *   **沉浸式体验**：Monaco Editor 的背景色必须与应用主题完美融合（建议使用 `#1E222D`），避免出现“补丁”感。
    *   **滚动条**：自定义编辑器的滚动条样式，使其更细、更隐蔽，非滚动状态下隐藏。

4.  **空状态 (Empty States)**：
    *   当扫描目录为空时，不要只显示“无数据”。提供一个包含幽默感或鼓励性质的 SVG 插画，并提供清晰的“创建服务”或“扫描”引导按钮。

---

## 8. 里程碑完成定义（DoD）

- Services 列表页实现
- Service 详情页实现（四个 Tab）
- 文件管理器实现
- Monaco Editor 集成
- Service 创建/删除流程实现
- Services Store 实现
- 对应单元测试完成并通过
- 本里程碑文档入库
