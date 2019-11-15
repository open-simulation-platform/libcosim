# Find yaml-cpp
#
# Find the native yaml-cpp headers and libraries.
#
#   YAML_CPP_INCLUDE_DIRS   - where to find yaml-cpp/yaml.h
#   YAML_CPP_LIBRARIES      - List of libraries when using yaml-cpp.
#   YAML_CPP_FOUND          - True if yaml-cpp found.
#

find_path(YAML_CPP_INCLUDE_DIR NAMES yaml-cpp/yaml.h)
mark_as_advanced(YAML_CPP_INCLUDE_DIR)

find_library(YAML_CPP_LIBRARY NAMES yaml-cpp libyaml-cpp yaml-cppmd libyaml-cppmd libyaml-cppmdd)
mark_as_advanced(YAML_CPP_LIBRARY)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(YAML_CPP
        REQUIRED_VARS YAML_CPP_LIBRARY YAML_CPP_INCLUDE_DIR)

if (YAML_CPP_FOUND)

    set(YAML_CPP_INCLUDE_DIRS ${YAML_CPP_INCLUDE_DIR})

    if (NOT YAML_CPP_LIBRARIES)
        set(YAML_CPP_LIBRARIES ${YAML_CPP_LIBRARY})
    endif()

    if (NOT TARGET yaml-cpp)
        add_library(yaml-cpp UNKNOWN IMPORTED)
        set_target_properties(yaml-cpp PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIR}")
        set_property(TARGET yaml-cpp APPEND PROPERTY
                IMPORTED_LOCATION "${YAML_CPP_LIBRARY}")
    endif()

endif()
