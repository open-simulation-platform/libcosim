# Finds the nlohmann_json library.
#

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message("nlohmann_json_path is ${nlohmann_json_path}")
    find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp PATH ${nlohmann_json_path})
    message("nlohmann include dir: ${NLOHMANN_INCLUDE_DIR}")
    add_library(nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${NLOHMANN_INCLUDE_DIR}
            )
endif()
