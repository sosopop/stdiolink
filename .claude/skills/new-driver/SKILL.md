---
name: new-driver
description: Create a new C++ Driver with boilerplate code, handler, and CMakeLists.txt
argument-hint: <driver_name> [--with-meta]
---

Create a new stdiolink Driver following the project conventions.

**Arguments:**
- `$1`: Driver name (without `stdio.drv.` prefix)
- `--with-meta`: Include metadata support (default: true, use `--no-meta` to disable)

**Steps:**

1. Parse driver name from `$1` (required)
2. Create directory: `src/drivers/driver_{name}/`
3. Generate three files:

**handler.h:**
```cpp
#pragma once
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/meta_builder.h"

class {Name}Handler : public stdiolink::IMetaCommandHandler {
public:
    {Name}Handler();
    const stdiolink::meta::DriverMeta& driverMeta() const override;
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& resp) override;
private:
    stdiolink::meta::DriverMeta m_meta;
};
```

**handler.cpp:**
```cpp
#include "handler.h"

{Name}Handler::{Name}Handler() {
    using namespace stdiolink::meta;
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("{name}", "{Name} Driver", "1.0.0", "Description")
        .vendor("stdiolink")
        .command(CommandBuilder("echo")
            .title("Echo message")
            .param(FieldBuilder("msg", FieldType::String)
                .required()
                .description("Message to echo"))
            .returns(FieldType::Object, "Echo result"))
        .build();
}

const stdiolink::meta::DriverMeta& {Name}Handler::driverMeta() const {
    return m_meta;
}

void {Name}Handler::handle(const QString& cmd, const QJsonValue& data,
                           stdiolink::IResponder& resp) {
    if (cmd == "echo") {
        QString msg = data.toObject()["msg"].toString();
        resp.done(0, QJsonObject{{"echo", msg}});
    } else {
        resp.error(404, QJsonObject{{"message", "unknown command"}});
    }
}
```

**main.cpp:**
```cpp
#include <QCoreApplication>
#include "stdiolink/driver/driver_core.h"
#include "handler.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    {Name}Handler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
```

**CMakeLists.txt:**
```cmake
add_executable(driver_{name} main.cpp handler.cpp)
target_link_libraries(driver_{name} PRIVATE stdiolink)
set_target_properties(driver_{name} PROPERTIES
    OUTPUT_NAME "stdio.drv.{name}"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_{name})
```

4. Replace `{Name}` with PascalCase and `{name}` with lowercase
5. If `--no-meta`, use `ICommandHandler` instead and remove MetaBuilder code

**After creation, remind user:**
- Add `add_subdirectory(driver_{name})` to `src/drivers/CMakeLists.txt`
- Run `build.bat` or `build.sh` to compile
