#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "csecore::csecorecpp" for configuration "Debug"
set_property(TARGET csecore::csecorecpp APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(csecore::csecorecpp PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/csecorecpp.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/csecorecpp.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS csecore::csecorecpp )
list(APPEND _IMPORT_CHECK_FILES_FOR_csecore::csecorecpp "${_IMPORT_PREFIX}/lib/csecorecpp.lib" "${_IMPORT_PREFIX}/bin/csecorecpp.dll" )

# Import target "csecore::csecorec" for configuration "Debug"
set_property(TARGET csecore::csecorec APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(csecore::csecorec PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/csecorec.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/csecorec.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS csecore::csecorec )
list(APPEND _IMPORT_CHECK_FILES_FOR_csecore::csecorec "${_IMPORT_PREFIX}/lib/csecorec.lib" "${_IMPORT_PREFIX}/bin/csecorec.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
