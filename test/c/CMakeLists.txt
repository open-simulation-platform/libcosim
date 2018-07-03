set(tests
    "hello_world"
)

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "c-${test}"
        SOURCES "${test}.c"
        DEPENDENCIES csecorec
    )
endforeach()
