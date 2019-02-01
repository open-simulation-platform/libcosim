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

        if (j.count("events")) {
            std::cout << "We found events" << std::endl;
            auto events = j.at("events");
            for (auto& event : events) {
                std::cout << "Here is an event: " << event <<  std::endl;
                auto trigger = event.at("trigger");
                std::cout << "Trigger is; " << trigger << std::endl;
                auto time = trigger.at("time");
                std::cout << "Time for triggering; " << time << std::endl;
            }
        }

        return scenario::scenario();
    }
}