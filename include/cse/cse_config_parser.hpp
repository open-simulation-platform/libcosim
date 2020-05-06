#ifndef LIBCOSIM_CONFIG_PARSER_HPP
#define LIBCOSIM_CONFIG_PARSER_HPP

#include <cse/model.hpp>
#include <cse/orchestration.hpp>
#include <cse/system_structure.hpp>

#include <boost/filesystem/path.hpp>


namespace cosim
{


struct cse_config
{
    cosim::system_structure system_structure;
    time_point start_time;
    duration step_size;
    variable_value_map initial_values;
};

cse_config load_cse_config(
    const boost::filesystem::path& configPath,
    cosim::model_uri_resolver&);


} // namespace cse
#endif //LIBCOSIM_CONFIG_PARSER_HPP
