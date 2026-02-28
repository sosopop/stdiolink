add_executable(demo_host main.cpp)
target_link_libraries(demo_host PRIVATE stdiolink)
set_target_properties(demo_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS demo_host)
