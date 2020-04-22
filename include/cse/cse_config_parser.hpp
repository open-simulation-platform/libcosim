#ifndef CSECORE_CONFIG_PARSER_HPP
#define CSECORE_CONFIG_PARSER_HPP

#include <cse/model.hpp>
#include <cse/orchestration.hpp>
#include <cse/system_structure.hpp>

#include <boost/filesystem/path.hpp>


namespace cse
{


struct cse_config
{
    cse::system_structure system_structure;
    time_point start_time;
    duration step_size;
    variable_value_map initial_values;
};

cse_config load_cse_config(
    const boost::filesystem::path& configPath,
    cse::model_uri_resolver&);


} // namespace cse
#endif //CSECORE_CONFIG_PARSER_HPP
