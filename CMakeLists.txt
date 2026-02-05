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
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(GTest CONFIG REQUIRED)

# 启用测试
enable_testing()

# 添加子目录
add_subdirectory(src)
