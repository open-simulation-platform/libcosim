set(tests
    "async_slave_mockup_test"
    "fixed_step_algorithm_test"
    "hello_world_test"
)
set(unittests
    "fmi_v1_fmu_unittest"
    "fmi_v2_fmu_unittest"
    "libevent_unittest"
    "model_unittest"
    "utility_filesystem_unittest"
    "utility_uuid_unittest"
    "utility_zip_unittest"
    "file_observer_logging_test"
    "trend_buffer_test"
    "membuffer_observer_test"
    "ssp_parser_test"
)

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES csecorecpp
    )
endforeach()

foreach(test IN LISTS unittests)
    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ unit tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES csecorecpp "Boost::unit_test_framework"
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )
    target_include_directories("cpp_${test}" PRIVATE "$<TARGET_PROPERTY:csecorecpp,SOURCE_DIR>")
endforeach()
