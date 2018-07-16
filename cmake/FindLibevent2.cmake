# - CMake script for locating libevent 2.x
#
# If the script succeeds, it will create IMPORTED targets named
# libevent2::core and libevent2::extras, plus set the following variables:
#
#    Libevent2_FOUND           - If the library was found
#    Libevent2_INCLUDE_DIRS    - The directory that contains the header files.
#    Libevent2_LIBRARIES       - Contains all IMPORTED targets
#    Libevent2_CORE_LIBRARY    - Path to libevent_core static/import library file.
#    Libevent2_EXTRAS_LIBRARY  - Path to libevent_extras static/import library file.
#
cmake_minimum_required (VERSION 3.0.0)

# Find include dirs and libraries
find_path(
    Libevent2_INCLUDE_DIR
    "event2/event.h"
    HINTS "${LIBEVENT2_DIR}" ENV LIBEVENT2_DIR
)
set(Libevent2_INCLUDE_DIRS "${Libevent2_INCLUDE_DIR}")
get_filename_component(Libevent2_PREFIX "${Libevent2_INCLUDE_DIR}" PATH)
find_library(
    Libevent2_CORE_LIBRARY
    NAMES "libevent_core" "event_core"
    HINTS "${Libevent2_PREFIX}" "${LIBEVENT2_DIR}" ENV LIBEVENT2_DIR
)
find_library(
    Libevent2_EXTRAS_LIBRARY
    NAMES "libevent_extras" "event_extras"
    HINTS "${Libevent2_PREFIX}" "${LIBEVENT2_DIR}" ENV LIBEVENT2_DIR
)

set(Libevent2_LIBRARIES)
if(Libevent2_CORE_LIBRARY)
    add_library("libevent2::core" STATIC IMPORTED)
    set_target_properties("libevent2::core" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${Libevent2_CORE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Libevent2_INCLUDE_DIR}"
    )
    if(WIN32)
        set_target_properties("libevent2::core" PROPERTIES
            INTERFACE_LINK_LIBRARIES "ws2_32"
        )
    endif()
    list(APPEND Libevent2_LIBRARIES "libevent2::core")
endif()
if(Libevent2_EXTRAS_LIBRARY)
    add_library("libevent2::extras" STATIC IMPORTED)
    set_target_properties("libevent2::extras" PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${Libevent2_EXTRAS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Libevent2_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "libevent2::core"
    )
    list(APPEND Libevent2_LIBRARIES "libevent2::extras")
endif()

mark_as_advanced(
    Libevent2_INCLUDE_DIR
    Libevent2_CORE_LIBRARY
    Libevent2_EXTRAS_LIBRARY
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libevent2 DEFAULT_MSG
    Libevent2_LIBRARIES
    Libevent2_INCLUDE_DIRS
    Libevent2_CORE_LIBRARY
)
