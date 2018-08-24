set(tests
    "hello_world_test"
    "single_fmu_execution_test"
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
