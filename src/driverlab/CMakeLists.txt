# DriverLab - Driver 测试工具
# Qt GUI 应用程序

set(DRIVERLAB_SOURCES
    main.cpp
    ui/mainwindow.cpp
    ui/driver_explorer.cpp
    ui/driver_test_page.cpp
    ui/command_list.cpp
    ui/parameter_form.cpp
    ui/result_panel.cpp
    ui/doc_viewer.cpp
    ui/widgets/form_widget_factory.cpp
    ui/widgets/json_editor.cpp
    ui/widgets/array_editor.cpp
    ui/widgets/json_tree_widget.cpp
    models/driver_session.cpp
    models/command_history.cpp
)

set(DRIVERLAB_HEADERS
    ui/mainwindow.h
    ui/driver_explorer.h
    ui/driver_test_page.h
    ui/command_list.h
    ui/parameter_form.h
    ui/result_panel.h
    ui/doc_viewer.h
    ui/widgets/form_widget_factory.h
    ui/widgets/json_editor.h
    ui/widgets/array_editor.h
    ui/widgets/json_tree_widget.h
    ui/widgets/emoji_icon.h
    models/driver_session.h
    models/command_history.h
)

set(DRIVERLAB_RESOURCES
    resources/driverlab.qrc
)

add_executable(driverlab WIN32
    ${DRIVERLAB_SOURCES}
    ${DRIVERLAB_HEADERS}
    ${DRIVERLAB_RESOURCES}
)

target_include_directories(driverlab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(driverlab PRIVATE
    stdiolink
    Qt6::Core
    Qt6::Widgets
)

set_target_properties(driverlab PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if (WIN32)
  add_custom_command(TARGET driverlab POST_BUILD
    COMMAND Qt6::windeployqt $<TARGET_FILE:driverlab>
    COMMENT "Deploying Qt runtime (DLLs + plugins) with windeployqt"
  )
endif()