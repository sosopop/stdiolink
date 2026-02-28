cmake_minimum_required(VERSION 3.16)
project(driver_modbusrtu_serial)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core SerialPort QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core SerialPort REQUIRED)
    set(QT_LIBRARIES Qt5::Core Qt5::SerialPort)
else()
    set(QT_LIBRARIES Qt6::Core Qt6::SerialPort)
endif()

if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

set(MODBUSRTU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../driver_modbusrtu")

add_executable(driver_modbusrtu_serial
    main.cpp
    handler.cpp
    modbus_rtu_serial_client.cpp
    ${MODBUSRTU_DIR}/modbus_types.cpp
)
target_include_directories(driver_modbusrtu_serial PRIVATE
    ${MODBUSRTU_DIR}
)
target_link_libraries(driver_modbusrtu_serial PRIVATE
    stdiolink ${QT_LIBRARIES}
)
set_target_properties(driver_modbusrtu_serial PROPERTIES
    OUTPUT_NAME "stdio.drv.modbusrtu_serial"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_modbusrtu_serial)
