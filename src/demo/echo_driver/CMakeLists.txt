# echo_driver CMakeLists.txt

add_executable(echo_driver main.cpp)

target_link_libraries(echo_driver PRIVATE stdiolink)

set_target_properties(echo_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
