# cmake/assemble_runtime_impl.cmake
# 由 assemble_runtime target 通过 cmake -P 调用
# 输入变量: RAW_DIR, RUNTIME_DIR, SOURCE_DIR

if(NOT IS_DIRECTORY "${RAW_DIR}")
    message(FATAL_ERROR "RAW_DIR does not exist: ${RAW_DIR}")
endif()

set(RUNTIME_BIN "${RUNTIME_DIR}/bin")
set(RUNTIME_DATA_ROOT "${RUNTIME_DIR}/data_root")
set(RUNTIME_DEMOS "${RUNTIME_DIR}/demos")
set(RUNTIME_SCRIPTS "${RUNTIME_DIR}/scripts")

# ── 0. 增量清理：仅清理构建产物目录，保留用户态数据 ──
# bin/ 和 data_root/drivers/ 由构建系统全权管理，每次重建
# logs/ workspaces/ config.json 等用户态数据绝不触碰
foreach(_clean_dir "${RUNTIME_BIN}" "${RUNTIME_DATA_ROOT}/drivers")
    if(IS_DIRECTORY "${_clean_dir}")
        file(REMOVE_RECURSE "${_clean_dir}")
    endif()
endforeach()

# ── 1. 创建目录骨架 ──
foreach(_dir
    "${RUNTIME_BIN}"
    "${RUNTIME_DATA_ROOT}/drivers"
    "${RUNTIME_DATA_ROOT}/services"
    "${RUNTIME_DATA_ROOT}/projects"
    "${RUNTIME_DATA_ROOT}/workspaces"
    "${RUNTIME_DATA_ROOT}/logs"
    "${RUNTIME_DEMOS}"
    "${RUNTIME_SCRIPTS}")
    file(MAKE_DIRECTORY "${_dir}")
endforeach()

# ── 2. 复制核心二进制到 bin/（排除驱动） ──
file(GLOB _raw_files "${RAW_DIR}/*")
foreach(_file IN LISTS _raw_files)
    if(IS_DIRECTORY "${_file}")
        continue()
    endif()
    get_filename_component(_name "${_file}" NAME)
    string(FIND "${_name}" "stdio.drv." _drv_pos)
    if(_drv_pos EQUAL 0)
        continue()
    endif()
    file(COPY_FILE "${_file}" "${RUNTIME_BIN}/${_name}"
         RESULT _copy_result)
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy ${_name} to runtime/bin/: ${_copy_result}")
    endif()
endforeach()

# ── 3. 复制驱动到 data_root/drivers/<name>/ ──
file(GLOB _driver_files "${RAW_DIR}/stdio.drv.*")
foreach(_file IN LISTS _driver_files)
    get_filename_component(_name "${_file}" NAME)
    string(REGEX REPLACE "\\.(exe|app|pdb)$" "" _drv_name "${_name}")
    set(_drv_dir "${RUNTIME_DATA_ROOT}/drivers/${_drv_name}")
    file(MAKE_DIRECTORY "${_drv_dir}")
    file(COPY_FILE "${_file}" "${_drv_dir}/${_name}"
         RESULT _copy_result)
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy driver ${_name}: ${_copy_result}")
    endif()
endforeach()

# ── 4. 复制 Qt 插件子目录到 bin/ ──
file(GLOB _raw_entries LIST_DIRECTORIES true "${RAW_DIR}/*")
foreach(_entry IN LISTS _raw_entries)
    if(IS_DIRECTORY "${_entry}")
        file(COPY "${_entry}" DESTINATION "${RUNTIME_BIN}")
    endif()
endforeach()

# ── 5. 两层合并 data_root（production + demo） ──
set(_prod_data "${SOURCE_DIR}/src/data_root")
set(_demo_data "${SOURCE_DIR}/src/demo/server_manager_demo/data_root")
if(IS_DIRECTORY "${_prod_data}")
    file(COPY "${_prod_data}/" DESTINATION "${RUNTIME_DATA_ROOT}")
endif()
if(IS_DIRECTORY "${_demo_data}")
    file(COPY "${_demo_data}/" DESTINATION "${RUNTIME_DATA_ROOT}")
endif()

# ── 6. 复制 demo 资产 ──
set(_js_demo "${SOURCE_DIR}/src/demo/js_runtime_demo")
set(_cfg_demo "${SOURCE_DIR}/src/demo/config_demo")
if(IS_DIRECTORY "${_js_demo}")
    file(COPY "${_js_demo}/" DESTINATION "${RUNTIME_DEMOS}/js_runtime_demo")
endif()
if(IS_DIRECTORY "${_cfg_demo}")
    file(COPY "${_cfg_demo}/" DESTINATION "${RUNTIME_DEMOS}/config_demo")
endif()

# ── 7. 复制 demo 脚本 ──
set(_demo_scripts "${SOURCE_DIR}/src/demo/server_manager_demo/scripts")
if(IS_DIRECTORY "${_demo_scripts}")
    file(COPY "${_demo_scripts}/" DESTINATION "${RUNTIME_SCRIPTS}")
endif()

message(STATUS "Runtime assembled: ${RUNTIME_DIR}")
