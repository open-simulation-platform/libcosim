# Runs SWIG for one target language and fails if SWIG had to rename an
# identifier because it is a keyword in that language (warning 314). Such a
# rename gives the binding a language-specific name; the fix is a shared
# %rename in cosim.i (see the proposal, section 12.8).
#
# cmake -DSWIG_EXECUTABLE=... -DSWIG_DIR=... -DINTERFACE=<.i> -DOUTDIR=<dir>
#       -DSTAMP=<file> "-DARGS=<swig args>" -P CheckSwigKeywords.cmake
file(REMOVE_RECURSE "${OUTDIR}")
file(MAKE_DIRECTORY "${OUTDIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SWIG_LIB=${SWIG_DIR}"
            "${SWIG_EXECUTABLE}" ${ARGS} -outdir "${OUTDIR}" -o "${OUTDIR}/wrap.cxx" "${INTERFACE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "SWIG failed on ${INTERFACE}:\n${out}")
endif()
string(REGEX MATCHALL "[^\n]*Warning 314[^\n]*" hits "${out}")
if(hits)
    list(JOIN hits "\n" hits)
    message(FATAL_ERROR
        "SWIG renamed identifiers that are keywords in the target language "
        "(${INTERFACE}). Add a shared %rename in cosim.i:\n${hits}")
endif()
file(REMOVE_RECURSE "${OUTDIR}")
file(WRITE "${STAMP}" "")
