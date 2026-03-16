# stdiolink Copilot Instructions

## Start with the knowledge base

- Before taking any action (searching code, editing files, running tests, or proposing an implementation), read `doc/knowledge/README.md`.
- Route the task through the knowledge base first: map `goal -> affected subsystems -> code entry points -> constraints/risks -> test entry -> documentation sync points`.
- Read the target subsystem `README.md` next (`00-overview` / `04-service` / `05-server` / `06-webui` / `07-testing-build`), then the topic file you need.
- For cross-subsystem work or dependency routing, start from `doc/knowledge/08-workflows/`.
- Treat `doc/knowledge/` as the primary AI retrieval layer; use `doc/manual/` only when you need deeper detail.

## Build, test, and lint

- Windows build: `build.bat` or `build.bat Release`
- Unix build: `./build.sh` or `./build.sh Release`
- Full native test run: `ctest --test-dir build --output-on-failure`
- Inspect what CTest actually registered before debugging test mismatches: `ctest --test-dir build -N -V`
- Run one registered CTest target: `ctest --test-dir build --output-on-failure -R <registered_test_name>`
- Run one GTest case directly from the assembled runtime: `build\runtime_debug\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName`
- Run all smoke tests: `python src/smoke_tests/run_smoke.py --plan all`
- Run one smoke plan or case: `python src/smoke_tests/run_smoke.py --plan mXX_name --case S01`
- Native static check: `run_code_check.bat` or `./run_code_check.sh`
- WebUI unit tests: `cd src/webui && npm run test`
- One WebUI test file: `cd src/webui && npx vitest run src/api/__tests__/client.test.ts`
- WebUI E2E: `cd src/webui && npx playwright test`
- One Playwright spec: `cd src/webui && npx playwright test e2e/<spec>.ts`
- WebUI lint/type-check: `cd src/webui && npm run lint && npm run type-check`

## High-level architecture

- `src/stdiolink/` is the core library: JSONL protocol, metadata model, driver runtime, host-side process wrapper, and console helpers.
- `src/stdiolink_service/` is a QuickJS orchestration layer on top of host capabilities. JS services are not a new protocol layer; they compose drivers and system bindings.
- `src/stdiolink_server/` is the control plane. It scans services/drivers, manages `Service -> Project -> Instance`, exposes REST/SSE/WebSocket endpoints, and runs scheduling/process-guard logic.
- `src/webui/` is a React frontend that only talks to the server API and realtime channels. It should not read `data_root` directly.
- Driver processes are separate executables connected over `stdin/stdout` with one JSON object per line. Metadata exported by drivers is reused for validation, docs, and UI generation.
- Service and Project definitions in `src/data_root/` are source assets. The runnable layout is assembled into `build/runtime_<config>/`, and most runtime/debugging issues come from using raw build outputs instead of the assembled runtime tree.

## Key repository conventions

- Prefer Qt types for file I/O, text streams, and JSON in native code (`QFile`, `QTextStream`, `QString`, `QJsonObject`, `QJsonArray`).
- The wire protocol is always JSONL. On Windows pipes, line reads should use `QTextStream::readLine()`.
- Naming conventions are fixed: namespace `lower_case`, class `CamelCase`, method `camelBack`, member `m_`.
- JS services normally follow the existing pattern: `getConfig()` for config, `createLogger()` for logs, and `resolveDriver()` + `openDriver()` when orchestrating drivers.
- Keep the Service / Project boundary intact:
  - service template: `src/data_root/services/<service_id>/manifest.json`, `config.schema.json`, `index.js`
  - project metadata: `src/data_root/projects/<project_id>/config.json`
  - service business parameters: `src/data_root/projects/<project_id>/param.json`
- Runtime layout matters:
  - executables live in `build/runtime_<config>/bin/`
  - drivers live in `build/runtime_<config>/data_root/drivers/<driver-dir>/`
  - services/projects are loaded from `build/runtime_<config>/data_root/`
- On Windows, running a driver executable directly usually requires `build\runtime_debug\bin` on `PATH`.
- Public protocol, metadata, config shape, HTTP API, or runtime behavior changes must update `doc/knowledge/`; deeper details belong in `doc/manual/` only when needed.
- If a Web dashboard API changes, also check `doc/todolist.md`.
- When adding a smoke test, register all three places: the script in `src/smoke_tests/`, the plan map in `src/smoke_tests/run_smoke.py`, and the CTest entry in `src/smoke_tests/CMakeLists.txt`.
- For WebUI work, keep `src/webui/src/locales/en.json` as the structure baseline and update all locale files for new keys. Do not hardcode user-facing labels that already belong in locale data.
- Preserve the existing WebUI design language (“Style 06” / glassmorphism / bento-grid patterns) and reuse existing API clients, stores, and shared components before adding new ones.
