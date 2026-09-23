function(prepare_dahlquist_test_data)
    if(NOT DEFINED GEN_TEST_DATA_DIR OR GEN_TEST_DATA_DIR STREQUAL "")
        message(FATAL_ERROR
            "GEN_TEST_DATA_DIR must be set before preparing Dahlquist test data")
    endif()

    set(dahlquist_fmu_path
        "${REFERENCE_FMUS_V2_BINARY_DIR}/fmus/Dahlquist.fmu")
    file(MAKE_DIRECTORY "${GEN_TEST_DATA_DIR}/msmi")
    file(RELATIVE_PATH DAHLQUIST_FMU_RELATIVE_PATH
        "${GEN_TEST_DATA_DIR}/msmi"
        "${dahlquist_fmu_path}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/data/msmi/OspSystemStructure_Dahlquist.xml.in"
        "${GEN_TEST_DATA_DIR}/msmi/OspSystemStructure_Dahlquist.xml"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/data/msmi/OspSystemStructure_Dahlquist_proxyfmu.xml.in"
        "${GEN_TEST_DATA_DIR}/msmi/OspSystemStructure_Dahlquist_proxyfmu.xml"
        @ONLY)
endfunction()
