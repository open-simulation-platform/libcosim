# Compiles the SWIG-generated Java sources together with the hand-written ones.
# Invoked with -DJAVAC=... -DGENERATED_DIR=... -DSRC_DIR=... -DTEST_DIR=... -DCLASSES_DIR=...
file(GLOB_RECURSE sources "${GENERATED_DIR}/*.java")
foreach(dir IN ITEMS "${SRC_DIR}" "${TEST_DIR}")
    file(GLOB_RECURSE extra "${dir}/*.java")
    list(APPEND sources ${extra})
endforeach()
file(REMOVE_RECURSE "${CLASSES_DIR}")
file(MAKE_DIRECTORY "${CLASSES_DIR}")
execute_process(
    COMMAND "${JAVAC}" -encoding UTF-8 -d "${CLASSES_DIR}" ${sources}
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "javac failed")
endif()
