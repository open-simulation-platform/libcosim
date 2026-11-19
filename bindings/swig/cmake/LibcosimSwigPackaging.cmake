# Packaging helpers for the libcosim bindings.

# Directories to search for libcosim's runtime dependencies when bundling:
# libcosim's own directory plus the dependency directories Conan reports.
set(LIBCOSIM_SWIG_RUNTIME_DIRS "$<TARGET_FILE_DIR:${LIBCOSIM_TARGET}>")
if(DEFINED CONAN_RUNTIME_LIB_DIRS)
    list(APPEND LIBCOSIM_SWIG_RUNTIME_DIRS ${CONAN_RUNTIME_LIB_DIRS})
    if(WIN32)
        # Conan lists lib directories; DLLs live in the sibling bin directories.
        foreach(dir IN LISTS CONAN_RUNTIME_LIB_DIRS)
            get_filename_component(parent "${dir}" DIRECTORY)
            list(APPEND LIBCOSIM_SWIG_RUNTIME_DIRS "${parent}/bin")
        endforeach()
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(LIBCOSIM_SWIG_PATCHELF patchelf)
endif()

# Platform name used in package layouts.
if(WIN32)
    set(LIBCOSIM_SWIG_OS "windows")
    set(LIBCOSIM_SWIG_DOTNET_RID "win-x64")
elseif(APPLE)
    set(LIBCOSIM_SWIG_OS "macos")
    set(LIBCOSIM_SWIG_DOTNET_RID "osx-x64")
else()
    set(LIBCOSIM_SWIG_OS "linux")
    set(LIBCOSIM_SWIG_DOTNET_RID "linux-x64")
endif()
set(LIBCOSIM_SWIG_PLATFORM "${LIBCOSIM_SWIG_OS}-x86_64")

# libcosim_swig_install_with_runtime(<target> DESTINATION <dir> COMPONENT <c>)
#
# Installs <target> (a binding's native library) together with all shared
# libraries it needs that are not system libraries: libcosim and its
# dependencies (Boost, Xerces, FMI Library, ...). The result is a
# self-contained directory. On Linux every library gets RPATH $ORIGIN, so they
# find each other there. On Windows the Visual C++ runtime is expected to be
# installed on the system and is not bundled.
function(libcosim_swig_install_with_runtime target)
    cmake_parse_arguments(ARG "" "DESTINATION;COMPONENT" "" ${ARGN})
    if(NOT WIN32)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "\$ORIGIN")
    endif()
    # COMPONENT applies to the artifact kind right before it, so name it for each.
    install(TARGETS ${target}
        RUNTIME DESTINATION "${ARG_DESTINATION}" COMPONENT ${ARG_COMPONENT}
        LIBRARY DESTINATION "${ARG_DESTINATION}" COMPONENT ${ARG_COMPONENT})
    install(CODE "
        set(BUNDLE_TARGET_FILE \"$<TARGET_FILE:${target}>\")
        set(BUNDLE_DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${ARG_DESTINATION}\")
        set(BUNDLE_DIRS \"$<JOIN:${LIBCOSIM_SWIG_RUNTIME_DIRS},;>\")
        set(BUNDLE_PATCHELF \"${LIBCOSIM_SWIG_PATCHELF}\")
        include(\"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BundleRuntime.cmake\")
    " COMPONENT ${ARG_COMPONENT})
endfunction()
