#ifndef LIBCOSIM_SCENARIO_PARSER_H
#define LIBCOSIM_SCENARIO_PARSER_H

#include <cosim/algorithm.hpp>
#include <cosim/scenario.hpp>

#include <boost/filesystem/path.hpp>

namespace cosim
{
/**
 * Parses a scenario from file.
 *
 * \param scenarioFile
 *  The path to a proprietary `json` or `yaml` file defining the scenario.
 *
 * \param simulators
 *  A map containing the simulators currently loaded in the execution.
 */
scenario::scenario parse_scenario(
    const boost::filesystem::path& scenarioFile,
    const std::unordered_map<simulator_index, manipulable*>& simulators);
} // namespace cosim

#endif //LIBCOSIM_SCENARIO_PARSER_H
