include(ExternalProject)

set(REFERENCE_FMUS_REPOSITORY
    "https://github.com/open-simulation-platform/Reference-FMUs.git"
    CACHE STRING "Reference-FMUs repository used by FMI 2 derivative tests")
set(REFERENCE_FMUS_VERSION
    "v0.0.41"
    CACHE STRING "Reference-FMUs release tag used by FMI 2 derivative tests")
find_package(Git REQUIRED)

set(REFERENCE_FMUS_SOURCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-source"
    CACHE PATH "Reference-FMUs source directory used by FMI 2 tests")
if(NOT EXISTS "${REFERENCE_FMUS_SOURCE_DIR}/CMakeLists.txt")
    if(EXISTS "${REFERENCE_FMUS_SOURCE_DIR}")
        file(GLOB reference_fmus_existing_entries
            "${REFERENCE_FMUS_SOURCE_DIR}/*")
        if(reference_fmus_existing_entries)
            message(FATAL_ERROR
                "Reference-FMUs source directory is not an empty checkout: "
                "${REFERENCE_FMUS_SOURCE_DIR}")
        endif()
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone
            --branch "${REFERENCE_FMUS_VERSION}"
            "${REFERENCE_FMUS_REPOSITORY}"
            "${REFERENCE_FMUS_SOURCE_DIR}"
        RESULT_VARIABLE reference_fmus_clone_result
        ERROR_VARIABLE reference_fmus_clone_error)
    if(NOT reference_fmus_clone_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to clone Reference-FMUs: ${reference_fmus_clone_error}")
    endif()
endif()

set(REFERENCE_FMUS_V2_BINARY_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-build")
set(REFERENCE_FMUS_V2_STAMP_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-stamp-${REFERENCE_FMUS_VERSION}")
ExternalProject_Add(
    reference_fmus_v2_build
    SOURCE_DIR "${REFERENCE_FMUS_SOURCE_DIR}"
    BINARY_DIR "${REFERENCE_FMUS_V2_BINARY_DIR}"
    STAMP_DIR "${REFERENCE_FMUS_V2_STAMP_DIR}"
    CMAKE_ARGS
        "-DFMI_VERSION=2"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    INSTALL_COMMAND ""
    TEST_COMMAND "")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT DEFINED GEN_TEST_DATA_DIR)
    set(GEN_TEST_DATA_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated-test-data")
endif()
include(PrepareDahlquistTestData)
prepare_dahlquist_test_data()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FMI2_TEST_PLATFORM "win64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(FMI2_TEST_PLATFORM "linux64")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(FMI2_TEST_PLATFORM "darwin64")
else()
    message(FATAL_ERROR
        "No FMI 2 test platform tuple for ${CMAKE_SYSTEM_NAME} "
        "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(fmi2_derivative_binary_directory
    "${CMAKE_CURRENT_BINARY_DIR}/fmi2-derivative-binary")
add_library(
    fmi2_derivative_fixture_library
    SHARED
    "fmi2_derivative_fixture/model.c")
target_include_directories(
    fmi2_derivative_fixture_library
    PRIVATE
        "${REFERENCE_FMUS_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi2_derivative_fixture")
set_target_properties(
    fmi2_derivative_fixture_library
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME "Fmi2Derivatives"
        RUNTIME_OUTPUT_DIRECTORY "${fmi2_derivative_binary_directory}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${fmi2_derivative_binary_directory}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${fmi2_derivative_binary_directory}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${fmi2_derivative_binary_directory}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${fmi2_derivative_binary_directory}"
        LIBRARY_OUTPUT_DIRECTORY "${fmi2_derivative_binary_directory}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${fmi2_derivative_binary_directory}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${fmi2_derivative_binary_directory}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${fmi2_derivative_binary_directory}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${fmi2_derivative_binary_directory}")

set(FMI2_DERIVATIVE_FIXTURE
    "${REFERENCE_FMUS_V2_BINARY_DIR}/fmus/Fmi2Derivatives.fmu")
add_custom_command(
    OUTPUT "${FMI2_DERIVATIVE_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT_FMU=${FMI2_DERIVATIVE_FIXTURE}"
        "-DFIXTURE_BINARY=$<TARGET_FILE:fmi2_derivative_fixture_library>"
        "-DMODEL_DESCRIPTION=${CMAKE_CURRENT_SOURCE_DIR}/fmi2_derivative_fixture/modelDescription.xml"
        "-DFMI2_TEST_PLATFORM=${FMI2_TEST_PLATFORM}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CreateFmi2MixedTypesFixture.cmake"
    DEPENDS
        reference_fmus_v2_build
        fmi2_derivative_fixture_library
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CreateFmi2MixedTypesFixture.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi2_derivative_fixture/modelDescription.xml"
    VERBATIM)
add_custom_target(
    fmi2_derivative_fixture
    DEPENDS "${FMI2_DERIVATIVE_FIXTURE}")

set(tests        
    "ecco_algorithm_from_system_structure_test"
    "ecco_algorithm_multi_bond_test"    
    "ecco_algorithm_test"
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
)

set(unittests
    "function_unittest"
    "fmi_v1_fmu_unittest"
    "fmi_v2_fmu_unittest"
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
        REFERENCE_FMU_V2_DIR "${REFERENCE_FMUS_V2_BINARY_DIR}/fmus"
        GEN_TEST_DATA_DIR "${GEN_TEST_DATA_DIR}"
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
        REFERENCE_FMU_V2_DIR "${REFERENCE_FMUS_V2_BINARY_DIR}/fmus"
        GEN_TEST_DATA_DIR "${GEN_TEST_DATA_DIR}"
    )
    target_include_directories("cpp_${test}" PRIVATE "$<TARGET_PROPERTY:cosim,SOURCE_DIR>")
endforeach()

add_dependencies(
    cpp_fmi_v2_fmu_unittest
    fmi2_derivative_fixture)

set(reference_fmu_test_targets
    cpp_save_state_test
    cpp_slave_simulator_unittest)
if(LIBCOSIM_WITH_PROXYFMU)
    list(APPEND reference_fmu_test_targets cpp_proxyfmu_save_state_test)
endif()

foreach(reference_fmu_test IN LISTS reference_fmu_test_targets)
    add_dependencies("${reference_fmu_test}" reference_fmus_v2_build)
endforeach()
