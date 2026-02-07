set(JS_RUNTIME_DEMO_DST_DIR "${CMAKE_BINARY_DIR}/bin/js_runtime_demo")

add_custom_target(js_runtime_demo_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/services"
        "${JS_RUNTIME_DEMO_DST_DIR}/services"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shared"
        "${JS_RUNTIME_DEMO_DST_DIR}/shared"
    COMMENT "Copy JS runtime demo services to ${JS_RUNTIME_DEMO_DST_DIR}"
)

if(TARGET stdiolink_service)
    add_dependencies(js_runtime_demo_assets stdiolink_service)
endif()

if(TARGET calculator_driver)
    add_dependencies(js_runtime_demo_assets calculator_driver)
endif()
