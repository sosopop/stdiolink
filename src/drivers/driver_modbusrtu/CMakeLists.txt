cmake_minimum_required(VERSION 3.16)
project(driver_modbusrtu)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt5/Qt6
find_package(Qt6 COMPONENTS Core Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Network REQUIRED)
    set(QT_LIBRARIES Qt5::Core Qt5::Network)
else()
    set(QT_LIBRARIES Qt6::Core Qt6::Network)
endif()

# 如果 stdiolink 已经作为子目录包含
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stdiolink/CMakeLists.txt")
    add_subdirectory(stdiolink)
endif()

# 查找 stdiolink (如果没作为子目录包含，假设它在系统路径或通过 CMAKE_PREFIX_PATH 提供)
if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

add_executable(driver_modbusrtu
    main.cpp
    modbus_rtu_client.cpp
    modbus_types.cpp
)
target_link_libraries(driver_modbusrtu PRIVATE stdiolink ${QT_LIBRARIES})

# 设置输出目录
set_target_properties(driver_modbusrtu PROPERTIES
    OUTPUT_NAME "stdio.drv.modbusrtu"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
