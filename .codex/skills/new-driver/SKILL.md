---
name: new-driver
description: Create or scaffold a new stdiolink C++ Driver under src/drivers, including boilerplate files, CMake registration, and optional metadata support.
argument-hint: <driver_name> [--with-meta|--no-meta]
---

Use this skill when the user wants a new C++ Driver in this repository.

Keep the workflow aligned with the current repo, not generic C++ scaffolding.

## Inputs

- Driver name may be given as `foo`, `driver_foo`, or `stdio.drv.foo`.
- Normalize it to:
  - directory: `src/drivers/driver_<name>/`
  - output binary: `stdio.drv.<name>`
  - class prefix: PascalCase, for example `foo_bar` -> `FooBar`

Metadata support is enabled by default. Only skip it if the user explicitly asks for no metadata.

## Workflow

1. Inspect `src/drivers/` and `src/drivers/CMakeLists.txt` first.
   - Avoid name collisions.
   - Reuse the closest existing driver as the structural reference.
2. Create `src/drivers/driver_<name>/`.
3. Generate the minimum files:
   - `main.cpp`
   - `handler.h`
   - `handler.cpp`
   - `CMakeLists.txt`
4. Update `src/drivers/CMakeLists.txt` with `add_subdirectory(driver_<name>)`.
5. If the user asked for a runnable example, add a simple `echo`-style command and a minimal metadata definition.
6. If the change introduces behavior worth testing, add or extend a focused test under `src/tests/`.
7. Validate with a build, or at minimum verify the new target is wired into CMake correctly.

## File Patterns

Default to the minimal metadata-enabled shape:

### `handler.h`

```cpp
#pragma once

#include "stdiolink/driver/meta_command_handler.h"

class {ClassName}Handler : public stdiolink::IMetaCommandHandler {
public:
    {ClassName}Handler();
    const stdiolink::meta::DriverMeta& driverMeta() const override;
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& resp) override;

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
};
```

### `handler.cpp`

- Include `stdiolink/driver/meta_builder.h` when metadata is enabled.
- Build `DriverMeta` in `buildMeta()`.
- Implement at least one simple command if the user asked for a starter driver.
- Use `resp.done(...)` / `resp.error(...)`.

### `main.cpp`

```cpp
#include <QCoreApplication>

#include "handler.h"
#include "stdiolink/driver/driver_core.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    {ClassName}Handler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
```

If metadata is disabled, use `ICommandHandler` and `core.setHandler(&handler)`.

### `CMakeLists.txt`

Use the repository's current style. For a simple driver, keep it close to:

```cmake
cmake_minimum_required(VERSION 3.16)
project(driver_{name})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core REQUIRED)
    set(QT_LIBRARIES Qt5::Core)
else()
    set(QT_LIBRARIES Qt6::Core)
endif()

if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

add_executable(driver_{name}
    main.cpp
    handler.cpp
)
target_link_libraries(driver_{name} PRIVATE
    stdiolink ${QT_LIBRARIES}
)
set_target_properties(driver_{name} PROPERTIES
    OUTPUT_NAME "stdio.drv.{name}"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_{name})
```

Add more Qt modules only if the implementation needs them.

## Validation

- Preferred build: `build.bat` on Windows, `./build.sh` on Unix.
- Driver standalone run on Windows requires `build/runtime_debug/bin` on `PATH`.
- Useful smoke check:
  - `build\\runtime_debug\\data_root\\drivers\\stdio.drv.<name>\\stdio.drv.<name>.exe --export-meta`

## References

- `doc/developer-guide.md`
- `src/drivers/CMakeLists.txt`
- Existing drivers under `src/drivers/`
