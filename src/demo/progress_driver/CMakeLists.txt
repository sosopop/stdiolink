# progress_driver CMakeLists.txt

add_executable(progress_driver main.cpp)

target_link_libraries(progress_driver PRIVATE stdiolink)

set_target_properties(progress_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
