# Find fmt
#
# Find the native fmt headers and libraries.
#
#   FMT_INCLUDE_DIRS   - where to find fmt/yaml.h
#   FMT_LIBRARIES      - List of libraries when using fmt.
#   FMT_FOUND          - True if fmt found.
#

find_path(FMT_INCLUDE_DIR NAMES fmt/core.h)
mark_as_advanced(FMT_INCLUDE_DIR)

find_library(FMT_LIBRARY NAMES fmt fmtd)
mark_as_advanced(FMT_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(FMT
        REQUIRED_VARS FMT_LIBRARY FMT_INCLUDE_DIR)

if (FMT_FOUND)

    set(FMT_INCLUDE_DIRS ${FMT_INCLUDE_DIR})

    if (NOT FMT_LIBRARIES)
        set(FMT_LIBRARIES ${FMT_LIBRARY})
    endif()

    if (NOT TARGET fmt)
        add_library(fmt UNKNOWN IMPORTED)
        set_target_properties(fmt PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${FMT_INCLUDE_DIR}")
        set_property(TARGET fmt APPEND PROPERTY
                IMPORTED_LOCATION "${FMT_LIBRARY}")
    endif()

endif()
