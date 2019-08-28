# Finds the core guidelines support library.
#
# If found, the following is defined:
#   - `MS_GSL_FOUND` and `MS_GSL_INCLUDE_DIRS` variables.
#   - `ms_gsl` target

find_path(MS_GSL_INCLUDE_DIR "gsl/gsl")
if(MS_GSL_INCLUDE_DIR)
    set(MS_GSL_INCLUDE_DIRS "${MS_GSL_INCLUDE_DIR}")
    add_library(ms_gsl INTERFACE IMPORTED)
    set_target_properties(ms_gsl PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MS_GSL_INCLUDE_DIR}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("MS_GSL" DEFAULT_MSG "MS_GSL_INCLUDE_DIRS")
