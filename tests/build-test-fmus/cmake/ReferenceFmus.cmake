# Clones the pinned Reference-FMUs release during configure and builds its
# FMI 3 and FMI 2 FMU sets as external projects.
#
# Defines the targets reference_fmus_build and reference_fmus_v2_build and the
# variables REFERENCE_FMUS_SOURCE_DIR, REFERENCE_FMUS_BINARY_DIR, and
# REFERENCE_FMUS_V2_BINARY_DIR. The generated archives are written to the
# fmus subdirectory of each binary directory.

include(ExternalProject)

set(REFERENCE_FMUS_REPOSITORY
    "https://github.com/open-simulation-platform/Reference-FMUs.git"
    CACHE STRING "Reference-FMUs repository used by FMI 3 tests")
set(REFERENCE_FMUS_VERSION
    "v0.0.42"
    CACHE STRING "Reference-FMUs release tag used by FMI 3 tests")

find_package(Git REQUIRED)
find_package(Patch REQUIRED)
set(REFERENCE_FMUS_SOURCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-source"
    CACHE PATH "Reference-FMUs source directory used by FMI 3 tests")

if(NOT EXISTS "${REFERENCE_FMUS_SOURCE_DIR}/CMakeLists.txt")
    if(EXISTS "${REFERENCE_FMUS_SOURCE_DIR}")
        file(GLOB reference_fmus_existing_entries
            "${REFERENCE_FMUS_SOURCE_DIR}/*")
        if(reference_fmus_existing_entries)
            message(FATAL_ERROR
                "Reference-FMUs source directory is not an empty checkout: "
                "${REFERENCE_FMUS_SOURCE_DIR}")
        endif()
    endif()

    message(STATUS
        "Cloning Reference-FMUs ${REFERENCE_FMUS_VERSION}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone
            --branch "${REFERENCE_FMUS_VERSION}"
            "${REFERENCE_FMUS_REPOSITORY}"
            "${REFERENCE_FMUS_SOURCE_DIR}"
        RESULT_VARIABLE reference_fmus_clone_result
        ERROR_VARIABLE reference_fmus_clone_error)
    if(NOT reference_fmus_clone_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to clone Reference-FMUs: ${reference_fmus_clone_error}")
    endif()
endif()

if(NOT EXISTS "${REFERENCE_FMUS_SOURCE_DIR}/.git")
    message(FATAL_ERROR
        "Reference-FMUs source directory is not a Git checkout: "
        "${REFERENCE_FMUS_SOURCE_DIR}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REFERENCE_FMUS_SOURCE_DIR}"
        rev-parse --verify "HEAD^{commit}"
    RESULT_VARIABLE reference_fmus_head_result
    OUTPUT_VARIABLE reference_fmus_head
    ERROR_VARIABLE reference_fmus_head_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REFERENCE_FMUS_SOURCE_DIR}"
        rev-parse --verify "${REFERENCE_FMUS_VERSION}^{commit}"
    RESULT_VARIABLE reference_fmus_version_result
    OUTPUT_VARIABLE reference_fmus_version_commit
    ERROR_VARIABLE reference_fmus_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT reference_fmus_head_result EQUAL 0 OR
   NOT reference_fmus_version_result EQUAL 0 OR
   NOT reference_fmus_head STREQUAL reference_fmus_version_commit)
    message(FATAL_ERROR
        "Reference-FMUs checkout ${REFERENCE_FMUS_SOURCE_DIR} does not match "
        "${REFERENCE_FMUS_VERSION}. Use a fresh build directory or a matching "
        "REFERENCE_FMUS_SOURCE_DIR; existing checkouts are not modified.\n"
        "${reference_fmus_head_error}${reference_fmus_version_error}")
endif()

message(STATUS
    "Using Reference-FMUs ${REFERENCE_FMUS_VERSION}")

# Adds an external project that builds the Reference-FMUs for one FMI version.
function(libcosim_add_reference_fmus target fmi_version binary_dir stamp_dir)
    set(cmake_args
        "-DFMI_VERSION=${fmi_version}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        list(APPEND cmake_args
            "-DCMAKE_C_FLAGS=-Wno-error=stringop-truncation")
    endif()
    ExternalProject_Add(
        ${target}
        SOURCE_DIR "${REFERENCE_FMUS_SOURCE_DIR}"
        BINARY_DIR "${binary_dir}"
        STAMP_DIR "${stamp_dir}"
        CMAKE_ARGS ${cmake_args}
        INSTALL_COMMAND ""
        TEST_COMMAND "")
endfunction()

set(REFERENCE_FMUS_BINARY_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-build")
libcosim_add_reference_fmus(
    reference_fmus_build
    3
    "${REFERENCE_FMUS_BINARY_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-stamp-${REFERENCE_FMUS_VERSION}")

set(REFERENCE_FMUS_V2_BINARY_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-build")
libcosim_add_reference_fmus(
    reference_fmus_v2_build
    2
    "${REFERENCE_FMUS_V2_BINARY_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/reference-fmus-v2-stamp-${REFERENCE_FMUS_VERSION}")
