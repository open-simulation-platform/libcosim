
#ifndef CSECORE_CONFIG_PARSER_HPP
#define CSECORE_CONFIG_PARSER_HPP

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

std::pair<execution, simulator_map> load_cse_config(
    cse::model_uri_resolver&,
    const boost::filesystem::path& configPath,
    const boost::filesystem::path& schemaPath,
    std::optional<cse::time_point> overrideStartTime = {});

} // namespace cse

#endif //CSECORE_CONFIG_PARSER_HPP
