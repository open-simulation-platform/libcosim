# Builds a small FMI 2 Co-Simulation FMU with connectable Real, Integer,
# Boolean, and Enumeration endpoints for the mixed-version interoperability
# tests.
#
# Defines the target fmi2_mixed_types_fixture and the variable
# FMI2_MIXED_TYPES_FIXTURE.

add_library(
    fmi2_mixed_types_fixture_library
    SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../fmi2_mixed_types_fixture/model.c")
libcosim_track_fixture_instances(fmi2_mixed_types_fixture_library)
target_include_directories(
    fmi2_mixed_types_fixture_library
    PRIVATE
        "${FMILibrary_INCLUDE_DIRS}")
libcosim_set_fixture_library_properties(
    fmi2_mixed_types_fixture_library
    OUTPUT_NAME "MixedTypes"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fmi2-mixed-types-binary")

set(FMI2_MIXED_TYPES_FIXTURE
    "${REFERENCE_FMUS_BINARY_DIR}/fmus/MixedTypes.fmu")
libcosim_add_fixture_archive(
    OUTPUT_FMU "${FMI2_MIXED_TYPES_FIXTURE}"
    FIXTURE_TARGET fmi2_mixed_types_fixture_library
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_LIST_DIR}/../fmi2_mixed_types_fixture/modelDescription.xml"
    FMI_VERSION 2)
add_custom_target(
    fmi2_mixed_types_fixture
    DEPENDS "${FMI2_MIXED_TYPES_FIXTURE}")
