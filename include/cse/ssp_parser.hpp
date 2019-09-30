
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <cse/cse_config.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/filesystem/path.hpp>

#include <map>
#include <optional>
#include <string>


namespace cse
{

using simulator_map = std::map<std::string, simulator_map_entry>;

std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    const boost::filesystem::path& sspDir);

std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    const boost::filesystem::path& sspDir,
    std::optional<cse::time_point> overrideStartTime);


std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver&,
    const boost::filesystem::path& sspDir,
    std::shared_ptr<cse::algorithm> overrideAlgorithm,
    std::optional<cse::time_point> overrideStartTime = std::nullopt);

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
