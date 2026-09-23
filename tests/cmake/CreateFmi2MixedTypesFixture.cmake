if(NOT DEFINED OUTPUT_FMU OR NOT DEFINED FIXTURE_BINARY OR
   NOT DEFINED MODEL_DESCRIPTION OR NOT DEFINED FMI2_TEST_PLATFORM)
    message(FATAL_ERROR
        "OUTPUT_FMU, FIXTURE_BINARY, MODEL_DESCRIPTION, and "
        "FMI2_TEST_PLATFORM are required")
endif()

get_filename_component(output_directory "${OUTPUT_FMU}" DIRECTORY)
set(work_directory "${OUTPUT_FMU}.contents")
file(REMOVE_RECURSE "${work_directory}")
file(MAKE_DIRECTORY
    "${work_directory}/binaries/${FMI2_TEST_PLATFORM}"
    "${output_directory}")
file(COPY_FILE
    "${MODEL_DESCRIPTION}"
    "${work_directory}/modelDescription.xml"
    ONLY_IF_DIFFERENT)
get_filename_component(fixture_binary_name "${FIXTURE_BINARY}" NAME)
file(COPY_FILE
    "${FIXTURE_BINARY}"
    "${work_directory}/binaries/${FMI2_TEST_PLATFORM}/${fixture_binary_name}"
    ONLY_IF_DIFFERENT)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT_FMU}"
        --format=zip modelDescription.xml binaries
    WORKING_DIRECTORY "${work_directory}"
    RESULT_VARIABLE archive_result
    ERROR_VARIABLE archive_error)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR
        "Could not create ${OUTPUT_FMU}: ${archive_error}")
endif()

file(REMOVE_RECURSE "${work_directory}")
