# Julia package `Libcosim`, on the C library. Its ccall layer is generated
# from the C header (generate_julia.py).

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(generatedJl "${CMAKE_CURRENT_BINARY_DIR}/generated.jl")
add_custom_command(
    OUTPUT "${generatedJl}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/generate_julia.py"
            "${LIBCOSIM_SWIG_C_HEADER}" "${generatedJl}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_julia.py" "${LIBCOSIM_SWIG_C_HEADER}"
    COMMENT "Generating the Julia ccall layer")
add_custom_target(libcosim_julia ALL DEPENDS "${generatedJl}")
add_dependencies(libcosim_julia cosim_c_generate)

# A package directory in the build tree (the library path comes from
# LIBCOSIM_C_LIBRARY), used by the tests.
set(buildPackage "${CMAKE_CURRENT_BINARY_DIR}/Libcosim")
add_custom_command(TARGET libcosim_julia POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/package" "${buildPackage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${generatedJl}" "${buildPackage}/src/generated.jl")

if(LIBCOSIM_SWIG_INSTALL)
    install(DIRECTORY package/ DESTINATION julia/Libcosim COMPONENT julia)
    install(FILES "${generatedJl}" DESTINATION julia/Libcosim/src COMPONENT julia)
    libcosim_swig_install_with_runtime(cosim_c DESTINATION julia/Libcosim/lib COMPONENT julia)
endif()

if(LIBCOSIM_SWIG_TESTS)
    find_program(JULIA_EXECUTABLE julia)
    if(JULIA_EXECUTABLE)
        add_test(
            NAME swig_julia_package
            COMMAND "${JULIA_EXECUTABLE}" --startup-file=no --compiled-modules=no
                    "--project=${buildPackage}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/runtests.jl"
                    "${LIBCOSIM_SWIG_TEST_DATA_DIR}/msmi/OspSystemStructure.xml"
                    "${LIBCOSIM_SWIG_TEST_DATA_DIR}/fmi1/identity.fmu")
        set_tests_properties(swig_julia_package PROPERTIES
            ENVIRONMENT "LIBCOSIM_C_LIBRARY=$<TARGET_FILE:cosim_c>")
        libcosim_swig_test_runtime_path(swig_julia_package)
    endif()
endif()
