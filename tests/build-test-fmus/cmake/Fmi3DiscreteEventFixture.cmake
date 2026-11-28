# Stair advertises early return but returns early at the requested endpoint,
# which FMI 3 prohibits. Disable that capability in a metadata-only derivative
# so its unchanged binary handles time events internally.
#
# Defines the variable FMI3_DISCRETE_EVENT_FIXTURE.

set(FMI3_DISCRETE_EVENT_FIXTURE
    "${REFERENCE_FMUS_BINARY_DIR}/fmus/DiscreteStair.fmu")
add_custom_command(
    OUTPUT "${FMI3_DISCRETE_EVENT_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_FMU=${REFERENCE_FMUS_BINARY_DIR}/fmus/Stair.fmu"
        "-DOUTPUT_FMU=${FMI3_DISCRETE_EVENT_FIXTURE}"
        -P "${TEST_FIXTURE_CMAKE_DIR}/CreateDiscreteEventFixture.cmake"
    DEPENDS
        reference_fmus_build
        "${TEST_FIXTURE_CMAKE_DIR}/CreateDiscreteEventFixture.cmake"
    VERBATIM)
add_custom_target(reference_fmu_discrete_event_fixture
    DEPENDS "${FMI3_DISCRETE_EVENT_FIXTURE}")

set(FMI3_NONCONVERGING_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/fmi3-nonconverging/Stair.fmu")
libcosim_add_fmi3_fixture_fmu(
    fmi3_nonconverging_fixture
    OUTPUT_NAME "Stair"
    OUTPUT_FMU "${FMI3_NONCONVERGING_FIXTURE}"
    MODEL_DESCRIPTION "${REFERENCE_FMUS_SOURCE_DIR}/Stair/FMI3.xml"
    CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/fmi3-nonconverging-contents"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_nonconverging_fixture/model.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
    INCLUDE_DIRECTORIES
        "${REFERENCE_FMUS_SOURCE_DIR}"
        "${REFERENCE_FMUS_SOURCE_DIR}/Stair")

set(FMI3_UNSCHEDULED_STAIR_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/fmi3-unscheduled/Stair.fmu")
libcosim_add_fmi3_fixture_fmu(
    fmi3_unscheduled_stair_fixture
    OUTPUT_NAME "Stair"
    OUTPUT_FMU "${FMI3_UNSCHEDULED_STAIR_FIXTURE}"
    MODEL_DESCRIPTION "${REFERENCE_FMUS_SOURCE_DIR}/Stair/FMI3.xml"
    CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/fmi3-unscheduled-contents"
    SOURCES
        "${REFERENCE_FMUS_SOURCE_DIR}/Stair/model.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_unscheduled_stair_fixture/fmi3_functions.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
    INCLUDE_DIRECTORIES
        "${REFERENCE_FMUS_SOURCE_DIR}"
        "${REFERENCE_FMUS_SOURCE_DIR}/Stair")
