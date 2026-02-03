# progress_driver CMakeLists.txt

set(STDIOLINK_DIR ${CMAKE_SOURCE_DIR}/src/stdiolink)

add_executable(progress_driver
    main.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_serializer.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_parser.cpp
    ${STDIOLINK_DIR}/driver/stdio_responder.cpp
    ${STDIOLINK_DIR}/driver/driver_core.cpp
)

target_include_directories(progress_driver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(progress_driver PRIVATE Qt5::Core)

set_target_properties(progress_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
