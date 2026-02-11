set(STDIOLINK_SERVICE_SOURCES
    main.cpp
    engine/js_engine.cpp
    engine/console_bridge.cpp
    engine/module_loader.cpp
    utils/js_convert.cpp
    bindings/js_task.cpp
    bindings/js_driver.cpp
    bindings/js_process.cpp
    bindings/js_task_scheduler.cpp
    bindings/js_wait_any_scheduler.cpp
    bindings/js_stdiolink_module.cpp
    bindings/js_config.cpp
    config/service_args.cpp
    proxy/driver_proxy.cpp
    proxy/wait_any_wrapper.cpp
)

set(STDIOLINK_SERVICE_HEADERS
    engine/js_engine.h
    engine/console_bridge.h
    engine/module_loader.h
    utils/js_convert.h
    bindings/js_task.h
    bindings/js_driver.h
    bindings/js_process.h
    bindings/js_task_scheduler.h
    bindings/js_wait_any_scheduler.h
    bindings/js_stdiolink_module.h
    bindings/js_config.h
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
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
