# Builds an FMI 1 Co-Simulation fixture with Integer-backed Enumeration values.
#
# Defines the target fmi1_enumeration_fixture and the variable
# FMI1_ENUMERATION_FIXTURE.

add_library(
    fmi1_enumeration_fixture_library
    SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/fmi1_enumeration_fixture/model.c")
target_include_directories(
    fmi1_enumeration_fixture_library
    PRIVATE
        "${FMILibrary_INCLUDE_DIRS}")
target_compile_definitions(
    fmi1_enumeration_fixture_library
    PRIVATE MODEL_IDENTIFIER=EnumerationFixture)
libcosim_set_fixture_library_properties(
    fmi1_enumeration_fixture_library
    OUTPUT_NAME "EnumerationFixture"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fmi1-enumeration-binary")

set(FMI1_ENUMERATION_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/fmi1-enumeration/EnumerationFixture.fmu")
libcosim_add_fixture_archive(
    OUTPUT_FMU "${FMI1_ENUMERATION_FIXTURE}"
    FIXTURE_TARGET fmi1_enumeration_fixture_library
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi1_enumeration_fixture/modelDescription.xml"
    FMI_VERSION 2)
add_custom_target(
    fmi1_enumeration_fixture
    DEPENDS "${FMI1_ENUMERATION_FIXTURE}")
