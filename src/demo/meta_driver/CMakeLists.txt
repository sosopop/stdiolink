# meta_driver CMakeLists.txt

set(STDIOLINK_DIR ${CMAKE_SOURCE_DIR}/src/stdiolink)

add_executable(meta_driver
    main.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_serializer.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_parser.cpp
    ${STDIOLINK_DIR}/protocol/meta_types.cpp
    ${STDIOLINK_DIR}/protocol/meta_validator.cpp
    ${STDIOLINK_DIR}/driver/stdio_responder.cpp
    ${STDIOLINK_DIR}/driver/driver_core.cpp
    ${STDIOLINK_DIR}/driver/help_generator.cpp
    ${STDIOLINK_DIR}/driver/meta_exporter.cpp
    ${STDIOLINK_DIR}/console/console_args.cpp
    ${STDIOLINK_DIR}/console/console_responder.cpp
    ${STDIOLINK_DIR}/doc/doc_generator.cpp
)

target_include_directories(meta_driver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(meta_driver PRIVATE Qt5::Core)

set_target_properties(meta_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
