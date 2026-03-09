# Repository Guidelines

## Knowledge Base First

- 做任何开发前，必须先从 `doc/knowledge/README.md` 开始。
- 先读目标子系统目录下的 `README.md`，再读对应主题文档；跨子系统需求优先看 `doc/knowledge/08-workflows/`。
- 先用知识库组织出“需求目标 -> 涉及子系统 -> 修改入口 -> 约束/风险 -> 测试入口 -> 文档同步点”的实现路径，再动代码。
- `doc/knowledge/` 是 AI 开发检索层；`doc/manual/` 只在需要展开细节时作为详细参考。

## Project Map

- `src/stdiolink/`：核心库，含 `protocol/`、`driver/`、`host/`、`console/`
- `src/stdiolink_service/`：QuickJS Service 运行时与绑定
- `src/stdiolink_server/`：Server 管理、扫描、调度、HTTP/SSE/WS
- `src/drivers/`：生产/示例 Driver
- `src/data_root/services/`、`src/data_root/projects/`：Service 与 Project 事实源
- `src/tests/`、`src/smoke_tests/`、`src/webui/`：测试与前端

## Build And Run

- Windows：`build.bat` 或 `build.bat Release`
- Unix：`./build.sh` 或 `./build.sh Release`
- 全量测试：`ctest --test-dir build --output-on-failure`
- Smoke：`python src/smoke_tests/run_smoke.py --plan all`
- WebUI 测试：在 `src/webui/` 下运行 `npm run test`、`npx playwright test`

## Driver Standalone Run

- Windows 直接运行 Driver 前，先把 `build\runtime_debug\bin` 加到 `PATH`
- Driver 路径：`build\runtime_debug\data_root\drivers\stdio.drv.{name}\stdio.drv.{name}.exe`

## Development Constraints

- 使用 Qt 类型处理 IO 与 JSON：`QFile`、`QTextStream`、`QString`、`QJsonObject`、`QJsonArray`
- 协议始终是 JSONL；Windows 管道读取必须使用 `QTextStream::readLine()`
- 命名：namespace `lower_case`，类 `CamelCase`，方法 `camelBack`，成员 `m_`
- 公共协议、元数据、HTTP API、运行行为变更时，需同步更新 `doc/knowledge/`，必要时再补 `doc/manual/`
- Web Dashboard API 变更时，同步维护 `doc/todolist.md`

## Testing Rules

- 协议、元数据、Host/Driver、JS 绑定改动优先补 GTest
- `src/stdiolink/host/` 请求生命周期改动必须覆盖 Driver 早退场景，参考 `test_host_driver.cpp`、`test_wait_any.cpp`
- 新增需要端到端覆盖的功能时，补 `src/smoke_tests/mXX_*.py`，并注册到 `run_smoke.py` 与 `CMakeLists.txt`

## Commit And Encoding

- 提交遵循 Conventional Commits：`feat:`、`fix:`、`docs:`、`refactor:`、`test:`、`chore:`
- PowerShell 读写文本文件时按 UTF-8 处理
