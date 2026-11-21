# Selects the FMI 3 and FMI 2 platform directory names used when packaging
# test fixture binaries for the current build platform.
#
# Defines the variables FMI3_TEST_PLATFORM and FMI2_TEST_PLATFORM.

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FMI3_TEST_PLATFORM "x86_64-windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(FMI3_TEST_PLATFORM "x86_64-linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(FMI3_TEST_PLATFORM "aarch64-linux")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(FMI3_TEST_PLATFORM "x86_64-darwin")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(FMI3_TEST_PLATFORM "aarch64-darwin")
else()
    message(FATAL_ERROR
        "No FMI 3 test platform tuple for ${CMAKE_SYSTEM_NAME} "
        "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FMI2_TEST_PLATFORM "win64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(FMI2_TEST_PLATFORM "linux64")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(FMI2_TEST_PLATFORM "darwin64")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(FMI2_TEST_PLATFORM "aarch64-darwin")
else()
    message(FATAL_ERROR
        "No FMI 2 test platform tuple for ${CMAKE_SYSTEM_NAME} "
        "${CMAKE_SYSTEM_PROCESSOR}")
endif()
