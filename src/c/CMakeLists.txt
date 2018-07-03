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

add_library(csecorec ${allSources})
target_compile_features(csecorec PRIVATE "cxx_std_17")
target_include_directories(csecorec PUBLIC "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
target_link_libraries(csecorec PUBLIC csecorecpp)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(csecorec PRIVATE "CSECOREC_DLL_EXPORT")
else()
    target_compile_definitions(csecorec PUBLIC "CSECOREC_STATIC")
    if(WIN32)
        set_target_properties(csecorec PROPERTIES OUTPUT_NAME "libcsecorec")
    endif()
endif()

install(
    TARGETS csecorec
    EXPORT "${CSECORE_EXPORT_TARGET}"
    ${CSECORE_INSTALL_DESTINATIONS}
)
install(
    FILES ${publicHeadersFull}
    DESTINATION "${CSECORE_HEADER_INSTALL_DIR}"
)
