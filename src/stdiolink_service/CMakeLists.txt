set(STDIOLINK_SERVICE_SOURCES
    main.cpp
    engine/js_engine.cpp
    engine/console_bridge.cpp
    engine/module_loader.cpp
    utils/js_convert.cpp
    utils/js_freeze.cpp
    bindings/js_task.cpp
    bindings/js_driver.cpp
    bindings/js_process.cpp
    bindings/js_task_scheduler.cpp
    bindings/js_wait_any_scheduler.cpp
    bindings/js_stdiolink_module.cpp
    bindings/js_config.cpp
    bindings/js_constants.cpp
    bindings/js_path.cpp
    bindings/js_fs.cpp
    bindings/js_time.cpp
    bindings/js_http.cpp
    bindings/js_log.cpp
    bindings/js_process_async.cpp
    bindings/js_driver_resolve.cpp
    bindings/js_driver_resolve_binding.cpp
    config/service_args.cpp
    proxy/driver_proxy.cpp
    proxy/wait_any_wrapper.cpp
)

set(STDIOLINK_SERVICE_HEADERS
    engine/js_engine.h
    engine/console_bridge.h
    engine/module_loader.h
    utils/js_convert.h
    utils/js_freeze.h
    bindings/js_task.h
    bindings/js_driver.h
    bindings/js_process.h
    bindings/js_task_scheduler.h
    bindings/js_wait_any_scheduler.h
    bindings/js_stdiolink_module.h
    bindings/js_config.h
    bindings/js_constants.h
    bindings/js_path.h
    bindings/js_fs.h
    bindings/js_time.h
    bindings/js_http.h
    bindings/js_log.h
    bindings/js_process_async.h
    bindings/js_driver_resolve.h
    bindings/js_driver_resolve_binding.h
    config/service_config_schema.h
    config/service_args.h
    config/service_config_validator.h
    config/service_config_help.h
    config/service_manifest.h
    config/service_directory.h
    proxy/driver_proxy.h
    proxy/wait_any_wrapper.h
)

add_library(stdiolink_service_config STATIC
    config/service_config_schema.cpp
    config/service_config_validator.cpp
    config/service_config_help.cpp
    config/service_manifest.cpp
    config/service_directory.cpp
)

target_include_directories(stdiolink_service_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_service_config PUBLIC
    stdiolink
    Qt6::Core
)

add_executable(stdiolink_service
    ${STDIOLINK_SERVICE_SOURCES}
    ${STDIOLINK_SERVICE_HEADERS}
)

target_include_directories(stdiolink_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_service PRIVATE
    stdiolink_service_config
    stdiolink
    Qt6::Core
    Qt6::Network
)

if(TARGET qjs)
    target_link_libraries(stdiolink_service PRIVATE qjs)
elseif(TARGET qjs::qjs)
    target_link_libraries(stdiolink_service PRIVATE qjs::qjs)
elseif(TARGET quickjs)
    target_link_libraries(stdiolink_service PRIVATE quickjs)
elseif(TARGET quickjs::quickjs)
    target_link_libraries(stdiolink_service PRIVATE quickjs::quickjs)
else()
    message(FATAL_ERROR "Cannot find QuickJS target from qjs package")
endif()

set_target_properties(stdiolink_service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS stdiolink_service)
