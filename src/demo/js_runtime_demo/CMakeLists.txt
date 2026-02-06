set(JS_RUNTIME_DEMO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(JS_RUNTIME_DEMO_DST_DIR "${CMAKE_BINARY_DIR}/bin/js_runtime_demo")

add_custom_target(js_runtime_demo_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${JS_RUNTIME_DEMO_SRC_DIR}" "${JS_RUNTIME_DEMO_DST_DIR}"
    COMMENT "Copy JS runtime demo scripts to ${JS_RUNTIME_DEMO_DST_DIR}"
)

if(TARGET stdiolink_service)
    add_dependencies(js_runtime_demo_assets stdiolink_service)
endif()

if(TARGET calculator_driver)
    add_dependencies(js_runtime_demo_assets calculator_driver)
endif()
