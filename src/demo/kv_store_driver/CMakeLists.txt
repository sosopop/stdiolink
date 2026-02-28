add_executable(kv_store_driver main.cpp)
target_link_libraries(kv_store_driver PRIVATE stdiolink)
set_target_properties(kv_store_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.kv-store"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS kv_store_driver)
