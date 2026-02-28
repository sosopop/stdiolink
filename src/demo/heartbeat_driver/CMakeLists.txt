add_executable(heartbeat_driver main.cpp)
target_link_libraries(heartbeat_driver PRIVATE stdiolink)
set_target_properties(heartbeat_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.heartbeat"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS heartbeat_driver)
