# stdiolink 基础库 CMakeLists.txt

set(PROTOCOL_SOURCES
    protocol/jsonl_serializer.cpp
    protocol/jsonl_parser.cpp
    protocol/jsonl_stream_parser.cpp
    protocol/meta_types.cpp
)

set(DRIVER_SOURCES
    driver/stdio_responder.cpp
    driver/driver_core.cpp
)

set(HOST_SOURCES
    host/task.cpp
    host/driver.cpp
    host/wait_any.cpp
)

set(CONSOLE_SOURCES
    console/console_args.cpp
    console/console_responder.cpp
)

set(DOC_SOURCES
    doc/doc_generator.cpp
)

# 定义库源文件（供其他模块使用）
set(STDIOLINK_SOURCES
    ${PROTOCOL_SOURCES}
    ${DRIVER_SOURCES}
    ${HOST_SOURCES}
    ${CONSOLE_SOURCES}
    ${DOC_SOURCES}
    PARENT_SCOPE
)
