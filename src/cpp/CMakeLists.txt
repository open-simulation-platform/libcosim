set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include/cse")
set(publicHeaders
    "config.hpp"
    "hello_world.hpp"
)
set(sources
    "hello_world.cpp"
)

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${sources})

function(configure_target target)
    target_compile_features(${target} PUBLIC "cxx_std_17")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
    target_link_libraries(${target} PUBLIC gsl)
endfunction()

if(CSECORE_BUILD_STATIC)
    set(target "cpp-static")
    add_library("${target}" STATIC ${allSources})
    configure_target("${target}")
    target_compile_definitions("${target}" PUBLIC "CSECORE_STATIC")
    if(WIN32)
        set_target_properties("${target}" PROPERTIES OUTPUT_NAME "libcsecorecpp")
    else()
        set_target_properties("${target}" PROPERTIES OUTPUT_NAME "csecorecpp")
    endif()
    install(
        TARGETS "${target}"
        EXPORT "${CSECORE_EXPORT_TARGET}"
        ${CSECORE_INSTALL_DESTINATIONS}
    )
endif()

if(CSECORE_BUILD_SHARED)
    set(target "cpp-shared")
    add_library("${target}" SHARED ${allSources})
    configure_target("${target}")
    target_compile_definitions("${target}" PRIVATE "CSECORE_DLL_EXPORT")
    set_target_properties("${target}" PROPERTIES OUTPUT_NAME "csecorecpp")
    install(
        TARGETS "${target}"
        EXPORT "${CSECORE_EXPORT_TARGET}"
        ${CSECORE_INSTALL_DESTINATIONS}
    )
endif()

install(
    DIRECTORY "${publicHeaderDir}"
    DESTINATION "${CSECORE_HEADER_INSTALL_DIR}"
)
