find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME smoke_m94_server_run_oneshot
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m94_server_run_oneshot
)

set_tests_properties(
    smoke_m94_server_run_oneshot
    PROPERTIES
        LABELS "smoke;m94"
        TIMEOUT 240
)
