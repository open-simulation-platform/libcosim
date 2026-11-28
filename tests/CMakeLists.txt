set(TEST_FIXTURE_CMAKE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${TEST_FIXTURE_CMAKE_DIR}")

include(TestFixtureDependencies)
set(TEST_FMI1_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/fmi1")
set(TEST_FMI2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/fmi2")
set(TEST_FMI3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/fmi3")

if(NOT DEFINED GEN_TEST_DATA_DIR)
    set(GEN_TEST_DATA_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated-test-data")
endif()
include(PrepareDahlquistTestData)
prepare_dahlquist_test_data()

include(Fmi3SystemStructureFixtures)

set(tests
    "ecco_algorithm_from_system_structure_test"
    "ecco_algorithm_multi_bond_test"
    "ecco_algorithm_powerbond_logconfig_test"
    "ecco_algorithm_test"
    "ecco_algorithm_two_bond_test"
    "file_observer_dynamic_logging_test"
    "file_observer_logging_test"
    "file_observer_logging_from_config_test"
    "fixed_step_algorithm_test"
    "last_value_observer_test"
    "monitor_modified_variables_test"
    "multi_fixed_step_algorithm_test"
    "osp_config_parser_test"
    "ramp_modifier_test"
    "save_state_test"
    "time_series_observer_test"
    "trend_buffer_test"
    "scenario_manager_test"
    "synchronized_xy_series_test"
    "config_end_time_test"
    "state_init_test"
    "fmi3_osp_system_structure_test"
    "fmi3_osp_ecco_test"
)

set(unittests
    "function_unittest"
    "file_cache_unittest"
    "clock_routing_unittest"
    "fixed_step_decimation_unittest"
    "fmi_v1_fmu_unittest"
    "fmi_v2_fmu_unittest"
    "fmi_v3_fmu_unittest"
    "fmi_v3_conformance_unittest"
    "fmi3_validation_unittest"
    "fmi3_fmi2_mixed_tests"
    "orchestration_unittest"
    "scenario_parser_unittest"
    "slave_simulator_unittest"
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
            "proxyfmu_save_state_test"
            )
endif()

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ tests"
        SOURCES "${test}.cpp"
        DEPENDENCIES "cosim"
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
        REFERENCE_FMU_V2_DIR "${TEST_FMI2_DIR}"
        GEN_TEST_DATA_DIR "${GEN_TEST_DATA_DIR}"
    )
endforeach()

foreach(test IN LISTS unittests)

    set(dependencies cosim "Boost::unit_test_framework" "Boost::timer")
    set(test_sources "${test}.cpp")
    if(test STREQUAL "fmi_v2_fmu_unittest" OR test STREQUAL "fmi_v3_fmu_unittest")
        list(APPEND test_sources fmu_archive_fixture.cpp)
    endif()
    if(test STREQUAL "fmi_v3_fmu_unittest")
        list(APPEND test_sources
            fmi3_values_unittest.cpp
            fmi3_events_unittest.cpp
            fmi3_clocks_unittest.cpp)
    endif()
    if (LIBCOSIM_WITH_PROXYFMU)
        list(APPEND dependencies "proxyfmu::proxyfmu-client")
    endif()

    add_test_executable(
        "cpp_${test}"
        FOLDER "C++ unit tests"
        SOURCES ${test_sources}
        DEPENDENCIES ${dependencies}
        DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
        REFERENCE_FMU_V2_DIR "${TEST_FMI2_DIR}"
        GEN_TEST_DATA_DIR "${GEN_TEST_DATA_DIR}"
    )
    target_include_directories("cpp_${test}" PRIVATE "$<TARGET_PROPERTY:cosim,SOURCE_DIR>")
endforeach()

# The general FMI 3 suite exercises supported behavior. The separate
# conformance suite covers operation statuses, state recovery, serialization,
# and every prohibited public lifecycle transition.
libcosim_use_test_fixtures(cpp_file_cache_unittest
    ENVIRONMENT "REFERENCE_FMU_DIR=${TEST_FMI3_DIR}")

libcosim_use_test_fixtures(cpp_fmi_v3_fmu_unittest
    ENVIRONMENT
        "REFERENCE_FMU_DIR=${TEST_FMI3_DIR}"
        "FMI3_LOGGING_FIXTURE=${TEST_FMI3_DIR}/Fmi3Derivatives.fmu"
        "FMI3_FAULT_FIXTURE_DIR=${TEST_FMI3_DIR}"
        "FMI3_STEP_PROBE_FMU=${TEST_FMI3_DIR}/StepProbe.fmu"
        "FMI3_ARRAY_FIXTURE_DIR=${TEST_FMI3_DIR}"
        "FMI3_NONCONVERGING_FMU=${TEST_FMI3_DIR}/nonconverging/Stair.fmu"
        "FMI3_UNSCHEDULED_STAIR_FMU=${TEST_FMI3_DIR}/unscheduled/Stair.fmu"
        "FMI2_MIXED_TYPES_FMU=${TEST_FMI2_DIR}/MixedTypes.fmu")

libcosim_use_test_fixtures(cpp_fmi_v3_conformance_unittest
    ENVIRONMENT "FMI3_FAULT_FIXTURE_DIR=${TEST_FMI3_DIR}")

libcosim_use_test_fixtures(cpp_orchestration_unittest
    ENVIRONMENT "REFERENCE_FMU_DIR=${TEST_FMI3_DIR}")

libcosim_use_test_fixtures(cpp_fmi3_validation_unittest
    ENVIRONMENT
        "REFERENCE_FMU_DIR=${TEST_FMI3_DIR}"
        "FMI2_MIXED_TYPES_FMU=${TEST_FMI2_DIR}/MixedTypes.fmu")

libcosim_use_test_fixtures(cpp_fmi_v2_fmu_unittest
    ENVIRONMENT
        "FMI2_MIXED_TYPES_FMU=${TEST_FMI2_DIR}/MixedTypes.fmu"
        "FMI2_DERIVATIVE_FIXTURE=${TEST_FMI2_DIR}/Fmi2Derivatives.fmu")

libcosim_use_test_fixtures(cpp_fmi_v1_fmu_unittest
    ENVIRONMENT
        "FMI1_ENUMERATION_FMU=${TEST_FMI1_DIR}/EnumerationFixture.fmu"
        "FMI2_MIXED_TYPES_FMU=${TEST_FMI2_DIR}/MixedTypes.fmu")

# Exercise actual FMI 2 and FMI 3 Co-Simulation FMUs in one ordinary
# OspSystemStructure execution.
#
# cpp_fmi3_osp_system_structure_test validates FMI 3 fixed-step topologies
# through both real OspSystemStructure.xml files and the public
# cosim::system_structure API. It covers exact values, arrays, discrete time
# events, relative FMU resolution, periodic boundary step capping, triggered
# Clock routing, associated-data ordering, held Clock-associated observations,
# and invalid configurations.
#
# cpp_fmi3_osp_ecco_test validates FMI 3 ECCO power bonds, adaptive step
# selection, Clock-boundary capping, triggered routing, and exact side-channel
# transport through XML and programmatic system structures.
foreach(system_structure_test IN ITEMS
        cpp_fmi3_fmi2_mixed_tests
        cpp_fmi3_osp_system_structure_test
        cpp_fmi3_osp_ecco_test)
    libcosim_use_test_fixtures(${system_structure_test}
        TARGETS fmi3_osp_system_structure_fixtures
        ENVIRONMENT
            "FMI3_SYSTEM_STRUCTURE_DIR=${FMI3_SYSTEM_STRUCTURE_FIXTURE_DIR}")
endforeach()
