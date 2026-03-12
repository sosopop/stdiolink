cmake_minimum_required(VERSION 3.16)
project(driver_limaco_1_radar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core Network SerialPort QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Network SerialPort REQUIRED)
    set(QT_LIBRARIES Qt5::Core Qt5::Network Qt5::SerialPort)
else()
    set(QT_LIBRARIES Qt6::Core Qt6::Network Qt6::SerialPort)
endif()

if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

set(DRIVERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(MODBUSRTU_DIR "${DRIVERS_DIR}/driver_modbusrtu")
set(COMMON_DIR "${DRIVERS_DIR}/driver_limaco_common")

add_executable(driver_limaco_1_radar
    main.cpp
    handler.cpp
    ${COMMON_DIR}/limaco_transport.cpp
    ${COMMON_DIR}/limaco_decode.cpp
    ${MODBUSRTU_DIR}/modbus_rtu_client.cpp
    ${MODBUSRTU_DIR}/modbus_types.cpp
    ${DRIVERS_DIR}/driver_modbusrtu_serial/modbus_rtu_serial_client.cpp
)
target_include_directories(driver_limaco_1_radar PRIVATE
    ${DRIVERS_DIR}
    ${MODBUSRTU_DIR}
)
target_link_libraries(driver_limaco_1_radar PRIVATE
    stdiolink ${QT_LIBRARIES}
)
set_target_properties(driver_limaco_1_radar PROPERTIES
    OUTPUT_NAME "stdio.drv.limaco_1_radar"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_limaco_1_radar)
