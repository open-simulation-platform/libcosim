# Find spdlog
#
# Find the native spdlog headers and libraries.
#
#   SPDLOG_INCLUDE_DIRS   - where to find spdlog/yaml.h
#   SPDLOG_LIBRARIES      - List of libraries when using spdlog.
#   SPDLOG_FOUND          - True if spdlog found.
#

find_path(SPDLOG_INCLUDE_DIR NAMES spdlog/spdlog.h)
mark_as_advanced(SPDLOG_INCLUDE_DIR)

find_library(SPDLOG_LIBRARY NAMES spdlog spdlogd)
mark_as_advanced(SPDLOG_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SPDLOG
        REQUIRED_VARS SPDLOG_LIBRARY SPDLOG_INCLUDE_DIR)

if (SPDLOG_FOUND)

    set(SPDLOG_INCLUDE_DIRS ${SPDLOG_INCLUDE_DIR})

    if (NOT SPDLOG_LIBRARIES)
        set(SPDLOG_LIBRARIES ${SPDLOG_LIBRARY})
    endif()

    if (NOT TARGET spdlog)
        add_library(spdlog UNKNOWN IMPORTED)
        set_target_properties(spdlog PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${SPDLOG_INCLUDE_DIR}")
        set_property(TARGET spdlog APPEND PROPERTY
                IMPORTED_LOCATION "${SPDLOG_LIBRARY}")
    endif()

endif()
