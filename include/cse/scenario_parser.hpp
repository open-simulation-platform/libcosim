#ifndef CSECORE_SCENARIO_PARSER_H
#define CSECORE_SCENARIO_PARSER_H

#include <cse/algorithm.hpp>
#include <cse/scenario.hpp>

#include <boost/filesystem/path.hpp>

namespace cse
{
/**
 * Parses a scenario from file.
 *
 * \param scenarioFile
 *  The path to a proprietary `yaml` file defining the scenario.
 *
 * \param simulators
 *  A map containing the simulators currently loaded in the execution.
 */
scenario::scenario parse_scenario(
    const boost::filesystem::path& scenarioFile,
    const std::unordered_map<simulator_index, simulator*>& simulators);
} // namespace cse

#endif //CSECORE_SCENARIO_PARSER_H
