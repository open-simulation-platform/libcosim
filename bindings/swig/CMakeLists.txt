# Experimental SWIG bindings for libcosim (spike).
#
# The language-independent interface lives in cosim.i. Each target language
# has its own subdirectory with a thin language-specific .i file.

find_package(SWIG 4.4 REQUIRED)
include(UseSWIG)
message(STATUS "SWIG ${SWIG_VERSION}: ${SWIG_EXECUTABLE}")

# SWIG's stdint.i must be told whether int64_t is 'long' (LP64: Linux) or
# 'long long' (Windows, and macOS where int64_t is also 'long long').
set(LIBCOSIM_SWIG_FLAGS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND LIBCOSIM_SWIG_FLAGS "-DSWIGWORDSIZE64")
endif()

set(LIBCOSIM_SWIG_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

option(LIBCOSIM_SWIG_PYTHON "Build the SWIG Python bindings" ON)
option(LIBCOSIM_SWIG_C "Build the SWIG C bindings (SWIG's experimental C backend)" ON)

if(LIBCOSIM_SWIG_PYTHON)
    add_subdirectory(python)
endif()
if(LIBCOSIM_SWIG_C)
    add_subdirectory(c)
endif()
