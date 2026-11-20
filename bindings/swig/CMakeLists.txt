# SWIG bindings for libcosim.
#
# The language-independent interface lives in cosim.i. Each target language
# has its own subdirectory with a thin language-specific .i file.
#
# This directory builds in two ways:
#  - inside libcosim's build (LIBCOSIM_BUILD_SWIG_BINDINGS=ON), against the
#    in-tree `cosim` target;
#  - standalone, against an installed libcosim (find_package(libcosim), e.g.
#    from its Conan package). This is what the packaging uses, and it is what
#    would let the bindings live in a repository of their own.

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(libcosim_bindings LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(libcosim REQUIRED)
    set(LIBCOSIM_TARGET libcosim::cosim)
    get_target_property(LIBCOSIM_INCLUDE_DIR libcosim::cosim INTERFACE_INCLUDE_DIRECTORIES)
    set(LIBCOSIM_SWIG_VERSION "${libcosim_VERSION}")
    set(LIBCOSIM_SWIG_STANDALONE ON)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output/bin")
    set(LIBCOSIM_SWIG_TEST_DATA_DIR "" CACHE PATH
        "libcosim's tests/data directory; binding tests are added only if set")
    option(LIBCOSIM_SWIG_INSTALL "Add install rules (packaging)" ON)
else()
    set(LIBCOSIM_TARGET cosim)
    set(LIBCOSIM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
    set(LIBCOSIM_SWIG_VERSION "${PROJECT_VERSION}")
    set(LIBCOSIM_SWIG_STANDALONE OFF)
    set(LIBCOSIM_SWIG_TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/data")
    # Off by default, so libcosim's own install does not include the bindings.
    option(LIBCOSIM_SWIG_INSTALL "Add install rules (packaging)" OFF)
endif()
message(STATUS "libcosim bindings ${LIBCOSIM_SWIG_VERSION} (standalone: ${LIBCOSIM_SWIG_STANDALONE})")

# Pinned exactly: SWIG's C backend is experimental and its generated API may
# change between versions (see the swig_c_api_reference test).
# MODULE: Conan's toolchain prefers config packages, and a failed config
# search would reset SWIG_DIR (set by the bindings' conanfile.py).
find_package(SWIG 4.4.1 EXACT REQUIRED MODULE)
include(UseSWIG)
message(STATUS "SWIG ${SWIG_VERSION}: ${SWIG_EXECUTABLE}")

# SWIG's stdint.i must be told whether int64_t is 'long' (LP64: Linux) or
# 'long long' (Windows, and macOS where int64_t is also 'long long').
set(LIBCOSIM_SWIG_FLAGS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND LIBCOSIM_SWIG_FLAGS "-DSWIGWORDSIZE64")
endif()

set(LIBCOSIM_SWIG_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Generated wrapper code is not held to libcosim's warning standards.
if(MSVC)
    set(LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS "/W0;/WX-")
else()
    set(LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS "-w;-Wno-error")
endif()

# Tests need libcosim's test data (FMUs, configurations).
if(LIBCOSIM_SWIG_TEST_DATA_DIR)
    set(LIBCOSIM_SWIG_TESTS ON)
    enable_testing()
else()
    set(LIBCOSIM_SWIG_TESTS OFF)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibcosimSwigPackaging.cmake")

# An FMU that supports state export/import, for the state tests (passed as
# LIBCOSIM_STATE_FMU; the tests skip state export without it). In libcosim's
# build with tests, the Dahlquist reference FMU that libcosim's tests build.
# (The quarter-truck FMUs in the test data advertise state serialization,
# but crash when deserializing a state.)
set(defaultStateFmu "")
if(NOT LIBCOSIM_SWIG_STANDALONE AND LIBCOSIM_BUILD_TESTS)
    set(defaultStateFmu "${CMAKE_BINARY_DIR}/tests/reference-fmus-v2-build/fmus/Dahlquist.fmu")
endif()
set(LIBCOSIM_SWIG_STATE_FMU "${defaultStateFmu}" CACHE FILEPATH
    "An FMU that supports state export/import, for the binding state tests")

# Adds the bindings' output directory and libcosim's runtime directories
# (with Conan: those of all its dependencies) to PATH and LD_LIBRARY_PATH for
# a test. On Windows, when java.exe or julia.exe loads cosim_java.dll /
# cosim_c.dll, the libraries those depend on (cosim.dll, ...) are not
# searched for in the DLL's own directory, only in the executable's
# directory, the system directories and PATH. On Linux, .NET's DllImport
# finds the bindings via LD_LIBRARY_PATH. Python on Windows ignores PATH; the
# package registers the directories in LIBCOSIM_DLL_DIRS instead.
function(libcosim_swig_test_runtime_path testName)
    # Multi-config generators (Visual Studio) add a per-configuration directory.
    get_property(multiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(outputDirs "")
    foreach(dir IN ITEMS "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        if(dir AND multiConfig)
            list(APPEND outputDirs "${dir}/$<CONFIG>")
        elseif(dir)
            list(APPEND outputDirs "${dir}")
        endif()
    endforeach()
    if(LIBCOSIM_SWIG_STATE_FMU)
        set_property(TEST ${testName} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "LIBCOSIM_STATE_FMU=set:${LIBCOSIM_SWIG_STATE_FMU}")
    endif()
    foreach(dir IN ITEMS ${outputDirs} ${LIBCOSIM_SWIG_RUNTIME_DIRS})
        if(dir)
            set_property(TEST ${testName} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${dir}"
                "LD_LIBRARY_PATH=path_list_prepend:${dir}"
                "LIBCOSIM_DLL_DIRS=path_list_prepend:${dir}")
        endif()
    endforeach()
endfunction()

# Keyword check: SWIG renames identifiers that are keywords in a target
# language (warning 314), which would give that binding a different name.
# Checked for every SWIG language, whether or not it is built.
set(keywordArgs_python -python)
set(keywordArgs_java -java -package libcosim)
set(keywordArgs_csharp -csharp -namespace Libcosim)
set(keywordArgs_c -c -nocxx -namespace cosim)
set(keywordStamps "")
foreach(lang IN ITEMS python java csharp c)
    set(iface "${lang}/cosim_${lang}.i")
    set(langArgs ${keywordArgs_${lang}})
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/keyword-check/${lang}.stamp")
    set(args -c++ ${langArgs} ${LIBCOSIM_SWIG_FLAGS} "-I${LIBCOSIM_INCLUDE_DIR}")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND "${CMAKE_COMMAND}"
            "-DSWIG_EXECUTABLE=${SWIG_EXECUTABLE}" "-DSWIG_DIR=${SWIG_DIR}"
            "-DINTERFACE=${CMAKE_CURRENT_SOURCE_DIR}/${iface}"
            "-DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}/keyword-check/${lang}"
            "-DSTAMP=${stamp}" "-DARGS=${args}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSwigKeywords.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${iface}" "${CMAKE_CURRENT_SOURCE_DIR}/cosim.i"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSwigKeywords.cmake"
        COMMENT "Checking ${lang} names for keyword renames (SWIG warning 314)"
        VERBATIM)
    list(APPEND keywordStamps "${stamp}")
endforeach()
add_custom_target(swig_keyword_check ALL DEPENDS ${keywordStamps})

option(LIBCOSIM_SWIG_PYTHON "Build the SWIG Python bindings" ON)
option(LIBCOSIM_SWIG_C "Build the SWIG C bindings (SWIG's experimental C backend)" ON)
option(LIBCOSIM_SWIG_JULIA "Build the Julia package (on the C bindings)" ON)
option(LIBCOSIM_SWIG_JAVA "Build the SWIG Java bindings (needs a JDK)" OFF)
option(LIBCOSIM_SWIG_CSHARP "Build the SWIG C# bindings (needs the .NET SDK)" OFF)

if(LIBCOSIM_SWIG_PYTHON)
    add_subdirectory(python)
endif()
if(LIBCOSIM_SWIG_C)
    add_subdirectory(c)
    if(LIBCOSIM_SWIG_JULIA)
        add_subdirectory(julia)
    endif()
endif()
if(LIBCOSIM_SWIG_JAVA)
    add_subdirectory(java)
endif()
if(LIBCOSIM_SWIG_CSHARP)
    add_subdirectory(csharp)
endif()
