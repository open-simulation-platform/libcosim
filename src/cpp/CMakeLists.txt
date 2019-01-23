set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include/cse")
set(privateHeaderDir "${CMAKE_CURRENT_SOURCE_DIR}/cse")

set(publicHeaders
    "algorithm.hpp"
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
    "slave_value_provider.hpp"
    "utility/concurrency.hpp"
    "utility/filesystem.hpp"
    "utility/uuid.hpp"
    "utility/zip.hpp"
)
set(sources
    "algorithm.cpp"
    "async_slave.cpp"
    "event_loop.cpp"
    "exception.cpp"
    "execution.cpp"
    "ssp_parser.cpp"
    "fmi/importer.cpp"
    "fmi/v1/fmu.cpp"
    "fmi/v2/fmu.cpp"
    "membuffer_observer.cpp"
    "file_observer.cpp"
    "slave_value_provider.cpp"
    "time_series_observer.cpp"

    "error.cpp"
    "fmi/glue.cpp"
    "fmi/windows.cpp"
    "libevent.cpp"
    "log.cpp"
    "slave_simulator.cpp"
    "timer.cpp"
    "utility/filesystem.cpp"
    "utility/uuid.cpp"
    "utility/zip.cpp"
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
target_compile_definitions(csecorecpp PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
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
        Boost::date_time
        Boost::fiber
        Boost::filesystem
        Boost::log
        gsl
    )
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(csecorecpp INTERFACE "atomic")
endif()
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    set_target_properties(csecorecpp PROPERTIES OUTPUT_NAME "libcsecorecpp")
endif()
if(CSECORE_STANDALONE_INSTALLATION)
    set_target_properties(csecorecpp PROPERTIES INSTALL_RPATH "\$ORIGIN")
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
