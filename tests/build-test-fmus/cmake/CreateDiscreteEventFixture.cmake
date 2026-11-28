if(NOT DEFINED SOURCE_FMU OR NOT DEFINED OUTPUT_FMU)
    message(FATAL_ERROR "SOURCE_FMU and OUTPUT_FMU are required")
endif()

set(work_dir "${OUTPUT_FMU}.contents")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(ARCHIVE_EXTRACT INPUT "${SOURCE_FMU}" DESTINATION "${work_dir}")

set(model_description "${work_dir}/modelDescription.xml")
file(READ "${model_description}" xml)
set(original_xml "${xml}")
string(REPLACE
    "    canReturnEarlyAfterIntermediateUpdate=\"true\"\n"
    ""
    xml "${xml}")
if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not disable early return in ${SOURCE_FMU}")
endif()

file(WRITE "${model_description}" "${xml}")
file(GLOB archive_entries RELATIVE "${work_dir}" "${work_dir}/*")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT_FMU}"
        --format=zip ${archive_entries}
    WORKING_DIRECTORY "${work_dir}"
    RESULT_VARIABLE archive_result
    ERROR_VARIABLE archive_error)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR
        "Could not create ${OUTPUT_FMU}: ${archive_error}")
endif()

file(REMOVE_RECURSE "${work_dir}")
