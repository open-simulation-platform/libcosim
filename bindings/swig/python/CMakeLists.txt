find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

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
    # Generated code is not held to libcosim's warning standards.
    COMPILE_OPTIONS "-Wno-error;-w"
    INSTALL_RPATH "$ORIGIN")
target_include_directories(cosim_python PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(cosim_python PRIVATE cosim Python3::Module)

# libcosim sets per-configuration output directories globally; override them.
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfgU)
    set_target_properties(cosim_python PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${cfgU} "${outDir}")
endforeach()

# Package files
configure_file(package_init.py "${outDir}/__init__.py" COPYONLY)
