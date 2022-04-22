set(publicHeaderDir "${CMAKE_SOURCE_DIR}/include")

# ==============================================================================
# Source files
# ==============================================================================

set(publicHeaders
    "cosim/algorithm.hpp"
    "cosim/algorithm/algorithm.hpp"
    "cosim/algorithm/fixed_step_algorithm.hpp"
    "cosim/algorithm/simulator.hpp"
    "cosim/config.hpp"
    "cosim/exception.hpp"
    "cosim/execution.hpp"
    "cosim/execution_runner.hpp"
    "cosim/file_cache.hpp"
    "cosim/fmi/fmu.hpp"
    "cosim/fmi/importer.hpp"
    "cosim/fmi/v1/fmu.hpp"
    "cosim/fmi/v2/fmu.hpp"
    "cosim/fs_portability.hpp"
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
    "cosim/model_description.hpp"
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
    "cosim/time.hpp"
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
    "cosim/utility/thread_pool.hpp"
    "cosim/utility/utility.hpp"
    "cosim/utility/uuid.hpp"
    "cosim/utility/zip.hpp"
)
set(sources
    "cosim/algorithm/fixed_step_algorithm.cpp"
    "cosim/error.cpp"
    "cosim/exception.cpp"
    "cosim/execution.cpp"
    "cosim/execution_runner.cpp"
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
    "cosim/model_description.cpp"
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
set(generatedSources
    "cosim/lib_info.cpp"
)

if(LIBCOSIM_WITH_PROXYFMU)
    list(APPEND privateHeaders
        "cosim/proxy/proxy_uri_sub_resolver.hpp"
        "cosim/proxy/remote_fmu.hpp"
        "cosim/proxy/remote_slave.hpp"
    )
    list(APPEND sources
        "cosim/proxy/proxy_uri_sub_resolver.cpp"
        "cosim/proxy/remote_fmu.cpp"
        "cosim/proxy/remote_slave.cpp"
    )
endif()

# ==============================================================================
# Code generation
# ==============================================================================

set(generatedSourcesDir "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generatedSourcesDir}")

# Generate sources from .in files
set(generatedFiles)
foreach(src IN LISTS generatedSources)
    set(tgt "${generatedSourcesDir}/${src}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${src}.in"
        "${tgt}"
        @ONLY
    )
    list(APPEND generatedFiles "${tgt}")
endforeach()

# Generate header file from OSP XSD
set(ospSystemStructureXSD "${CMAKE_SOURCE_DIR}/data/xsd/OspSystemStructure.xsd")
set(ospSystemStructureHeader "${generatedSourcesDir}/osp_system_structure_schema.hpp")
add_custom_command(
    OUTPUT "${ospSystemStructureHeader}"
    COMMAND osp-xsd-embedder "${ospSystemStructureXSD}" "${ospSystemStructureHeader}"
    DEPENDS osp-xsd-embedder "${ospSystemStructureXSD}"
)
list(APPEND generatedFiles "${ospSystemStructureHeader}")


# ==============================================================================
# Target definition
# ==============================================================================

set(publicHeadersFull)
foreach(h IN LISTS publicHeaders)
    list(APPEND publicHeadersFull "${publicHeaderDir}/${h}")
endforeach()

add_library(cosim ${publicHeadersFull} ${privateHeaders} ${sources} ${generatedFiles})

target_compile_definitions(cosim PUBLIC "BOOST_ALL_NO_LIB=0" "BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1")
if(NOT Boost_USE_STATIC_LIBS)
    target_compile_definitions(cosim PUBLIC "BOOST_ALL_DYN_LINK=1")
endif()

target_compile_features(cosim PUBLIC "cxx_std_17")
target_include_directories(cosim
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${generatedSourcesDir}"
)
target_link_libraries(cosim
    PUBLIC
        Boost::boost
        Boost::date_time
        Boost::log
        gsl
    PRIVATE
        ${FMILibrary_LIBRARIES}
        libzip::libzip
        XercesC::XercesC
        yaml-cpp
    )

if(LIBCOSIM_WITH_PROXYFMU)
    target_compile_definitions(cosim PRIVATE "HAS_PROXYFMU")
    target_link_libraries(cosim PRIVATE proxyfmu::proxyfmu-client)
endif ()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(cosim INTERFACE "atomic" "stdc++fs")
endif()
if(WIN32)
    target_link_libraries(cosim PUBLIC "Bcrypt")
    if(BUILD_SHARED_LIBS)
        target_link_libraries(cosim PUBLIC "Shlwapi") # Windows API DLLs
    else()
        set_target_properties(cosim PROPERTIES OUTPUT_NAME "libcosim")
    endif()
endif()
if(LIBCOSIM_STANDALONE_INSTALLATION)
    set_target_properties(cosim PROPERTIES INSTALL_RPATH "\$ORIGIN")
endif()

# ==============================================================================
# Installation rules
# ==============================================================================

install(
    TARGETS cosim
    EXPORT "${LIBCOSIM_EXPORT_TARGET}"
    ${LIBCOSIM_INSTALL_DESTINATIONS}
)
install(
    DIRECTORY "${publicHeaderDir}/cosim"
    DESTINATION "${LIBCOSIM_HEADER_INSTALL_DIR}"
)
