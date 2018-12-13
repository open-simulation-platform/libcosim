
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <boost/filesystem/path.hpp>
#include <string>
#include <map>

#include <cse/execution.hpp>
#include <cse/model.hpp>


namespace cse
{


struct simulator_map_entry {
    simulator_index index;
    std::string source;
};

using simulator_map = std::map<std::string, simulator_map_entry>;

std::pair<execution, simulator_map> load_ssp(const boost::filesystem::path& sspDir, cse::time_point startTime);

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
