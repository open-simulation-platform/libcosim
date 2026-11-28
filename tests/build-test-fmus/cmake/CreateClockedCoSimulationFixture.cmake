if(NOT DEFINED SOURCE_FMU OR NOT DEFINED OUTPUT_FMU OR
   NOT DEFINED FIXTURE_BINARY OR NOT DEFINED FMI3_TEST_PLATFORM)
    message(FATAL_ERROR
        "SOURCE_FMU, OUTPUT_FMU, FIXTURE_BINARY, and FMI3_TEST_PLATFORM are required")
endif()

set(work_dir "${OUTPUT_FMU}.contents")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(ARCHIVE_EXTRACT INPUT "${SOURCE_FMU}" DESTINATION "${work_dir}")

set(model_description "${work_dir}/modelDescription.xml")
file(READ "${model_description}" xml)

set(original_xml "${xml}")
string(REPLACE
    "<ScheduledExecution modelIdentifier=\"Clocks\"/>"
    "<CoSimulation modelIdentifier=\"Clocks\" hasEventMode=\"true\"/>"
    xml "${xml}")
if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not select Co-Simulation in ${SOURCE_FMU}")
endif()

set(original_xml "${xml}")
string(REPLACE
    "name=\"inClock3\" valueReference=\"1003\" causality=\"input\" intervalVariability=\"countdown\" priority=\"2\" clocks=\"1001\""
    "name=\"inClock3\" valueReference=\"1003\" causality=\"input\" intervalVariability=\"constant\" intervalDecimal=\"2.0\" priority=\"2\""
    xml "${xml}")
if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not constrain the countdown Clock in ${SOURCE_FMU}")
endif()

set(original_xml "${xml}")
string(REPLACE
    "name=\"input2\" valueReference=\"2006\" causality=\"input\" start=\"0\""
    "name=\"input2\" valueReference=\"2006\" causality=\"input\" start=\"0\" clocks=\"1002\""
    xml "${xml}")
if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not mark the clocked input in ${SOURCE_FMU}")
endif()

set(original_xml "${xml}")
string(REPLACE
    "<Int32 name=\"result2\" valueReference=\"2005\" causality=\"output\" clocks=\"1002\"/>"
    "<Int32 name=\"result2\" valueReference=\"2005\" causality=\"output\" clocks=\"1002\"/>\n   <Int32 name=\"result2Snapshot\" valueReference=\"2008\" causality=\"output\"/>"
    xml "${xml}")
if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not add the routed result snapshot to ${SOURCE_FMU}")
endif()

set(original_xml "${xml}")
string(REPLACE
    "<Output valueReference=\"2005\" dependencies=\"1002 2006\"/>"
    "<Output valueReference=\"2005\" dependencies=\"1002 2006\"/>\n   <Output valueReference=\"2008\" dependencies=\"2005\"/>"
    xml "${xml}")
if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not add the routed result snapshot dependency to ${SOURCE_FMU}")
endif()

file(WRITE "${model_description}" "${xml}")
get_filename_component(fixture_binary_name "${FIXTURE_BINARY}" NAME)
file(COPY_FILE
    "${FIXTURE_BINARY}"
    "${work_dir}/binaries/${FMI3_TEST_PLATFORM}/${fixture_binary_name}"
    ONLY_IF_DIFFERENT)
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
