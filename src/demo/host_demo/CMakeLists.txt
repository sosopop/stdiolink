# host_demo CMakeLists.txt

add_executable(host_demo main.cpp)

target_link_libraries(host_demo PRIVATE stdiolink)

set_target_properties(host_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
