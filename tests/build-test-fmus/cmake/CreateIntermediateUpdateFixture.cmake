if(NOT DEFINED SOURCE_FMU OR NOT DEFINED OUTPUT_FMU OR NOT DEFINED FIXTURE_KIND)
    message(FATAL_ERROR
        "SOURCE_FMU, OUTPUT_FMU, and FIXTURE_KIND are required")
endif()

set(work_dir "${OUTPUT_FMU}.contents")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(ARCHIVE_EXTRACT INPUT "${SOURCE_FMU}" DESTINATION "${work_dir}")

set(model_description "${work_dir}/modelDescription.xml")
file(READ "${model_description}" xml)
set(original_xml "${xml}")

if(FIXTURE_KIND STREQUAL "Feedthrough")
    string(REPLACE
        "name=\"Float32_continuous_input\"  valueReference=\"1\" causality=\"input\" start=\"0\""
        "name=\"Float32_continuous_input\"  valueReference=\"1\" causality=\"input\" variability=\"continuous\" start=\"0\" intermediateUpdate=\"true\""
        xml "${xml}")
    string(REPLACE
        "name=\"Float32_continuous_output\" valueReference=\"2\" causality=\"output\""
        "name=\"Float32_continuous_output\" valueReference=\"2\" causality=\"output\" variability=\"continuous\" intermediateUpdate=\"true\""
        xml "${xml}")
elseif(FIXTURE_KIND STREQUAL "StateSpace")
    string(REPLACE
        "name=\"u\" valueReference=\"9\" description=\"Input vector\" causality=\"input\" start=\"1 2 3\""
        "name=\"u\" valueReference=\"9\" description=\"Input vector\" causality=\"input\" start=\"1 2 3\" intermediateUpdate=\"true\""
        xml "${xml}")
    string(REPLACE
        "name=\"y\" valueReference=\"10\" description=\"Output vector\" causality=\"output\""
        "name=\"y\" valueReference=\"10\" description=\"Output vector\" causality=\"output\" intermediateUpdate=\"true\""
        xml "${xml}")
elseif(FIXTURE_KIND STREQUAL "BouncingBallOutputDerivatives")
    string(REPLACE
        "fixedInternalStepSize=\"1e-3\""
        "fixedInternalStepSize=\"1e-3\"\n    maxOutputDerivativeOrder=\"1\""
        xml "${xml}")
else()
    message(FATAL_ERROR "Unknown Intermediate Update fixture: ${FIXTURE_KIND}")
endif()

if(xml STREQUAL original_xml)
    message(FATAL_ERROR
        "Could not mark Intermediate Update variables in ${SOURCE_FMU}")
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
