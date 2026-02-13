# 里程碑 60：布局框架、路由系统与设计系统

> **前置条件**: 里程碑 59 已完成（WebUI 工程脚手架与 API 客户端层已就绪）
> **目标**: 实现 WebUI 的整体布局框架（Header + Sidebar + Content）、React Router 路由系统、Ant Design 主题定制（暗色工业风）、全局样式与设计令牌，为后续所有页面提供统一的视觉与导航基础

---

## 1. 目标

- 安装并配置 Ant Design 5.x、React Router v6、Zustand 4.x
- 实现 `AppLayout` 布局组件：顶部栏（Header）+ 可折叠侧边栏（Sidebar）+ 主内容区（Content）
- 实现 React Router 路由配置（声明式，含嵌套路由和 404 页面）
- 实现 Ant Design 主题定制（暗色/亮色双主题，对齐设计文档 4.x 节的色彩体系）
- 实现全局样式：CSS 变量（设计令牌）、字体栈、玻璃态、光晕效果，支持 `[data-theme]` 切换
- 实现通知系统（Toast / Message）封装
- 实现响应式侧边栏（折叠/展开）
- 实现主题切换功能（持久化存储）

---

## 2. 背景与问题

所有 WebUI 页面共享统一的布局框架和导航系统。设计文档定义了"Modern Minimalist"视觉风格，支持深色（默认）和亮色两种模式。需要通过 Ant Design 主题定制和 CSS 变量系统落地，并提供无缝切换体验。本里程碑建立视觉基础，后续页面里程碑只需关注业务逻辑。

**范围**：布局 + 路由 + 主题 + 全局样式。页面内容为空占位组件。

---

## 3. 技术要点

### 3.1 新增依赖

```json
{
  "dependencies": {
    "antd": "^5.20.0",
    "react-router-dom": "^6.26.0",
    "zustand": "^4.5.0",
    "@ant-design/icons": "^5.4.0"
  }
}
```

### 3.2 路由配置

```typescript
// src/router.tsx
const routes = [
  {
    path: '/',
    element: <AppLayout />,
    children: [
      { index: true, element: <Navigate to="/dashboard" replace /> },
      { path: 'dashboard', element: <DashboardPage /> },
      { path: 'services', element: <ServicesPage /> },
      { path: 'services/:id', element: <ServiceDetailPage /> },
      { path: 'projects', element: <ProjectsPage /> },
      { path: 'projects/create', element: <ProjectCreatePage /> },
      { path: 'projects/:id', element: <ProjectDetailPage /> },
      { path: 'instances', element: <InstancesPage /> },
      { path: 'instances/:id', element: <InstanceDetailPage /> },
      { path: 'drivers', element: <DriversPage /> },
      { path: 'drivers/:id', element: <DriverDetailPage /> },
      { path: 'driverlab', element: <DriverLabPage /> },
      { path: '*', element: <NotFoundPage /> },
    ]
  }
];
```

> 所有页面组件在本里程碑中为空占位（仅显示页面名称），在后续里程碑中逐步实现。

### 3.3 布局结构

```
┌─────────────────────────────────────────────────────────┐
│  [LOGO] stdiolink             [Server Status] [Theme]   │ ← Header (48px)
├────────┬────────────────────────────────────────────────┤
│        │                                                │
│  📊    │  <Outlet />                                    │
│  Dashboard│                                             │
│        │  路由匹配的页面内容                              │
│  📦    │                                                │
│  Services│                                              │
│        │                                                │
│  🗂️    │                                                │
│  Projects│                                              │
│        │                                                │
│  🚀    │                                                │
│  Instances│                                             │
│        │                                                │
│  🔌    │                                                │
│  Drivers│                                               │
│        │                                                │
│  ──────│                                                │
│  🧪    │                                                │
│  DriverLab│                                             │
│        │                                                │
└────────┴────────────────────────────────────────────────┘
  ↑ Sidebar (200px / 64px collapsed)
```

### 3.4 Ant Design 主题配置

```typescript
// src/theme/antd-theme.ts
import type { ThemeConfig } from 'antd';
import { theme } from 'antd';

const commonToken = {
  colorPrimary: '#6366F1',       // Indigo 500
  colorSuccess: '#10B981',       // Emerald 500
  colorWarning: '#F59E0B',       // Amber 500
  colorError: '#EF4444',         // Red 500
  colorInfo: '#3B82F6',          // Blue 500
  fontFamily: "'Inter', system-ui, -apple-system, BlinkMacSystemFont, sans-serif",
  fontFamilyCode: "'JetBrains Mono', 'Fira Code', monospace",
  borderRadius: 8,
  fontSize: 14,
  wireframe: false,
};

export const darkTheme: ThemeConfig = {
  algorithm: theme.darkAlgorithm,
  token: {
    ...commonToken,
    colorBgBase: '#0F1117',        // Surface-Base
    colorBgContainer: '#1E222D',   // Surface-Layer1
    colorBgElevated: '#2A2F3E',    // Surface-Layer2
    colorBorderSecondary: 'rgba(255, 255, 255, 0.06)',
  },
  components: {
    Layout: {
      siderBg: '#0F1117',
      headerBg: 'rgba(30, 34, 45, 0.7)',
      bodyBg: '#0F1117',
    },
    Menu: {
      darkItemBg: 'transparent',
      darkItemSelectedBg: 'rgba(99, 102, 241, 0.15)',
      darkItemSelectedColor: '#6366F1',
    },
    Table: {
      headerBg: '#1E222D',
      rowHoverBg: '#2A2F3E',
      borderColor: 'rgba(255, 255, 255, 0.06)',
    },
    Card: { actionsBg: 'rgba(0, 0, 0, 0.1)' }
  }
};

export const lightTheme: ThemeConfig = {
  algorithm: theme.defaultAlgorithm,
  token: {
    ...commonToken,
    colorBgBase: '#F3F4F6',        // Cool Gray 100
    colorBgContainer: '#FFFFFF',   // White
    colorBgElevated: '#F9FAFB',    // Cool Gray 50
    colorBorderSecondary: '#E5E7EB',
  },
  components: {
    Layout: {
      siderBg: '#FFFFFF',
      headerBg: 'rgba(255, 255, 255, 0.7)',
      bodyBg: '#F3F4F6',
    },
    Menu: {
      itemBg: 'transparent',
      itemSelectedBg: 'rgba(99, 102, 241, 0.1)',
      itemSelectedColor: '#6366F1',
    },
    Table: {
      headerBg: '#F9FAFB',
      rowHoverBg: '#F3F4F6',
      borderColor: '#E5E7EB',
    },
    Card: { actionsBg: '#F9FAFB' }
  }
};
```

### 3.5 CSS 变量（设计令牌）

```css
/* src/styles/variables.css */
:root {
  /* Default (Dark) */
  --surface-base: #0F1117;
  --surface-layer1: #1E222D;
  --surface-layer2: #2A2F3E;
  --surface-overlay: rgba(15, 17, 23, 0.8);
  --border-subtle: rgba(255, 255, 255, 0.06);
  --border-focus: rgba(99, 102, 241, 0.4);

  /* Brand */
  --primary-base: #6366F1;
  --primary-hover: #818CF8;
  --primary-dim: rgba(99, 102, 241, 0.15);
  --secondary-pink: #EC4899;

  /* Semantic */
  --color-success: #10B981;
  --color-warning: #F59E0B;
  --color-error: #EF4444;
  --color-info: #3B82F6;

  /* Typography */
  --font-ui: 'Inter', system-ui, -apple-system, BlinkMacSystemFont, sans-serif;
  --font-code: 'JetBrains Mono', 'Fira Code', monospace;

  /* Glass & Shadows (Dark) */
  --glass-bg: rgba(30, 34, 45, 0.7);
  --glass-blur: blur(16px) saturate(180%);
  --glass-border: 1px solid rgba(255, 255, 255, 0.05);
  --shadow-card: 0 4px 6px -1px rgba(0, 0, 0, 0.3);
  --shadow-elevated: 0 20px 25px -5px rgba(0, 0, 0, 0.5);
}

[data-theme='light'] {
  /* Surface (Light) */
  --surface-base: #F3F4F6;
  --surface-layer1: #FFFFFF;
  --surface-layer2: #F9FAFB;
  --surface-overlay: rgba(255, 255, 255, 0.8);
  --border-subtle: #E5E7EB;
  
  /* Brand Dim (Light) */
  --primary-dim: rgba(99, 102, 241, 0.1);

  /* Glass & Shadows (Light) */
  --glass-bg: rgba(255, 255, 255, 0.7);
  --glass-border: 1px solid rgba(229, 231, 235, 0.5);
  --shadow-card: 0 4px 6px -1px rgba(0, 0, 0, 0.05), 0 2px 4px -1px rgba(0, 0, 0, 0.03);
  --shadow-elevated: 0 20px 25px -5px rgba(0, 0, 0, 0.1), 0 10px 10px -5px rgba(0, 0, 0, 0.04);
}
```

### 3.6 玻璃态与光晕效果

```css
/* src/styles/effects.css */
.glass-panel {
  background: var(--glass-bg);
  backdrop-filter: var(--glass-blur);
  border-bottom: var(--glass-border);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
  margin-right: 8px;
}

.status-dot--running {
  background: var(--color-success);
  box-shadow: 0 0 0 2px rgba(16, 185, 129, 0.2); /* 柔和光晕 */
}

.status-dot--stopped {
  background: #6B7280; /* Gray 500 */
}

.status-dot--error {
  background: var(--color-error);
  box-shadow: 0 0 0 2px rgba(239, 68, 68, 0.2);
}

/* 呼吸感卡片悬停效果 */
.hover-card {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.hover-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--shadow-elevated);
  /* 仅深色模式下变亮边框，亮色模式下无需 */
  border-color: var(--primary-base); 
}
```

### 3.7 布局与主题状态管理

```typescript
// src/stores/useLayoutStore.ts
import { create } from 'zustand';
import { persist } from 'zustand/middleware';

interface LayoutState {
  sidebarCollapsed: boolean;
  themeMode: 'dark' | 'light';
  toggleSidebar: () => void;
  setSidebarCollapsed: (collapsed: boolean) => void;
  setThemeMode: (mode: 'dark' | 'light') => void;
  toggleTheme: () => void;
}

export const useLayoutStore = create<LayoutState>()(
  persist(
    (set) => ({
      sidebarCollapsed: false,
      themeMode: 'dark', // 默认深色
      toggleSidebar: () => set((s) => ({ sidebarCollapsed: !s.sidebarCollapsed })),
      setSidebarCollapsed: (collapsed) => set({ sidebarCollapsed: collapsed }),
      setThemeMode: (mode) => set({ themeMode: mode }),
      toggleTheme: () => set((s) => ({ themeMode: s.themeMode === 'dark' ? 'light' : 'dark' })),
    }),
    {
      name: 'stdiolink-layout-storage',
      partialize: (state) => ({ 
        sidebarCollapsed: state.sidebarCollapsed,
        themeMode: state.themeMode 
      }),
    }
  )
);
```

### 3.8 通知封装

```typescript
// src/utils/notification.ts
import { message, notification } from 'antd';

export const notify = {
  success: (msg: string) => message.success(msg),
  error: (msg: string) => notification.error({ message: '错误', description: msg }),
  warning: (msg: string) => message.warning(msg),
  info: (msg: string) => message.info(msg),
};
```

---

## 4. 实现方案

### 4.1 AppLayout 组件

```tsx
// src/components/Layout/AppLayout.tsx
import { Layout } from 'antd';
import { Outlet } from 'react-router-dom';
import { AppHeader } from './AppHeader';
import { AppSidebar } from './AppSidebar';
import { useLayoutStore } from '@/stores/useLayoutStore';

export const AppLayout: React.FC = () => {
  const collapsed = useLayoutStore((s) => s.sidebarCollapsed);

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <AppSidebar collapsed={collapsed} />
      <Layout>
        <AppHeader />
        <Layout.Content style={{ padding: 24, background: 'var(--surface-base)' }}>
          <Outlet />
        </Layout.Content>
      </Layout>
    </Layout>
  );
};
```

### 4.2 AppSidebar 组件

```tsx
// src/components/Layout/AppSidebar.tsx
// 使用 Ant Design Menu 组件
// 导航项：Dashboard / Services / Projects / Instances / Drivers / DriverLab
// 支持折叠/展开
// 当前路由高亮
// Badge 显示运行中实例数（后续里程碑接入 SSE 实时更新）
```

### 4.3 AppHeader 组件

```tsx
// src/components/Layout/AppHeader.tsx
// 左侧：Logo + 产品名称 + 侧边栏折叠按钮
// 右侧：Server 连接状态指示灯（后续里程碑接入）
// 玻璃态背景
```

### 4.4 NotFoundPage

```tsx
// src/pages/NotFound.tsx
// 404 页面，提供返回 Dashboard 的链接
```

### 4.5 页面占位组件

为每个路由创建最小占位组件：

```tsx
// src/pages/Dashboard/index.tsx
export const DashboardPage: React.FC = () => (
  <div>Dashboard - 待实现（M61）</div>
);
```

---

## 5. 文件变更清单

### 5.1 新增文件

**布局组件**：
- `src/webui/src/components/Layout/AppLayout.tsx`
- `src/webui/src/components/Layout/AppHeader.tsx`
- `src/webui/src/components/Layout/AppSidebar.tsx`
- `src/webui/src/components/Layout/AppLayout.module.css`

**主题与样式**：
- `src/webui/src/theme/antd-theme.ts`
- `src/webui/src/styles/variables.css`
- `src/webui/src/styles/effects.css`
- `src/webui/src/styles/global.css`

**路由**：
- `src/webui/src/router.tsx`

**状态管理**：
- `src/webui/src/stores/useLayoutStore.ts`

**工具**：
- `src/webui/src/utils/notification.ts`

**页面占位**：
- `src/webui/src/pages/Dashboard/index.tsx`
- `src/webui/src/pages/Services/index.tsx`
- `src/webui/src/pages/Services/Detail.tsx`
- `src/webui/src/pages/Projects/index.tsx`
- `src/webui/src/pages/Projects/Create.tsx`
- `src/webui/src/pages/Projects/Detail.tsx`
- `src/webui/src/pages/Instances/index.tsx`
- `src/webui/src/pages/Instances/Detail.tsx`
- `src/webui/src/pages/Drivers/index.tsx`
- `src/webui/src/pages/Drivers/Detail.tsx`
- `src/webui/src/pages/DriverLab/index.tsx`
- `src/webui/src/pages/NotFound.tsx`

**测试**：
- `src/webui/src/__tests__/components/AppLayout.test.tsx`
- `src/webui/src/__tests__/components/AppSidebar.test.tsx`
- `src/webui/src/__tests__/components/AppHeader.test.tsx`
- `src/webui/src/__tests__/stores/useLayoutStore.test.ts`
- `src/webui/src/__tests__/router.test.tsx`
- `src/webui/src/__tests__/utils/notification.test.ts`

### 5.2 修改文件

- `src/webui/package.json` — 新增 antd、react-router-dom、zustand 依赖
- `src/webui/src/App.tsx` — 集成 RouterProvider + ConfigProvider（主题）
- `src/webui/src/main.tsx` — 导入全局样式

---

## 6. 测试与验收

### 6.1 单元测试场景

**AppLayout（AppLayout.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 渲染布局 | Header、Sidebar、Content 区域均存在 |
| 2 | Sidebar 折叠 | 点击折叠按钮后 Sidebar 宽度变窄 |
| 3 | Sidebar 展开 | 再次点击后恢复 |

**AppSidebar（AppSidebar.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 4 | 渲染所有导航项 | Dashboard/Services/Projects/Instances/Drivers/DriverLab 均可见 |
| 5 | 点击导航项 | 路由跳转到对应路径 |
| 6 | 当前路由高亮 | 活动菜单项有选中样式 |
| 7 | 折叠模式 | 仅显示图标，不显示文字 |

**AppHeader（AppHeader.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | 渲染 Logo | 包含 "stdiolink" 文字 |
| 9 | 折叠按钮 | 点击后触发 toggleSidebar |

**useLayoutStore（useLayoutStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 10 | 初始状态 | `sidebarCollapsed` 为 false |
| 11 | `toggleSidebar()` | 状态翻转 |
| 12 | `setSidebarCollapsed(true)` | 状态设为 true |

**路由（router.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 13 | 访问 `/` | 重定向到 `/dashboard` |
| 14 | 访问 `/dashboard` | 渲染 Dashboard 占位 |
| 15 | 访问 `/services` | 渲染 Services 占位 |
| 16 | 访问 `/projects` | 渲染 Projects 占位 |
| 17 | 访问 `/unknown` | 渲染 404 页面 |

**通知（notification.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 18 | `notify.success()` | 调用 `message.success` |
| 19 | `notify.error()` | 调用 `notification.error` |

### 6.2 验收标准

- 布局框架正确渲染（Header + Sidebar + Content）
- 路由导航正常工作
- 暗色主题正确应用（背景色、文字色、组件色）
- 玻璃态效果可见
- 侧边栏折叠/展开正常
- 404 页面正常显示
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：Ant Design 暗色主题与自定义 CSS 变量冲突
  - 控制：优先使用 Ant Design 的 `token` 系统定制，仅在 Ant Design 不覆盖的场景使用 CSS 变量
- **风险 2**：React Router v6 嵌套路由与 Layout 的 Outlet 配合问题
  - 控制：使用标准的 `createBrowserRouter` + `RouterProvider` 模式，参考 React Router 官方文档

---

## 8. 里程碑完成定义（DoD）

- 布局框架（Header + Sidebar + Content）正确渲染
- 路由系统配置完成，所有路径可导航
- Ant Design 暗色主题定制完成
- 全局样式（CSS 变量、玻璃态、光晕）就绪
- 侧边栏折叠/展开功能正常
- 页面占位组件就位
- 对应单元测试完成并通过
- 本里程碑文档入库
