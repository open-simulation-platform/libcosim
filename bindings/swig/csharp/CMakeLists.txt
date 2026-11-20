# C# bindings (SWIG C# backend). Needs the .NET SDK (`dotnet`).

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)

set(genDir "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(csGenDir "${genDir}/cs")
set(testOutDir "${CMAKE_CURRENT_BINARY_DIR}/test")

set_property(SOURCE cosim_csharp.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE cosim_csharp.i PROPERTY SWIG_MODULE_NAME cosim)
set_property(SOURCE cosim_csharp.i PROPERTY COMPILE_OPTIONS
    -namespace Libcosim -dllimport cosim_csharp ${LIBCOSIM_SWIG_FLAGS})
set_property(SOURCE cosim_csharp.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)
set_property(SOURCE cosim_csharp.i PROPERTY DEPENDS "${LIBCOSIM_SWIG_COMMON_DIR}/cosim.i")

swig_add_library(cosim_csharp
    TYPE SHARED
    LANGUAGE csharp
    OUTPUT_DIR "${csGenDir}"
    OUTFILE_DIR "${genDir}"
    SOURCES cosim_csharp.i)
set_target_properties(cosim_csharp PROPERTIES
    COMPILE_OPTIONS "${LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS}")
target_include_directories(cosim_csharp PRIVATE "${LIBCOSIM_INCLUDE_DIR}")
target_link_libraries(cosim_csharp PRIVATE ${LIBCOSIM_TARGET})

# Build the C# sources (generated + hand-written + test) with dotnet.
add_custom_command(TARGET cosim_csharp POST_BUILD
    COMMAND "${DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/test/SmokeTest.csproj"
        --nologo -v quiet -c Release
        "-p:GeneratedDir=${csGenDir}"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/obj/"
        -o "${testOutDir}"
    COMMENT "Building C# sources")

if(LIBCOSIM_SWIG_INSTALL)
    # Libcosim.<version>.nupkg, with the native libraries for this platform
    # under runtimes/<rid>/native/. The staging directory (csharp/stage) can be
    # extended with other platforms' runtimes/ and packed again.
    set(stage "csharp/stage")
    install(DIRECTORY "${csGenDir}/" DESTINATION "${stage}/gen" COMPONENT csharp)
    install(DIRECTORY src/ DESTINATION "${stage}/src" COMPONENT csharp)
    install(FILES package/Libcosim.csproj DESTINATION "${stage}" COMPONENT csharp)
    libcosim_swig_install_with_runtime(cosim_csharp
        DESTINATION "${stage}/runtimes/${LIBCOSIM_SWIG_DOTNET_RID}/native" COMPONENT csharp)
    install(CODE "
        execute_process(
            COMMAND \"${DOTNET_EXECUTABLE}\" pack \"\${CMAKE_INSTALL_PREFIX}/${stage}/Libcosim.csproj\"
                --nologo -v quiet -c Release
                -p:LibcosimVersion=${LIBCOSIM_SWIG_VERSION}
                -o \"\${CMAKE_INSTALL_PREFIX}/csharp\"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"dotnet pack failed\")
        endif()
        message(STATUS \"Created \${CMAKE_INSTALL_PREFIX}/csharp/Libcosim.${LIBCOSIM_SWIG_VERSION}.nupkg\")
    " COMPONENT csharp)
endif()

if(NOT LIBCOSIM_SWIG_TESTS)
    return()
endif()

add_test(
    NAME swig_csharp_smoke
    COMMAND "${DOTNET_EXECUTABLE}" "${testOutDir}/SmokeTest.dll"
        "${LIBCOSIM_SWIG_TEST_DATA_DIR}/msmi/OspSystemStructure.xml"
        "${LIBCOSIM_SWIG_TEST_DATA_DIR}/fmi1/identity.fmu")
libcosim_swig_test_runtime_path(swig_csharp_smoke)
