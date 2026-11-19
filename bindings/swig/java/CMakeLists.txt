# Java bindings (SWIG Java backend). The JDK comes from Conan
# (zulu-openjdk, option swig_java) or the system.

find_package(Java 21 REQUIRED COMPONENTS Development Runtime)
find_package(JNI REQUIRED)

set(genDir "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(javaGenDir "${genDir}/java/libcosim")
set(classesDir "${CMAKE_CURRENT_BINARY_DIR}/classes")

set_property(SOURCE cosim_java.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE cosim_java.i PROPERTY SWIG_MODULE_NAME cosim)
set_property(SOURCE cosim_java.i PROPERTY COMPILE_OPTIONS -package libcosim ${LIBCOSIM_SWIG_FLAGS})
set_property(SOURCE cosim_java.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)
set_property(SOURCE cosim_java.i PROPERTY DEPENDS "${LIBCOSIM_SWIG_COMMON_DIR}/cosim.i")

swig_add_library(cosim_java
    TYPE SHARED
    LANGUAGE java
    OUTPUT_DIR "${javaGenDir}"
    OUTFILE_DIR "${genDir}"
    SOURCES cosim_java.i)
set_target_properties(cosim_java PROPERTIES
    COMPILE_OPTIONS "${LIBCOSIM_SWIG_GENERATED_CODE_OPTIONS}")
target_include_directories(cosim_java PRIVATE "${CMAKE_SOURCE_DIR}/include" ${JNI_INCLUDE_DIRS})
target_link_libraries(cosim_java PRIVATE cosim)

# Compile the generated and hand-written Java sources (the generated file
# names are only known after SWIG has run).
add_custom_command(TARGET cosim_java POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DJAVAC=${Java_JAVAC_EXECUTABLE}"
        "-DGENERATED_DIR=${javaGenDir}"
        "-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src"
        "-DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test"
        "-DCLASSES_DIR=${classesDir}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compile_java.cmake"
    COMMENT "Compiling Java sources")

add_test(
    NAME swig_java_smoke
    COMMAND "${Java_JAVA_EXECUTABLE}"
        "-Djava.library.path=$<TARGET_FILE_DIR:cosim_java>"
        -cp "${classesDir}"
        libcosim.SmokeTest
        "${LIBCOSIM_SWIG_TEST_DATA_DIR}/msmi/OspSystemStructure.xml"
        "${LIBCOSIM_SWIG_TEST_DATA_DIR}/fmi1/identity.fmu")
libcosim_swig_test_runtime_path(swig_java_smoke)
