set(tests
    "hello_world"
)

include("AddTestExecutable")
foreach(test IN LISTS tests)
    add_test_executable(
        "cpp-${test}"
        SOURCES "${test}.cpp"
        DEPENDENCIES csecorecpp
    )
endforeach()
