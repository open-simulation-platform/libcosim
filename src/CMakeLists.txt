set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include")

set(publicHeaders
    "cosim/algorithm.hpp"
    "cosim/algorithm/algorithm.hpp"
    "cosim/algorithm/fixed_step_algorithm.hpp"
    "cosim/algorithm/simulator.hpp"
    "cosim/async_slave.hpp"
    "cosim/config.hpp"
    "cosim/exception.hpp"
    "cosim/execution.hpp"
    "cosim/file_cache.hpp"
    "cosim/fmi/fmu.hpp"
    "cosim/fmi/importer.hpp"
    "cosim/fmi/v1/fmu.hpp"
    "cosim/fmi/v2/fmu.hpp"
    "cosim/function/description.hpp"
    "cosim/function/function.hpp"
    "cosim/function/linear_transformation.hpp"
    "cosim/function/utility.hpp"
    "cosim/function/vector_sum.hpp"
    "cosim/lib_info.hpp"
    "cosim/log/logger.hpp"
    "cosim/log/simple.hpp"
    "cosim/manipulator.hpp"
    "cosim/manipulator/manipulator.hpp"
    "cosim/manipulator/scenario_manager.hpp"
    "cosim/manipulator/override_manipulator.hpp"
    "cosim/model.hpp"
    "cosim/observer.hpp"
    "cosim/observer/observer.hpp"
    "cosim/observer/file_observer.hpp"
    "cosim/observer/last_value_provider.hpp"
    "cosim/observer/time_series_observer.hpp"
    "cosim/observer/time_series_provider.hpp"
    "cosim/orchestration.hpp"
    "cosim/osp_config_parser.hpp"
    "cosim/scenario.hpp"
    "cosim/scenario_parser.hpp"
    "cosim/slave.hpp"
    "cosim/ssp/ssp_loader.hpp"
    "cosim/system_structure.hpp"
    "cosim/timer.hpp"
    "cosim/uri.hpp"
)
set(privateHeaders
    "cosim/error.hpp"
    "cosim/fmi/fmilib.h"
    "cosim/fmi/glue.hpp"
    "cosim/fmi/windows.hpp"
    "cosim/observer/slave_value_provider.hpp"
    "cosim/slave_simulator.hpp"
    "cosim/ssp/ssp_parser.hpp"
    "cosim/utility/concurrency.hpp"
    "cosim/utility/filesystem.hpp"
    "cosim/utility/utility.hpp"
    "cosim/utility/uuid.hpp"
    "cosim/utility/zip.hpp"
)
set(generatedHeaders
    "osp_system_structure_schema.hpp"
)
set(sources
    "cosim/algorithm/fixed_step_algorithm.cpp"
    "cosim/async_slave.cpp"
    "cosim/error.cpp"
    "cosim/exception.cpp"
    "cosim/execution.cpp"
    "cosim/file_cache.cpp"
    "cosim/fmi/glue.cpp"
    "cosim/fmi/importer.cpp"
    "cosim/fmi/v1/fmu.cpp"
    "cosim/fmi/v2/fmu.cpp"
    "cosim/fmi/windows.cpp"
    "cosim/function/linear_transformation.cpp"
    "cosim/function/utility.cpp"
    "cosim/function/vector_sum.cpp"
    "cosim/log/logger.cpp"
    "cosim/manipulator/scenario_manager.cpp"
    "cosim/manipulator/override_manipulator.cpp"
    "cosim/model.cpp"
    "cosim/observer/file_observer.cpp"
    "cosim/observer/last_value_observer.cpp"
    "cosim/observer/slave_value_provider.cpp"
    "cosim/observer/time_series_observer.cpp"
    "cosim/orchestration.cpp"
    "cosim/osp_config_parser.cpp"
    "cosim/scenario_parser.cpp"
    "cosim/slave_simulator.cpp"
    "cosim/ssp/ssp_loader.cpp"
    "cosim/ssp/ssp_parser.cpp"
    "cosim/system_structure.cpp"
    "cosim/timer.cpp"
    "cosim/uri.cpp"
    "cosim/utility/concurrency.cpp"
    "cosim/utility/filesystem.cpp"
    "cosim/utility/uuid.cpp"
    "cosim/utility/zip.cpp"
)

if(LIBCOSIM_WITH_FMUPROXY)
    list(APPEND publicHeaders
            "cosim/fmuproxy/fmu_service.hpp"
            "cosim/fmuproxy/fmuproxy_client.hpp"
            "cosim/fmuproxy/fmuproxy_uri_sub_resolver.hpp"
            "cosim/fmuproxy/remote_fmu.hpp"
            "cosim/fmuproxy/service_types.hpp"
            "cosim/fmuproxy/thrift_state.hpp"
    )
    list(APPEND privateHeaders
            "cosim/fmuproxy/fmuproxy_helper.hpp"
            "cosim/fmuproxy/remote_slave.hpp"
    )
    list(APPEND sources
            "cosim/fmuproxy/fmu_service.cpp"
            "cosim/fmuproxy/fmuproxy_client.cpp"
            "cosim/fmuproxy/fmuproxy_uri_sub_resolver.cpp"
            "cosim/fmuproxy/remote_slave.cpp"
            "cosim/fmuproxy/remote_fmu.cpp"
            "cosim/fmuproxy/service_types.cpp"
            "cosim/fmuproxy/thrift_state.cpp"
    )
endif()

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()
set(allSources ${publicHeadersFull} ${privateHeaders} ${generatedHeaders} ${sources})

add_custom_command(
    OUTPUT osp_system_structure_schema.hpp
    COMMAND osp-xsd-embedder "${CMAKE_SOURCE_DIR}/data/xsd/OspSystemStructure.xsd" osp_system_structure_schema.hpp
    DEPENDS ${CMAKE_SOURCE_DIR}/data/xsd/OspSystemStructure.xsd osp-xsd-embedder)

add_library(cosim ${allSources})
target_compile_definitions(cosim PUBLIC "BOOST_ALL_DYN_LINK=1" "BOOST_ALL_NO_LIB=1" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
target_compile_features(cosim PUBLIC "cxx_std_17")
target_include_directories(cosim
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(cosim
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

if(LIBCOSIM_WITH_FMUPROXY)
    target_compile_definitions(cosim PRIVATE "HAS_FMUPROXY")
    target_link_libraries(cosim
        PUBLIC
            thrift::thrift
        )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(cosim INTERFACE "atomic")
endif()
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    set_target_properties(cosim PROPERTIES OUTPUT_NAME "libcosim")
endif()
if(LIBCOSIM_STANDALONE_INSTALLATION)
    set_target_properties(cosim PROPERTIES INSTALL_RPATH "\$ORIGIN")
endif()

install(
    TARGETS cosim
    EXPORT "${LIBCOSIM_EXPORT_TARGET}"
    ${LIBCOSIM_INSTALL_DESTINATIONS}
)
install(
    FILES "${publicHeaders}"
    DESTINATION "${LIBCOSIM_HEADER_INSTALL_DIR}"
)
