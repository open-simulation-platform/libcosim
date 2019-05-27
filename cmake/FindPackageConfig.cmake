# find_package_config(<package> [COMPONENTS <component>...])
#
# Wrapper for find_package() that uses CONFIG as the preferred search mode.
#
macro(find_package_config package)
    find_package(${package} QUIET ${ARGN} CONFIG)
    if(NOT ${package}_FOUND)
        find_package(${package} MODULE REQUIRED ${ARGN})
    endif()
endmacro()
