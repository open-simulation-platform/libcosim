cmake_minimum_required(VERSION 3.9)
project("csecore" VERSION "0.4.0")

# ==============================================================================
# Build settings
# ==============================================================================

option(BUILD_SHARED_LIBS "Build shared libraries instead of static libraries" ON)
option(CSECORE_TREAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CSECORE_BUILD_TESTS "Build test suite" ON)
option(CSECORE_BUILD_APIDOC "Build API documentation" ON)
option(CSECORE_BUILD_PRIVATE_APIDOC "Build private API documentation" OFF)
option(CSECORE_STANDALONE_INSTALLATION "Whether to build for a standalone installation (Linux only; sets a relative RPATH)" OFF)
option(CSECORE_USING_CONAN "Whether Conan is used for package management" OFF)
option(CSECORE_WITH_FMUPROXY "Whether or not to build with fmuproxy integration" OFF)
option(CSECORE_TEST_FMUPROXY "Whether or not to run fmuproxy tests (only if CSECORE_WITH_FMUPROXY=ON)" ON)

# ==============================================================================
# Global internal configuration
# ==============================================================================

# Our custom CMake scripts go in the cmake/ subdirectory.
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake")

# Use a common output directory for all targets.
# The main purpose of this is to ensure that the DLLs and test executables
# end up in the same directory on Windows, so that the OS finds the former
# when running the latter.
if(CMAKE_CONFIGURATION_TYPES)
    set(configTypes ${CMAKE_CONFIGURATION_TYPES})
else()
    set(configTypes Debug Release RelWithDebInfo MinSizeRel)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
endif()
foreach(c IN LISTS configTypes)
    string(TOLOWER "${c}" configL)
    string(TOUPPER "${c}" configU)
    set(outputDir "${CMAKE_BINARY_DIR}/output/${configL}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${configU} "${outputDir}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${configU} "${outputDir}/lib")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${configU}     "${outputDir}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configU} "${outputDir}/bin")
endforeach()

# Use the highest warning levels and treat all warnings as errors,
# but ignore a few selected warnings.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options("-Wall" "-Wextra" "-Wpedantic")
    add_compile_options("-Wno-parentheses")
    if(CSECORE_TREAT_WARNINGS_AS_ERRORS)
        add_compile_options("-Werror")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options("/W4")
    add_compile_options("/wd4996")
    if(CSECORE_TREAT_WARNINGS_AS_ERRORS)
        add_compile_options("/WX")
    endif()
    if(CSECORE_USING_CONAN)
        # C4251: 'identifier' : class 'type' needs to have dll-interface to be
        #        used by clients of class 'type2'
        #
        # This could be a problem if a dependent DLL is compiled against a
        # different version or variant of the C++ runtime than the one we
        # use.  An example would be if Boost was built in release mode and
        # we are compiling in release mode. However, when we use Conan, this
        # should not be a problem, so we disable the warning.
        add_compile_options("/wd4251")
    endif()
    add_definitions("-D_SCL_SECURE_NO_WARNINGS" "-D_CRT_SECURE_NO_WARNINGS")
endif()

# Automatically export all symbols in Windows DLLs.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Organise projects in folders in IDEs that support it
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Prepare for export and installation.
set(CSECORE_HEADER_INSTALL_DIR "include")
if(WIN32)
    set(CSECORE_CMAKE_INSTALL_DIR "cmake")
    set(CSECORE_DOC_INSTALL_DIR   "doc")
else()
    set(CSECORE_CMAKE_INSTALL_DIR "share/${PROJECT_NAME}/cmake")
    set(CSECORE_DOC_INSTALL_DIR   "share/doc/${PROJECT_NAME}")
endif()
set(CSECORE_INSTALL_DESTINATIONS
    ARCHIVE DESTINATION "lib"
    LIBRARY DESTINATION "lib"
    RUNTIME DESTINATION "bin"
    INCLUDES DESTINATION "${CSECORE_HEADER_INSTALL_DIR}")
set(CSECORE_EXPORT_TARGET "${PROJECT_NAME}-targets")

# ==============================================================================
# Dependencies
# ==============================================================================

if(CSECORE_USING_CONAN)
    if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
        include("${CMAKE_BINARY_DIR}/conanbuildinfo.cmake")
        conan_basic_setup(NO_OUTPUT_DIRS)
    else()
        message(FATAL_ERROR "The file conanbuildinfo.cmake doesn't exist, you have to run conan install first")
    endif()
endif()

set(Boost_components date_time fiber filesystem log)
if (CSECORE_BUILD_TESTS)
    list(APPEND Boost_components timer unit_test_framework)
endif()

find_package(GSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS ${Boost_components})
find_package(FMILibrary REQUIRED)
find_package(LIBZIP REQUIRED)
find_package(nlohmann_json REQUIRED)
if(CSECORE_WITH_FMUPROXY)
    find_package(Thrift REQUIRED)
endif()

# ==============================================================================
# Targets
# ==============================================================================

add_subdirectory("src/cpp")
add_subdirectory("src/c")
if(CSECORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory("test/cpp")
    add_subdirectory("test/c")
endif()

# ==============================================================================
# API documentation
# ==============================================================================

if(CSECORE_BUILD_APIDOC)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_PROJECT_NAME "CSE core")
    set(DOXYGEN_PROJECT_BRIEF "C/C++ library for distributed co-simulation")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output/doc")
    set(DOXYGEN_GENERATE_LATEX "NO")
    set(DOXYGEN_RECURSIVE "YES")
    set(DOXYGEN_SORT_BY_SCOPE_NAME "YES")
    set(DOXYGEN_STRIP_FROM_INC_PATH "${CMAKE_SOURCE_DIR}/include")
    set(DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}/include")
    set(DOXYGEN_TAGFILES "${CMAKE_SOURCE_DIR}/cmake/cppreference-doxygen-web.tag.xml=http://en.cppreference.com/w/")
    set(DOXYGEN_JAVADOC_AUTOBRIEF "YES")
    set(DOXYGEN_QT_AUTOBRIEF "YES")
    set(DOXYGEN_MULTILINE_CPP_IS_BRIEF "YES")
    set(DOXYGEN_EXCLUDE_PATTERNS "*.cpp" "*.c")
    set(doxygenInputs "${CMAKE_SOURCE_DIR}/include")
    if(CSECORE_BUILD_PRIVATE_APIDOC)
        list(APPEND doxygenInputs
            "${CMAKE_SOURCE_DIR}/src/cpp"
            "${CMAKE_SOURCE_DIR}/src/c"
        )
        list(APPEND DOXYGEN_STRIP_FROM_INC_PATH
            "${CMAKE_SOURCE_DIR}/src/cpp"
            "${CMAKE_SOURCE_DIR}/src/c"
        )
        list(APPEND DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}")
    endif()
    doxygen_add_docs(doc ${doxygenInputs})
    add_custom_target(
        install-doc
        "${CMAKE_COMMAND}" "-E" "copy_directory"
            "${DOXYGEN_OUTPUT_DIRECTORY}"
            "${CMAKE_INSTALL_PREFIX}/${CSECORE_DOC_INSTALL_DIR}/"
        DEPENDS doc
    )
endif()

# ==============================================================================
# Exports and remaining installation
# ==============================================================================

install(
    FILES "README.md" "LICENCE.txt"
    DESTINATION "${CSECORE_DOC_INSTALL_DIR}"
)
install(
    EXPORT "${CSECORE_EXPORT_TARGET}"
    DESTINATION "${CSECORE_CMAKE_INSTALL_DIR}"
    NAMESPACE "${PROJECT_NAME}::"
)

include(CMakePackageConfigHelpers)

# Generate and install package-config file.
set(configFile "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config.cmake")
set(targetsFile "${CSECORE_CMAKE_INSTALL_DIR}/${CSECORE_EXPORT_TARGET}.cmake")
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/project-config.cmake.in"
    "${configFile}"
    INSTALL_DESTINATION "${CSECORE_CMAKE_INSTALL_DIR}"
    PATH_VARS targetsFile
)
install(FILES "${configFile}" DESTINATION "${CSECORE_CMAKE_INSTALL_DIR}")

# Generate and install package-version file
set(versionFile "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake")
write_basic_package_version_file(
    "${versionFile}"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY "SameMajorVersion")
install(FILES "${versionFile}" DESTINATION "${CSECORE_CMAKE_INSTALL_DIR}")

# Install custom find modules
install(FILES
        "${CMAKE_SOURCE_DIR}/cmake/FindFMILibrary.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FindLIBZIP.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FindGSL.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/Findnlohmann_json.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FindThrift.cmake"
        DESTINATION
        "${CSECORE_CMAKE_INSTALL_DIR}"
)
