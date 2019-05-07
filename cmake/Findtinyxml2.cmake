# Finds the tinyxml2 library.
#

cmake_minimum_required (VERSION 3.0.0)

# Find static library, and use its path prefix to provide a HINTS option to the
# other find_*() commands.
find_library (TINYXML2_LIBRARY "tinyxml2"
        PATHS ${TINYXML2_DIR} $ENV{TINYXML2_DIR}
        PATH_SUFFIXES "lib")
mark_as_advanced (TINYXML2_LIBRARY)
unset (_TINYXML2_hints)
if (TINYXML2_LIBRARY)
    get_filename_component (_TINYXML2_prefix "${TINYXML2_LIBRARY}" PATH)
    get_filename_component (_TINYXML2_prefix "${_TINYXML2_prefix}" PATH)
    set (_TINYXML2_hints "HINTS" "${_TINYXML2_prefix}")
    unset (_TINYXML2_prefix)
endif ()

# Find header files and, on Windows, the DLL
find_path (TINYXML2_INCLUDE_DIR "tinyxml2.h"
        ${_TINYXML2_hints}
        PATHS ${TINYXML2_DIR} $ENV{TINYXML2_DIR}
        PATH_SUFFIXES "include")
set (TINYXML2_INCLUDE_DIRS
        "${TINYXML2_INCLUDE_DIR}")

mark_as_advanced (TINYXML2_INCLUDE_DIRS)

if (WIN32)
    find_file (TINYXML2_DLL NAMES "TINYXML2.dll"
            ${_TINYXML2_hints}
            PATHS ${TINYXML2_DIR} $ENV{TINYXML2_DIR}
            PATH_SUFFIXES "bin" "lib"
            NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    mark_as_advanced (TINYXML2_DLL)
endif ()
unset (_TINYXML2_hints)

# Create the IMPORTED targets.
if (TINYXML2_LIBRARY)
    add_library ("TINYXML2::TINYXML2" SHARED IMPORTED)
    set_target_properties ("TINYXML2::TINYXML2" PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "${TINYXML2_LIBRARY}"
            INTERFACE_COMPILE_DEFINITIONS "TINYXML2_STATIC_LIB_ONLY"
            INTERFACE_INCLUDE_DIRECTORIES "${TINYXML2_INCLUDE_DIRS}")
    if (WIN32)
        set_target_properties ("TINYXML2::TINYXML2" PROPERTIES
                IMPORTED_IMPLIB "${TINYXML2_LIBRARY}"
                IMPORTED_LOCATION "${TINYXML2_DLL}")
    else () # not WIN32
        set_target_properties ("TINYXML2::TINYXML2" PROPERTIES
                IMPORTED_LOCATION "${TINYXML2_LIBRARY}")
    endif ()

    set (TINYXML2_LIBRARIES "TINYXML2::TINYXML2")
endif ()

# Debug print-out.
if (TINYXML2_PRINT_VARS)
    message ("TINYXML2 find script variables:")
    message ("  TINYXML2_INCLUDE_DIRS   = ${TINYXML2_INCLUDE_DIRS}")
    message ("  TINYXML2_LIBRARIES      = ${TINYXML2_LIBRARIES}")
    message ("  TINYXML2_DLL            = ${TINYXML2_DLL}")
    message ("  TINYXML2_LIBRARY        = ${TINYXML2_LIBRARY}")
endif ()

# Standard find_package stuff.
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (TINYXML2 DEFAULT_MSG TINYXML2_LIBRARIES TINYXML2_INCLUDE_DIRS)