get_filename_component(LIBCOSIM_DOCUMENTATION_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

find_package(Doxygen REQUIRED)
set(DOXYGEN_PROJECT_NAME "libcosim")
set(DOXYGEN_PROJECT_BRIEF "C++ library for distributed co-simulation")
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output/doc")
set(DOXYGEN_GENERATE_LATEX "NO")
set(DOXYGEN_GENERATE_XML "YES")
set(DOXYGEN_RECURSIVE "YES")
set(DOXYGEN_SORT_BY_SCOPE_NAME "YES")
set(DOXYGEN_STRIP_FROM_INC_PATH
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/include"
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/src/cosim")
set(DOXYGEN_STRIP_FROM_PATH
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/include"
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}")
set(DOXYGEN_TAGFILES
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/cmake/cppreference-doxygen-web.tag.xml=http://en.cppreference.com/w/")
set(DOXYGEN_JAVADOC_AUTOBRIEF "YES")
set(DOXYGEN_QT_AUTOBRIEF "YES")
set(DOXYGEN_MULTILINE_CPP_IS_BRIEF "YES")
set(DOXYGEN_EXCLUDE_PATTERNS "*.cpp" "*.c")
set(DOXYGEN_EXTRACT_PRIVATE "YES")
doxygen_add_docs(doxygen-doc
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/include"
    "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/src/cosim")
add_custom_target(clean-doxygen-output
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DOXYGEN_OUTPUT_DIRECTORY}"
    VERBATIM)
add_dependencies(doxygen-doc clean-doxygen-output)

set(LIBCOSIM_SPHINX_BUILD "" CACHE FILEPATH "Path to sphinx-build")
if(NOT LIBCOSIM_SPHINX_BUILD)
    find_program(LIBCOSIM_SPHINX_EXECUTABLE NAMES sphinx-build
        HINTS "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/.venv/Scripts"
              "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/.venv/bin")
else()
    set(LIBCOSIM_SPHINX_EXECUTABLE "${LIBCOSIM_SPHINX_BUILD}")
endif()
if(NOT LIBCOSIM_SPHINX_EXECUTABLE)
    message(FATAL_ERROR "sphinx-build not found; install docs/requirements.txt or set LIBCOSIM_SPHINX_BUILD")
endif()
set(sphinxOutput "${CMAKE_BINARY_DIR}/output/sphinx")
add_custom_target(sphinx-doc
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${sphinxOutput}"
    COMMAND "${CMAKE_COMMAND}" -E env
        "LIBCOSIM_DOXYGEN_XML=${DOXYGEN_OUTPUT_DIRECTORY}/xml"
        "${LIBCOSIM_SPHINX_EXECUTABLE}" -W --keep-going -b html
        "${LIBCOSIM_DOCUMENTATION_SOURCE_DIR}/docs" "${sphinxOutput}"
    DEPENDS doxygen-doc
    VERBATIM)
add_custom_target(doc ALL DEPENDS sphinx-doc)
