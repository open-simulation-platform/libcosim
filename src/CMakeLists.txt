set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include")

set(publicHeaders
    "cse/algorithm.hpp"
    "cse/algorithm/algorithm.hpp"
    "cse/algorithm/fixed_step_algorithm.hpp"
    "cse/algorithm/simulator.hpp"
    "cse/async_slave.hpp"
    "cse/config.hpp"
    "cse/cse_config_parser.hpp"
    "cse/exception.hpp"
    "cse/execution.hpp"
    "cse/file_cache.hpp"
    "cse/fmi/fmu.hpp"
    "cse/fmi/importer.hpp"
    "cse/fmi/v1/fmu.hpp"
    "cse/fmi/v2/fmu.hpp"
    "cse/function/description.hpp"
    "cse/function/function.hpp"
    "cse/function/linear_transformation.hpp"
    "cse/function/utility.hpp"
    "cse/function/vector_sum.hpp"
    "cse/lib_info.hpp"
    "cse/log/logger.hpp"
    "cse/log/simple.hpp"
    "cse/manipulator.hpp"
    "cse/manipulator/manipulator.hpp"
    "cse/manipulator/scenario_manager.hpp"
    "cse/manipulator/override_manipulator.hpp"
    "cse/model.hpp"
    "cse/observer.hpp"
    "cse/observer/observer.hpp"
    "cse/observer/file_observer.hpp"
    "cse/observer/last_value_provider.hpp"
    "cse/observer/time_series_observer.hpp"
    "cse/observer/time_series_provider.hpp"
    "cse/orchestration.hpp"
    "cse/scenario.hpp"
    "cse/scenario_parser.hpp"
    "cse/slave.hpp"
    "cse/ssp/ssp_loader.hpp"
    "cse/system_structure.hpp"
    "cse/timer.hpp"
    "cse/uri.hpp"
)
set(privateHeaders
    "cse/error.hpp"
    "cse/fmi/fmilib.h"
    "cse/fmi/glue.hpp"
    "cse/fmi/windows.hpp"
    "cse/observer/slave_value_provider.hpp"
    "cse/slave_simulator.hpp"
    "cse/ssp/ssp_parser.hpp"
    "cse/utility/concurrency.hpp"
    "cse/utility/filesystem.hpp"
    "cse/utility/utility.hpp"
    "cse/utility/uuid.hpp"
    "cse/utility/zip.hpp"
)
set(generatedHeaders
    "cse_system_structure.hpp"
)
set(sources
    "cse/algorithm/fixed_step_algorithm.cpp"
    "cse/async_slave.cpp"
    "cse/cse_config_parser.cpp"
    "cse/error.cpp"
    "cse/exception.cpp"
    "cse/execution.cpp"
    "cse/file_cache.cpp"
    "cse/fmi/glue.cpp"
    "cse/fmi/importer.cpp"
    "cse/fmi/v1/fmu.cpp"
    "cse/fmi/v2/fmu.cpp"
    "cse/fmi/windows.cpp"
    "cse/function/linear_transformation.cpp"
    "cse/function/utility.cpp"
    "cse/function/vector_sum.cpp"
    "cse/log/logger.cpp"
    "cse/manipulator/scenario_manager.cpp"
    "cse/manipulator/override_manipulator.cpp"
    "cse/model.cpp"
    "cse/observer/file_observer.cpp"
    "cse/observer/last_value_observer.cpp"
    "cse/observer/slave_value_provider.cpp"
    "cse/observer/time_series_observer.cpp"
    "cse/orchestration.cpp"
    "cse/scenario_parser.cpp"
    "cse/slave_simulator.cpp"
    "cse/ssp/ssp_loader.cpp"
    "cse/ssp/ssp_parser.cpp"
    "cse/system_structure.cpp"
    "cse/timer.cpp"
    "cse/uri.cpp"
    "cse/utility/concurrency.cpp"
    "cse/utility/filesystem.cpp"
    "cse/utility/uuid.cpp"
    "cse/utility/zip.cpp"
)

if(CSECORE_WITH_FMUPROXY)
    list(APPEND publicHeaders
            "cse/fmuproxy/fmu_service.hpp"
            "cse/fmuproxy/fmuproxy_client.hpp"
            "cse/fmuproxy/fmuproxy_uri_sub_resolver.hpp"
            "cse/fmuproxy/remote_fmu.hpp"
            "cse/fmuproxy/service_types.hpp"
            "cse/fmuproxy/thrift_state.hpp"
    )
    list(APPEND privateHeaders
            "cse/fmuproxy/fmuproxy_helper.hpp"
            "cse/fmuproxy/remote_slave.hpp"
    )
    list(APPEND sources
            "cse/fmuproxy/fmu_service.cpp"
            "cse/fmuproxy/fmuproxy_client.cpp"
            "cse/fmuproxy/fmuproxy_uri_sub_resolver.cpp"
            "cse/fmuproxy/remote_slave.cpp"
            "cse/fmuproxy/remote_fmu.cpp"
            "cse/fmuproxy/service_types.cpp"
            "cse/fmuproxy/thrift_state.cpp"
    )
endif()

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${privateHeaders} ${generatedHeaders} ${sources})

add_custom_command(
    OUTPUT cse_system_structure.hpp
    COMMAND csexsdembedder "${CMAKE_CURRENT_SOURCE_DIR}/cse/xsd/OspSystemStructure.xsd" cse_system_structure.hpp
    DEPENDS cse/xsd/OspSystemStructure.xsd)

add_library(csecorecpp ${allSources})
target_compile_definitions(csecorecpp PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
target_compile_features(csecorecpp PUBLIC "cxx_std_17")
target_include_directories(csecorecpp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    PRIVATE
        "cse"
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(csecorecpp
    PUBLIC
        Boost::boost
        Boost::date_time
        Boost::fiber
        Boost::filesystem
        Boost::log
        gsl
    PRIVATE
        ${FMILibrary_LIBRARIES}
        libzip::libzip
        XercesC::XercesC
        yaml-cpp
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
