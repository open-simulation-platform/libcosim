cmake_minimum_required(VERSION 3.9)
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" projectVersion)
project("libcosim" VERSION ${projectVersion})
message("Current libcosim version: ${CMAKE_PROJECT_VERSION}\n")

# ==============================================================================
# Build settings
# ==============================================================================

option(BUILD_SHARED_LIBS "Build shared libraries instead of static libraries" ON)
option(LIBCOSIM_TREAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(LIBCOSIM_BUILD_TESTS "Build test suite" ON)
option(LIBCOSIM_BUILD_APIDOC "Build API documentation (requires Doxygen)" ON)
option(LIBCOSIM_BUILD_PRIVATE_APIDOC "Build private API documentation (only used if LIBCOSIM_BUILD_APIDOC=ON)" OFF)
option(LIBCOSIM_STANDALONE_INSTALLATION "Whether to build for a standalone installation (Linux only; sets a relative RPATH)" OFF)
option(LIBCOSIM_USING_CONAN "Whether Conan is used for package management" OFF)
option(LIBCOSIM_USING_CONAN_AUTO_CONFIG "Whether Conan should be configured automatically by CMake" OFF)
option(LIBCOSIM_WITH_PROXYFMU "Whether or not to build with proxy-fmu integration" OFF)
option(LIBCOSIM_NO_FMI_LOGGING "Disable FMI logging during simulation" OFF)

# ==============================================================================
# Global internal configuration
# ==============================================================================

# Configure logging in the fmi callback function
if(LIBCOSIM_NO_FMI_LOGGING)
    add_compile_definitions(LIBCOSIM_NO_FMI_LOGGING=1)
endif()

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
    if(LIBCOSIM_TREAT_WARNINGS_AS_ERRORS)
        add_compile_options("-Werror")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options("/W4")
    if(CMAKE_GENERATOR MATCHES "NMake Makefiles.*")
        string(REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    endif()
    add_compile_options("/wd4251" "/wd4996")
    if(LIBCOSIM_TREAT_WARNINGS_AS_ERRORS)
        add_compile_options("/WX")
    endif()
    add_definitions("-D_SCL_SECURE_NO_WARNINGS" "-D_CRT_SECURE_NO_WARNINGS")
endif()

# Automatically export all symbols in Windows DLLs.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Suppress warnings for using boost/1.81.0 (will be removed with the future release of cmake)
set(Boost_NO_WARN_NEW_VERSIONS ON)

# Organise projects in folders in IDEs that support it
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Prepare for export and installation.
set(LIBCOSIM_HEADER_INSTALL_DIR "include")
if(WIN32)
    set(LIBCOSIM_CMAKE_INSTALL_DIR "cmake")
    set(LIBCOSIM_DOC_INSTALL_DIR   "doc")
else()
    set(LIBCOSIM_CMAKE_INSTALL_DIR "share/${PROJECT_NAME}/cmake")
    set(LIBCOSIM_DOC_INSTALL_DIR   "share/doc/${PROJECT_NAME}")
endif()
set(LIBCOSIM_INSTALL_DESTINATIONS
    ARCHIVE DESTINATION "lib"
    LIBRARY DESTINATION "lib"
    RUNTIME DESTINATION "bin"
    INCLUDES DESTINATION "${LIBCOSIM_HEADER_INSTALL_DIR}")
set(LIBCOSIM_EXPORT_TARGET "${PROJECT_NAME}-targets")

# ==============================================================================
# Dependencies
# ==============================================================================

if(LIBCOSIM_USING_CONAN)
    if(NOT LIBCOSIM_USING_CONAN_AUTO_CONFIG OR CONAN_EXPORTED)
        # Opting for manual invocation of conan install prior to loading CMake
        # or conan create has been invoked, setting CONAN_EXPORTED.
        if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
            include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
            conan_basic_setup(NO_OUTPUT_DIRS)
        else()
            message(FATAL_ERROR "The file conanbuildinfo.cmake doesn't exist, you have to run conan install first")
        endif()
    else()
        # Auto config specified and conan create is not running.
        # We'll let CMake run the conan commands for us.

        # This is where the magic happens.
        # The file has been modified to search for the conan executable on linux systems.
        include(cmake/conan.cmake)
        conan_cmake_autodetect(settings)

        set(proxyfmu_option False)
        if(LIBCOSIM_WITH_PROXYFMU)
            set(proxyfmu_option True)
        endif()

        # Force add osp remote
        conan_add_remote(NAME osp URL https://osp.jfrog.io/artifactory/api/conan/conan-local)

        # Run conan install with auto-detected settings and -b missing flag
        # and -o proxyfmu=True if LIBCOSIM_WITH_PROXYFMU=ON
        conan_cmake_run(
                CONANFILE conanfile.py
                OPTIONS proxyfmu=${proxyfmu_option}
                BUILD missing
                SETTINGS ${settings}
                BASIC_SETUP NO_OUTPUT_DIRS)
    endif()
endif()

set(Boost_components date_time log)
if (LIBCOSIM_BUILD_TESTS)
    list(APPEND Boost_components timer unit_test_framework)
endif()
if(NOT BUILD_SHARED_LIBS AND NOT DEFINED Boost_USE_STATIC_LIBS)
    set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost REQUIRED COMPONENTS ${Boost_components})

if(BUILD_SHARED_LIBS AND NOT DEFINED FMILibrary_USE_SHARED_LIB)
    set(FMILibrary_USE_SHARED_LIB TRUE)
endif()
find_package(FMILibrary REQUIRED)
find_package(LIBZIP REQUIRED)
find_package(MS_GSL REQUIRED)
find_package(YAML_CPP REQUIRED)
find_package(XercesC REQUIRED)
if(LIBCOSIM_WITH_PROXYFMU)
    find_package(PROXYFMU CONFIG REQUIRED)
endif()

# ==============================================================================
# Targets
# ==============================================================================

add_subdirectory("src")
if(LIBCOSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()

# ==============================================================================
# API documentation
# ==============================================================================

if(LIBCOSIM_BUILD_APIDOC)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_PROJECT_NAME "libcosim")
    set(DOXYGEN_PROJECT_BRIEF "C++ library for distributed co-simulation")
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
    set(doxygenInputs "${CMAKE_SOURCE_DIR}/docs" "${CMAKE_SOURCE_DIR}/include")
    if(LIBCOSIM_BUILD_PRIVATE_APIDOC)
        list(APPEND doxygenInputs
            "${CMAKE_SOURCE_DIR}/src/cpp"
        )
        list(APPEND DOXYGEN_STRIP_FROM_INC_PATH
            "${CMAKE_SOURCE_DIR}/src/cpp"
        )
        list(APPEND DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}")
    endif()
    doxygen_add_docs(doc ${doxygenInputs})
    add_custom_target(
        install-doc
        "${CMAKE_COMMAND}" "-E" "copy_directory"
            "${DOXYGEN_OUTPUT_DIRECTORY}"
            "${CMAKE_INSTALL_PREFIX}/${LIBCOSIM_DOC_INSTALL_DIR}/"
        DEPENDS doc
    )
endif()

# ==============================================================================
# Exports and remaining installation
# ==============================================================================

install(
    FILES "README.md" "LICENSE"
    DESTINATION "${LIBCOSIM_DOC_INSTALL_DIR}"
)
install(
    EXPORT "${LIBCOSIM_EXPORT_TARGET}"
    DESTINATION "${LIBCOSIM_CMAKE_INSTALL_DIR}"
    NAMESPACE "${PROJECT_NAME}::"
)

include(CMakePackageConfigHelpers)

# Generate and install package-config file.
set(configFile "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config.cmake")
set(targetsFile "${LIBCOSIM_CMAKE_INSTALL_DIR}/${LIBCOSIM_EXPORT_TARGET}.cmake")
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/project-config.cmake.in"
    "${configFile}"
    INSTALL_DESTINATION "${LIBCOSIM_CMAKE_INSTALL_DIR}"
    PATH_VARS targetsFile
)
install(FILES "${configFile}" DESTINATION "${LIBCOSIM_CMAKE_INSTALL_DIR}")

# Generate and install package-version file
set(versionFile "${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake")
write_basic_package_version_file(
    "${versionFile}"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY "SameMajorVersion")
install(FILES "${versionFile}" DESTINATION "${LIBCOSIM_CMAKE_INSTALL_DIR}")

# Install custom find modules
install(FILES
        "${CMAKE_SOURCE_DIR}/cmake/FindFMILibrary.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FindLIBZIP.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FindMS_GSL.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/FindYAML_CPP.cmake"
        DESTINATION
        "${LIBCOSIM_CMAKE_INSTALL_DIR}"
)
