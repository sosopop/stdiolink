add_executable(demo_host main.cpp)
target_link_libraries(demo_host PRIVATE stdiolink)
set_target_properties(demo_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
