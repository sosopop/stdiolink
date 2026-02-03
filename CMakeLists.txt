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
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# 设置编码为UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 设置vcpkg工具链
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")

# 查找依赖包
find_package(spdlog CONFIG REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

# ============================================
# stdiolink 基础库源文件
# ============================================
set(PROTOCOL_SOURCES
    src/stdiolink/protocol/jsonl_serializer.cpp
    src/stdiolink/protocol/jsonl_parser.cpp
    src/stdiolink/protocol/jsonl_stream_parser.cpp
)

set(DRIVER_SOURCES
    src/stdiolink/driver/stdio_responder.cpp
    src/stdiolink/driver/driver_core.cpp
)

set(HOST_SOURCES
    src/stdiolink/host/task.cpp
    src/stdiolink/host/driver.cpp
    src/stdiolink/host/wait_any.cpp
)

set(CONSOLE_SOURCES
    src/stdiolink/console/console_args.cpp
    src/stdiolink/console/console_responder.cpp
)

set(STDIOLINK_SOURCES
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
    ${HOST_SOURCES}
    ${CONSOLE_SOURCES}
)

# ============================================
# 单元测试
# ============================================
enable_testing()
find_package(GTest CONFIG REQUIRED)

set(TEST_SOURCES
    src/tests/test_main.cpp
    src/tests/test_jsonl_serializer.cpp
    src/tests/test_jsonl_parser.cpp
    src/tests/test_jsonl_stream_parser.cpp
    src/tests/test_driver_core.cpp
    src/tests/test_host_driver.cpp
    src/tests/test_wait_any.cpp
    src/tests/test_console.cpp
)

add_executable(stdiolink_tests
    ${TEST_SOURCES}
    ${STDIOLINK_SOURCES}
)

target_include_directories(stdiolink_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_tests PRIVATE
    GTest::gtest
    Qt5::Core
)

add_test(NAME stdiolink_tests COMMAND stdiolink_tests)

# ============================================
# Demo 程序
# ============================================

# test_driver (用于单元测试)
add_executable(test_driver
    src/stdiolink/test_driver_main.cpp
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
)

target_include_directories(test_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_driver PRIVATE Qt5::Core)

# echo_driver
add_executable(echo_driver
    src/demo/echo_driver/main.cpp
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
)

target_include_directories(echo_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(echo_driver PRIVATE Qt5::Core)

set_target_properties(echo_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# progress_driver
add_executable(progress_driver
    src/demo/progress_driver/main.cpp
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
)

target_include_directories(progress_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(progress_driver PRIVATE Qt5::Core)

set_target_properties(progress_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# host_demo
add_executable(host_demo
    src/demo/host_demo/main.cpp
    ${STDIOLINK_SOURCES}
)

target_include_directories(host_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(host_demo PRIVATE Qt5::Core)

set_target_properties(host_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
