# cmake/assemble_runtime.cmake
# 组装运行时目录，使其与发布包同构

# 源层重叠检测（从 server_manager_demo/CMakeLists.txt 迁移）
foreach(_subdir services projects)
    set(_prod_dir "${CMAKE_SOURCE_DIR}/src/data_root/${_subdir}")
    set(_demo_dir "${CMAKE_SOURCE_DIR}/src/demo/server_manager_demo/data_root/${_subdir}")
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
            message(WARNING "Production and demo data_root/${_subdir}/ overlap: ${_overlap}")
        endif()
    endif()
endforeach()

add_custom_target(assemble_runtime ALL
    COMMAND ${CMAKE_COMMAND}
        -DRAW_DIR="${STDIOLINK_RAW_DIR}"
        -DRUNTIME_DIR="${STDIOLINK_RUNTIME_DIR}"
        -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/assemble_runtime_impl.cmake"
    COMMENT "Assembling runtime directory: ${STDIOLINK_RUNTIME_DIR}"
)

# assemble_runtime 依赖所有可执行 target，确保编译完成后再组装
get_property(_all_exe_targets GLOBAL PROPERTY STDIOLINK_EXECUTABLE_TARGETS)
foreach(_target IN LISTS _all_exe_targets)
    if(TARGET ${_target})
        add_dependencies(assemble_runtime ${_target})
    endif()
endforeach()
