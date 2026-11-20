# In a wheel build (scikit-build-core), find exactly the building interpreter.
if(SKBUILD)
    set(Python3_EXECUTABLE "${PYTHON_EXECUTABLE}")
endif()

# Stable ABI: build the module against Python's limited API, so that one
# module (an abi3 wheel) works with this Python version and all later ones.
# A wheel build with `wheel.py-api = "cp3X"` (pyproject.toml) sets it.
set(LIBCOSIM_SWIG_PYTHON_SABI "" CACHE STRING
    "Build the Python module for the stable ABI of this and later Python versions (e.g. 3.10); empty: for the building Python only")
if(SKBUILD_SABI_VERSION)
    set(LIBCOSIM_SWIG_PYTHON_SABI "${SKBUILD_SABI_VERSION}")
endif()
if(LIBCOSIM_SWIG_PYTHON_SABI)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development.SABIModule)
    set(pythonModuleTarget Python3::SABIModule)
    string(REPLACE "." ";" sabi "${LIBCOSIM_SWIG_PYTHON_SABI}")
    list(GET sabi 0 sabiMajor)
    list(GET sabi 1 sabiMinor)
    math(EXPR pyLimitedApi "(${sabiMajor} << 24) | (${sabiMinor} << 16)" OUTPUT_FORMAT HEXADECIMAL)
    message(STATUS "Python bindings: stable ABI, Python >= ${LIBCOSIM_SWIG_PYTHON_SABI} (Py_LIMITED_API=${pyLimitedApi})")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
    set(pythonModuleTarget Python3::Module)
endif()

set(outDir "${CMAKE_BINARY_DIR}/bindings/python/libcosim")

set_property(SOURCE cosim_python.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE cosim_python.i PROPERTY SWIG_MODULE_NAME cosim)
set_property(SOURCE cosim_python.i PROPERTY COMPILE_OPTIONS ${LIBCOSIM_SWIG_FLAGS})
set_property(SOURCE cosim_python.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)
set_property(SOURCE cosim_python.i PROPERTY DEPENDS "${LIBCOSIM_SWIG_COMMON_DIR}/cosim.i")

swig_add_library(cosim_python
    TYPE MODULE
    LANGUAGE python
    OUTPUT_DIR "${outDir}"
    OUTFILE_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen"
    SOURCES cosim_python.i)

set_target_properties(cosim_python PROPERTIES
    OUTPUT_NAME "cosim"   # UseSWIG adds the "_" prefix -> _cosim.so
    LIBRARY_OUTPUT_DIRECTORY "${outDir}"
    COMPILE_OPTIONS "${LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS}"
    INSTALL_RPATH "$ORIGIN")
target_include_directories(cosim_python PRIVATE "${LIBCOSIM_INCLUDE_DIR}")
target_link_libraries(cosim_python PRIVATE ${LIBCOSIM_TARGET} ${pythonModuleTarget})
if(LIBCOSIM_SWIG_PYTHON_SABI)
    target_compile_definitions(cosim_python PRIVATE "Py_LIMITED_API=${pyLimitedApi}")
endif()

# libcosim sets per-configuration output directories globally; override them.
# On Windows the module is a DLL (_cosim.pyd) whose import library would be
# named cosim.lib and clash with libcosim's own cosim.lib, so it goes to a
# separate directory (it is never used).
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfgU)
    set_target_properties(cosim_python PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${cfgU} "${outDir}"
        RUNTIME_OUTPUT_DIRECTORY_${cfgU} "${outDir}"
        ARCHIVE_OUTPUT_DIRECTORY_${cfgU} "${CMAKE_CURRENT_BINARY_DIR}/importlib")
endforeach()

# Package files
configure_file(package_init.py "${outDir}/__init__.py" COPYONLY)

if(LIBCOSIM_SWIG_INSTALL)
    # Python package `libcosim` (installed into the wheel's site-packages by
    # scikit-build-core): module, proxy, __init__, and the bundled libraries.
    libcosim_swig_install_with_runtime(cosim_python DESTINATION libcosim COMPONENT python)
    install(FILES "${outDir}/cosim.py" DESTINATION libcosim COMPONENT python)
    install(FILES package_init.py DESTINATION libcosim RENAME __init__.py COMPONENT python)
endif()

if(NOT LIBCOSIM_SWIG_TESTS)
    return()
endif()

add_test(
    NAME swig_python
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bindings.py")
set_tests_properties(swig_python PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/bindings/python")
libcosim_swig_test_runtime_path(swig_python)

# Optional: run libcosimpy's own test suite against the libcosimpy
# compatibility layer (compat/libcosimpy). Set LIBCOSIM_SWIG_LIBCOSIMPY_DIR to a
# libcosimpy checkout; requires pytest.
set(LIBCOSIM_SWIG_LIBCOSIMPY_DIR "" CACHE PATH "libcosimpy checkout whose tests run against the compatibility layer")
if(LIBCOSIM_SWIG_LIBCOSIMPY_DIR)
    add_test(
        NAME swig_libcosimpy_compat
        COMMAND Python3::Interpreter -m pytest -q -p no:cacheprovider tests
            # Depends on std::unordered_map iteration order in libcosimc,
            # which is implementation-defined (see README).
            -k "not test_from_ssp_file_with_step_size_float"
        WORKING_DIRECTORY "${LIBCOSIM_SWIG_LIBCOSIMPY_DIR}")
    if(WIN32)
        set(pathSep "\\;") # escaped: ';' also separates CTest environment entries
    else()
        set(pathSep ":")
    endif()
    set_tests_properties(swig_libcosimpy_compat PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/compat${pathSep}${CMAKE_BINARY_DIR}/bindings/python;LIBCOSIM_DLL_DIRS=$<TARGET_FILE_DIR:${LIBCOSIM_TARGET}>")
endif()
