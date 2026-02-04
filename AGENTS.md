# Repository Guidelines

## Project Structure & Module Organization

- `src/stdiolink/` contains the core library.
- `src/stdiolink/protocol/` holds JSONL protocol and meta types/validators.
- `src/stdiolink/driver/` is the Driver-side runtime (stdio responders, DriverCore, meta builder).
- `src/stdiolink/host/` is the Host-side runtime (Driver wrapper, wait_any, meta cache, UI form generator).
- `src/stdiolink/console/` provides console-mode support.
- `src/tests/` contains unit and integration tests (GoogleTest).
- `src/demo/` contains sample drivers and host demos.
- `tools/` holds developer utilities such as `run-clang-tidy.py`.
- `doc/` contains design and milestone documents.

## Build, Test, and Development Commands

- `build_ninja.bat` or `build_ninja.bat Release`: configure/build with CMake + Ninja (Windows).
- `ninja -C build_ninja` or `cmake --build build_ninja --parallel 8`: incremental build.
- `./build_ninja/src/tests/stdiolink_tests.exe`: run all tests.
- `./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=TestName.*`: run a single test suite.
- `python ./tools/run-clang-tidy.py -p build_ninja -j 8 -quiet -config-file .clang-tidy`: static analysis.

## Coding Style & Naming Conventions

- Use Qt types for IO and JSON: `QFile`, `QTextStream`, `QString`, `QJsonObject`, `QJsonArray`.
- Naming: namespaces `lower_case`, classes `CamelCase`, methods `camelBack`, members `m_` prefix.
- Prefer JSONL line-based parsing. On Windows, avoid `fread()` with pipes; use `QTextStream::readLine()`.
- Formatting/linting: follow `.clang-tidy` and `.clang-format` in repo root.

## Testing Guidelines

- Framework: GoogleTest. Tests live in `src/tests/` and follow `test_*.cpp` naming.
- Add unit tests for protocol/meta serialization, validation, and host/driver utilities.
- Prefer focused tests with clear assertions; cover edge cases (invalid input, boundary values).

## Commit & Pull Request Guidelines

- Commit style follows Conventional Commits observed in history: `feat: ...`, `docs: ...`.
- Keep commits scoped and descriptive.
- PRs should include a concise summary, testing notes (commands run), and links to relevant issues/design docs if applicable.

## Configuration & Tooling Notes

- Build artifacts land in `build_ninja/`.
- Meta-system design documents live under `doc/` for reference when changing public schemas.
