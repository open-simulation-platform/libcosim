# Build a BouncingBall variant with controlled DoStep outcomes.
set(FMI3_STEP_PROBE_FMU
    "${CMAKE_CURRENT_BINARY_DIR}/fmi3-step-probe/StepProbe.fmu")
libcosim_add_fmi3_fixture_fmu(
    fmi3_step_probe_fixture
    OUTPUT_NAME "BouncingBall"
    OUTPUT_FMU "${FMI3_STEP_PROBE_FMU}"
    MODEL_DESCRIPTION "${REFERENCE_FMUS_SOURCE_DIR}/BouncingBall/FMI3.xml"
    CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/fmi3-step-probe-contents"
    SOURCES
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/BouncingBall/model.c"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_step_probe_fixture/fmi3_step_probe.c"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_step_probe_fixture/fmi3_step_probe_reference.c"
    INCLUDE_DIRECTORIES
        "${REFERENCE_FMUS_SOURCE_DIR}/src"
        "${REFERENCE_FMUS_SOURCE_DIR}/BouncingBall")
