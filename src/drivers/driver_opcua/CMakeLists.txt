cmake_minimum_required(VERSION 3.16)
project(driver_opcua)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core REQUIRED)
    set(QT_LIBRARIES Qt5::Core)
else()
    set(QT_LIBRARIES Qt6::Core)
endif()

if(NOT TARGET stdiolink)
    find_package(stdiolink REQUIRED)
endif()

if(NOT TARGET open62541::open62541 AND NOT TARGET open62541)
    find_package(open62541 CONFIG REQUIRED)
endif()

set(OPCUA_TARGET open62541)
if(TARGET open62541::open62541)
    set(OPCUA_TARGET open62541::open62541)
endif()

add_executable(driver_opcua
    main.cpp
    handler.cpp
)
target_include_directories(driver_opcua PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(driver_opcua PRIVATE
    stdiolink
    ${QT_LIBRARIES}
    ${OPCUA_TARGET}
)
set_target_properties(driver_opcua PROPERTIES
    OUTPUT_NAME "stdio.drv.opcua"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_opcua)
