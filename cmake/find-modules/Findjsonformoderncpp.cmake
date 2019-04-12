# Finds the jsonformoderncpp library.
#

find_package(jsonformoderncpp CONFIG QUIET)
if(NOT jsonformoderncpp_FOUND)
    message("jsonformoderncpp_path is ${jsonformoderncpp_path}")
    find_path(JSONFORMODERNCPP_INCLUDE_DIR nlohmann/json.hpp PATH ${jsonformoderncpp_path})
    message("jsonformoderncpp include dir: ${JSONFORMODERNCPP_INCLUDE_DIR}")
    add_library("jsonformoderncpp::jsonformoderncpp" INTERFACE IMPORTED)
    set_target_properties("jsonformoderncpp::jsonformoderncpp" PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${JSONFORMODERNCPP_INCLUDE_DIR}
    )
endif()
