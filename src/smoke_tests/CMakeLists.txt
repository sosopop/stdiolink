find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME smoke_m101a_service_driver_timeout
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m101a_service_driver_timeout
)

set_tests_properties(
    smoke_m101a_service_driver_timeout
    PROPERTIES
        LABELS "smoke;m101a"
        TIMEOUT 180
)

add_test(
    NAME smoke_m98_json_cli_normalization
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m98_json_cli_normalization
)

set_tests_properties(
    smoke_m98_json_cli_normalization
    PROPERTIES
        LABELS "smoke;m98"
        TIMEOUT 120
)

add_test(
    NAME smoke_m99_cli_example_unification
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m99_cli_example_unification
)

set_tests_properties(
    smoke_m99_cli_example_unification
    PROPERTIES
        LABELS "smoke;m99"
        TIMEOUT 120
)

add_test(
    NAME smoke_m100_cli_docs_and_migration
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py
        --plan m100_cli_docs_and_migration
)

set_tests_properties(
    smoke_m100_cli_docs_and_migration
    PROPERTIES
        LABELS "smoke;m100"
        TIMEOUT 60
)

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
