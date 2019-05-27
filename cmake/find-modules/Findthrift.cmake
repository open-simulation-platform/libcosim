# Find Thrift
#
# Find the native Thrift headers and libraries.
#
#   THRIFT_INCLUDE_DIRS   - where to find thrift/thrift.h
#   THRIFT_LIBRARIES      - List of libraries when using Thrift.
#   THRIFT_FOUND          - True if Thrift found.
#

find_path(THRIFT_INCLUDE_DIR NAMES thrift/Thrift.h)
mark_as_advanced(THRIFT_INCLUDE_DIR)

find_library(THRIFT_LIBRARY NAMES thrift thriftmd)
mark_as_advanced(THRIFT_LIBRARY)

find_library(THRIFT_LIBRARY_DEBUG NAMES thriftmdd)
mark_as_advanced(THRIFT_LIBRARY_DEBUG)

add_library(thrift::thrift UNKNOWN IMPORTED)
set_target_properties(thrift::thrift PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${THRIFT_INCLUDE_DIR}")

unset(THRIFT_LIBRARIES)
if (THRIFT_LIBRARY AND THRIFT_LIBRARY_DEBUG)
    set(THRIFT_LIBRARIES
        optimized ${THRIFT_LIBRARY}
        debug ${THRIFT_LIBRARY_DEBUG}
    )
    set_target_properties(thrift::thrift PROPERTIES
        IMPORTED_LOCATION_RELEASE "${THRIFT_LIBRARY}"
        IMPORTED_LOCATION_DEBUG "${THRIFT_LIBRARY}"
    )
else()
    if (THRIFT_LIBRARY)
        list(APPEND THRIFT_LIBRARIES "${THRIFT_LIBRARY}")
    endif ()
    if (THRIFT_LIBRARY_DEBUG)
        list(APPEND THRIFT_LIBRARIES "${THRIFT_LIBRARY_DEBUG}")
    endif ()
    set_target_properties(thrift::thrift PROPERTIES
        IMPORTED_LOCATION "${THRIFT_LIBRARIES}")
endif()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(THRIFT
    REQUIRED_VARS THRIFT_LIBRARIES THRIFT_INCLUDE_DIR)
