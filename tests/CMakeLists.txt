set(tests
    "fixed_step_algorithm_test"
    "ecco_algorithm_test"
    "ecco_algorithm_multi_bond_test"
    "file_observer_dynamic_logging_test"
    "file_observer_logging_test"
    "file_observer_logging_from_config_test"
    "last_value_observer_test"
    "monitor_modified_variables_test"
    "multi_fixed_step_algorithm_test"
    "osp_config_parser_test"
    "ramp_modifier_test"
    "time_series_observer_test"
    "trend_buffer_test"
    "scenario_manager_test"
    "synchronized_xy_series_test"
)

set(unittests
    "function_unittest"
    "fmi_v1_fmu_unittest"
    "fmi_v2_fmu_unittest"
    "orchestration_unittest"
    "scenario_parser_unittest"
    "ssp_loader_unittest"
    "system_structure_unittest"
    "time_unittest"
    "uri_unittest"
    "utility_concurrency_unittest"
    "utility_filesystem_unittest"
    "utility_uuid_unittest"
    "utility_zip_unittest"
)

if(LIBCOSIM_WITH_PROXYFMU)
    list(APPEND unittests
            "proxyfmu_integration_unittest"
            "proxyfmu_library_unittest"
            )
    list(APPEND tests
            "proxyfmu_osp_config_parser_test"
            )
endif()

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES cosim
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
    )
endforeach()

foreach(test IN LISTS unittests)

    set(dependencies cosim "Boost::unit_test_framework" "Boost::timer")
    if (LIBCOSIM_WITH_PROXYFMU)
        list(APPEND dependencies "proxyfmu::proxyfmu-client")
    endif()

    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ unit tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES ${dependencies}
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
    )
    target_include_directories("cpp_${test}" PRIVATE "$<TARGET_PROPERTY:cosim,SOURCE_DIR>")
endforeach()
