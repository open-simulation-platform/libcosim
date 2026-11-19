# Experimental SWIG bindings for libcosim (spike).
#
# The language-independent interface lives in cosim.i. Each target language
# has its own subdirectory with a thin language-specific .i file.

# Pinned exactly: SWIG's C backend is experimental and its generated API may
# change between versions (see the swig_c_api_reference test).
find_package(SWIG 4.4.1 EXACT REQUIRED)
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

enable_testing()
set(LIBCOSIM_SWIG_TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/data")

# Adds libcosim's runtime directory to PATH for a test. On Windows, when
# java.exe or julia.exe loads cosim_java.dll / cosim_c.dll, the libraries
# those depend on (cosim.dll, ...) are not searched for in the DLL's own
# directory, only in the executable's directory, the system directories and
# PATH. On Linux, .NET's DllImport finds the bindings via LD_LIBRARY_PATH.
function(libcosim_swig_test_runtime_path testName)
    set_property(TEST ${testName} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:cosim>"
        "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:cosim>")
endfunction()

option(LIBCOSIM_SWIG_PYTHON "Build the SWIG Python bindings" ON)
option(LIBCOSIM_SWIG_C "Build the SWIG C bindings (SWIG's experimental C backend)" ON)
option(LIBCOSIM_SWIG_JAVA "Build the SWIG Java bindings (needs a JDK)" OFF)
option(LIBCOSIM_SWIG_CSHARP "Build the SWIG C# bindings (needs the .NET SDK)" OFF)

if(LIBCOSIM_SWIG_PYTHON)
    add_subdirectory(python)
endif()
if(LIBCOSIM_SWIG_C)
    add_subdirectory(c)
endif()
if(LIBCOSIM_SWIG_JAVA)
    add_subdirectory(java)
endif()
if(LIBCOSIM_SWIG_CSHARP)
    add_subdirectory(csharp)
endif()
