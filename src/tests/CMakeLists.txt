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
    test_driver_catalog.cpp
    test_system_help.cpp
    test_js_engine_scaffold.cpp
    test_es_module_loader.cpp
    test_driver_task_binding.cpp
    test_process_binding.cpp
    test_proxy_and_scheduler.cpp
    test_js_integration.cpp
    test_js_stress.cpp
    test_service_config_schema.cpp
    test_service_args.cpp
    test_service_config_validator.cpp
    test_service_config_js.cpp
    test_constants_binding.cpp
    test_path_binding.cpp
    test_fs_binding.cpp
    test_time_binding.cpp
    test_http_binding.cpp
    test_log_binding.cpp
    test_process_async_binding.cpp
    test_service_manifest.cpp
    test_service_directory.cpp
    test_service_loader.cpp
    test_server_args.cpp
    test_server_config.cpp
    test_cors_middleware.cpp
    test_service_file_handler.cpp
    test_service_scanner.cpp
    test_driver_manager_scanner.cpp
    test_schedule.cpp
    test_project_manager.cpp
    test_instance_manager.cpp
    test_schedule_engine.cpp
    test_server_manager.cpp
    test_api_router.cpp
    test_driverlab_ws_handler.cpp
    test_process_monitor.cpp
    test_event_bus.cpp
    test_static_file_server.cpp
    test_process_guard.cpp
)

add_executable(stdiolink_tests ${TEST_SOURCES})

target_sources(stdiolink_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/engine/js_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/engine/console_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/engine/module_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/utils/js_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/utils/js_freeze.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_task.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_driver.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_process.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_task_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_wait_any_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_stdiolink_module.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_config.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_constants.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_path.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_fs.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_time.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_http.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_log.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/bindings/js_process_async.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/config/service_config_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/config/service_args.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/config/service_config_validator.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/config/service_manifest.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/config/service_directory.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/proxy/driver_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service/proxy/wait_any_wrapper.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/config/server_args.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/config/server_config.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/model/schedule.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/model/project.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/manager/project_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/manager/instance_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/manager/schedule_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/manager/process_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/scanner/service_scanner.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/scanner/driver_manager_scanner.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/api_router.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/cors_middleware.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/event_stream_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/service_file_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/static_file_server.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/driverlab_ws_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/driverlab_ws_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/server_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/utils/process_env_utils.cpp
)

target_include_directories(stdiolink_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/stdiolink_service
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server
)

target_link_libraries(stdiolink_tests PRIVATE
    stdiolink
    GTest::gtest
    Qt6::Network
    Qt6::HttpServer
    Qt6::WebSockets
    Qt6::Concurrent
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

target_compile_definitions(stdiolink_tests PRIVATE STDIOLINK_TESTING)

if(TARGET stdiolink_service)
    add_dependencies(stdiolink_tests stdiolink_service)
endif()

# test_driver (用于单元测试的辅助 Driver 程序)
add_executable(test_driver test_driver_main.cpp)

target_link_libraries(test_driver PRIVATE stdiolink)

set_target_properties(test_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# test_meta_driver (支持 --export-meta 的测试 Driver)
add_executable(test_meta_driver test_meta_driver_main.cpp)

target_link_libraries(test_meta_driver PRIVATE stdiolink)

set_target_properties(test_meta_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# test_service_stub (用于 server manager / scheduler 测试的服务进程替身)
add_executable(test_service_stub test_service_stub_main.cpp)

target_link_libraries(test_service_stub PRIVATE Qt6::Core)

set_target_properties(test_service_stub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(stdiolink_tests test_service_stub)

# test_process_async_stub (用于异步进程绑定测试的桩进程)
add_executable(test_process_async_stub test_process_async_stub_main.cpp)

target_link_libraries(test_process_async_stub PRIVATE Qt6::Core)

set_target_properties(test_process_async_stub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(stdiolink_tests test_process_async_stub)

# test_slow_meta_driver (用于 M48 metaTimeoutMs 测试的慢元数据驱动)
add_executable(test_slow_meta_driver test_slow_meta_driver_main.cpp)

target_link_libraries(test_slow_meta_driver PRIVATE stdiolink)

set_target_properties(test_slow_meta_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(stdiolink_tests test_slow_meta_driver)

# test_guard_stub (用于 ProcessGuard 测试的辅助子进程)
add_executable(test_guard_stub test_guard_stub_main.cpp)

target_link_libraries(test_guard_stub PRIVATE stdiolink Qt6::Core Qt6::Network)

set_target_properties(test_guard_stub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(stdiolink_tests test_guard_stub)

# test_output_flood_stub (用于 P0-4 输出缓冲上限测试的大输出桩进程)
add_executable(test_output_flood_stub test_output_flood_stub_main.cpp)

target_link_libraries(test_output_flood_stub PRIVATE Qt6::Core)

set_target_properties(test_output_flood_stub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(stdiolink_tests test_output_flood_stub)
