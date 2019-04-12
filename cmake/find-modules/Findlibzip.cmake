# - CMake script for locating libzip
#
# If the script succeeds, it will create an IMPORTED target named
# "libzip::libzip", plus set the following variables:
#
#    libzip_FOUND           - If the library was found
#    libzip_INCLUDE_DIRS    - The directory that contains the header files.
#    libzip_LIBRARIES       - Contains "libzip::libzip"
#    libzip_LIBRARY         - Path to static/import library file.
#
cmake_minimum_required (VERSION 3.0.0)

# Find static library, and use its path prefix to provide a HINTS option to the
# other find_*() commands.
find_library (libzip_LIBRARY "zip"
    PATHS ${libzip_DIR} $ENV{libzip_DIR}
    PATH_SUFFIXES "lib")
mark_as_advanced (libzip_LIBRARY)
unset (_libzip_hints)
if (libzip_LIBRARY)
    get_filename_component (_libzip_prefix "${libzip_LIBRARY}" PATH)
    get_filename_component (_libzip_prefix "${_libzip_prefix}" PATH)
    set (_libzip_hints "HINTS" "${_libzip_prefix}")
    unset (_libzip_prefix)
endif ()

# Find header files and, on Windows, the DLL
find_path (libzip_INCLUDE_DIR_zip "zip.h"
    ${_libzip_hints}
    PATHS ${libzip_DIR} $ENV{libzip_DIR}
    PATH_SUFFIXES "include")
find_path (libzip_INCLUDE_DIR_zipconf "zipconf.h"
    ${_libzip_hints}
    PATHS ${libzip_DIR} $ENV{libzip_DIR}
    PATH_SUFFIXES "include" "lib/libzip/include")
set (libzip_INCLUDE_DIRS
    "${libzip_INCLUDE_DIR_zip}" "${libzip_INCLUDE_DIR_zipconf}"
)

mark_as_advanced (libzip_INCLUDE_DIRS)

if (WIN32)
    find_file (libzip_DLL NAMES "zip.dll" "libzip.dll"
        ${_libzip_hints}
        PATHS ${libzip_DIR} $ENV{libzip_DIR}
        PATH_SUFFIXES "bin" "lib"
        NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    mark_as_advanced (libzip_DLL)
endif ()
unset (_libzip_hints)

# Create the IMPORTED targets.
if (libzip_LIBRARY)
    add_library ("libzip::libzip" SHARED IMPORTED)
    set_target_properties ("libzip::libzip" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${libzip_LIBRARY}"
        INTERFACE_COMPILE_DEFINITIONS "libzip_STATIC_LIB_ONLY"
        INTERFACE_INCLUDE_DIRECTORIES "${libzip_INCLUDE_DIRS}")
    if (WIN32)
        set_target_properties ("libzip::libzip" PROPERTIES
            IMPORTED_IMPLIB "${libzip_LIBRARY}"
            IMPORTED_LOCATION "${libzip_DLL}")
    else () # not WIN32
        set_target_properties ("libzip::libzip" PROPERTIES
            IMPORTED_LOCATION "${libzip_LIBRARY}")
    endif ()

    set (libzip_LIBRARIES "libzip::libzip")
endif ()

# Debug print-out.
if (libzip_PRINT_VARS)
    message ("libzip find script variables:")
    message ("  libzip_INCLUDE_DIRS   = ${libzip_INCLUDE_DIRS}")
    message ("  libzip_LIBRARIES      = ${libzip_LIBRARIES}")
    message ("  libzip_DLL            = ${libzip_DLL}")
    message ("  libzip_LIBRARY        = ${libzip_LIBRARY}")
endif ()

# Standard find_package stuff.
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (libzip DEFAULT_MSG libzip_LIBRARIES libzip_INCLUDE_DIRS)
