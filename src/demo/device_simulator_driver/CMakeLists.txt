add_executable(device_simulator_driver main.cpp)
target_link_libraries(device_simulator_driver PRIVATE stdiolink)
set_target_properties(device_simulator_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.device-simulator"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
