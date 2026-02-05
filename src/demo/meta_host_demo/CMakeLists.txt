# meta_host_demo CMakeLists.txt

add_executable(meta_host_demo main.cpp)

target_link_libraries(meta_host_demo PRIVATE stdiolink)

set_target_properties(meta_host_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
