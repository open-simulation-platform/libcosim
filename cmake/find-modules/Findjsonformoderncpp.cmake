# Finds the nlohmann_json library.
#

find_package(nlohmann_json CONFIG)

if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp PATH ${nlohmann_json_path})
    add_library(nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${NLOHMANN_INCLUDE_DIR}
    )
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(nlohmann_json DEFAULT_MSG NLOHMANN_INCLUDE_DIR)
endif()

# Harmonise with Conan package names and the cmake_find_package_multi generator.
add_library("jsonformoderncpp::jsonformoderncpp" INTERFACE IMPORTED GLOBAL)
target_link_libraries("jsonformoderncpp::jsonformoderncpp" INTERFACE "nlohmann_json")
