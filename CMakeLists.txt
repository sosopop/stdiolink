cmake_minimum_required(VERSION 3.20)
project(stdiolink)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 自动化工具配置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Windows版本定义
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# 设置编码为UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h" "src/*.ui" "src/*.c")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${SOURCES})
foreach(CODE_FILE ${SOURCES})
    get_filename_component(FILENAME ${CODE_FILE} NAME)
    set_source_files_properties(${CODE_FILE} PROPERTIES COMPILE_FLAGS -D__FILENAME__=${FILENAME})
endforeach(CODE_FILE)

# 设置vcpkg工具链
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")

# 查找依赖包
find_package(spdlog CONFIG REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

# Add executable targets
add_executable(stdiolink
    src/main.cpp
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
    ${HOST_SOURCES}
)

target_include_directories(stdiolink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接库到可执行文件
target_link_libraries(stdiolink PRIVATE 
    spdlog::spdlog
    Qt5::Core
    Qt5::Widgets
)

# 设置输出目录
set_target_properties(stdiolink PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ============================================
# 测试配置
# ============================================
enable_testing()
find_package(GTest CONFIG REQUIRED)

# 协议库源文件
set(PROTOCOL_SOURCES
    src/protocol/jsonl_serializer.cpp
    src/protocol/jsonl_parser.cpp
    src/protocol/jsonl_stream_parser.cpp
)

# Driver 源文件
set(DRIVER_SOURCES
    src/driver/stdio_responder.cpp
    src/driver/driver_core.cpp
)

# Host 源文件
set(HOST_SOURCES
    src/host/task.cpp
    src/host/driver.cpp
    src/host/wait_any.cpp
)

# Console 源文件
set(CONSOLE_SOURCES
    src/console/console_args.cpp
    src/console/console_responder.cpp
)

# 测试源文件
set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_jsonl_serializer.cpp
    tests/test_jsonl_parser.cpp
    tests/test_jsonl_stream_parser.cpp
    tests/test_driver_core.cpp
    tests/test_host_driver.cpp
    tests/test_wait_any.cpp
    tests/test_console.cpp
)

# 测试可执行文件
add_executable(stdiolink_tests
    ${TEST_SOURCES}
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
    ${HOST_SOURCES}
    ${CONSOLE_SOURCES}
)

target_include_directories(stdiolink_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_tests PRIVATE
    GTest::gtest
    Qt5::Core
)

add_test(NAME stdiolink_tests COMMAND stdiolink_tests)

# 测试用 Driver 程序
add_executable(test_driver
    src/test_driver_main.cpp
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
)

target_include_directories(test_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_driver PRIVATE
    Qt5::Core
)
