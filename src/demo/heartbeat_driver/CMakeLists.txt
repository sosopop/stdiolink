add_executable(heartbeat_driver main.cpp)
target_link_libraries(heartbeat_driver PRIVATE stdiolink)
set_target_properties(heartbeat_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.heartbeat"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
