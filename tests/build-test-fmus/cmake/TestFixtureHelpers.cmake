# Shared helpers for building and packaging fixture FMUs.

function(libcosim_track_fixture_instances target)
    set(support "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../support")
    target_sources(${target} PRIVATE "${support}/fixture_instances.cpp")
    target_include_directories(${target} PRIVATE "${support}")
    target_compile_features(${target} PRIVATE cxx_std_11)
    if(LIBCOSIM_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # FMI Library's callbacks use distinct enum types with the same ABI.
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-fno-sanitize=function>)
    endif()
endfunction()

# Names the shared library built by <target> OUTPUT_NAME without a platform
# prefix and places its runtime and library outputs in OUTPUT_DIRECTORY for
# single- and multi-configuration generators. Import libraries are written to
# ARCHIVE_DIRECTORY, or to OUTPUT_DIRECTORY when ARCHIVE_DIRECTORY is omitted.
# Linker PDBs use the target name so fixtures with identical binary names do
# not collide in a shared PDB output directory.
#
#   libcosim_set_fixture_library_properties(
#       <target>
#       OUTPUT_NAME <binary name>
#       OUTPUT_DIRECTORY <directory>
#       [ARCHIVE_DIRECTORY <directory>])
function(libcosim_set_fixture_library_properties target)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "OUTPUT_NAME;OUTPUT_DIRECTORY;ARCHIVE_DIRECTORY" "")
    if(NOT arg_ARCHIVE_DIRECTORY)
        set(arg_ARCHIVE_DIRECTORY "${arg_OUTPUT_DIRECTORY}")
    endif()
    set(properties)
    foreach(config IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        list(APPEND properties
            "RUNTIME_OUTPUT_DIRECTORY${config}" "${arg_OUTPUT_DIRECTORY}"
            "LIBRARY_OUTPUT_DIRECTORY${config}" "${arg_OUTPUT_DIRECTORY}"
            "ARCHIVE_OUTPUT_DIRECTORY${config}" "${arg_ARCHIVE_DIRECTORY}")
    endforeach()
    set_target_properties(${target}
        PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${arg_OUTPUT_NAME}"
            PDB_NAME "${target}"
            ${properties})
endfunction()

# Adds a custom command that packages the shared library built by
# FIXTURE_TARGET together with MODEL_DESCRIPTION as OUTPUT_FMU. FMI_VERSION 2
# uses the FMI 1/2 platform directory layout and FMI_VERSION 3 uses the FMI 3
# layout. DEPENDS lists additional targets or files that must be up to date.
#
#   libcosim_add_fixture_archive(
#       OUTPUT_FMU <archive path>
#       FIXTURE_TARGET <shared library target>
#       MODEL_DESCRIPTION <modelDescription.xml>
#       FMI_VERSION <2|3>
#       [DEPENDS <targets or files...>])
function(libcosim_add_fixture_archive)
    cmake_parse_arguments(PARSE_ARGV 0 arg ""
        "OUTPUT_FMU;FIXTURE_TARGET;MODEL_DESCRIPTION;FMI_VERSION" "DEPENDS")
    if(arg_FMI_VERSION STREQUAL "3")
        set(platform "${FMI3_TEST_PLATFORM}")
    elseif(arg_FMI_VERSION STREQUAL "2")
        set(platform "${FMI2_TEST_PLATFORM}")
    else()
        message(FATAL_ERROR
            "Unsupported fixture FMI_VERSION '${arg_FMI_VERSION}'")
    endif()
    set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CreateFixtureArchive.cmake")
    add_custom_command(
        OUTPUT "${arg_OUTPUT_FMU}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT_FMU=${arg_OUTPUT_FMU}"
            "-DFIXTURE_BINARY=$<TARGET_FILE:${arg_FIXTURE_TARGET}>"
            "-DMODEL_DESCRIPTION=${arg_MODEL_DESCRIPTION}"
            "-DFMI_TEST_PLATFORM=${platform}"
            -P "${script}"
        DEPENDS
            ${arg_DEPENDS}
            ${arg_FIXTURE_TARGET}
            "${script}"
            "${arg_MODEL_DESCRIPTION}"
        VERBATIM)
endfunction()

# Builds <target> as an FMI 3 Co-Simulation shared library on top of the
# Reference-FMU framework headers, stages it in CONTENTS_DIR, and packages it
# with MODEL_DESCRIPTION as OUTPUT_FMU when either input changes. Import
# libraries are written to ARCHIVE_DIR, or next to the staged binary when
# it is omitted.
#
#   libcosim_add_fmi3_fixture_fmu(
#       <target>
#       OUTPUT_NAME <binary name>
#       OUTPUT_FMU <archive path>
#       MODEL_DESCRIPTION <modelDescription.xml>
#       CONTENTS_DIR <staging directory>
#       [ARCHIVE_DIR <directory>] [TRACK_INSTANCES]
#       SOURCES <sources...>
#       [INCLUDE_DIRECTORIES <directories...>]
#       [DEPENDS <targets...>])
function(libcosim_add_fmi3_fixture_fmu target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "TRACK_INSTANCES"
        "OUTPUT_NAME;OUTPUT_FMU;MODEL_DESCRIPTION;CONTENTS_DIR;ARCHIVE_DIR"
        "SOURCES;INCLUDE_DIRECTORIES;DEPENDS")
    set(binaries_dir "${arg_CONTENTS_DIR}/binaries/${FMI3_TEST_PLATFORM}")
    if(NOT arg_ARCHIVE_DIR)
        set(arg_ARCHIVE_DIR "${binaries_dir}")
    endif()
    file(MAKE_DIRECTORY "${binaries_dir}" "${arg_ARCHIVE_DIR}")

    set(library_target "${target}_library")
    set(common_source "${REFERENCE_FMUS_SOURCE_DIR}/src/common.c")
    if(arg_TRACK_INSTANCES)
        set(common_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../support/reference_instances.c")
    endif()
    add_library(${library_target} SHARED "${common_source}" ${arg_SOURCES})
    if(arg_TRACK_INSTANCES)
        libcosim_track_fixture_instances(${library_target})
        target_include_directories(${library_target} PRIVATE "${REFERENCE_FMUS_SOURCE_DIR}/src")
    endif()
    if(arg_DEPENDS)
        add_dependencies(${library_target} ${arg_DEPENDS})
    endif()
    target_compile_definitions(${library_target} PRIVATE FMI_VERSION=3 DISABLE_PREFIX)
    target_include_directories(${library_target}
        PRIVATE
            "${REFERENCE_FMUS_SOURCE_DIR}/include"
            ${arg_INCLUDE_DIRECTORIES})
    libcosim_set_fixture_library_properties(${library_target}
        OUTPUT_NAME "${arg_OUTPUT_NAME}"
        OUTPUT_DIRECTORY "${binaries_dir}"
        ARCHIVE_DIRECTORY "${arg_ARCHIVE_DIR}")
    libcosim_add_fixture_archive(
        OUTPUT_FMU "${arg_OUTPUT_FMU}"
        FIXTURE_TARGET ${library_target}
        MODEL_DESCRIPTION "${arg_MODEL_DESCRIPTION}"
        FMI_VERSION 3)
    add_custom_target(${target} DEPENDS "${arg_OUTPUT_FMU}")
endfunction()
