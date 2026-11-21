# The ECCO fixture accepts arbitrary communication-step sizes and produces a
# deterministic non-zero power residual from two cross-connected instances.
# The tracked patch is applied to the pinned Reference-FMU framework sources in
# a clean build-directory staging area.
#
# Defines the variable FMI3_ECCO_POWER_BOND_FIXTURE.

set(ecco_power_bond_patch_file
    "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_ecco_power_bond_fixture/ecco_power_bond.patch")
set(ecco_power_bond_patched_source_directory
    "${CMAKE_CURRENT_BINARY_DIR}/ecco-power-bond-patched-source")
set(ecco_power_bond_cosimulation_source
    "${ecco_power_bond_patched_source_directory}/src/cosimulation.c")
set(ecco_power_bond_fmi3_functions_source
    "${ecco_power_bond_patched_source_directory}/src/fmi3Functions.c")

add_custom_command(
    OUTPUT
        "${ecco_power_bond_cosimulation_source}"
        "${ecco_power_bond_fmi3_functions_source}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
        "${ecco_power_bond_patched_source_directory}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${ecco_power_bond_patched_source_directory}/src"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
        "${ecco_power_bond_cosimulation_source}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c"
        "${ecco_power_bond_fmi3_functions_source}"
    COMMAND Patch::patch
        -d "${ecco_power_bond_patched_source_directory}"
        --batch
        -p1
        -i "${ecco_power_bond_patch_file}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS
        reference_fmus_build
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c"
        "${ecco_power_bond_patch_file}"
    VERBATIM)
add_custom_target(
    ecco_power_bond_patched_sources
    DEPENDS
        "${ecco_power_bond_cosimulation_source}"
        "${ecco_power_bond_fmi3_functions_source}")

add_library(
    fmi3_ecco_power_bond_fixture
    SHARED
    "${ecco_power_bond_fmi3_functions_source}"
    "${REFERENCE_FMUS_SOURCE_DIR}/src/common.c"
    "${ecco_power_bond_cosimulation_source}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_ecco_power_bond_fixture/model.c")
add_dependencies(
    fmi3_ecco_power_bond_fixture
    ecco_power_bond_patched_sources)
target_compile_definitions(
    fmi3_ecco_power_bond_fixture
    PRIVATE FMI_VERSION=3 DISABLE_PREFIX)
target_include_directories(
    fmi3_ecco_power_bond_fixture
    PRIVATE
        "${REFERENCE_FMUS_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_ecco_power_bond_fixture")
libcosim_set_fixture_library_properties(
    fmi3_ecco_power_bond_fixture
    OUTPUT_NAME "PowerBond"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ecco-power-bond-binary")

set(FMI3_ECCO_POWER_BOND_FIXTURE
    "${REFERENCE_FMUS_BINARY_DIR}/fmus/PowerBond.fmu")
libcosim_add_fixture_archive(
    OUTPUT_FMU "${FMI3_ECCO_POWER_BOND_FIXTURE}"
    FIXTURE_TARGET fmi3_ecco_power_bond_fixture
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_ecco_power_bond_fixture/modelDescription.xml"
    FMI_VERSION 3
    DEPENDS
        "${ecco_power_bond_cosimulation_source}"
        "${ecco_power_bond_fmi3_functions_source}"
        "${ecco_power_bond_patch_file}")
