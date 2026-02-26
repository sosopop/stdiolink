cmake_minimum_required(VERSION 3.16)
project(driver_modbustcp_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Network REQUIRED)
    set(QT_LIBRARIES Qt5::Core Qt5::Network)
else()
    set(QT_LIBRARIES Qt6::Core Qt6::Network)
endif()

if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

set(MODBUSRTU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../driver_modbusrtu")

add_executable(driver_modbustcp_server
    main.cpp
    handler.cpp
    modbus_tcp_server.cpp
    ${MODBUSRTU_DIR}/modbus_types.cpp
)
target_include_directories(driver_modbustcp_server PRIVATE
    ${MODBUSRTU_DIR}
)
target_link_libraries(driver_modbustcp_server PRIVATE
    stdiolink ${QT_LIBRARIES}
)
set_target_properties(driver_modbustcp_server PROPERTIES
    OUTPUT_NAME "stdio.drv.modbustcp_server"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
