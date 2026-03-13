# cmake/assemble_runtime.cmake
# 组装运行时目录，使其与发布包同构

set(_published_driver_basenames "")
get_property(_all_exe_targets GLOBAL PROPERTY STDIOLINK_EXECUTABLE_TARGETS)
foreach(_target IN LISTS _all_exe_targets)
    if(NOT TARGET ${_target})
        continue()
    endif()

    get_target_property(_output_name ${_target} OUTPUT_NAME)
    if(NOT _output_name)
        set(_output_name "${_target}")
    endif()
    if(NOT _output_name MATCHES "^stdio\\.drv\\.")
        continue()
    endif()

    get_target_property(_source_dir ${_target} SOURCE_DIR)
    string(REPLACE "\\" "/" _source_dir "${_source_dir}")
    if(_source_dir MATCHES "/src/demo(/|$)")
        continue()
    endif()

    list(APPEND _published_driver_basenames "${_output_name}")
endforeach()
list(REMOVE_DUPLICATES _published_driver_basenames)
list(JOIN _published_driver_basenames "|" _published_driver_basenames_arg)

if(STDIOLINK_IS_MULTI_CONFIG)
    set(_assemble_raw_dir "${CMAKE_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>")
    set(_assemble_runtime_dir "${CMAKE_BINARY_DIR}/runtime_$<LOWER_CASE:$<CONFIG>>")
else()
    set(_assemble_raw_dir "${STDIOLINK_RAW_DIR}")
    set(_assemble_runtime_dir "${STDIOLINK_RUNTIME_DIR}")
endif()

add_custom_target(assemble_runtime ALL
    COMMAND ${CMAKE_COMMAND}
        -DRAW_DIR="${_assemble_raw_dir}"
        -DRUNTIME_DIR="${_assemble_runtime_dir}"
        -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
        -DPUBLISHED_DRIVER_BASENAMES_RAW="${_published_driver_basenames_arg}"
        -P "${CMAKE_SOURCE_DIR}/cmake/assemble_runtime_impl.cmake"
    COMMENT "Assembling runtime directory"
)

# assemble_runtime 依赖所有可执行 target，确保编译完成后再组装
foreach(_target IN LISTS _all_exe_targets)
    if(TARGET ${_target})
        add_dependencies(assemble_runtime ${_target})
    endif()
endforeach()

if(TARGET calculator_driver)
    set(_calculator_driver_name "stdio.drv.calculator${CMAKE_EXECUTABLE_SUFFIX}")
    set(_calculator_runtime_dir "${_assemble_runtime_dir}/data_root/drivers/stdio.drv.calculator")

    add_custom_target(assemble_test_runtime_assets ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_calculator_runtime_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_assemble_raw_dir}/${_calculator_driver_name}"
            "${_calculator_runtime_dir}/${_calculator_driver_name}"
        COMMENT "Staging test-only demo runtime assets"
    )
    add_dependencies(assemble_test_runtime_assets assemble_runtime calculator_driver)
endif()
