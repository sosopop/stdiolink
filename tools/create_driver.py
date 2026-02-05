#!/usr/bin/env python3
"""
stdiolink Driver 项目脚手架创建工具
"""

import argparse
import os
import sys
from pathlib import Path

# 项目模板
CMAKELISTS_TEMPLATE = """cmake_minimum_required(VERSION 3.16)
project({project_name})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt5/Qt6
find_package(Qt6 COMPONENTS Core QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core REQUIRED)
    set(QT_LIBRARIES Qt5::Core)
else()
    set(QT_LIBRARIES Qt6::Core)
endif()

# 如果 stdiolink 已经作为子目录包含
if(EXISTS "${{CMAKE_CURRENT_SOURCE_DIR}}/stdiolink/CMakeLists.txt")
    add_subdirectory(stdiolink)
endif()

# 查找 stdiolink (如果没作为子目录包含，假设它在系统路径或通过 CMAKE_PREFIX_PATH 提供)
if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

add_executable({project_name} main.cpp)
target_link_libraries({project_name} PRIVATE stdiolink ${{QT_LIBRARIES}})

# 设置输出目录
set_target_properties({project_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${{CMAKE_BINARY_DIR}}/bin"
)
"""

MAIN_CPP_TEMPLATE = """#include <QCoreApplication>
#include <QJsonObject>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

/**
 * {class_name} - 自动生成的 Driver 实现
 */
class {class_name} : public IMetaCommandHandler {{
public:
    {class_name}() {{
        buildMeta();
    }}

    const DriverMeta& driverMeta() const override {{
        return m_meta;
    }}

    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override {{
        if (cmd == "hello") {{
            QString name = data.toObject()["name"].toString("Guest");
            resp.done(0, QJsonObject{{"message", "Hello, " + name + "!"}});
        }} else {{
            resp.error(404, QJsonObject{{"message", "unknown command: " + cmd}});
        }}
    }}

private:
    void buildMeta() {{
        m_meta = DriverMetaBuilder()
            .schemaVersion("1.0")
            .info("{driver_id}", "{display_name}", "1.0.0", "{description}")
            .command(CommandBuilder("hello")
                .description("简单的问候命令")
                .param(FieldBuilder("name", FieldType::String)
                    .description("你的名字")
                    .defaultValue("Guest")))
            .build();
    }}

    DriverMeta m_meta;
}};

int main(int argc, char* argv[]) {{
    QCoreApplication app(argc, argv);

    {class_name} handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    core.setProfile(DriverCore::Profile::KeepAlive);

    return core.run(argc, argv);
}}
"""

GITIGNORE_TEMPLATE = """build/
build_ninja/
.vscode/
.idea/
*.user
*.user.*
"""

README_TEMPLATE = """# {display_name}

{description}

## 构建说明

本项目使用 CMake 构建，并依赖于 `stdiolink` 库和 `Qt5/6 Core`。

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 使用说明

本项目生成的 Driver 支持 StdIO 和 Console 模式。

### Console 模式调试

```bash
./bin/{project_name} --help
./bin/{project_name} hello --name="World"
```
"""

def create_project(args):
    target_dir = Path(args.output) / args.name
    if target_dir.exists() and not args.force:
        print(f"Error: Directory '{target_dir}' already exists. Use --force to overwrite.")
        return False

    target_dir.mkdir(parents=True, exist_ok=True)

    # 准备模板变量
    class_name = "".join(x.capitalize() for x in args.name.replace("-", "_").split("_")) + "Handler"
    if not class_name.endswith("Handler"):
        class_name += "Handler"

    context = {
        "project_name": args.name,
        "class_name": class_name,
        "driver_id": args.id or f"com.stdiolink.{args.name.replace('-', '.')}",
        "display_name": args.display_name or args.name.replace("-", " ").title(),
        "description": args.desc or f"{args.name} driver project.",
    }

    # 写入文件
    (target_dir / "CMakeLists.txt").write_text(CMAKELISTS_TEMPLATE.format(**context), encoding="utf-8")
    (target_dir / "main.cpp").write_text(MAIN_CPP_TEMPLATE.format(**context), encoding="utf-8")
    (target_dir / ".gitignore").write_text(GITIGNORE_TEMPLATE, encoding="utf-8")
    (target_dir / "README.md").write_text(README_TEMPLATE.format(**context), encoding="utf-8")

    print(f"Project '{args.name}' created successfully at: {target_dir}")
    print("\nNext steps:")
    print(f"  cd {target_dir}")
    print("  mkdir build && cd build")
    print("  cmake ..")
    print("  cmake --build .")
    
    return True

def main():
    parser = argparse.ArgumentParser(description="stdiolink Driver 项目脚手架工具")
    parser.add_argument("name", help="项目名称 (例如: my-driver)")
    parser.add_argument("-o", "--output", default=".", help="输出根目录 (默认为当前目录)")
    parser.add_argument("--id", help="Driver ID (例如: com.example.driver)")
    parser.add_argument("--display-name", help="显示名称")
    parser.add_argument("--desc", help="项目描述")
    parser.add_argument("-f", "--force", action="store_true", help="强制覆盖已存在的目录")

    args = parser.parse_args()

    if create_project(args):
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())