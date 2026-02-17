add_executable(file_processor_driver main.cpp)
target_link_libraries(file_processor_driver PRIVATE stdiolink)
set_target_properties(file_processor_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.file-processor"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
