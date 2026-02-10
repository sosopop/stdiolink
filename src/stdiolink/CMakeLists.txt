# stdiolink 基础库 CMakeLists.txt

set(PROTOCOL_SOURCES
    protocol/jsonl_serializer.cpp
    protocol/jsonl_parser.cpp
    protocol/jsonl_stream_parser.cpp
    protocol/meta_types.cpp
    protocol/meta_schema_validator.cpp
    protocol/meta_validator.cpp
)

set(DRIVER_SOURCES
    driver/stdio_responder.cpp
    driver/driver_core.cpp
    driver/meta_builder.cpp
    driver/help_generator.cpp
    driver/meta_exporter.cpp
    driver/log_redirector.cpp
)

set(HOST_SOURCES
    host/task.cpp
    host/driver.cpp
    host/wait_any.cpp
    host/meta_cache.cpp
    host/form_generator.cpp
    host/config_injector.cpp
    host/meta_version_checker.cpp
    host/driver_registry.cpp
)

set(CONSOLE_SOURCES
    console/console_args.cpp
    console/console_responder.cpp
    console/system_options.cpp
)

set(DOC_SOURCES
    doc/doc_generator.cpp
)

set(PLATFORM_SOURCES
    platform/platform_utils.cpp
)

set(ALL_SOURCES
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
    ${HOST_SOURCES}
    ${CONSOLE_SOURCES}
    ${DOC_SOURCES}
    ${PLATFORM_SOURCES}
)

add_library(stdiolink STATIC ${ALL_SOURCES})

target_include_directories(stdiolink PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink PUBLIC Qt6::Core)

set_target_properties(stdiolink PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
