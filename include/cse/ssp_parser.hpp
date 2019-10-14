
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <cse/cse_config.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>
#include <cse/system_structure.hpp>

#include <boost/filesystem/path.hpp>

#include <map>
#include <optional>
#include <string>


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

system_structure load_ssp_new(
    cse::model_uri_resolver&,
    const boost::filesystem::path& sspDir);

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
