#include "cse/scenario_parser.hpp"

#include <boost/filesystem/fstream.hpp>

#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>


namespace cse
{

namespace
{

std::pair<cse::simulator_index, cse::simulator*> find_simulator(const std::unordered_map<simulator_index, simulator*>& simulators, const std::string& model)
{
    for (const auto& [idx, simulator] : simulators) {
        if (simulator->name() == model) {
            return std::make_pair(idx, simulator);
        }
    }
    throw std::invalid_argument("Can't find model with this name");
}

cse::variable_type find_variable_type(const nlohmann::json& j)
{
    if (j.count("real")) {
        return variable_type::real;
    } else if (j.count("integer")) {
        return variable_type::integer;
    } else if (j.count("boolean")) {
        return variable_type::boolean;
    } else if (j.count("string")) {
        return variable_type::string;
    }
    throw std::invalid_argument("Can't process unknown variable type");
}

std::pair<cse::variable_causality, std::string> find_causality_and_name(const nlohmann::json& j)
{
    if (j.count("output")) {
        return std::make_pair(variable_causality::output, j.at("output").get<std::string>());
    } else if (j.count("input")) {
        return std::make_pair(variable_causality::input, j.at("input").get<std::string>());
    } else if (j.count("parameter")) {
        return std::make_pair(variable_causality::parameter, j.at("parameter").get<std::string>());
    } else if (j.count("calculatedParameter")) {
        return std::make_pair(variable_causality::calculated_parameter, j.at("calculatedParameter").get<std::string>());
    } else if (j.count("local")) {
        return std::make_pair(variable_causality::local, j.at("local").get<std::string>());
    }
    throw std::invalid_argument("Can't process unknown variable type");
}

cse::variable_index find_variable_index(const std::vector<variable_description>& variables,
    const std::string& name,
    const cse::variable_type type,
    const cse::variable_causality causality)
{
    for (const auto& vd : variables) {
        if ((vd.name == name) && (vd.type == type) && (vd.causality == causality)) {
            return vd.index;
        }
    }
    throw std::invalid_argument("Can't find variable index");
}

template<typename T>
std::function<T(T)> generate_manipulator(const std::string& kind, const nlohmann::json& valueRep)
{
    if ("reset" == kind) {
        return nullptr;
    }
    T value = valueRep.get<T>();
    if ("bias" == kind) {
        return [value](T original) { return original + value; };
    } else if ("override" == kind) {
        return [value](T /*original*/) { return value; };
    }
    throw std::invalid_argument("Can't process unknown modifier kind");
}

cse::scenario::variable_action get_real_action(const std::function<double(double)>& f, cse::variable_causality causality, cse::simulator_index sim, cse::variable_index variable)
{
    switch (causality) {
        case input:
        case parameter:
            return cse::scenario::variable_action{sim, variable, cse::scenario::real_input_manipulator{f}};
        case calculated_parameter:
        case output:
            return cse::scenario::variable_action{sim, variable, cse::scenario::real_output_manipulator{f}};
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}

cse::scenario::variable_action get_integer_action(const std::function<int(int)>& f, cse::variable_causality causality, cse::simulator_index sim, cse::variable_index variable)
{
    switch (causality) {
        case input:
        case parameter:
            return cse::scenario::variable_action{sim, variable, cse::scenario::integer_input_manipulator{f}};
        case calculated_parameter:
        case output:
            return cse::scenario::variable_action{sim, variable, cse::scenario::integer_output_manipulator{f}};
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}

} // namespace


scenario::event generate_event(const nlohmann::json& j, std::string mode, int id, scenario::time_trigger tr, const std::unordered_map<simulator_index, simulator*>& simulators)
{

    auto model = j.at("model");
    auto indexAndSim = find_simulator(simulators, model);
    simulator_index index = indexAndSim.first;
    simulator* sim = indexAndSim.second;

    variable_type type = find_variable_type(j);
    auto causalityAndName = find_causality_and_name(j);
    variable_causality causality = causalityAndName.first;
    std::string varName = causalityAndName.second;

    variable_index varIndex = find_variable_index(sim->model_description().variables, varName, type, causality);

    // Generate manipulator
    switch (type) {
        case variable_type::real: {
            auto real_m = j.at("real");
            std::function f = generate_manipulator<double>(mode, real_m);
            scenario::variable_action a = get_real_action(f, causality, index, varIndex);
            return cse::scenario::event{id, tr, a};
        }
        case variable_type::integer: {
            auto int_m = j.at("integer");
            std::function f = generate_manipulator<int>(mode, int_m);
            scenario::variable_action a = get_integer_action(f, causality, index, varIndex);
            return cse::scenario::event{id, tr, a};
        }
        default:
            throw std::invalid_argument("No support for this variable type yet");
    }
}

scenario::scenario parse_scenario(boost::filesystem::path& scenarioFile, const std::unordered_map<simulator_index, simulator*>& simulators)
{
    boost::filesystem::ifstream i(scenarioFile);
    nlohmann::json j;
    i >> j;

    std::vector<scenario::event> events;

    if (j.count("events")) {
        for (auto& event : j.at("events")) {
            auto id = event.at("id").get<int>();
            auto trigger = event.at("trigger");
            auto time = trigger.at("time");
            auto triggerTime = time.get<double>();
            scenario::time_trigger tr{to_time_point(triggerTime)};

            auto action = event.at("action");
            if (action.count("bias")) {
                auto bias = action.at("bias");
                events.emplace_back(generate_event(bias, "bias", id, tr, simulators));
            } else if (action.count("override")) {
                auto override = action.at("override");
                events.emplace_back(generate_event(override, "override", id, tr, simulators));
            } else if (action.count("reset")) {
                auto reset = action.at("reset");
                events.emplace_back(generate_event(reset, "reset", id, tr, simulators));
            } else {
                throw std::invalid_argument("Unknown variable manipulation type");
            }
        }
    }

    return scenario::scenario{events};
}
} // namespace cse