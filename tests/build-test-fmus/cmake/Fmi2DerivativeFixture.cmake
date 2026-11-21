# Builds a dedicated FMI 2 Co-Simulation fixture that validates directional
# derivative reference ordering and statuses.
#
# Defines the target fmi2_derivative_fixture and the variable
# FMI2_DERIVATIVE_FIXTURE.

add_library(
    fmi2_derivative_fixture_library
    SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../fmi2_derivative_fixture/model.c")
libcosim_track_fixture_instances(fmi2_derivative_fixture_library)
target_include_directories(
    fmi2_derivative_fixture_library
    PRIVATE
        "${REFERENCE_FMUS_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi2_derivative_fixture")
libcosim_set_fixture_library_properties(
    fmi2_derivative_fixture_library
    OUTPUT_NAME "Fmi2Derivatives"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fmi2-derivative-binary")

set(FMI2_DERIVATIVE_FIXTURE
    "${REFERENCE_FMUS_V2_BINARY_DIR}/fmus/Fmi2Derivatives.fmu")
libcosim_add_fixture_archive(
    OUTPUT_FMU "${FMI2_DERIVATIVE_FIXTURE}"
    FIXTURE_TARGET fmi2_derivative_fixture_library
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_LIST_DIR}/../fmi2_derivative_fixture/modelDescription.xml"
    FMI_VERSION 2)
add_custom_target(
    fmi2_derivative_fixture
    DEPENDS "${FMI2_DERIVATIVE_FIXTURE}")
