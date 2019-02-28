#ifndef CSECORE_SCENARIO_PARSER_H
#define CSECORE_SCENARIO_PARSER_H

#include <cse/algorithm.hpp>
#include <cse/scenario.hpp>

#include <boost/filesystem/path.hpp>

namespace cse
{
scenario::scenario parse_scenario(const boost::filesystem::path& scenarioFile, const std::unordered_map<simulator_index, simulator*>& simulators);
}

#endif //CSECORE_SCENARIO_PARSER_H
