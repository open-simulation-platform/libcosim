
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <cse/algorithm.hpp>
#include <cse/cse_config.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>
#include <cse/system_structure.hpp>

#include <boost/filesystem/path.hpp>

#include <map>
#include <optional>
#include <string>
#include <tuple>


namespace cse
{

using simulator_map = std::map<std::string, simulator_map_entry>;


/**
 *  Creates an execution based on a SystemStructure.ssd file.
 *
 *  \param [in] configPath
 *      Path to an .ssd file, or a directory holding SystemStructure.ssd
 *  \param [in] overrideStartTime
 *      If defined, the (logical) time point at which the simulation should start.
 *      If not defined, this variable will be read from .ssd file.
 */
std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    const boost::filesystem::path& configPath,
    std::optional<cse::time_point> overrideStartTime);

/**
 *  Creates an execution based on a SystemStructure.ssd file.
 *
 *  \param [in] configPath
 *      Path to an .ssd file, or a directory holding SystemStructure.ssd
 *  \param [in] overrideAlgorithm
 *      If defined, the co-simulation algorithm used in the execution.
 *      If not defined, the algorithm will be a `fixed_step_algorithm` with
 *      `stepSize` defined in the .ssd file.
 *  \param [in] overrideStartTime
 *      If defined, the (logical) time point at which the simulation should start.
 *      If not defined, this variable will be read from the .ssd file.
 */
std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    const boost::filesystem::path& configPath,
    std::shared_ptr<cse::algorithm> overrideAlgorithm = nullptr,
    std::optional<cse::time_point> overrideStartTime = std::nullopt);


/// Default simulation settings from an SSP file.
struct ssp_default_experiment
{
    std::optional<time_point> start_time;
    std::optional<time_point> stop_time;
    std::optional<duration> step_size;
    std::unique_ptr<cse::algorithm> algorithm;
};


/**
 *  Loads an SSP system structure description.
 *
 *  \param [in] resolver
 *      An URI resolver that will be used for lookup of component "source" URIs.
 *  \param [in] configPath
 *      Path to an .ssd file, or a directory holding SystemStructure.ssd
 *
 *  \returns
 *      A tuple consisting of the system structure (simulators and connections),
 *      a set of initial values, and default experiment settings.
 */
std::tuple<system_structure, parameter_set, ssp_default_experiment> load_ssp_v2(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& configPath);


} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
