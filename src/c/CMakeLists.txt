set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include")
set(publicHeaders
        "cse.h")
set(privateHeaders
        "slave_observer.hpp"
        "slave_logger.hpp")

set(sources
        "cse.cpp"
        "slave_observer.cpp"
        "slave_logger.cpp")

set(publicHeadersFull)
foreach (h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach ()
set(allSources ${publicHeadersFull} ${privateHeaders} ${sources})

add_library(csecorec ${allSources})
target_compile_features(csecorec PRIVATE "cxx_std_17")
target_include_directories(csecorec PUBLIC "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
target_link_libraries(csecorec PUBLIC csecorecpp)
if (WIN32 AND NOT BUILD_SHARED_LIBS)
    set_target_properties(csecorec PROPERTIES OUTPUT_NAME "libcsecorec")
endif ()

install(
        TARGETS csecorec
        EXPORT "${CSECORE_EXPORT_TARGET}" ${CSECORE_INSTALL_DESTINATIONS})
install(
        FILES ${publicHeadersFull} DESTINATION "${CSECORE_HEADER_INSTALL_DIR}")
