# C bindings generated with SWIG's experimental C backend.
#
# UseSWIG does not know the C backend's output layout, so we drive SWIG
# directly: it produces a C++ wrapper source (compiled into libcosim_c) and a
# pure C header for consumers.

set(genDir "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(wrapSrc "${genDir}/cosim_c_wrap.cxx")
set(wrapHdr "${genDir}/cosim_c_wrap.h")
file(MAKE_DIRECTORY "${genDir}")

add_custom_command(
    OUTPUT "${wrapSrc}" "${wrapHdr}"
    COMMAND "${SWIG_EXECUTABLE}" -c -c++ -nocxx -namespace cosim ${LIBCOSIM_SWIG_FLAGS}
            -I${CMAKE_SOURCE_DIR}/include
            -outdir "${genDir}"
            -o "${wrapSrc}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cosim_c.i"
    DEPENDS cosim_c.i "${LIBCOSIM_SWIG_COMMON_DIR}/cosim.i"
    COMMENT "Generating C bindings with SWIG"
    VERBATIM)

add_library(cosim_c SHARED "${wrapSrc}")
target_include_directories(cosim_c PUBLIC "${genDir}")
target_link_libraries(cosim_c PRIVATE cosim)
set_target_properties(cosim_c PROPERTIES COMPILE_OPTIONS "${LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS}")

# Smoke test: a plain C program that uses only the generated header.
add_executable(cosim_c_smoke smoke_test.c)
target_link_libraries(cosim_c_smoke PRIVATE cosim_c)
set_target_properties(cosim_c_smoke PROPERTIES LINKER_LANGUAGE C C_STANDARD 11)
if(MSVC)
    target_compile_options(cosim_c_smoke PRIVATE "/WX-")
endif()

add_test(
    NAME swig_c_smoke
    COMMAND cosim_c_smoke "${LIBCOSIM_SWIG_TEST_DATA_DIR}/msmi/OspSystemStructure.xml")
set_tests_properties(swig_c_smoke PROPERTIES
    ENVIRONMENT "IDENTITY_FMU=${LIBCOSIM_SWIG_TEST_DATA_DIR}/fmi1/identity.fmu")

# C API drift check: the generated header must match the committed reference,
# so any change to the C API (from libcosim, the interface files, or a SWIG
# upgrade) is a deliberate, reviewed change. After an intended change, run
# the update_swig_c_api_reference target and commit the result.
set(referenceHdr "${CMAKE_CURRENT_SOURCE_DIR}/reference/cosim_c_wrap.h")
add_test(
    NAME swig_c_api_reference
    COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${wrapHdr}" "${referenceHdr}")
add_custom_target(update_swig_c_api_reference
    COMMAND "${CMAKE_COMMAND}" -E copy "${wrapHdr}" "${referenceHdr}"
    DEPENDS "${wrapHdr}"
    COMMENT "Updating the reference C API header")

# Julia on the C library (only if Julia is installed).
find_program(JULIA_EXECUTABLE julia)
if(JULIA_EXECUTABLE)
    add_test(
        NAME swig_julia_smoke
        COMMAND "${JULIA_EXECUTABLE}" --startup-file=no
                "${CMAKE_CURRENT_SOURCE_DIR}/../julia/smoke_test.jl"
                "$<TARGET_FILE:cosim_c>"
                "${LIBCOSIM_SWIG_TEST_DATA_DIR}/msmi/OspSystemStructure.xml")
    libcosim_swig_test_runtime_path(swig_julia_smoke)
endif()
