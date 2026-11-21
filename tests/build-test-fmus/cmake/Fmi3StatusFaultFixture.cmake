# StatusFault wraps supported Co-Simulation lifecycle, value, state, event, and
# DoStep operations with deterministic status injection. It also advertises
# FMU state support for the save/restore and serialization tests.
#
# Defines the target fmi3_status_fault_fixture, which writes StatusFault.fmu to
# the Reference-FMU archive directory when its inputs change.

set(status_fault_upstream_source
    "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c")
file(READ "${status_fault_upstream_source}" status_fault_functions)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${status_fault_upstream_source}")
foreach(status_fault_function IN ITEMS
        EnterInitializationMode
        ExitInitializationMode
        EnterConfigurationMode
        ExitConfigurationMode
        Terminate
        Reset
        GetInt32
        SetInt32
        GetFMUState
        SetFMUState
        FreeFMUState
        SerializedFMUStateSize
        SerializeFMUState
        DeserializeFMUState
        UpdateDiscreteStates
        DoStep)
    set(status_fault_original_functions "${status_fault_functions}")
    string(REPLACE
        "fmi3Status fmi3${status_fault_function}("
        "fmi3Status reference_fmi3${status_fault_function}("
        status_fault_functions
        "${status_fault_functions}")
    if(status_fault_functions STREQUAL status_fault_original_functions)
        message(FATAL_ERROR
            "Could not rename fmi3${status_fault_function} in "
            "${status_fault_upstream_source}")
    endif()
endforeach()
set(status_fault_source
    "${CMAKE_CURRENT_BINARY_DIR}/status_fault_fmi3Functions.c")
file(CONFIGURE
    OUTPUT "${status_fault_source}"
    CONTENT "${status_fault_functions}"
    @ONLY)

libcosim_add_fmi3_fixture_fmu(
    fmi3_status_fault_fixture
    TRACK_INSTANCES
    OUTPUT_NAME "StatusFault"
    OUTPUT_FMU "${REFERENCE_FMUS_BINARY_DIR}/fmus/StatusFault.fmu"
    MODEL_DESCRIPTION
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_status_fault_fixture/modelDescription.xml"
    CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/status-fault-contents"
    SOURCES
        "${status_fault_source}"
        "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_status_fault_fixture/model.c"
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_status_fault_fixture/status_wrappers.c"
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}/../fmi3_status_fault_fixture")
