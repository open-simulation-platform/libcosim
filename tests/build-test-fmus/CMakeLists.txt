cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(libcosim_fixture_fmus LANGUAGES C CXX)
    set(fixture_standalone TRUE)
endif()

# This directory is also a standalone project so a source-bearing FMU can be
# rebuilt without configuring libcosim or its test executables.
set(TEST_FIXTURE_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${TEST_FIXTURE_CMAKE_DIR}")

option(LIBCOSIM_FIXTURE_FETCH_REFERENCE_FMUS
    "Fetch the pinned Reference-FMUs release for fixture generation" OFF)
if(fixture_standalone AND NOT LIBCOSIM_FIXTURE_FETCH_REFERENCE_FMUS)
    # The standalone build accepts the already verified checkout or embedded
    # source tree; no .git directory is required in a source-bearing FMU.
    file(TO_CMAKE_PATH "${REFERENCE_FMUS_SOURCE_DIR}" REFERENCE_FMUS_SOURCE_DIR)
    file(TO_CMAKE_PATH "${FMILibrary_INCLUDE_DIRS}" FMILibrary_INCLUDE_DIRS)
    if(NOT EXISTS "${REFERENCE_FMUS_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Set REFERENCE_FMUS_SOURCE_DIR to the Reference-FMUs source tree")
    endif()
    if(NOT EXISTS "${FMILibrary_INCLUDE_DIRS}/FMI1/fmiFunctions.h" OR
       NOT EXISTS "${FMILibrary_INCLUDE_DIRS}/FMI2/fmi2Functions.h")
        message(FATAL_ERROR "Set FMILibrary_INCLUDE_DIRS to the FMI Library include directory")
    endif()
    find_package(Patch REQUIRED)
    include(ExternalProject)
    function(add_reference_fmus target version binary_dir)
        set(args "-DFMI_VERSION=${version}" "-DCMAKE_BUILD_TYPE=Release")
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            list(APPEND args "-DCMAKE_C_FLAGS=-Wno-error=stringop-truncation")
        endif()
        ExternalProject_Add(${target}
            SOURCE_DIR "${REFERENCE_FMUS_SOURCE_DIR}"
            BINARY_DIR "${binary_dir}"
            CMAKE_ARGS ${args}
            INSTALL_COMMAND ""
            TEST_COMMAND "")
    endfunction()
    set(REFERENCE_FMUS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-build")
    set(REFERENCE_FMUS_V2_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-build")
    add_reference_fmus(reference_fmus_build 3 "${REFERENCE_FMUS_BINARY_DIR}")
    add_reference_fmus(reference_fmus_v2_build 2 "${REFERENCE_FMUS_V2_BINARY_DIR}")
else()
    include(ReferenceFmus)
endif()

# These modules define the custom fixture libraries and archives; the aggregate
# target below builds them alongside both upstream sets.
include(TestPlatforms)
include(TestFixtureHelpers)
include(IntermediateUpdateFixtures)
include(Fmi1EnumerationFixture)
include(Fmi2MixedTypesFixture)
include(Fmi2DerivativeFixture)
include(Fmi3ArrayFixtures)
include(Fmi3DerivativeFixture)
include(Fmi3ClockedCoSimulationFixture)
include(Fmi3StrictClockFixture)
include(Fmi3EccoPowerBondFixture)
include(Fmi3DiscreteEventFixture)
include(Fmi3StatusFaultFixture)
include(Fmi3EnumerationFixture)
include(Fmi3StepProbeFixture)

add_custom_target(power_bond_archive DEPENDS "${FMI3_ECCO_POWER_BOND_FIXTURE}")
set(fixture_targets
    reference_fmus_build reference_fmus_v2_build
    reference_fmu_intermediate_update_fixtures
    reference_fmu_clocked_co_simulation_fixture
    reference_fmu_discrete_event_fixture
    fmi1_enumeration_fixture fmi2_mixed_types_fixture fmi2_derivative_fixture
    fmi3_array_feedthrough_fixture fmi3_fixed_array_state_space_fixture
    fmi3_derivative_fixture fmi3_strict_clock_fixture fmi3_status_fault_fixture
    fmi3_enumeration_fixture fmi3_nonconverging_fixture
    fmi3_unscheduled_stair_fixture fmi3_step_probe_fixture power_bond_archive)

# Stage the native-platform FMUs under versioned directories and create one
# archive. GitHub Actions merges the platform builds into a multi-platform ZIP.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(fixture_staging_dir "${CMAKE_CURRENT_BINARY_DIR}/staged-test-fmus")
set(fixture_bundle "${CMAKE_CURRENT_BINARY_DIR}/generated-test-fmus.zip")
add_custom_target(build-test-fmus
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/package.py"
        stage "${CMAKE_CURRENT_BINARY_DIR}" "${fixture_staging_dir}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/package.py"
        bundle "${fixture_staging_dir}" "${fixture_bundle}"
    DEPENDS ${fixture_targets}
    BYPRODUCTS "${fixture_bundle}"
    VERBATIM)
