set(tests
    "hello_world"
)

set(libs)
if(CSECORE_BUILD_STATIC)
    list(APPEND libs "static")
endif()
if(CSECORE_BUILD_SHARED)
    list(APPEND libs "shared")
endif()

include("AddTestExecutable")
foreach(test IN LISTS tests)
    foreach(lib IN LISTS libs)
        add_test_executable(
            "c-${lib}-${test}"
            SOURCES "${test}.c"
            DEPENDENCIES "c-${lib}"
        )
    endforeach()
endforeach()
