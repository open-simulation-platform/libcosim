# Feedthrough and StateSpace advertise Intermediate Update support but do not
# mark the Float32 and variable-size array variables used by our callback tests.
# The BouncingBall binary implements output derivatives but omits the matching
# capability. These derived FMUs add only the missing metadata so tests exercise
# the upstream binaries without committing generated archives.
#
# Defines the target reference_fmu_intermediate_update_fixtures and the
# variable INTERMEDIATE_UPDATE_FIXTURES.

set(INTERMEDIATE_UPDATE_FIXTURES)
set(intermediate_update_commands)
foreach(intermediate_update_fixture IN ITEMS
        "Feedthrough|IntermediateFeedthrough|Feedthrough"
        "StateSpace|IntermediateStateSpace|StateSpace"
        "BouncingBall|OutputDerivativesBouncingBall|BouncingBallOutputDerivatives")
    string(REPLACE "|" ";" intermediate_update_fixture
        "${intermediate_update_fixture}")
    list(GET intermediate_update_fixture 0 intermediate_update_source)
    list(GET intermediate_update_fixture 1 intermediate_update_output)
    list(GET intermediate_update_fixture 2 intermediate_update_kind)
    set(intermediate_update_output
        "${REFERENCE_FMUS_BINARY_DIR}/fmus/${intermediate_update_output}.fmu")
    list(APPEND INTERMEDIATE_UPDATE_FIXTURES "${intermediate_update_output}")
    list(APPEND intermediate_update_commands
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_FMU=${REFERENCE_FMUS_BINARY_DIR}/fmus/${intermediate_update_source}.fmu"
            "-DOUTPUT_FMU=${intermediate_update_output}"
            "-DFIXTURE_KIND=${intermediate_update_kind}"
            -P "${TEST_FIXTURE_CMAKE_DIR}/CreateIntermediateUpdateFixture.cmake")
endforeach()
add_custom_command(
    OUTPUT ${INTERMEDIATE_UPDATE_FIXTURES}
    ${intermediate_update_commands}
    DEPENDS
        reference_fmus_build
        "${TEST_FIXTURE_CMAKE_DIR}/CreateIntermediateUpdateFixture.cmake"
    VERBATIM)
add_custom_target(
    reference_fmu_intermediate_update_fixtures
    DEPENDS ${INTERMEDIATE_UPDATE_FIXTURES})
