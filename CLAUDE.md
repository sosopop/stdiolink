# stdiolink 项目指引（简版）

## 知识库优先

- 做任何开发前，先从 `doc/knowledge/README.md` 建立上下文。
- 先读对应子系统目录 `README.md`，再读主题文档；跨子系统需求先看 `doc/knowledge/08-workflows/`。
- 先用知识库整理出准确的需求实现路径，再开始设计、编码、测试和文档同步。
- `doc/knowledge/` 是主检索入口；`doc/manual/` 是补充细节的详细参考。

## 最小项目地图

- `src/stdiolink/`：协议、Driver、Host、Console
- `src/stdiolink_service/`：JS Service 运行时与绑定
- `src/stdiolink_server/`：扫描、Project/Instance、调度、HTTP/SSE/WS
- `src/drivers/`：Driver 实现
- `src/data_root/services/`、`src/data_root/projects/`：Service/Project 模板
- `src/tests/`、`src/smoke_tests/`、`src/webui/`：测试与前端

## 常用命令

- 构建：`build.bat [Release]` / `./build.sh [Release]`
- 测试：`ctest --test-dir build --output-on-failure`
- Smoke：`python src/smoke_tests/run_smoke.py --plan all`
- 构建/测试/发布：`python tools/release.py <build|test|publish>`

## 开发硬约束

- 使用 Qt 类型处理文件、文本流和 JSON
- 协议固定为 JSONL；Windows 管道读取使用 `QTextStream::readLine()`
- 命名：类 `CamelCase`、方法 `camelBack`、成员 `m_`
- 公共行为或接口变更时，先更新 `doc/knowledge/`，必要时再更新 `doc/manual/`
- API 变更时检查测试、前后端调用点和 `doc/todolist.md`
