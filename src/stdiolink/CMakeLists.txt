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
    host/driver_catalog.cpp
)

set(CONSOLE_SOURCES
    console/console_args.cpp
    console/console_responder.cpp
    console/system_options.cpp
)

set(DOC_SOURCES
    doc/doc_generator.cpp
)

set(GUARD_SOURCES
    guard/force_fast_exit.cpp
    guard/process_guard_server.cpp
    guard/process_guard_client.cpp
    guard/process_tree_guard.cpp
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
    ${GUARD_SOURCES}
    ${PLATFORM_SOURCES}
)

add_library(stdiolink STATIC ${ALL_SOURCES})

target_include_directories(stdiolink PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink PUBLIC Qt6::Core Qt6::Network)

set_target_properties(stdiolink PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
