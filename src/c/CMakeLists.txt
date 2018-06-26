set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include")
set(publicHeaders
    "cse.h"
)
set(sources
    "cse.cpp"
)

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${sources})

function(configure_target target)
    target_compile_features(${target} PUBLIC "cxx_std_17")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
endfunction()

if(CSECORE_BUILD_STATIC)
    set(target "c-static")
    add_library("${target}" STATIC ${allSources})
    configure_target("${target}")
    target_compile_definitions("${target}" PUBLIC "CSECOREC_STATIC")
    target_link_libraries("${target}" PUBLIC cpp-static)
    if(WIN32)
        set_target_properties("${target}" PROPERTIES OUTPUT_NAME "libcsecorec")
    else()
        set_target_properties("${target}" PROPERTIES OUTPUT_NAME "csecorec")
    endif()
    install(
        TARGETS "${target}"
        EXPORT "${CSECORE_EXPORT_TARGET}"
        ${CSECORE_INSTALL_DESTINATIONS}
    )
endif()

if(CSECORE_BUILD_SHARED)
    set(target "c-shared")
    add_library("${target}" SHARED ${allSources})
    configure_target("${target}")
    target_compile_definitions("${target}" PRIVATE "CSECOREC_DLL_EXPORT")
    target_link_libraries("${target}" PUBLIC cpp-shared)
    set_target_properties("${target}" PROPERTIES OUTPUT_NAME "csecorec")
    install(
        TARGETS "${target}"
        EXPORT "${CSECORE_EXPORT_TARGET}"
        ${CSECORE_INSTALL_DESTINATIONS}
    )
endif()

install(
    FILES ${publicHeadersFull}
    DESTINATION "${CSECORE_HEADER_INSTALL_DIR}"
)
