set(tests
    "async_slave_mockup_test"
    "cse_config_parser_test"
    "fixed_step_algorithm_test"
    "file_observer_logging_test"
    "file_observer_logging_from_config_test"
    "last_value_observer_test"
    "multi_connections_test"
    "multi_fixed_step_algorithm_test"
    "time_series_observer_test"
    "trend_buffer_test"
    "monitor_modified_variables_test"
    "scenario_manager_test"
    "synchronized_xy_series_test"
)

if(CSECORE_WITH_FMUPROXY AND CSECORE_TEST_FMUPROXY)
    list(APPEND tests
        "fmuproxy_test"
        "ssp_parser_fmuproxy_test"
    )
endif()

set(unittests
    "async_slave_unittest"
    "fmi_v1_fmu_unittest"
    "fmi_v2_fmu_unittest"
    "model_unittest"
    "orchestration_unittest"
    "scenario_parser_unittest"
    "ssp_parser_unittest"
    "uri_unittest"
    "utility_concurrency_unittest"
    "utility_filesystem_unittest"
    "utility_uuid_unittest"
    "utility_zip_unittest"
)

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES csecorecpp
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )
endforeach()

foreach(test IN LISTS unittests)
    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ unit tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES csecorecpp "Boost::unit_test_framework" "Boost::timer"
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )
    target_include_directories("cpp_${test}" PRIVATE "$<TARGET_PROPERTY:csecorecpp,SOURCE_DIR>")
endforeach()
