# Finds the core guidelines support library.
#
# If found, the following is defined:
#   - `GSL_FOUND` and `GSL_INCLUDE_DIRS` variables.
#   - `gsl` target

find_path(GSL_INCLUDE_DIR "gsl/gsl")
if(GSL_INCLUDE_DIR)
    set(GSL_INCLUDE_DIRS "${GSL_INCLUDE_DIR}")
    add_library(gsl INTERFACE IMPORTED)
    set_target_properties(gsl PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${GSL_INCLUDE_DIR}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("GSL" DEFAULT_MSG "GSL_INCLUDE_DIRS")
