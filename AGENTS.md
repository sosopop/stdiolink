# Repository Guidelines

## Project Structure & Module Organization

- `src/stdiolink/` contains the core library modules.
- `src/stdiolink/protocol/` holds JSONL protocol plus meta types/validators.
- `src/stdiolink/driver/` is the Driver-side runtime (responders, DriverCore, meta builder/export).
- `src/stdiolink/host/` is the Host-side runtime (Driver wrapper, task/wait_any, meta cache, form generation).
- `src/stdiolink/console/` provides console-mode parsing/responding.
- `src/stdiolink/doc/` contains built-in document generation helpers.
- `src/stdiolink_service/` contains the JS/runtime service executable (engine, bindings, proxy, config).
- `src/driverlab/` contains the GUI testing tool (UI, models, resources).
- `src/drivers/` contains production/sample driver implementations (modbus, 3dvision, etc.).
- `src/demo/` contains demo drivers and host examples.
- `src/tests/` contains unit and integration tests (GoogleTest).
- `tools/` holds developer utilities such as `run-clang-tidy.py`.
- `doc/manual/` is the user/developer manual; `doc/milestone/` tracks implementation milestones.

## Build, Test, and Development Commands

- `build_ninja.bat` or `build_ninja.bat Release`: configure/build with CMake + Ninja (Windows).
- `./build_ninja/src/tests/stdiolink_tests.exe`: run all tests.
- `python ./tools/run-clang-tidy.py -p build_ninja -j 8 -quiet -config-file .clang-tidy`: static analysis.

## Coding Style & Naming Conventions

- Use Qt types for IO and JSON: `QFile`, `QTextStream`, `QString`, `QJsonObject`, `QJsonArray`.
- Naming: namespaces `lower_case`, classes `CamelCase`, methods `camelBack`, members `m_` prefix.
- Prefer JSONL line-based parsing. On Windows, avoid `fread()` with pipes; use `QTextStream::readLine()`.
- Formatting/linting: follow `.clang-tidy` and `.clang-format` in repo root.

## Testing Guidelines

- Framework: GoogleTest. Tests live in `src/tests/` and follow `test_*.cpp` naming.
- Add unit tests for protocol/meta serialization, validation, and host/driver utilities.
- For `stdiolink_service` changes, add/extend tests covering config validation, JS engine bindings, and proxy/scheduler behavior.
- Prefer focused tests with clear assertions; cover edge cases (invalid input, boundary values).

## Commit & Pull Request Guidelines

- Commit style follows Conventional Commits observed in history: `feat: ...`, `docs: ...`.
- Keep commits scoped and descriptive.
- PRs should include a concise summary, testing notes (commands run), and links to relevant issues/design docs if applicable.

## Configuration & Tooling Notes

- Build artifacts land in `build_ninja/`.
- Public protocol/meta changes should be aligned with docs under `doc/manual/` and relevant milestone docs under `doc/milestone/`.
