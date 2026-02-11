set(STDIOLINK_SERVER_SOURCES
    main.cpp
    config/server_args.cpp
    config/server_config.cpp
    model/schedule.cpp
    model/project.cpp
    manager/project_manager.cpp
    manager/instance_manager.cpp
    manager/schedule_engine.cpp
    scanner/service_scanner.cpp
    scanner/driver_manager_scanner.cpp
    http/api_router.cpp
    server_manager.cpp
)

set(STDIOLINK_SERVER_HEADERS
    config/server_args.h
    config/server_config.h
    model/instance.h
    model/schedule.h
    model/project.h
    manager/project_manager.h
    manager/instance_manager.h
    manager/schedule_engine.h
    scanner/service_scanner.h
    scanner/driver_manager_scanner.h
    http/http_helpers.h
    http/api_router.h
    server_manager.h
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
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::WebSockets
)

set_target_properties(stdiolink_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
