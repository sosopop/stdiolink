# 里程碑 69：E2E 集成测试与生产构建流水线

> **前置条件**: 里程碑 58-68 已完成（全部 WebUI 功能模块已就绪）
> **目标**: 实现端到端集成测试覆盖核心用户流程、配置 Vite 生产构建优化、集成到现有发布脚本、实现前端构建产物嵌入 Server 的完整流程

---

## 1. 目标

- 实现 E2E 集成测试：使用 Playwright 覆盖核心用户流程（Dashboard → Services → Projects → Instances → Drivers → DriverLab）
- 配置 Vite 生产构建：代码分割、Tree-shaking、Monaco Editor 独立 chunk、gzip 压缩
- 实现构建产物集成：前端 `dist/` 输出到 Server 可服务的目录，与 M58 静态文件服务对接
- 扩展发布脚本：`tools/publish_release.sh` 包含 WebUI 构建步骤
- 配置 CI 流水线：前端 lint + type-check + unit test + build 验证
- 实现环境变量配置：开发/生产环境 API 地址切换

---

## 2. 背景与问题

M58-M68 完成了全部 WebUI 功能开发，但缺少端到端测试验证和生产构建配置。需要确保：(1) 核心用户流程在真实浏览器中可用；(2) 生产构建产物体积合理、加载性能达标；(3) 构建流程可集成到现有发布脚本中。

**范围**：E2E 测试 + 生产构建配置 + 发布集成。

**非目标**：Docker 容器化部署（后续按需补充）。Nginx 反向代理配置（文档已在设计文档中提供，不需要代码实现）。

---

## 3. 技术要点

### 3.1 E2E 测试框架

```json
{
  "devDependencies": {
    "@playwright/test": "^1.40.0"
  }
}
```

Playwright 配置：

```typescript
// src/webui/playwright.config.ts
import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './e2e',
  timeout: 30000,
  retries: 1,
  use: {
    baseURL: 'http://localhost:3000',
    screenshot: 'only-on-failure',
    trace: 'on-first-retry',
  },
  webServer: {
    command: 'npm run dev',
    port: 3000,
    reuseExistingServer: true,
  },
  projects: [
    { name: 'chromium', use: { browserName: 'chromium' } },
  ],
});
```

### 3.2 E2E 测试场景

测试需要 Mock API（使用 Playwright 的 `page.route()` 拦截 API 请求），不依赖真实后端。

**核心用户流程**：

1. **Dashboard 流程**：访问首页 → 看到 KPI 卡片 → 看到服务器信息 → 看到活跃实例列表
2. **Services 流程**：导航到 Services → 看到列表 → 搜索过滤 → 点击进入详情 → 切换 Tab（概览/文件/Schema/项目引用）
3. **Projects 流程**：导航到 Projects → 看到列表 → 点击创建 → 完成四步向导 → 看到新项目 → 进入详情 → 启动项目
4. **Instances 流程**：导航到 Instances → 看到列表 → 进入详情 → 查看进程树 → 查看资源图表 → 查看日志
5. **Drivers 流程**：导航到 Drivers → 看到列表 → 进入详情 → 查看命令列表 → 导出元数据
6. **DriverLab 流程**：导航到 DriverLab → 选择 Driver → 连接 → 选择命令 → 执行 → 查看消息流

### 3.3 API Mock 策略

```typescript
// e2e/mocks/api-handlers.ts
// 集中定义所有 API Mock 响应

export const mockServices: ServiceInfo[] = [
  { id: 'demo-service', name: 'Demo Service', version: '1.0.0', ... },
];

export const mockProjects: Project[] = [
  { id: 'demo-project', name: 'Demo Project', serviceId: 'demo-service', ... },
];

// ... 其他 Mock 数据

// e2e/helpers/setup-mocks.ts
export async function setupApiMocks(page: Page): Promise<void> {
  await page.route('/api/services', (route) =>
    route.fulfill({ json: mockServices })
  );
  await page.route('/api/projects', (route) =>
    route.fulfill({ json: mockProjects })
  );
  await page.route('/api/server/status', (route) =>
    route.fulfill({ json: mockServerStatus })
  );
  // ... 其他路由
}
```

### 3.4 Vite 生产构建配置

```typescript
// src/webui/vite.config.ts 生产构建增强
export default defineConfig({
  // ... 已有配置
  build: {
    outDir: 'dist',
    sourcemap: false,
    // 代码分割
    rollupOptions: {
      output: {
        manualChunks: {
          'vendor-react': ['react', 'react-dom', 'react-router-dom'],
          'vendor-antd': ['antd', '@ant-design/icons'],
          'vendor-monaco': ['monaco-editor'],
          'vendor-charts': ['recharts'],
        },
      },
    },
    // 压缩
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,   // 生产环境移除 console
        drop_debugger: true,
      },
    },
    // chunk 大小警告阈值
    chunkSizeWarningLimit: 500,  // KB
  },
});
```

### 3.5 构建产物目标

生产构建后的 `dist/` 目录结构：

```
dist/
├── index.html
├── assets/
│   ├── index-[hash].js        (~150KB gzipped, 主应用)
│   ├── index-[hash].css       (~50KB gzipped, 样式)
│   ├── vendor-react-[hash].js (~40KB gzipped)
│   ├── vendor-antd-[hash].js  (~200KB gzipped)
│   ├── vendor-monaco-[hash].js (~800KB gzipped, 按需加载)
│   └── vendor-charts-[hash].js (~50KB gzipped)
└── favicon.ico
```

性能目标：
- 首屏加载（不含 Monaco）：< 500KB gzipped
- Monaco Editor：按需加载，仅在 Services 文件编辑和 Schema JSON 编辑时加载
- 首次内容绘制（FCP）：< 1.5s
- 可交互时间（TTI）：< 3s

### 3.6 环境变量

```bash
# .env.development
VITE_API_BASE_URL=/api

# .env.production
VITE_API_BASE_URL=/api
```

```typescript
// src/webui/src/api/client.ts 中使用
const apiClient = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || '/api',
  // ...
});
```

### 3.7 发布脚本集成

扩展 `tools/publish_release.sh`，在打包前构建 WebUI：

```bash
# 新增 WebUI 构建步骤
build_webui() {
  echo "Building WebUI..."
  cd src/webui
  npm ci
  npm run build
  cd ../..

  # 复制构建产物到发布目录
  mkdir -p "$OUTPUT_DIR/webui"
  cp -r src/webui/dist/* "$OUTPUT_DIR/webui/"
}
```

同时需要在 CMakeLists.txt 中添加可选的 WebUI 构建集成（将 `dist/` 复制到 `build/bin/webui/`）。

### 3.8 CI 流水线配置

```yaml
# .github/workflows/webui.yml
name: WebUI CI

on:
  push:
    paths: ['src/webui/**']
  pull_request:
    paths: ['src/webui/**']

jobs:
  lint-and-test:
    runs-on: ubuntu-latest
    defaults:
      run:
        working-directory: src/webui
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 18
          cache: 'npm'
          cache-dependency-path: src/webui/package-lock.json
      - run: npm ci
      - run: npm run lint
      - run: npm run type-check
      - run: npm run test -- --run
      - run: npm run build

  e2e:
    runs-on: ubuntu-latest
    needs: lint-and-test
    defaults:
      run:
        working-directory: src/webui
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 18
          cache: 'npm'
          cache-dependency-path: src/webui/package-lock.json
      - run: npm ci
      - run: npx playwright install --with-deps chromium
      - run: npx playwright test
      - uses: actions/upload-artifact@v4
        if: failure()
        with:
          name: playwright-report
          path: src/webui/playwright-report/
```

### 3.9 package.json 脚本补充

```json
{
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview",
    "lint": "eslint src --ext .ts,.tsx",
    "type-check": "tsc --noEmit",
    "test": "vitest",
    "test:run": "vitest --run",
    "test:e2e": "playwright test",
    "test:e2e:ui": "playwright test --ui"
  }
}
```

---

## 4. 实现方案

### 4.1 E2E 测试文件结构

```
src/webui/
├── e2e/
│   ├── mocks/
│   │   └── api-handlers.ts      — Mock 数据和路由处理
│   ├── helpers/
│   │   └── setup-mocks.ts       — Mock 设置工具
│   ├── dashboard.spec.ts        — Dashboard 流程测试
│   ├── services.spec.ts         — Services 流程测试
│   ├── projects.spec.ts         — Projects 流程测试
│   ├── instances.spec.ts        — Instances 流程测试
│   ├── drivers.spec.ts          — Drivers 流程测试
│   └── driverlab.spec.ts        — DriverLab 流程测试
├── playwright.config.ts
```

### 4.2 Dashboard E2E 测试

```typescript
// e2e/dashboard.spec.ts
import { test, expect } from '@playwright/test';
import { setupApiMocks } from './helpers/setup-mocks';

test.describe('Dashboard', () => {
  test.beforeEach(async ({ page }) => {
    await setupApiMocks(page);
    await page.goto('/');
  });

  test('显示 KPI 统计卡片', async ({ page }) => {
    await expect(page.getByText('Services')).toBeVisible();
    await expect(page.getByText('Projects')).toBeVisible();
    await expect(page.getByText('Instances')).toBeVisible();
  });

  test('显示服务器信息', async ({ page }) => {
    await expect(page.getByText('服务器信息')).toBeVisible();
  });

  test('导航到 Services', async ({ page }) => {
    await page.getByRole('link', { name: /services/i }).click();
    await expect(page).toHaveURL(/\/services/);
  });
});
```

### 4.3 Projects 创建向导 E2E 测试

```typescript
// e2e/projects.spec.ts（部分）
test('创建项目完整流程', async ({ page }) => {
  await page.goto('/projects');

  // 点击创建
  await page.getByRole('button', { name: /创建/i }).click();

  // 步骤 1：选择 Service
  await page.getByText('demo-service').click();
  await page.getByRole('button', { name: /下一步/i }).click();

  // 步骤 2：基本信息
  await page.getByLabel(/ID/i).fill('test-project');
  await page.getByLabel(/名称/i).fill('Test Project');
  await page.getByRole('button', { name: /下一步/i }).click();

  // 步骤 3：配置参数（Schema 表单自动生成）
  await expect(page.getByLabel(/host/i)).toBeVisible();
  await page.getByRole('button', { name: /下一步/i }).click();

  // 步骤 4：调度策略
  await page.getByText('Manual').click();
  await page.getByRole('button', { name: /创建/i }).click();

  // 验证创建成功
  await expect(page.getByText('创建成功')).toBeVisible();
});
```

### 4.4 构建验证脚本

```bash
#!/bin/bash
# tools/verify-webui-build.sh
# 验证 WebUI 构建产物

set -e

cd src/webui

echo "=== WebUI Build Verification ==="

# 1. 构建
npm run build

# 2. 检查产物存在
if [ ! -f dist/index.html ]; then
  echo "ERROR: dist/index.html not found"
  exit 1
fi

# 3. 检查产物大小
TOTAL_SIZE=$(du -sk dist/ | cut -f1)
echo "Total build size: ${TOTAL_SIZE}KB"

if [ "$TOTAL_SIZE" -gt 10240 ]; then
  echo "WARNING: Build size exceeds 10MB"
fi

# 4. 检查关键文件
JS_COUNT=$(find dist/assets -name "*.js" | wc -l)
CSS_COUNT=$(find dist/assets -name "*.css" | wc -l)
echo "JS chunks: $JS_COUNT"
echo "CSS files: $CSS_COUNT"

echo "=== Build verification passed ==="
```

---

## 5. 文件变更清单

### 5.1 新增文件

**Playwright 配置**：
- `src/webui/playwright.config.ts` — Playwright 配置

**E2E 测试**：
- `src/webui/e2e/mocks/api-handlers.ts` — Mock 数据定义
- `src/webui/e2e/helpers/setup-mocks.ts` — Mock 设置工具
- `src/webui/e2e/dashboard.spec.ts` — Dashboard E2E 测试
- `src/webui/e2e/services.spec.ts` — Services E2E 测试
- `src/webui/e2e/projects.spec.ts` — Projects E2E 测试
- `src/webui/e2e/instances.spec.ts` — Instances E2E 测试
- `src/webui/e2e/drivers.spec.ts` — Drivers E2E 测试
- `src/webui/e2e/driverlab.spec.ts` — DriverLab E2E 测试

**环境配置**：
- `src/webui/.env.development` — 开发环境变量
- `src/webui/.env.production` — 生产环境变量

**CI 配置**：
- `.github/workflows/webui.yml` — WebUI CI 流水线

**构建工具**：
- `tools/verify-webui-build.sh` — 构建验证脚本

### 5.2 修改文件

- `src/webui/vite.config.ts` — 添加生产构建优化配置（manualChunks、terser、chunkSizeWarningLimit）
- `src/webui/package.json` — 添加 Playwright 依赖和脚本
- `tools/publish_release.sh` — 添加 WebUI 构建步骤

---

## 6. 测试与验收

### 6.1 E2E 测试场景

**Dashboard（dashboard.spec.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 首页加载 | KPI 卡片可见 |
| 2 | 服务器信息 | 版本号、运行时长可见 |
| 3 | 活跃实例列表 | 实例条目可见 |
| 4 | 侧边栏导航 | 点击各菜单项正确跳转 |

**Services（services.spec.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 5 | 列表加载 | 服务列表可见 |
| 6 | 搜索过滤 | 输入关键词后列表过滤 |
| 7 | 进入详情 | 点击行后跳转到详情页 |
| 8 | Tab 切换 | 概览/文件/Schema/项目引用 Tab 可切换 |
| 9 | 创建服务 | 打开创建对话框，填写信息，提交成功 |

**Projects（projects.spec.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 10 | 列表加载 | 项目列表可见 |
| 11 | 创建向导完整流程 | 四步完成，创建成功 |
| 12 | 进入详情 | 详情页加载正常 |
| 13 | 启动项目 | 点击启动按钮，状态更新 |
| 14 | 停止项目 | 确认对话框，停止成功 |
| 15 | 启用开关 | 切换启用状态 |

**Instances（instances.spec.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 16 | 列表加载 | 实例列表可见 |
| 17 | 进入详情 | 详情页加载正常 |
| 18 | 进程树 Tab | 进程树渲染可见 |
| 19 | 资源 Tab | 图表渲染可见 |
| 20 | 日志 Tab | 日志内容可见 |
| 21 | 终止实例 | 确认对话框，终止成功 |

**Drivers（drivers.spec.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 22 | 列表加载 | 驱动列表可见 |
| 23 | 进入详情 | 详情页加载正常 |
| 24 | 命令 Tab | 命令列表可见 |
| 25 | 导出元数据 | 触发文件下载 |

**DriverLab（driverlab.spec.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 26 | 页面加载 | Driver 选择下拉可见 |
| 27 | 选择 Driver | 连接按钮可用 |
| 28 | 消息流面板 | 面板可见 |

### 6.2 构建验证场景

| # | 场景 | 验证点 |
|---|------|--------|
| 29 | `npm run build` | 构建成功，无错误 |
| 30 | `dist/index.html` 存在 | 入口文件生成 |
| 31 | JS chunk 分割 | vendor-react、vendor-antd、vendor-monaco 独立 chunk |
| 32 | 总体积 | 不含 Monaco < 500KB gzipped |
| 33 | `npm run lint` | 无 lint 错误 |
| 34 | `npm run type-check` | TypeScript 编译无错误 |
| 35 | `npm run test:run` | 全部单元测试通过 |

### 6.3 验收标准

- 全部 E2E 测试在 Chromium 中通过
- 生产构建成功，产物体积合理
- 代码分割正确（Monaco 独立 chunk）
- CI 流水线配置完整（lint + type-check + test + build）
- 发布脚本包含 WebUI 构建步骤
- 构建产物可被 M58 的静态文件服务正确服务
- 全部验证场景通过

---

## 7. 风险与控制

- **风险 1**：E2E 测试不稳定（flaky tests）
  - 控制：使用 Playwright 的 `waitFor` 和 `expect` 自动等待机制；配置 1 次重试；失败时保存截图和 trace
- **风险 2**：Monaco Editor chunk 体积过大
  - 控制：使用 `manualChunks` 将 Monaco 拆分为独立 chunk，按需加载；仅在 Services 文件编辑和 Schema JSON 编辑页面加载
- **风险 3**：CI 环境与本地环境差异
  - 控制：CI 使用固定 Node 18 版本；`npm ci` 确保依赖一致；Playwright 使用固定浏览器版本
- **风险 4**：发布脚本跨平台兼容性
  - 控制：WebUI 构建步骤使用 npm 命令（跨平台）；文件复制使用 `cp -r`（Linux/macOS）；Windows 用户使用 Git Bash

---

## 8. 里程碑完成定义（DoD）

- E2E 测试覆盖 6 个核心模块的主要用户流程
- 生产构建配置完成，产物体积达标
- CI 流水线配置完成并可运行
- 发布脚本集成 WebUI 构建
- 构建产物可被静态文件服务正确服务
- 全部 E2E 测试通过
- 本里程碑文档入库
