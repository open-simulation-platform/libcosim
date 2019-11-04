set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include/cse")
set(privateHeaderDir "${CMAKE_CURRENT_SOURCE_DIR}/cse")
set(generatedHeaderDir "${CMAKE_CURRENT_BINARY_DIR}")

set(publicHeaders
    "algorithm.hpp"
    "algorithm/algorithm.hpp"
    "algorithm/fixed_step_algorithm.hpp"
    "algorithm/simulator.hpp"
    "async_slave.hpp"
    "config.hpp"
    "config_loader.hpp"
    "connection.hpp"
    "connection/connection.hpp"
    "connection/scalar_connection.hpp"
    "connection/sum_connection.hpp"
    "cse_config_loader.hpp"
    "exception.hpp"
    "execution.hpp"
    "ssp_loader.hpp"
    "fmi/fmu.hpp"
    "fmi/importer.hpp"
    "fmi/v1/fmu.hpp"
    "fmi/v2/fmu.hpp"
    "lib_info.hpp"
    "log/logger.hpp"
    "log/simple.hpp"
    "manipulator.hpp"
    "manipulator/manipulator.hpp"
    "manipulator/scenario_manager.hpp"
    "manipulator/override_manipulator.hpp"
    "model.hpp"
    "observer.hpp"
    "observer/observer.hpp"
    "observer/file_observer.hpp"
    "observer/last_value_provider.hpp"
    "observer/time_series_observer.hpp"
    "observer/time_series_provider.hpp"
    "orchestration.hpp"
    "slave.hpp"
    "timer.hpp"
    "scenario.hpp"
    "scenario_parser.hpp"
    "uri.hpp"
)
set(privateHeaders
    "error.hpp"
    "fmi/fmilib.h"
    "fmi/glue.hpp"
    "fmi/windows.hpp"
    "slave_simulator.hpp"
    "observer/slave_value_provider.hpp"
    "utility/concurrency.hpp"
    "utility/filesystem.hpp"
    "utility/uuid.hpp"
    "utility/zip.hpp"
)
set(generatedHeaders
    "cse_system_structure.hpp"
)
set(sources
    "algorithm/fixed_step_algorithm.cpp"
    "async_slave.cpp"
    "connection/scalar_connection.cpp"
    "connection/sum_connection.cpp"
    "config_loader.cpp"
    "cse_config_loader.cpp"
    "exception.cpp"
    "execution.cpp"
    "fmi/importer.cpp"
    "fmi/v1/fmu.cpp"
    "fmi/v2/fmu.cpp"
    "log/logger.cpp"
    "manipulator/scenario_manager.cpp"
    "manipulator/override_manipulator.cpp"
    "model.cpp"
    "observer/file_observer.cpp"
    "observer/last_value_observer.cpp"
    "observer/time_series_observer.cpp"
    "orchestration.cpp"
    "scenario_parser.cpp"
    "ssp_loader.cpp"
    "uri.cpp"

    "error.cpp"
    "fmi/glue.cpp"
    "fmi/windows.cpp"
    "observer/slave_value_provider.cpp"
    "slave_simulator.cpp"
    "timer.cpp"
    "utility/filesystem.cpp"
    "utility/uuid.cpp"
    "utility/zip.cpp"
)

if(CSECORE_WITH_FMUPROXY)
    list(APPEND publicHeaders
            "fmuproxy/remote_fmu.hpp"
            "fmuproxy/service_types.hpp"
            "fmuproxy/thrift_state.hpp"
            "fmuproxy/fmu_service.hpp"
            "fmuproxy/fmuproxy_client.hpp"
            "fmuproxy/fmuproxy_uri_sub_resolver.hpp"
    )
    list(APPEND privateHeaders
            "fmuproxy/fmuproxy_helper.hpp"
            "fmuproxy/remote_slave.hpp"
    )
    list(APPEND sources
            "fmuproxy/remote_slave.cpp"
            "fmuproxy/remote_fmu.cpp"
            "fmuproxy/service_types.cpp"
            "fmuproxy/thrift_state.cpp"
            "fmuproxy/fmu_service.cpp"
            "fmuproxy/fmuproxy_client.cpp"
            "fmuproxy/fmuproxy_uri_sub_resolver.cpp"
    )
endif()

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(privateHeadersFull)
foreach(h IN LISTS privateHeaders)
    list(APPEND privateHeadersFull "${privateHeaderDir}/${h}")
endforeach()
set(generatedHeadersFull)
foreach(h IN LISTS generatedHeaders)
    list(APPEND generatedHeadersFull "${generatedHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${privateHeadersFull} ${generatedHeadersFull} ${sources})

add_custom_command(
    OUTPUT cse_system_structure.hpp
    COMMAND csexsdembedder "${CMAKE_CURRENT_SOURCE_DIR}/xsd/OspSystemStructure.xsd" cse_system_structure.hpp
    DEPENDS xsd/OspSystemStructure.xsd)

message(STATUS "${CMAKE_BINARY_DIR}")
message(STATUS "${CMAKE_CURRENT_BINARY_DIR}")

add_library(csecorecpp ${allSources})
target_compile_definitions(csecorecpp PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
target_compile_features(csecorecpp PUBLIC "cxx_std_17")
target_include_directories(csecorecpp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(csecorecpp
    PUBLIC
        ${FMILibrary_LIBRARIES}
        libzip::libzip
        Boost::boost
        Boost::date_time
        Boost::fiber
        Boost::filesystem
        Boost::log
        gsl
        XercesC::XercesC
    PRIVATE
        nlohmann_json
    )

if(CSECORE_WITH_FMUPROXY)
    target_compile_definitions(csecorecpp PRIVATE "HAS_FMUPROXY")
    target_link_libraries(csecorecpp
        PUBLIC
            thrift::thrift
        )
endif()

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
