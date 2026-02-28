cmake_minimum_required(VERSION 3.21)
project(stdiolink)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 目录布局变量 ──
# 防护：拒绝多配置生成器
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_is_multi_config)
    message(FATAL_ERROR "Multi-config generators are not supported. Use -G Ninja.")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" _build_type_lower)
if(_build_type_lower STREQUAL "")
    set(_build_type_lower "debug")
endif()

set(STDIOLINK_RAW_DIR "${CMAKE_BINARY_DIR}/${_build_type_lower}")
set(STDIOLINK_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime_${_build_type_lower}")

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
find_package(qjs CONFIG REQUIRED)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Widgets Network WebSockets HttpServer Concurrent)
find_package(GTest CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# 启用测试
enable_testing()

# 添加子目录
add_subdirectory(src)

# 运行时目录组装
include(cmake/assemble_runtime.cmake)
