# meta_driver CMakeLists.txt

add_executable(meta_driver main.cpp)

target_link_libraries(meta_driver PRIVATE stdiolink)

set_target_properties(meta_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
