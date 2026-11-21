# Configure the independent generator only when its explicit target is built.
set(fixture_build_dir "${CMAKE_BINARY_DIR}/test-fmu-generator")
set(fixture_configure_args
    -S "${CMAKE_SOURCE_DIR}/tests/build-test-fmus"
    -B "${fixture_build_dir}"
    -G "${CMAKE_GENERATOR}"
    "-DFMILibrary_INCLUDE_DIRS:PATH=${FMILibrary_INCLUDE_DIRS}"
    "-DLIBCOSIM_FIXTURE_FETCH_REFERENCE_FMUS=ON"
    "-DCMAKE_BUILD_TYPE=$<CONFIG>")
if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND fixture_configure_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND fixture_configure_args -T "${CMAKE_GENERATOR_TOOLSET}")
endif()
if(CMAKE_TOOLCHAIN_FILE)
    get_filename_component(fixture_toolchain "${CMAKE_TOOLCHAIN_FILE}"
        ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
    list(APPEND fixture_configure_args "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${fixture_toolchain}")
endif()
foreach(variable IN ITEMS REFERENCE_FMUS_SOURCE_DIR REFERENCE_FMUS_REPOSITORY
        REFERENCE_FMUS_VERSION Patch_EXECUTABLE)
    if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
        list(APPEND fixture_configure_args "-D${variable}=${${variable}}")
    endif()
endforeach()

add_custom_target(build-test-fmus
    COMMAND "${CMAKE_COMMAND}" ${fixture_configure_args}
    COMMAND "${CMAKE_COMMAND}" --build "${fixture_build_dir}"
        --config "$<CONFIG>" --target build-test-fmus --parallel
    USES_TERMINAL
    VERBATIM)
