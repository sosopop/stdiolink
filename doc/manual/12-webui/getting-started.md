# 开发与构建

## 环境要求

- Node.js 18+
- npm 或 pnpm

## 目录结构

```
src/webui/
├── src/
│   ├── api/            API 客户端模块
│   ├── components/     共享组件（Layout、SchemaForm 等）
│   ├── hooks/          自定义 Hooks
│   ├── locales/        i18n 翻译文件（9 种语言）
│   ├── pages/          页面组件
│   ├── stores/         Zustand 状态仓库
│   ├── styles/         全局样式与 CSS 变量
│   ├── theme/          Ant Design 主题配置
│   ├── types/          TypeScript 类型定义
│   ├── i18n.ts         国际化初始化
│   ├── router.tsx      路由定义
│   └── main.tsx        入口文件
├── vite.config.ts      Vite 配置
├── tsconfig.json       TypeScript 配置
└── package.json        依赖与脚本
```

## 开发服务器

```bash
cd src/webui
npm install
npm run dev
```

开发服务器启动在 `http://localhost:3000`，自动将 `/api` 请求代理到 `http://127.0.0.1:18080`（stdiolink_server）。

WebSocket 请求同样通过代理转发，无需额外配置。

## 生产构建

```bash
npm run build
```

构建产物输出到 `dist/` 目录，包含以下优化：

- 代码分割：`vendor-react`、`vendor-antd`、`vendor-charts` 独立 chunk
- Terser 压缩，移除 `console` 和 `debugger`
- Chunk 大小警告阈值 500KB

## 与 stdiolink_server 集成部署

`stdiolink_server` 可直接托管 WebUI 静态文件。推荐部署流程：

```bash
# 1) 构建前端
cd src/webui
npm run build

# 2) 拷贝 dist 到 data_root/webui（示例）
cp -r dist/* <data_root>/webui/

# 3) 启动 server
stdiolink_server --data-root=<data_root>
```

访问地址：

- `http://127.0.0.1:8080/`
- `http://127.0.0.1:8080/dashboard`

说明：

- 默认静态目录是 `<data_root>/webui`
- 可用 `--webui-dir=<path>` 或 `config.json` 的 `webuiDir` 字段覆盖
- 若 `webuiDir` 是相对路径，则相对于 `data_root` 解析
- `/api/*` 路径始终保留给后端 API，不会被静态资源覆盖

Windows 发布场景可直接使用 `tools/publish_release.ps1`，默认会构建并打包 WebUI（可通过 `--skip-webui` 关闭）。

## 测试

```bash
# 单元测试
npm run test

# E2E 测试
npx playwright test
```

单元测试使用 Vitest + jsdom 环境，E2E 测试使用 Playwright。

## 路径别名

`@/*` 映射到 `./src/*`，在导入时使用：

```typescript
import { useProjectsStore } from '@/stores/useProjectsStore';
import type { ServerEvent } from '@/types/server';
```

## 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VITE_API_BASE_URL` | API 基础路径 | `/api` |
