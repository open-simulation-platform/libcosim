set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include/cse")
set(privateHeaderDir "${CMAKE_CURRENT_SOURCE_DIR}/cse")

set(publicHeaders
    "algorithm.hpp"
    "hello_world.hpp"
    "async_slave.hpp"
    "config.hpp"
    "event_loop.hpp"
    "exception.hpp"
    "execution.hpp"
    "ssp_parser.hpp"
    "fmi/fmu.hpp"
    "fmi/importer.hpp"
    "fmi/v1/fmu.hpp"
    "fmi/v2/fmu.hpp"
    "lib_info.hpp"
    "libevent.hpp"
    "log.hpp"
    "model.hpp"
    "observer.hpp"
    "slave.hpp"
    "timer.hpp"
)
set(privateHeaders
    "error.hpp"
    "fmi/fmilib.h"
    "fmi/glue.hpp"
    "fmi/windows.hpp"
    "log/logger.hpp"
    "slave_simulator.hpp"
    "utility/filesystem.hpp"
    "utility/uuid.hpp"
    "utility/zip.hpp"
)
set(sources
    "hello_world.cpp"

    "algorithm.cpp"
    "async_slave.cpp"
    "event_loop.cpp"
    "exception.cpp"
    "execution.cpp"
    "ssp_parser.cpp"
    "fmi_importer.cpp"
    "fmi_v1_fmu.cpp"
    "fmi_v2_fmu.cpp"
    "membuffer_observer.cpp"
    "file_observer.cpp"

    "error.cpp"
    "fmi_glue.cpp"
    "fmi_windows.cpp"
    "libevent.cpp"
    "log.cpp"
    "slave_simulator.cpp"
    "timer.cpp"
    "utility_filesystem.cpp"
    "utility_uuid.cpp"
    "utility_zip.cpp"
)

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(privateHeadersFull)
foreach(h IN LISTS privateHeaders)
    list(APPEND privateHeadersFull "${privateHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${privateHeadersFull} ${sources})

add_library(csecorecpp ${allSources})
target_compile_definitions(csecorecpp PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1")
target_compile_features(csecorecpp PUBLIC "cxx_std_17")
target_include_directories(csecorecpp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(csecorecpp
    PUBLIC
        ${FMILibrary_LIBRARIES}
        libzip::libzip
        libevent2::core
        Boost::boost
        Boost::context
        Boost::date_time
        Boost::fiber
        Boost::filesystem
        Boost::log
        Boost::system
        Boost::thread
        gsl
        Threads::Threads
    )
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(csecorecpp INTERFACE "atomic")
endif()
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    set_target_properties(csecorecpp PROPERTIES OUTPUT_NAME "libcsecorecpp")
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
