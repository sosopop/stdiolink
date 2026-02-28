set(STDIOLINK_SERVER_SOURCES
    main.cpp
    config/server_args.cpp
    config/server_config.cpp
    model/schedule.cpp
    model/project.cpp
    model/instance.cpp
    manager/project_manager.cpp
    manager/instance_manager.cpp
    manager/instance_log_writer.cpp
    manager/schedule_engine.cpp
    manager/process_monitor.cpp
    scanner/service_scanner.cpp
    scanner/driver_manager_scanner.cpp
    http/api_router.cpp
    http/cors_middleware.cpp
    http/event_bus.cpp
    http/event_log.cpp
    http/event_stream_handler.cpp
    http/service_file_handler.cpp
    http/static_file_server.cpp
    http/driverlab_ws_handler.cpp
    http/driverlab_ws_connection.cpp
    server_manager.cpp
    utils/process_env_utils.cpp
    utils/server_logger.cpp
)

set(STDIOLINK_SERVER_HEADERS
    config/server_args.h
    config/server_config.h
    model/instance.h
    model/schedule.h
    model/project.h
    manager/project_manager.h
    manager/instance_manager.h
    manager/instance_log_writer.h
    manager/schedule_engine.h
    manager/process_monitor.h
    model/process_info.h
    scanner/service_scanner.h
    scanner/driver_manager_scanner.h
    http/http_helpers.h
    http/cors_middleware.h
    http/event_bus.h
    http/event_log.h
    http/event_stream_handler.h
    http/service_file_handler.h
    http/static_file_server.h
    http/driverlab_ws_handler.h
    http/driverlab_ws_connection.h
    http/api_router.h
    server_manager.h
    utils/process_env_utils.h
    utils/server_logger.h
)

add_executable(stdiolink_server
    ${STDIOLINK_SERVER_SOURCES}
    ${STDIOLINK_SERVER_HEADERS}
)

target_include_directories(stdiolink_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_server PRIVATE
    stdiolink_service_config
    stdiolink
    spdlog::spdlog
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::WebSockets
    Qt6::Concurrent
)

set_target_properties(stdiolink_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STDIOLINK_RAW_DIR}"
)
set_property(GLOBAL APPEND PROPERTY STDIOLINK_EXECUTABLE_TARGETS stdiolink_server)

if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET stdiolink_server POST_BUILD
        COMMAND Qt6::windeployqt
            --dir "$<TARGET_FILE_DIR:stdiolink_server>"
            "$<TARGET_FILE:stdiolink_server>"
        COMMENT "Deploying Qt runtime/plugins for stdiolink_server"
    )
endif()
