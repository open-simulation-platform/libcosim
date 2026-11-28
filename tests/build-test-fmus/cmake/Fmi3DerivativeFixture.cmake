# Builds a dedicated FMI 3 Co-Simulation fixture that validates directional and
# adjoint derivative reference ordering, flattened array counts, and statuses.
#
# Defines the target fmi3_derivative_fixture and the variable
# FMI3_DERIVATIVE_FIXTURE.

add_library(
    fmi3_derivative_fixture_library
    SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../fmi3_derivative_fixture/model.c")
libcosim_track_fixture_instances(fmi3_derivative_fixture_library)
target_include_directories(
    fmi3_derivative_fixture_library
    PRIVATE
        "${REFERENCE_FMUS_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_derivative_fixture")
libcosim_set_fixture_library_properties(
    fmi3_derivative_fixture_library
    OUTPUT_NAME "Fmi3Derivatives"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fmi3-derivative-binary")

set(FMI3_DERIVATIVE_FIXTURE
    "${REFERENCE_FMUS_BINARY_DIR}/fmus/Fmi3Derivatives.fmu")
libcosim_add_fixture_archive(
    OUTPUT_FMU "${FMI3_DERIVATIVE_FIXTURE}"
    FIXTURE_TARGET fmi3_derivative_fixture_library
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_derivative_fixture/modelDescription.xml"
    FMI_VERSION 3)
add_custom_target(
    fmi3_derivative_fixture
    DEPENDS "${FMI3_DERIVATIVE_FIXTURE}")
