# stdiolink 项目指引（简版）

## 知识库优先

- 做任何开发前，必须先从 `doc/knowledge/README.md` 组织信息。
- 先读目标子系统目录的 `README.md`，再进入具体主题文档；跨子系统实现路径先看 `doc/knowledge/08-workflows/`。
- 先在知识库中构建“需求目标、涉及模块、修改入口、约束风险、测试与文档同步点”的准确实现路径，再开始编码。
- `doc/knowledge/` 是紧凑检索层；`doc/manual/` 是展开细节的详细参考。

## 最小目录图

- `src/stdiolink/`：协议、Driver、Host、Console
- `src/stdiolink_service/`：JS 运行时与绑定
- `src/stdiolink_server/`：Server 管理与 HTTP/实时通信
- `src/drivers/`：Driver 实现
- `src/data_root/services/`、`src/data_root/projects/`：Service/Project
- `src/tests/`、`src/smoke_tests/`、`src/webui/`：测试与前端

## 常用命令

- 构建：`build.bat` / `build.bat Release`
- 全量测试：`ctest --test-dir build --output-on-failure`
- Smoke：`python src/smoke_tests/run_smoke.py --plan all`
- Driver 单跑前先把 `build\runtime_debug\bin` 加入 `PATH`

## 开发约束

- Qt 优先：`QFile`、`QTextStream`、`QJsonObject`
- 协议固定为 JSONL；Windows 管道读取必须用 `QTextStream::readLine()`
- 提交遵循 Conventional Commits
- 任何公共行为、协议、元数据、API 变更，先同步 `doc/knowledge/`，再按需补 `doc/manual/`
- 新增需要端到端覆盖的功能时，补 `src/smoke_tests/mXX_*.py` 并注册到 `run_smoke.py` 和 `CMakeLists.txt`
