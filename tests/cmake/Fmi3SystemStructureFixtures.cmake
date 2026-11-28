# Stage the committed FMI 3 OSP configurations and prebuilt FMUs they reference.
# The Scheduled Execution-only Clocks FMU is staged for rejection tests. Defines the target
# fmi3_osp_system_structure_fixtures and the variable
# FMI3_SYSTEM_STRUCTURE_FIXTURE_DIR.

set(FMI3_SYSTEM_STRUCTURE_FIXTURE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/fmi3-system-structure")
set(FMI3_SYSTEM_STRUCTURE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/data/fmi3/system_structure")
add_custom_target(fmi3_osp_system_structure_fixtures
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${FMI3_SYSTEM_STRUCTURE_SOURCE_DIR}"
        "-DOUTPUT_DIR=${FMI3_SYSTEM_STRUCTURE_FIXTURE_DIR}"
        "-DFEEDTHROUGH_FMU=${TEST_FMI3_DIR}/Feedthrough.fmu"
        "-DSTATE_SPACE_FMU=${TEST_FMI3_DIR}/StateSpace.fmu"
        "-DSTAIR_FMU=${TEST_FMI3_DIR}/DiscreteStair.fmu"
        "-DCLOCKED_FMU=${TEST_FMI3_DIR}/ClockedCoSimulation.fmu"
        "-DSCHEDULED_FMU=${TEST_FMI3_DIR}/Clocks.fmu"
        "-DPOWER_BOND_FMU=${TEST_FMI3_DIR}/PowerBond.fmu"
        "-DFMI2_VECTOR_FMU=${TEST_FMI2_DIR}/vector.fmu"
        "-DFMI2_MIXED_TYPES_FMU=${TEST_FMI2_DIR}/MixedTypes.fmu"
        -P "${TEST_FIXTURE_CMAKE_DIR}/StageFmi3SystemStructureFixtures.cmake"
    DEPENDS
        "${TEST_FIXTURE_CMAKE_DIR}/StageFmi3SystemStructureFixtures.cmake"
    VERBATIM)
