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
set_target_properties(cosim_c PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")

# Smoke test: a plain C program that uses only the generated header.
add_executable(cosim_c_smoke smoke_test.c)
target_link_libraries(cosim_c_smoke PRIVATE cosim_c)
set_target_properties(cosim_c_smoke PROPERTIES LINKER_LANGUAGE C C_STANDARD 11)
