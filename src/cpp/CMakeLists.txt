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

add_library(csecorecpp ${allSources})
target_compile_features(csecorecpp PUBLIC "cxx_std_17")
target_include_directories(csecorecpp PUBLIC "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
target_link_libraries(csecorecpp PUBLIC gsl)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(csecorecpp PRIVATE "CSECORE_DLL_EXPORT")
else()
    target_compile_definitions(csecorecpp PUBLIC "CSECORE_STATIC")
    if(WIN32)
        set_target_properties(csecorecpp PROPERTIES OUTPUT_NAME "libcsecorecpp")
    endif()
endif()

install(
    TARGETS csecorecpp
    EXPORT "${CSECORE_EXPORT_TARGET}"
    ${CSECORE_INSTALL_DESTINATIONS}
)
install(
    DIRECTORY "${publicHeaderDir}"
    DESTINATION "${CSECORE_HEADER_INSTALL_DIR}"
)
