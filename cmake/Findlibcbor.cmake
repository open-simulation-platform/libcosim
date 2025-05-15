# FindLibCBOR.cmake
# Locate libcbor library and headers, and define libcbor::libcbor target

find_path(LIBCBOR_INCLUDE_DIR
        NAMES cbor.h
        PATH_SUFFIXES include
)

find_library(LIBCBOR_LIBRARY
        NAMES cbor libcbor
        PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libcbor
        REQUIRED_VARS LIBCBOR_LIBRARY LIBCBOR_INCLUDE_DIR
)

if(LIBCBOR_FOUND)
    set(LIBCBOR_INCLUDE_DIRS ${LIBCBOR_INCLUDE_DIR})
    set(LIBCBOR_LIBRARIES ${LIBCBOR_LIBRARY})

    if(NOT TARGET libcbor::libcbor)
        add_library(libcbor::libcbor UNKNOWN IMPORTED)
        set_target_properties(libcbor::libcbor PROPERTIES
                IMPORTED_LOCATION "${LIBCBOR_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBCBOR_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LIBCBOR_INCLUDE_DIR LIBCBOR_LIBRARY)
