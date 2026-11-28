# Stages and patches only the two upstream models needed by the dedicated
# FMI 3 array tests. The standard Reference-FMU build remains unmodified.
#
# Defines the targets fmi3_array_feedthrough_fixture and
# fmi3_fixed_array_state_space_fixture and the variable FMI3_ARRAY_FIXTURE_DIR.

set(FMI3_ARRAY_FIXTURE_SOURCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/fmi3-array-fixture-source")
file(REMOVE_RECURSE "${FMI3_ARRAY_FIXTURE_SOURCE_DIR}")
file(MAKE_DIRECTORY "${FMI3_ARRAY_FIXTURE_SOURCE_DIR}")
file(COPY
    "${REFERENCE_FMUS_SOURCE_DIR}/Feedthrough"
    "${REFERENCE_FMUS_SOURCE_DIR}/StateSpace"
    DESTINATION "${FMI3_ARRAY_FIXTURE_SOURCE_DIR}")
set(FMI3_ARRAY_FIXTURE_PATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/fmi3_array_fixture/reference_fmus.patch")
# Use GNU patch rather than `git apply`: when the build tree is nested inside
# another Git work tree (e.g. a Conan cache under the CI checkout), `git apply`
# resolves paths against that work tree and silently skips the files.
execute_process(
    COMMAND "${Patch_EXECUTABLE}"
        -d "${FMI3_ARRAY_FIXTURE_SOURCE_DIR}"
        --batch
        --forward
        -p1
        -i "${FMI3_ARRAY_FIXTURE_PATCH}"
    RESULT_VARIABLE fmi3_array_fixture_patch_result
    OUTPUT_VARIABLE fmi3_array_fixture_patch_output
    ERROR_VARIABLE fmi3_array_fixture_patch_error)
if(NOT fmi3_array_fixture_patch_result EQUAL 0)
    message(FATAL_ERROR
        "Could not patch the staged FMI 3 array fixtures: "
        "${fmi3_array_fixture_patch_output}"
        "${fmi3_array_fixture_patch_error}")
endif()
foreach(fmi3_array_fixture_marker IN ITEMS
        "StateSpace/FMI3.xml|name=\"fixed\""
        "StateSpace/model.c|case vr_fixed:"
        "Feedthrough/FMI3.xml|name=\"Enumeration_array\""
        "Feedthrough/model.c|case vr_Enumeration_array:")
    string(REPLACE "|" ";" fmi3_array_fixture_marker
        "${fmi3_array_fixture_marker}")
    list(GET fmi3_array_fixture_marker 0 fmi3_array_fixture_marker_file)
    list(GET fmi3_array_fixture_marker 1 fmi3_array_fixture_marker_text)
    file(READ
        "${FMI3_ARRAY_FIXTURE_SOURCE_DIR}/${fmi3_array_fixture_marker_file}"
        fmi3_array_fixture_marker_contents)
    string(FIND "${fmi3_array_fixture_marker_contents}"
        "${fmi3_array_fixture_marker_text}"
        fmi3_array_fixture_marker_position)
    if(fmi3_array_fixture_marker_position EQUAL -1)
        message(FATAL_ERROR
            "The staged FMI 3 array fixture "
            "${fmi3_array_fixture_marker_file} was not patched: missing "
            "'${fmi3_array_fixture_marker_text}'")
    endif()
endforeach()

set(FMI3_ARRAY_FIXTURE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/fmi3-array-fixtures")

foreach(fmi3_array_fixture IN ITEMS
        "fmi3_array_feedthrough_fixture|Feedthrough|ArrayFeedthrough.fmu"
        "fmi3_fixed_array_state_space_fixture|StateSpace|FixedArrayStateSpace.fmu")
    string(REPLACE "|" ";" fmi3_array_fixture "${fmi3_array_fixture}")
    list(GET fmi3_array_fixture 0 fmi3_array_fixture_target)
    list(GET fmi3_array_fixture 1 fmi3_array_fixture_model)
    list(GET fmi3_array_fixture 2 fmi3_array_fixture_archive)
    set(fmi3_array_fixture_model_dir
        "${FMI3_ARRAY_FIXTURE_SOURCE_DIR}/${fmi3_array_fixture_model}")
    libcosim_add_fmi3_fixture_fmu(
        ${fmi3_array_fixture_target}
        OUTPUT_NAME "${fmi3_array_fixture_model}"
        OUTPUT_FMU "${FMI3_ARRAY_FIXTURE_DIR}/${fmi3_array_fixture_archive}"
        MODEL_DESCRIPTION "${fmi3_array_fixture_model_dir}/FMI3.xml"
        CONTENTS_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/${fmi3_array_fixture_target}-contents"
        ARCHIVE_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/${fmi3_array_fixture_target}-archive"
        SOURCES
            "${REFERENCE_FMUS_SOURCE_DIR}/src/fmi3Functions.c"
            "${REFERENCE_FMUS_SOURCE_DIR}/src/cosimulation.c"
            "${fmi3_array_fixture_model_dir}/model.c"
        INCLUDE_DIRECTORIES "${fmi3_array_fixture_model_dir}")
endforeach()
