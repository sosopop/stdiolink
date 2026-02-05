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
    test_advanced_ui.cpp
    test_config_inject.cpp
    test_version_negotiate.cpp
    test_driver_registry.cpp
    test_system_help.cpp
)

add_executable(stdiolink_tests ${TEST_SOURCES})

target_link_libraries(stdiolink_tests PRIVATE
    stdiolink
    GTest::gtest
)

add_test(NAME stdiolink_tests COMMAND stdiolink_tests)

# Windows: 复制 DLL 到测试目录
if(WIN32)
    add_custom_command(TARGET stdiolink_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:stdiolink>
            $<TARGET_FILE_DIR:stdiolink_tests>
    )
endif()

# test_driver (用于单元测试的辅助 Driver 程序)
add_executable(test_driver test_driver_main.cpp)

target_link_libraries(test_driver PRIVATE stdiolink)

set_target_properties(test_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/tests"
)
