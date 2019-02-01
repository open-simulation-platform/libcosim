#ifndef CSECORE_SCENARIO_PARSER_H
#define CSECORE_SCENARIO_PARSER_H

#include <cse/scenario.hpp>
#include <boost/filesystem/path.hpp>
#include "scenario.hpp"

namespace cse {


    scenario::scenario parse_scenario(boost::filesystem::path& scenarioFile);
}
#endif //CSECORE_SCENARIO_PARSER_H
