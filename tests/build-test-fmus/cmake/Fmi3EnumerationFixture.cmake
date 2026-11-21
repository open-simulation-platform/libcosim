# This fixture exercises FMI 3 Enumeration scalar, array, and zero-sized-array
# transport through Co-Simulation.
#
# Defines the target fmi3_enumeration_fixture, which writes Enumeration.fmu to
# the Reference-FMU archive directory when its inputs change.

libcosim_add_fmi3_fixture_fmu(
    fmi3_enumeration_fixture
    TRACK_INSTANCES
    OUTPUT_NAME "Enumeration"
    OUTPUT_FMU "${REFERENCE_FMUS_BINARY_DIR}/fmus/Enumeration.fmu"
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_enumeration_fixture/modelDescription.xml"
    CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/enumeration-contents"
    SOURCES
        "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_enumeration_fixture/model.c"
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_enumeration_fixture")
