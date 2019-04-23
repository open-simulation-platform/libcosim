# Finds the tinyxml2 library.
#

find_package(tinyxml2 CONFIG QUIET)
if(NOT tinyxml2_FOUND)
    message("tinyxml2_path is ${tinyxml_path}")
    find_path(TINYXML2_INCLUDE_DIR tinyxml2.h PATH ${tinyxml2_path})
    message("tinyxml2 include dir: ${TINYXML2_INCLUDE_DIR}")
    add_library(tinyxml2 INTERFACE IMPORTED)
    set_target_properties(tinyxml2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${TINYXML2_INCLUDE_DIR}
            )
endif()
