# meta_host_demo CMakeLists.txt

set(STDIOLINK_DIR ${CMAKE_SOURCE_DIR}/src/stdiolink)

add_executable(meta_host_demo
    main.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_serializer.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_parser.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_stream_parser.cpp
    ${STDIOLINK_DIR}/protocol/meta_types.cpp
    ${STDIOLINK_DIR}/protocol/meta_validator.cpp
    ${STDIOLINK_DIR}/host/task.cpp
    ${STDIOLINK_DIR}/host/driver.cpp
    ${STDIOLINK_DIR}/host/wait_any.cpp
    ${STDIOLINK_DIR}/host/meta_cache.cpp
    ${STDIOLINK_DIR}/host/form_generator.cpp
)

target_include_directories(meta_host_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(meta_host_demo PRIVATE Qt5::Core)

set_target_properties(meta_host_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
