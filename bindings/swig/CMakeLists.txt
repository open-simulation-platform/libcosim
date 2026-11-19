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

option(LIBCOSIM_SWIG_PYTHON "Build the SWIG Python bindings" ON)
option(LIBCOSIM_SWIG_C "Build the SWIG C bindings (SWIG's experimental C backend)" ON)
option(LIBCOSIM_SWIG_JAVA "Build the SWIG Java bindings (needs a JDK)" OFF)

if(LIBCOSIM_SWIG_PYTHON)
    add_subdirectory(python)
endif()
if(LIBCOSIM_SWIG_C)
    add_subdirectory(c)
endif()
if(LIBCOSIM_SWIG_JAVA)
    add_subdirectory(java)
endif()
