# Find proxy-fmu
#
# Find the native proxy-server headers and libraries.
#
#   PROXY_FMU_INCLUDE_DIRS   - where to find proxy-fmu/...
#   PROXY_FMU_LIBRARIES      - List of libraries when using proxy-server.
#   PROXY_FMU_FOUND          - True if proxy-fmu found.
#

find_path(PROXY_FMU_INCLUDE_DIR NAMES proxyfmu/client/proxy_fmu.hpp)
mark_as_advanced(PROXY_FMU_INCLUDE_DIR)

find_library(PROXY_FMU_LIBRARY NAMES proxy-client)
mark_as_advanced(PROXY_FMU_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PROXY_FMU
        REQUIRED_VARS PROXY_FMU_LIBRARY PROXY_FMU_INCLUDE_DIR)

if (PROXY_FMU_FOUND)

  set(PROXY_FMU_INCLUDE_DIRS ${PROXY_FMU_INCLUDE_DIR})

  if (NOT PROXY_FMU_LIBRARIES)
    set(PROXY_FMU_LIBRARIES ${PROXY_FMU_LIBRARY})
  endif()

  if (NOT TARGET proxy-server)
    add_library(proxy-server UNKNOWN IMPORTED)
    set_target_properties(proxy-server PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PROXY_FMU_INCLUDE_DIR}")
    set_property(TARGET proxy-server APPEND PROPERTY
            IMPORTED_LOCATION "${PROXY_FMU_LIBRARY}")
  endif()

endif()
