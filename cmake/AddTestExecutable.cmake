# This function is a short-hand way to create an executable target, add
# dependencies to it, and create a test that runs it.
#
#   add_test_executable(
#       <test name>
#       SOURCES <sources...>
#       DEPENDENCIES <targets...>
#   )
#
function(add_test_executable name)
    # Parse argument list
    set(params "SOURCES" "DEPENDENCIES")
    foreach(p IN LISTS params)
        set(arg_${p})
    endforeach()
    set(mode)
    foreach(arg IN LISTS ARGN)
        if("${arg}" IN_LIST params)
            set(mode "${arg}")
        elseif("${mode}" IN_LIST params)
            list(APPEND "arg_${mode}" "${arg}")
        else()
            message(FATAL_ERROR "Invalid/unexpected argument: ${arg}")
        endif()
    endforeach()

    # Add test target
    set(targetName "test-${name}")
    add_executable("${targetName}" ${arg_SOURCES})
    target_link_libraries(${targetName} PRIVATE ${arg_DEPENDENCIES})
    add_test(NAME "${name}" COMMAND "${targetName}")
endfunction()
