# StdioLink WebUI Codex Prototype

这是基于 `doc/stdiolink-ui-design-spec.md` 实现的静态原型页面。

## 文件
- `index.html`: 页面入口
- `styles.css`: 视觉系统与响应式样式
- `app.js`: mock 数据和交互逻辑

## 预览
可直接双击 `index.html` 打开，或在仓库根目录运行：

```bash
cd design/webui_codex
python3 -m http.server 8088
```

然后访问 `http://localhost:8088`。

## 说明
- 不连接后端服务器，全部为前端 mock 数据展示。
- 包含核心页面切换、日志弹窗、快捷键面板、命令面板、刷新加载态等原型交互。
