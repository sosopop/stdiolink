set(CONFIG_DEMO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(CONFIG_DEMO_DST_DIR "${CMAKE_BINARY_DIR}/bin/config_demo")

add_custom_target(config_demo_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CONFIG_DEMO_SRC_DIR}" "${CONFIG_DEMO_DST_DIR}"
    COMMENT "Copy config demo scripts to ${CONFIG_DEMO_DST_DIR}"
)

if(TARGET stdiolink_service)
    add_dependencies(config_demo_assets stdiolink_service)
endif()
