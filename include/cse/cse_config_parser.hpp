
#ifndef CSECORE_CONFIG_PARSER_HPP
#define CSECORE_CONFIG_PARSER_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/filesystem/path.hpp>

#include <map>
#include <optional>
#include <string>


namespace cse
{


struct simulator_map_entry
{
    simulator_index index;
    std::string source;
    model_description description;
};

using simulator_map = std::map<std::string, simulator_map_entry>;

std::pair<execution, simulator_map> load_cse_config(cse::model_uri_resolver&, const boost::filesystem::path& configPath, std::optional<cse::time_point> overrideStartTime = {});

} // namespace cse

#endif //CSECORE_CONFIG_PARSER_HPP
