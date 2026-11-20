# Install-time script: copies the shared libraries a binding needs into the
# install directory, so the package is self-contained.
#
# Input variables: BUNDLE_TARGET_FILE, BUNDLE_DESTINATION, BUNDLE_DIRS (list),
# BUNDLE_PATCHELF (Linux; may be empty), BUNDLE_EXECUTABLES (programs
# installed next to the libraries, e.g. proxyfmu; their libraries are
# bundled too).
#
# CMake resolves a library to the system copy if one exists (ldconfig paths
# come before DIRECTORIES), even when the binding was built against a copy
# from a dependency directory (Conan). So every resolved library is checked:
# if a library with the same file name exists in BUNDLE_DIRS, that copy is
# bundled; other system libraries (C/C++ runtime, ...) are left out. The same
# goes for a library found in more than one place.

set(executablesArgument "")
if(BUNDLE_EXECUTABLES)
    set(executablesArgument EXECUTABLES ${BUNDLE_EXECUTABLES})
endif()
file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${BUNDLE_TARGET_FILE}"
    ${executablesArgument}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicting
    DIRECTORIES ${BUNDLE_DIRS}
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^python[0-9]+\\.dll$" "^libpython")

# A library found in several places, e.g. libcrypto.so.3 from Conan and from
# the system: take it like any other resolved library, so that the copy in
# BUNDLE_DIRS is chosen below.
foreach(name IN LISTS conflicting_FILENAMES)
    list(GET conflicting_${name} 0 first)
    list(APPEND resolved "${first}")
endforeach()

set(bundled "")
foreach(lib IN LISTS resolved)
    get_filename_component(name "${lib}" NAME)
    set(source "")
    foreach(dir IN LISTS BUNDLE_DIRS)
        if(EXISTS "${dir}/${name}")
            set(source "${dir}/${name}")
            break()
        endif()
    endforeach()
    if(NOT source)
        string(TOLOWER "${lib}" lower)
        if(lower MATCHES "system32|^/lib/|^/lib64/|^/usr/lib/|^/usr/lib64/")
            continue() # a genuine system library
        endif()
        set(source "${lib}")
    endif()
    # Copy only the file name the loader asks for (the soname), with the
    # contents of the real file: packages such as wheels store symlinks as
    # copies, so a symlink chain would store each library several times.
    file(REAL_PATH "${source}" real)
    file(MAKE_DIRECTORY "${BUNDLE_DESTINATION}")
    file(COPY_FILE "${real}" "${BUNDLE_DESTINATION}/${name}")
    message(STATUS "Installing: ${BUNDLE_DESTINATION}/${name}")
    list(APPEND bundled "${name}")
endforeach()

if(unresolved)
    message(WARNING "Unresolved runtime dependencies of ${BUNDLE_TARGET_FILE}: ${unresolved}")
endif()
message(STATUS "Bundled with ${BUNDLE_TARGET_FILE}: ${bundled}")

if(BUNDLE_PATCHELF AND EXISTS "${BUNDLE_PATCHELF}")
    file(GLOB libs "${BUNDLE_DESTINATION}/*.so*")
    foreach(exe IN LISTS BUNDLE_EXECUTABLES)
        get_filename_component(name "${exe}" NAME)
        list(APPEND libs "${BUNDLE_DESTINATION}/${name}")
    endforeach()
    foreach(lib IN LISTS libs)
        if(NOT IS_SYMLINK "${lib}")
            execute_process(COMMAND "${BUNDLE_PATCHELF}" --set-rpath "$ORIGIN" "${lib}"
                RESULT_VARIABLE rc)
            if(NOT rc EQUAL 0)
                message(WARNING "patchelf failed on ${lib}")
            endif()
        endif()
    endforeach()
endif()
