# This function is a short-hand way to create an executable target, add
# dependencies to it, and create a test that runs it.
#
#   add_test_executable(
#       <test name>
#       FOLDER <target folder>
#       SOURCES <sources...>
#       DEPENDENCIES <targets...>
#       DATA_DIR <directory>
#   )
#
# "target folder" is just a human-readable name for the IDE folder that the
# target will be grouped under. If omitted, it will be a top-level target.
function(add_test_executable name)
    # Parse argument list
    set(params "FOLDER" "SOURCES" "DEPENDENCIES" "DATA_DIR")
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

    # Add target
    add_executable("${name}" ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE ${arg_DEPENDENCIES})
    if(arg_FOLDER)
        set_property(TARGET "${name}" PROPERTY FOLDER "${arg_FOLDER}")
    endif()

    # Add test
    add_test(NAME "${name}" COMMAND "${name}")
    if(arg_DATA_DIR)
        set_property(TEST "${name}" PROPERTY ENVIRONMENT "TEST_DATA_DIR=${arg_DATA_DIR}")
    endif()
endfunction()
