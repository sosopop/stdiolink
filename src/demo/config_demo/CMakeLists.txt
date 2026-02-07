set(CONFIG_DEMO_DST_DIR "${CMAKE_BINARY_DIR}/bin/config_demo")

add_custom_target(config_demo_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/services"
        "${CONFIG_DEMO_DST_DIR}/services"
    COMMENT "Copy config demo services to ${CONFIG_DEMO_DST_DIR}"
)

if(TARGET stdiolink_service)
    add_dependencies(config_demo_assets stdiolink_service)
endif()
