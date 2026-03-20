cmake_minimum_required(VERSION 3.16)
project(driver_3d_temp_scanner)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core SerialPort Gui QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core SerialPort Gui REQUIRED)
    set(QT_LIBRARIES Qt5::Core Qt5::SerialPort Qt5::Gui)
else()
    set(QT_LIBRARIES Qt6::Core Qt6::SerialPort Qt6::Gui)
endif()

if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

add_executable(driver_3d_temp_scanner
    main.cpp
    protocol_codec.cpp
    thermal_transport.cpp
    thermal_session.cpp
    handler.cpp
)
target_include_directories(driver_3d_temp_scanner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(driver_3d_temp_scanner PRIVATE
    stdiolink ${QT_LIBRARIES}
)
set_target_properties(driver_3d_temp_scanner PROPERTIES
    OUTPUT_NAME "stdio.drv.3d_temp_scanner"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_3d_temp_scanner)
