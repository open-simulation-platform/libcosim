libcosim_add_fmi3_fixture_fmu(
    fmi3_strict_clock_fixture
    OUTPUT_NAME "StrictClock"
    OUTPUT_FMU "${REFERENCE_FMUS_BINARY_DIR}/fmus/StrictClock.fmu"
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_strict_clock_fixture/modelDescription.xml"
    CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/strict-clock-contents"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_strict_clock_fixture/fmi3_functions.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_strict_clock_fixture/model.c"
    INCLUDE_DIRECTORIES
        "${REFERENCE_FMUS_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_strict_clock_fixture")
