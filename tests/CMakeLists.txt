include(ExternalProject)

set(REFERENCE_FMUS_REPOSITORY
    "https://github.com/open-simulation-platform/Reference-FMUs.git"
    CACHE STRING "Reference-FMUs repository used by FMI 2 derivative tests")
set(REFERENCE_FMUS_VERSION
    "v0.0.42"
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
set(reference_fmus_v2_extra_args)
set(reference_fmus_architecture "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE)
    # Reference-FMUs picks the architecture of the host, so tell it which
    # one we build for, e.g. x86_64 on an Apple silicon host.
    if(CMAKE_OSX_ARCHITECTURES)
        set(reference_fmus_architecture "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(reference_fmus_architecture MATCHES "^(arm64|aarch64)$")
        list(APPEND reference_fmus_v2_extra_args
            "-DFMI_ARCHITECTURE=aarch64"
            "-DCMAKE_OSX_ARCHITECTURES=arm64")
    elseif(reference_fmus_architecture STREQUAL "x86_64")
        list(APPEND reference_fmus_v2_extra_args
            "-DFMI_ARCHITECTURE=x86_64"
            "-DCMAKE_OSX_ARCHITECTURES=x86_64")
    else()
        message(FATAL_ERROR
            "No Reference FMUs for macOS on ${reference_fmus_architecture}")
    endif()
endif()
ExternalProject_Add(
    reference_fmus_v2_build
    SOURCE_DIR "${REFERENCE_FMUS_SOURCE_DIR}"
    BINARY_DIR "${REFERENCE_FMUS_V2_BINARY_DIR}"
    STAMP_DIR "${REFERENCE_FMUS_V2_STAMP_DIR}"
    CMAKE_ARGS
        "-DFMI_VERSION=2"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        ${reference_fmus_v2_extra_args}
    INSTALL_COMMAND ""
    TEST_COMMAND "")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT DEFINED GEN_TEST_DATA_DIR)
    set(GEN_TEST_DATA_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated-test-data")
endif()
include(PrepareDahlquistTestData)
prepare_dahlquist_test_data()

# Package the FMI 2 test FMUs that we build ourselves for the platform that
# FMI Library looks for binaries for. This is the x86 platform name (e.g.
# "linux64") or, on other platforms, the FMI 3 platform tuple (e.g.
# "aarch64-darwin") if FMI Library was built accordingly.
file(STRINGS "${FMILibrary_INCLUDE_DIRS}/fmilib_config.h" fmi2_platform_define
    REGEX "^#define FMI2_PLATFORM ")
string(REGEX REPLACE "^#define FMI2_PLATFORM \"([^\"]*)\".*$" "\\1"
    FMI2_TEST_PLATFORM "${fmi2_platform_define}")
if(NOT FMI2_TEST_PLATFORM)
    message(FATAL_ERROR
        "Could not read FMI2_PLATFORM from ${FMILibrary_INCLUDE_DIRS}/fmilib_config.h")
endif()
message(STATUS "FMI 2 test platform: ${FMI2_TEST_PLATFORM}")

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
    "execution_api_test"
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

# Many FMI 2 FMUs keep the binaries for other architectures in an x86
# platform folder, e.g. a universal library in darwin64, and the importer
# falls back to those. To test this, build the Reference FMUs once more with
# such binaries. (This is keyed on FMI Library's platform, not on the
# processor, as a stock FMI Library build looks in darwin64 on Apple silicon
# too.)
set(reference_fmus_v2_x86_folder_args)
if(FMI2_TEST_PLATFORM STREQUAL "aarch64-darwin")
    set(reference_fmus_v2_x86_folder_args
        "-DFMI_ARCHITECTURE=x86_64"
        "-DCMAKE_OSX_ARCHITECTURES=arm64|x86_64")
elseif(FMI2_TEST_PLATFORM STREQUAL "aarch64-linux")
    # On Linux, the architecture only selects the folder, so this puts
    # 64-bit ARM code in linux64.
    set(reference_fmus_v2_x86_folder_args "-DFMI_ARCHITECTURE=x86_64")
endif()
if(reference_fmus_v2_x86_folder_args)
    set(REFERENCE_FMUS_V2_X86_FOLDER_BINARY_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-x86-folder-build")
    ExternalProject_Add(
        reference_fmus_v2_x86_folder_build
        SOURCE_DIR "${REFERENCE_FMUS_SOURCE_DIR}"
        BINARY_DIR "${REFERENCE_FMUS_V2_X86_FOLDER_BINARY_DIR}"
        STAMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-x86-folder-stamp-${REFERENCE_FMUS_VERSION}"
        LIST_SEPARATOR "|"
        CMAKE_ARGS
            "-DFMI_VERSION=2"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            ${reference_fmus_v2_x86_folder_args}
        INSTALL_COMMAND ""
        TEST_COMMAND "")
    set(dahlquist_x86_folder_fmu
        "${REFERENCE_FMUS_V2_X86_FOLDER_BINARY_DIR}/fmus/Dahlquist.fmu")
endif()

if(FMI2_TEST_PLATFORM STREQUAL "aarch64-darwin")
    # Test the fallback with the universal Dahlquist FMU, and with a copy that
    # only has x86_64 code.
    set(dahlquist_x86_64_fmu "${REFERENCE_FMUS_V2_X86_FOLDER_BINARY_DIR}/fmus/Dahlquist_x86_64.fmu")
    set(dahlquist_x86_64_dir "${CMAKE_CURRENT_BINARY_DIR}/dahlquist-x86_64")
    set(dahlquist_x86_64_library "${dahlquist_x86_64_dir}/binaries/darwin64/Dahlquist.dylib")
    add_custom_command(
        OUTPUT "${dahlquist_x86_64_fmu}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${dahlquist_x86_64_dir}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${dahlquist_x86_64_dir}"
        COMMAND "${CMAKE_COMMAND}" -E chdir "${dahlquist_x86_64_dir}"
            "${CMAKE_COMMAND}" -E tar xf "${dahlquist_x86_folder_fmu}"
        COMMAND lipo "${dahlquist_x86_64_library}" -thin x86_64
            -output "${dahlquist_x86_64_library}.thin"
        COMMAND "${CMAKE_COMMAND}" -E rename
            "${dahlquist_x86_64_library}.thin" "${dahlquist_x86_64_library}"
        COMMAND "${CMAKE_COMMAND}" -E chdir "${dahlquist_x86_64_dir}"
            "${CMAKE_COMMAND}" -E tar cf "${dahlquist_x86_64_fmu}" --format=zip
            modelDescription.xml binaries
        DEPENDS reference_fmus_v2_x86_folder_build "${dahlquist_x86_folder_fmu}"
        VERBATIM)
    add_custom_target(dahlquist_x86_64_fmu DEPENDS "${dahlquist_x86_64_fmu}")
    set(fmi2_fallback_test_fmus
        "${dahlquist_x86_folder_fmu}"
        "${dahlquist_x86_64_fmu}")
    list(APPEND unittests "fmi2_platform_fallback_unittest")
elseif(FMI2_TEST_PLATFORM STREQUAL "aarch64-linux")
    # Test the fallback with the Dahlquist FMU with 64-bit ARM code in
    # linux64, and with vector.fmu, whose linux64 library is x86_64 code.
    set(fmi2_fallback_test_fmus
        "${dahlquist_x86_folder_fmu}"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/fmi2/vector.fmu")
    list(APPEND unittests "fmi2_platform_fallback_unittest")
endif()

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
    cpp_execution_api_test
    cpp_save_state_test
    cpp_slave_simulator_unittest)
if(LIBCOSIM_WITH_PROXYFMU)
    list(APPEND reference_fmu_test_targets cpp_proxyfmu_save_state_test)
endif()

if(TARGET cpp_fmi2_platform_fallback_unittest)
    # The first FMU has native code in the x86 platform folder, the second
    # has not.
    list(GET fmi2_fallback_test_fmus 0 fmu_with_native_code)
    list(GET fmi2_fallback_test_fmus 1 fmu_without_native_code)
    target_compile_definitions(cpp_fmi2_platform_fallback_unittest PRIVATE
        "FMI2_TEST_PLATFORM=\"${FMI2_TEST_PLATFORM}\""
        "FMU_WITH_NATIVE_CODE=\"${fmu_with_native_code}\""
        "FMU_WITHOUT_NATIVE_CODE=\"${fmu_without_native_code}\"")
    add_dependencies(cpp_fmi2_platform_fallback_unittest reference_fmus_v2_x86_folder_build)
    if(TARGET dahlquist_x86_64_fmu)
        add_dependencies(cpp_fmi2_platform_fallback_unittest dahlquist_x86_64_fmu)
    endif()
    list(APPEND reference_fmu_test_targets cpp_fmi2_platform_fallback_unittest)
endif()

foreach(reference_fmu_test IN LISTS reference_fmu_test_targets)
    add_dependencies("${reference_fmu_test}" reference_fmus_v2_build)
endforeach()

# Some tests use FMUs from the test data directory which only contain binaries
# for x86 Linux and Windows. Those cannot run elsewhere, so we disable the tests
# which consist entirely of such cases and skip the individual cases in the
# others.
set(tests_requiring_bundled_fmu_binaries
    cpp_config_end_time_test
    cpp_ecco_algorithm_from_system_structure_test
    cpp_ecco_algorithm_multi_bond_test
    cpp_ecco_algorithm_powerbond_logconfig_test
    cpp_ecco_algorithm_two_bond_test
    cpp_fmi_v1_fmu_unittest
    cpp_osp_config_parser_test
    cpp_state_init_test)
if(LIBCOSIM_WITH_PROXYFMU)
    list(APPEND tests_requiring_bundled_fmu_binaries
        cpp_proxyfmu_integration_unittest
        cpp_proxyfmu_library_unittest
        cpp_proxyfmu_osp_config_parser_test)
endif()
set(cpp_fmi_v2_fmu_unittest_cases_requiring_bundled_fmu_binaries
    v2_fmu)
set(cpp_ssp_loader_unittest_cases_requiring_bundled_fmu_binaries
    basic_test
    no_algorithm_test
    ssp_archive
    ssp_linear_transformation_test
    ssp_multiple_parameter_sets_test)

if(APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set_tests_properties(${tests_requiring_bundled_fmu_binaries}
        PROPERTIES DISABLED TRUE)
    foreach(test IN ITEMS cpp_fmi_v2_fmu_unittest cpp_ssp_loader_unittest)
        list(JOIN ${test}_cases_requiring_bundled_fmu_binaries "," excludedCases)
        set_property(TEST "${test}" APPEND PROPERTY
            ENVIRONMENT "BOOST_TEST_RUN_FILTERS=!${excludedCases}")
    endforeach()
endif()
