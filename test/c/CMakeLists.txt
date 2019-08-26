set(tests
    "single_fmu_execution_test"
    "multiple_fmus_execution_test"
    "load_config_and_teardown_test"
    "observer_initial_samples_test"
    "observer_can_buffer_samples"
    "observer_multiple_slaves_test"
    "real_time_test"
    "connections_test"
    "execution_from_ssp_test"
    "time_series_observer_test"
    "variable_metadata_test"
    "model_metadata_test"
)

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "c_${test}"
        FOLDER "C tests"
        SOURCES "${test}.c"
        DEPENDENCIES csecorec
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )
endforeach()
