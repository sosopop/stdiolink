# 主题与国际化

## 双主题系统

WebUI 支持深色和浅色两种主题，用户可通过顶部导航栏的主题切换按钮切换。主题偏好持久化到 `localStorage`。

### 深色主题（默认）

| CSS 变量 | 值 | 说明 |
|----------|-----|------|
| `--bg-body` | `#05060A` | 页面背景 |
| `--surface-card` | `rgba(20, 22, 30, 0.6)` | 卡片背景（毛玻璃） |
| `--surface-glass` | `rgba(10, 10, 16, 0.7)` | 玻璃面板基底 |
| `--brand-primary` | `#6366F1` | 品牌主色（Indigo） |

### 浅色主题

| CSS 变量 | 值 | 说明 |
|----------|-----|------|
| `--bg-body` | `#F8F9FA` | 页面背景 |
| `--surface-card` | `#FFFFFF` | 卡片背景 |
| `--brand-primary` | `#6366F1` | 品牌主色（不变） |

### Glassmorphism 效果

核心视觉效果通过 CSS 类实现：

- `.glass-panel`：毛玻璃容器，使用 `backdrop-filter: blur` 实现半透明模糊
- `.hover-card`：悬停时提升阴影和边框发光
- `.status-dot`：运行状态脉冲动画

### Ant Design 主题集成

Ant Design 组件通过 `theme/antd-theme.ts` 配置主题 Token，与 CSS 变量保持一致：

- 按钮：主色发光阴影（`primaryShadow`）
- 表格：透明背景，融入毛玻璃容器
- 菜单：选中项 Indigo 高亮
- 卡片：半透明背景，圆角 12px

## 国际化（i18n）

WebUI 使用 i18next + react-i18next 实现多语言支持，通过浏览器语言自动检测或手动切换。

### 支持语言

| 代码 | 语言 |
|------|------|
| `en` | English |
| `zh` | 简体中文 |
| `zh-TW` | 繁體中文 |
| `ja` | 日本語 |
| `ko` | 한국어 |
| `fr` | Français |
| `de` | Deutsch |
| `es` | Español |
| `ru` | Русский |

### 语言检测优先级

1. `localStorage` 中保存的用户选择
2. 浏览器 `navigator.language`
3. 回退到 `en`

### 翻译文件

翻译文件位于 `src/locales/` 目录，每种语言一个 JSON 文件（如 `en.json`、`zh.json`）。使用扁平命名空间：

```typescript
// 使用示例
const { t } = useTranslation();
t('dashboard.title');
t('projects.search_placeholder');
```
