set(SERVER_MANAGER_DEMO_DST_DIR "${CMAKE_BINARY_DIR}/bin/server_manager_demo")

# 源层重叠检测：production 与 demo 的 services/ 和 projects/ 不应有同名条目
foreach(_subdir services projects)
    set(_prod_dir "${CMAKE_SOURCE_DIR}/src/data_root/${_subdir}")
    set(_demo_dir "${CMAKE_CURRENT_SOURCE_DIR}/data_root/${_subdir}")
    if(IS_DIRECTORY "${_prod_dir}" AND IS_DIRECTORY "${_demo_dir}")
        file(GLOB _prod_entries RELATIVE "${_prod_dir}" "${_prod_dir}/*")
        file(GLOB _demo_entries RELATIVE "${_demo_dir}" "${_demo_dir}/*")
        set(_overlap "")
        foreach(_entry IN LISTS _prod_entries)
            if("${_entry}" IN_LIST _demo_entries)
                list(APPEND _overlap "${_entry}")
            endif()
        endforeach()
        if(_overlap)
            message(WARNING "Production and demo data_root/${_subdir}/ have overlapping names: ${_overlap} — demo will overwrite production")
        endif()
    endif()
endforeach()

add_custom_target(server_manager_demo_assets ALL
    # 第 1 层: production data_root
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/data_root"
        "${SERVER_MANAGER_DEMO_DST_DIR}/data_root"
    # 第 2 层: demo data_root（叠加）
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

if(TARGET driver_modbusrtu)
    add_dependencies(server_manager_demo_assets driver_modbusrtu)
endif()

if(TARGET driver_modbustcp)
    add_dependencies(server_manager_demo_assets driver_modbustcp)
endif()

if(TARGET driver_3dvision)
    add_dependencies(server_manager_demo_assets driver_3dvision)
endif()

if(TARGET heartbeat_driver)
    add_dependencies(server_manager_demo_assets heartbeat_driver)
endif()

if(TARGET kv_store_driver)
    add_dependencies(server_manager_demo_assets kv_store_driver)
endif()
