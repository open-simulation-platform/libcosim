set(tests
    "hello_world_test"
    "single_fmu_execution_test"
    "multiple_fmus_execution_test"
    "observer_should_contain_initial_samples"
    "observer_can_buffer_samples"
    "observer_can_contain_multiple_slaves_test"
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
