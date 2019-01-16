# - CMake script for locating FMI Library
#
# This script is run by the command find_package(FMILibrary). The location of the
# package may be explicitly specified using an environment variable named
# FMILibrary_DIR or a CMake variable of the same name.  If both are specified, the
# latter takes precedence.  The variable should point to the package
# installation prefix, i.e. the directory that contains the "bin", "lib" and
# "include" subdirectories.
#
# The script searches for both static and shared/dynamic libraries, and creates
# IMPORTED targets named "fmilib::static" and "fmilib::shared", respectively.  These
# targets will have several of their IMPORTED_* and INTERFACE_* properties set,
# making explicit use of commands like include_directories() superfluous in most
# cases.
#
# If the variable FMILibrary_USE_SHARED_LIB is set to TRUE, FMILibrary_LIBRARIES will
# contain the name of the IMPORTED target "fmilib::shared". Otherwise, it will
# contain "fmilib::static".
#
# After the script has completed, the variable FMILibrary_FOUND will contain whether
# the package was found or not.  If it was, then the following variables will
# also be set:
#
#    FMILibrary_INCLUDE_DIRS    - The directory that contains the header files.
#    FMILibrary_LIBRARIES       - The name of an IMPORTED target.
#
#    FMILibrary_DLL             - Path to dynamic library (Windows only).
#    FMILibrary_LIBRARY         - Path to static library.
#    FMILibrary_SHARED_LIBRARY  - Path to shared/import library.
#
cmake_minimum_required (VERSION 2.8.11)

# Find static library, and use its path prefix to provide a HINTS option to the
# other find_*() commands.
if (UNIX)
    set (_FMILibrary_oldsuffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set (CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif ()
find_library (FMILibrary_LIBRARY
    NAMES "fmilib2" "fmilib" "fmilib_shared"
    PATHS ${FMILibrary_DIR} $ENV{FMILibrary_DIR}
    PATH_SUFFIXES "lib")
mark_as_advanced (FMILibrary_LIBRARY)
unset (_FMILibrary_hints)
if (FMILibrary_LIBRARY)
    get_filename_component (_FMILibrary_prefix "${FMILibrary_LIBRARY}" PATH)
    get_filename_component (_FMILibrary_prefix "${_FMILibrary_prefix}" PATH)
    set (_FMILibrary_hints "HINTS" "${_FMILibrary_prefix}")
    unset (_FMILibrary_prefix)
endif ()

# Find shared/import library and append its path prefix to the HINTS option.
if (UNIX)
    set (CMAKE_FIND_LIBRARY_SUFFIXES ".so")
    set (_FMILibrary_shlibs "fmilib_shared" "fmilib2" "fmilib")
else ()
    set (_FMILibrary_shlibs "fmilib_shared")
endif ()
find_library (FMILibrary_SHARED_LIBRARY
    NAMES ${_FMILibrary_shlibs}
    ${_FMILibrary_hints}
    PATHS ${FMILibrary_DIR} $ENV{FMILibrary_DIR}
    PATH_SUFFIXES "lib")
mark_as_advanced (FMILibrary_SHARED_LIBRARY)
if (FMILibrary_SHARED_LIBRARY)
    get_filename_component (_FMILibrary_shared_prefix "${FMILibrary_SHARED_LIBRARY}" PATH)
    get_filename_component (_FMILibrary_shared_prefix "${_FMILibrary_shared_prefix}" PATH)
    if (NOT _FMILibrary_hints)
        set (_FMILibrary_hints "HINTS")
    endif ()
    list (APPEND _FMILibrary_hints "${_FMILibrary_shared_prefix}")
    unset (_FMILibrary_shared_prefix)
endif ()

# Reset CMAKE_FIND_LIBRARY_SUFFIXES
if (UNIX)
    set (CMAKE_FIND_LIBRARY_SUFFIXES ${_FMILibrary_oldsuffixes})
    unset (_FMILibrary_oldsuffixes)
endif ()

# Find header files and, on Windows, the dynamic library
find_path (FMILibrary_INCLUDE_DIRS "fmilib.h"
    ${_FMILibrary_hints}
    PATHS ${FMILibrary_DIR} $ENV{FMILibrary_DIR}
    PATH_SUFFIXES "include")
mark_as_advanced (FMILibrary_INCLUDE_DIRS)

if (WIN32)
    find_file (FMILibrary_DLL "fmilib_shared.dll"
        ${_FMILibrary_hints}
        PATHS ${FMILibrary_DIR} $ENV{FMILibrary_DIR}
        PATH_SUFFIXES "bin" "lib"
        NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    mark_as_advanced (FMILibrary_DLL)
endif ()
unset (_FMILibrary_hints)

# Create the IMPORTED targets.
if (FMILibrary_LIBRARY)
    add_library ("fmilib::static" STATIC IMPORTED)
    set_target_properties ("fmilib::static" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LINK_INTERFACE_LIBRARIES "${CMAKE_DL_LIBS}"
        IMPORTED_LOCATION "${FMILibrary_LIBRARY}"
        INTERFACE_COMPILE_DEFINITIONS "FMILibrary_STATIC_LIB_ONLY"
        INTERFACE_INCLUDE_DIRECTORIES "${FMILibrary_INCLUDE_DIRS}")
endif ()

if (FMILibrary_SHARED_LIBRARY)
    add_library ("fmilib::shared" SHARED IMPORTED)
    set_target_properties ("fmilib::shared" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        INTERFACE_INCLUDE_DIRECTORIES "${FMILibrary_INCLUDE_DIRS}")
    if (WIN32)
        set_target_properties ("fmilib::shared" PROPERTIES
            IMPORTED_IMPLIB "${FMILibrary_SHARED_LIBRARY}")
        if (FMILibrary_DLL)
            set_target_properties ("fmilib::shared" PROPERTIES
                IMPORTED_LOCATION "${FMILibrary_DLL}")
        endif ()
    else () # not WIN32
        set_target_properties ("fmilib::shared" PROPERTIES
            IMPORTED_LOCATION "${FMILibrary_SHARED_LIBRARY}")
    endif ()
endif ()

# Set the FMILibrary_LIBRARIES variable.
unset (FMILibrary_LIBRARIES)
if (FMILibrary_USE_SHARED_LIB)
    if (FMILibrary_SHARED_LIBRARY)
        set (FMILibrary_LIBRARIES "fmilib::shared")
    endif ()
else ()
    if (FMILibrary_LIBRARY)
        set (FMILibrary_LIBRARIES "fmilib::static")
    endif ()
endif ()

# Debug print-out.
if (FMILibrary_PRINT_VARS)
    message ("FMILibrary find script variables:")
    message ("  FMILibrary_INCLUDE_DIRS   = ${FMILibrary_INCLUDE_DIRS}")
    message ("  FMILibrary_LIBRARIES      = ${FMILibrary_LIBRARIES}")
    message ("  FMILibrary_DLL            = ${FMILibrary_DLL}")
    message ("  FMILibrary_LIBRARY        = ${FMILibrary_LIBRARY}")
    message ("  FMILibrary_SHARED_LIBRARY = ${FMILibrary_SHARED_LIBRARY}")
endif ()

# Standard find_package stuff.
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (FMILibrary DEFAULT_MSG FMILibrary_LIBRARIES FMILibrary_INCLUDE_DIRS)
