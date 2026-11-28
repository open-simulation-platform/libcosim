# The pinned Clocks FMU is Scheduled Execution-only. Build a Co-Simulation
# variant whose input Clock activations execute the original model partitions,
# then derive metadata for Event Mode, triggered/periodic Clock validation,
# clocked value access, discrete-state convergence, and Step Mode rejection.
#
# Defines the target reference_fmu_clocked_co_simulation_fixture and the
# variable CLOCKED_CO_SIMULATION_FIXTURE.

set(clock_fixture_model_source
    "${CMAKE_CURRENT_BINARY_DIR}/clocked_cosimulation_model.c")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_clocked_cosimulation_fixture/model.c.in"
    "${clock_fixture_model_source}"
    @ONLY)
add_library(
    fmi3_clocked_cosimulation_fixture
    SHARED
    "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c"
    "${REFERENCE_FMUS_SOURCE_DIR}/src/common.c"
    "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
    "${clock_fixture_model_source}")
target_compile_definitions(
    fmi3_clocked_cosimulation_fixture
    PRIVATE FMI_VERSION=3 DISABLE_PREFIX)
target_include_directories(
    fmi3_clocked_cosimulation_fixture
    PRIVATE
        "${REFERENCE_FMUS_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_clocked_cosimulation_fixture"
        "${REFERENCE_FMUS_SOURCE_DIR}/Clocks")
libcosim_set_fixture_library_properties(
    fmi3_clocked_cosimulation_fixture
    OUTPUT_NAME "Clocks"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/clocked-cosimulation-binary")

set(CLOCKED_CO_SIMULATION_FIXTURE
    "${REFERENCE_FMUS_BINARY_DIR}/fmus/ClockedCoSimulation.fmu")
add_custom_command(
    OUTPUT "${CLOCKED_CO_SIMULATION_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_FMU=${REFERENCE_FMUS_BINARY_DIR}/fmus/Clocks.fmu"
        "-DOUTPUT_FMU=${CLOCKED_CO_SIMULATION_FIXTURE}"
        "-DFIXTURE_BINARY=$<TARGET_FILE:fmi3_clocked_cosimulation_fixture>"
        "-DFMI3_TEST_PLATFORM=${FMI3_TEST_PLATFORM}"
        -P "${TEST_FIXTURE_CMAKE_DIR}/CreateClockedCoSimulationFixture.cmake"
    DEPENDS
        reference_fmus_build
        fmi3_clocked_cosimulation_fixture
        "${TEST_FIXTURE_CMAKE_DIR}/CreateClockedCoSimulationFixture.cmake"
    VERBATIM)
add_custom_target(
    reference_fmu_clocked_co_simulation_fixture
    DEPENDS "${CLOCKED_CO_SIMULATION_FIXTURE}")
