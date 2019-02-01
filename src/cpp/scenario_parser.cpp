#include "cse/scenario_parser.hpp"

#include <boost/filesystem/fstream.hpp>
#include <iostream>

#include <nlohmann/json.hpp>

namespace cse {

    scenario::scenario parse_scenario(boost::filesystem::path& scenarioFile) {
        boost::filesystem::ifstream i(scenarioFile);
        nlohmann::json j;
        i >> j;
        std::cout << j.dump(2) << std::endl;

        return scenario::scenario();
    }
}