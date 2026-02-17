add_executable(calculator_driver main.cpp)
target_link_libraries(calculator_driver PRIVATE stdiolink)
set_target_properties(calculator_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.calculator"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
