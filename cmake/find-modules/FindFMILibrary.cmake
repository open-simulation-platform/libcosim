# - CMake script for locating FMI Library
#
# This script is run by the command find_package(FMILibrary). The location of the
# package may be explicitly specified using an environment variable named
# FMILibrary_DIR or a CMake variable of the same name.  If both are specified, the
# latter takes precedence.  The variable should point to the package
# installation prefix, i.e. the directory that contains the "bin", "lib" and
# "include" subdirectories.
#
# The script searches for both static and shared/dynamic libraries, and
# creates IMPORTED targets named FMILibrary::static and FMILibrary::shared,
# respectively.  These targets will have several of their IMPORTED_* and
# INTERFACE_* properties set, making explicit use of commands like
# include_directories() superfluous in most cases.  The target
# FMILibrary::FMILibrary will be an alias for either FMILibrary::static or
# FMILibrary::shared depending on whether the variable FMILibrary_USE_SHARED_LIB
# is FALSE or TRUE, respectively.
#
cmake_minimum_required (VERSION 3.9)

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
    add_library ("FMILibrary::static" STATIC IMPORTED)
    set_target_properties ("FMILibrary::static" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LINK_INTERFACE_LIBRARIES "${CMAKE_DL_LIBS}"
        IMPORTED_LOCATION "${FMILibrary_LIBRARY}"
        INTERFACE_COMPILE_DEFINITIONS "FMILibrary_STATIC_LIB_ONLY"
        INTERFACE_INCLUDE_DIRECTORIES "${FMILibrary_INCLUDE_DIRS}")
endif ()

if (FMILibrary_SHARED_LIBRARY)
    add_library ("FMILibrary::shared" SHARED IMPORTED)
    set_target_properties ("FMILibrary::shared" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        INTERFACE_INCLUDE_DIRECTORIES "${FMILibrary_INCLUDE_DIRS}")
    if (WIN32)
        set_target_properties ("FMILibrary::shared" PROPERTIES
            IMPORTED_IMPLIB "${FMILibrary_SHARED_LIBRARY}")
        if (FMILibrary_DLL)
            set_target_properties ("FMILibrary::shared" PROPERTIES
                IMPORTED_LOCATION "${FMILibrary_DLL}")
        endif ()
    else () # not WIN32
        set_target_properties ("FMILibrary::shared" PROPERTIES
            IMPORTED_LOCATION "${FMILibrary_SHARED_LIBRARY}")
    endif ()
endif ()

# Create the main target.
if (FMILibrary_USE_SHARED_LIB AND FMILibrary_SHARED_LIBRARY)
    add_library ("FMILibrary::FMILibrary" INTERFACE IMPORTED)
    set_property (
        TARGET "FMILibrary::FMILibrary"
        APPEND
        PROPERTY INTERFACE_LINK_LIBRARIES "FMILibrary::shared")
    set (FMILibrary_LIBRARIES "FMILibrary::FMILibrary")
elseif ((NOT FMILibrary_USE_SHARED_LIB) AND FMILibrary_LIBRARY)
    add_library ("FMILibrary::FMILibrary" INTERFACE IMPORTED)
    set_property (
        TARGET "FMILibrary::FMILibrary"
        APPEND
        PROPERTY INTERFACE_LINK_LIBRARIES "FMILibrary::static")
    set (FMILibrary_LIBRARIES "FMILibrary::FMILibrary")
endif ()

# Standard find_package stuff.
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (FMILibrary DEFAULT_MSG FMILibrary_LIBRARIES FMILibrary_INCLUDE_DIRS)
