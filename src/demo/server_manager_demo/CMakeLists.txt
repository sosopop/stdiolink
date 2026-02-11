set(SERVER_MANAGER_DEMO_DST_DIR "${CMAKE_BINARY_DIR}/bin/server_manager_demo")

add_custom_target(server_manager_demo_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data_root"
        "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "${SERVER_MANAGER_DEMO_DST_DIR}/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${SERVER_MANAGER_DEMO_DST_DIR}/README.md"
    COMMENT "Copy server manager demo assets to ${SERVER_MANAGER_DEMO_DST_DIR}"
)

if(TARGET stdiolink_server)
    add_dependencies(server_manager_demo_assets stdiolink_server)
endif()

if(TARGET stdiolink_service)
    add_dependencies(server_manager_demo_assets stdiolink_service)
endif()

if(TARGET calculator_driver)
    add_dependencies(server_manager_demo_assets calculator_driver)
endif()
