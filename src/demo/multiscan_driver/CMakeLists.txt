add_executable(multiscan_driver main.cpp multiscan_driver.cpp)
target_link_libraries(multiscan_driver PRIVATE stdiolink)
set_target_properties(multiscan_driver PROPERTIES
    OUTPUT_NAME "stdio.drv.multiscan"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS multiscan_driver)
