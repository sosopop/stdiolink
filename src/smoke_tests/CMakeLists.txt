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

add_test(
    NAME smoke_m95_driver_examples
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m95_driver_examples
)

set_tests_properties(
    smoke_m95_driver_examples
    PROPERTIES
        LABELS "smoke;m95"
        TIMEOUT 480
)

add_test(
    NAME smoke_m96_3dvision_ws
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m96_3dvision_ws
)

set_tests_properties(
    smoke_m96_3dvision_ws
    PROPERTIES
        LABELS "smoke;m96"
        TIMEOUT 120
)

add_test(
    NAME smoke_m97_plc_crane_sim
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m97_plc_crane_sim
)

set_tests_properties(
    smoke_m97_plc_crane_sim
    PROPERTIES
        LABELS "smoke;m97"
        TIMEOUT 180
)
