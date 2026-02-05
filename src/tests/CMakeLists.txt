# 单元测试 CMakeLists.txt

set(TEST_SOURCES
    test_main.cpp
    test_jsonl_serializer.cpp
    test_jsonl_parser.cpp
    test_jsonl_stream_parser.cpp
    test_driver_core.cpp
    test_host_driver.cpp
    test_wait_any.cpp
    test_console.cpp
    test_meta_types.cpp
    test_meta_describe.cpp
    test_meta_builder.cpp
    test_meta_validator.cpp
    test_meta_host.cpp
    test_doc_generator.cpp
    test_typed_event.cpp
)

# 获取 stdiolink 源文件的绝对路径
set(STDIOLINK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../stdiolink)

add_executable(stdiolink_tests
    ${TEST_SOURCES}
    ${STDIOLINK_DIR}/protocol/jsonl_serializer.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_parser.cpp
    ${STDIOLINK_DIR}/protocol/jsonl_stream_parser.cpp
    ${STDIOLINK_DIR}/protocol/meta_types.cpp
    ${STDIOLINK_DIR}/protocol/meta_validator.cpp
    ${STDIOLINK_DIR}/driver/stdio_responder.cpp
    ${STDIOLINK_DIR}/driver/driver_core.cpp
    ${STDIOLINK_DIR}/driver/meta_builder.cpp
    ${STDIOLINK_DIR}/driver/help_generator.cpp
    ${STDIOLINK_DIR}/driver/meta_exporter.cpp
    ${STDIOLINK_DIR}/host/task.cpp
    ${STDIOLINK_DIR}/host/driver.cpp
    ${STDIOLINK_DIR}/host/wait_any.cpp
    ${STDIOLINK_DIR}/host/meta_cache.cpp
    ${STDIOLINK_DIR}/host/form_generator.cpp
    ${STDIOLINK_DIR}/console/console_args.cpp
    ${STDIOLINK_DIR}/console/console_responder.cpp
    ${STDIOLINK_DIR}/doc/doc_generator.cpp
)

target_include_directories(stdiolink_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_tests PRIVATE
    GTest::gtest
    Qt5::Core
)

add_test(NAME stdiolink_tests COMMAND stdiolink_tests)

# test_driver (用于单元测试的辅助 Driver 程序)
add_executable(test_driver
    test_driver_main.cpp
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

target_include_directories(test_driver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_driver PRIVATE Qt5::Core)
