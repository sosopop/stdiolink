cmake_minimum_required(VERSION 3.16)
project(driver_plc_crane_sim)

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

set(MODBUS_TCP_SERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../driver_modbustcp_server")

add_executable(driver_plc_crane_sim
    main.cpp
    handler.cpp
    sim_device.cpp
    ${MODBUS_TCP_SERVER_DIR}/modbus_tcp_server.cpp
)
target_include_directories(driver_plc_crane_sim PRIVATE
    ${CMAKE_SOURCE_DIR}/src/drivers
    ${MODBUS_TCP_SERVER_DIR}
)
target_link_libraries(driver_plc_crane_sim PRIVATE
    stdiolink
    ${QT_LIBRARIES}
)
set_target_properties(driver_plc_crane_sim PROPERTIES
    OUTPUT_NAME "stdio.drv.plc_crane_sim"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_plc_crane_sim)
