# C bindings generated with SWIG's experimental C backend.
#
# UseSWIG does not know the C backend's output layout, so we drive SWIG
# directly: it produces a C++ wrapper source (compiled into cosim_c) and a
# pure C header for consumers.

set(genDir "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(wrapSrc "${genDir}/cosim_c_wrap.cxx")
set(wrapHdr "${genDir}/cosim_c_wrap.h")
file(MAKE_DIRECTORY "${genDir}")

add_custom_command(
    OUTPUT "${wrapSrc}" "${wrapHdr}"
    COMMAND "${CMAKE_COMMAND}" -E env "SWIG_LIB=${SWIG_DIR}" "${SWIG_EXECUTABLE}" -c -c++ -nocxx -namespace cosim ${LIBCOSIM_SWIG_FLAGS}
            "-I${LIBCOSIM_INCLUDE_DIR}"
            -outdir "${genDir}"
            -o "${wrapSrc}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cosim_c.i"
    DEPENDS cosim_c.i "${LIBCOSIM_SWIG_COMMON_DIR}/cosim.i"
    COMMENT "Generating C bindings with SWIG"
    VERBATIM)
add_custom_target(cosim_c_generate DEPENDS "${wrapSrc}" "${wrapHdr}")

add_library(cosim_c SHARED "${wrapSrc}")
target_include_directories(cosim_c PUBLIC "$<BUILD_INTERFACE:${genDir}>")
target_link_libraries(cosim_c PRIVATE ${LIBCOSIM_TARGET})
set_target_properties(cosim_c PROPERTIES COMPILE_OPTIONS "${LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS}")
if(WIN32)
    # The C API has no export macros; export everything from the wrapper.
    set_target_properties(cosim_c PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
set(LIBCOSIM_SWIG_C_HEADER "${wrapHdr}" PARENT_SCOPE)

if(LIBCOSIM_SWIG_INSTALL)
    # The C library package (Conan): library + header. Dependencies come from
    # the package manager, so they are not bundled.
    install(TARGETS cosim_c
        RUNTIME DESTINATION bin COMPONENT c
        LIBRARY DESTINATION lib COMPONENT c
        ARCHIVE DESTINATION lib COMPONENT c)
    install(FILES "${wrapHdr}" DESTINATION include RENAME cosim_c.h COMPONENT c)
endif()

if(LIBCOSIM_SWIG_TESTS)
    # Smoke test: a plain C program that uses only the generated header.
    add_executable(cosim_c_smoke smoke_test.c)
    target_link_libraries(cosim_c_smoke PRIVATE cosim_c)
    set_target_properties(cosim_c_smoke PROPERTIES LINKER_LANGUAGE C C_STANDARD 11)
    if(NOT WIN32)
        # libcosim's own dependencies live in other (Conan) directories; the
        # linker resolves them through the RPATH.
        set_property(TARGET cosim_c_smoke APPEND PROPERTY BUILD_RPATH ${LIBCOSIM_SWIG_RUNTIME_DIRS})
    endif()
    if(MSVC)
        target_compile_options(cosim_c_smoke PRIVATE "/WX-")
    endif()
    add_test(
        NAME swig_c_smoke
        COMMAND cosim_c_smoke "${LIBCOSIM_SWIG_TEST_DATA_DIR}/msmi/OspSystemStructure.xml")
    set_tests_properties(swig_c_smoke PROPERTIES
        ENVIRONMENT "IDENTITY_FMU=${LIBCOSIM_SWIG_TEST_DATA_DIR}/fmi1/identity.fmu;LIBCOSIM_TEST_DATA=${LIBCOSIM_SWIG_TEST_DATA_DIR}")
    libcosim_swig_test_runtime_path(swig_c_smoke)

    # C API drift check: the generated header must match the committed
    # reference, so any change to the C API (from libcosim, the interface
    # files, or a SWIG upgrade) is a deliberate, reviewed change. After an
    # intended change, run the update_swig_c_api_reference target and commit.
    set(referenceHdr "${CMAKE_CURRENT_SOURCE_DIR}/reference/cosim_c_wrap.h")
    add_test(
        NAME swig_c_api_reference
        COMMAND "${CMAKE_COMMAND}" -E compare_files --ignore-eol "${wrapHdr}" "${referenceHdr}")
    add_custom_target(update_swig_c_api_reference
        COMMAND "${CMAKE_COMMAND}" -E copy "${wrapHdr}" "${referenceHdr}"
        DEPENDS "${wrapHdr}"
        COMMENT "Updating the reference C API header")

    # Julia directly on the C library (only if Julia is installed).
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
endif()
