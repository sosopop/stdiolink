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
    test_js_engine_scaffold.cpp
    test_es_module_loader.cpp
    test_driver_task_binding.cpp
    test_process_binding.cpp
    test_proxy_and_scheduler.cpp
    test_js_integration.cpp
)

add_executable(stdiolink_tests ${TEST_SOURCES})

target_sources(stdiolink_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/engine/js_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/engine/console_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/engine/module_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/utils/js_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_task.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_driver.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_process.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_task_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_stdiolink_module.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/proxy/driver_proxy.cpp
)

target_include_directories(stdiolink_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service
)

target_link_libraries(stdiolink_tests PRIVATE
    stdiolink
    GTest::gtest
)

if(TARGET qjs)
    target_link_libraries(stdiolink_tests PRIVATE qjs)
elseif(TARGET qjs::qjs)
    target_link_libraries(stdiolink_tests PRIVATE qjs::qjs)
elseif(TARGET quickjs)
    target_link_libraries(stdiolink_tests PRIVATE quickjs)
elseif(TARGET quickjs::quickjs)
    target_link_libraries(stdiolink_tests PRIVATE quickjs::quickjs)
else()
    message(FATAL_ERROR "Cannot find QuickJS target from qjs package")
endif()

set_target_properties(stdiolink_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME stdiolink_tests COMMAND stdiolink_tests)

if(TARGET stdiolink_service)
    add_dependencies(stdiolink_tests stdiolink_service)
endif()

# test_driver (用于单元测试的辅助 Driver 程序)
add_executable(test_driver test_driver_main.cpp)

target_link_libraries(test_driver PRIVATE stdiolink)

set_target_properties(test_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
