# Finds Microsoft's core guidelines support library.
#
# If found, the following is defined:
#   - `gsl_microsoft_FOUND` and `gsl_microsoft_INCLUDE_DIRS` variables.
#   - `gsl_microsoft::gsl_microsoft` target

find_path(gsl_microsoft_INCLUDE_DIR "gsl/gsl")
if(gsl_microsoft_INCLUDE_DIR)
    set(gsl_microsoft_INCLUDE_DIRS "${gsl_microsoft_INCLUDE_DIR}")
    add_library("gsl_microsoft::gsl_microsoft" INTERFACE IMPORTED)
    set_target_properties("gsl_microsoft::gsl_microsoft" PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${gsl_microsoft_INCLUDE_DIR}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("gsl_microsoft" DEFAULT_MSG "gsl_microsoft_INCLUDE_DIRS")
