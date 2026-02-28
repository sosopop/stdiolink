add_executable(driver_3dvision
    main.cpp
    http_client.cpp
    websocket_client.cpp
)

target_link_libraries(driver_3dvision PRIVATE
    stdiolink
    Qt6::Network
    Qt6::WebSockets
)

set_target_properties(driver_3dvision PROPERTIES
    OUTPUT_NAME "stdio.drv.3dvision"
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS driver_3dvision)
