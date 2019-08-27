#include "cse/scenario_parser.hpp"

#include <boost/filesystem/fstream.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>


namespace cse
{

namespace
{

std::pair<cse::simulator_index, cse::simulator*> find_simulator(
    const std::unordered_map<simulator_index, simulator*>& simulators,
    const std::string& model)
{
    for (const auto& [idx, simulator] : simulators) {
        if (simulator->name() == model) {
            return std::make_pair(idx, simulator);
        }
    }
    std::ostringstream oss;
    oss << "Can't find model with name: " << model;
    throw std::invalid_argument(oss.str());
}

bool is_input(cse::variable_causality causality)
{
    switch (causality) {
        case input:
        case parameter:
            return true;
        case calculated_parameter:
        case output:
            return false;
        default:
            std::ostringstream oss;
            oss << "No support for modifying a variable with causality: "
                << to_text(causality);
            throw std::invalid_argument(oss.str());
    }
}

cse::variable_description find_variable(
    const std::vector<variable_description>& variables,
    const std::string& name)
{
    for (const auto& vd : variables) {
        if (vd.name == name) {
            return vd;
        }
    }

    throw std::invalid_argument("Cannot find variable with name " + name);
}

template<typename T>
std::function<T(T)> generate_modifier(
    const std::string& kind,
    const nlohmann::json& event)
{
    if ("reset" == kind) {
        return nullptr;
    }
    T value = event.at("value").get<T>();
    if ("bias" == kind) {
        return [value](T original) { return original + value; };
    } else if ("override" == kind) {
        return [value](T /*original*/) { return value; };
    }
    std::ostringstream oss;
    oss << "Can't process unrecognized modifier kind: " << kind;
    throw std::invalid_argument(oss.str());
}

cse::scenario::variable_action generate_action(
    const nlohmann::json& event,
    const std::string& mode,
    cse::simulator_index sim,
    cse::variable_type type,
    bool isInput,
    cse::value_reference var)
{
    switch (type) {
        case cse::variable_type::real: {
            auto f = generate_modifier<double>(mode, event);
            return cse::scenario::variable_action{
                sim, var, cse::scenario::real_modifier{f}, isInput};
        }
        case cse::variable_type::integer: {
            auto f = generate_modifier<int>(mode, event);
            return cse::scenario::variable_action{
                sim, var, cse::scenario::integer_modifier{f}, isInput};
        }
        case cse::variable_type::boolean: {
            auto f = generate_modifier<bool>(mode, event);
            return cse::scenario::variable_action{
                sim, var, cse::scenario::boolean_modifier{f}, isInput};
        }
        default:
            std::ostringstream oss;
            oss << "No scenario action support for variable type: " << to_text(type);
            throw std::invalid_argument(oss.str());
    }
}

struct defaults
{
    std::optional<std::string> model;
    std::optional<std::string> variable;
    std::optional<std::string> action;
};

std::optional<std::string> parse_element(
    const nlohmann::json& j,
    const std::string& name)
{
    if (j.count(name)) {
        return j.at(name).get<std::string>();
    } else {
        return std::nullopt;
    }
}

defaults parse_defaults(const nlohmann::json& scenario)
{
    if (scenario.count("defaults")) {
        auto j = scenario.at("defaults");
        return defaults{
            parse_element(j, "model"),
            parse_element(j, "variable"),
            parse_element(j, "action")};
    }
    return defaults{};
}

std::string specified_or_default(
    const nlohmann::json& j,
    const std::string& name,
    std::optional<std::string> defaultOption)
{
    if (j.count(name)) {
        return j.at(name).get<std::string>();
    } else if (defaultOption.has_value()) {
        return *defaultOption;
    }
    std::ostringstream oss;
    oss << "Option is not specified explicitly nor in defaults: " << name;
    throw std::invalid_argument(oss.str());
}

std::optional<cse::time_point> parse_end_time(const nlohmann::json& j)
{
    if (j.count("end")) {
        auto endTime = j.at("end").get<double>();
        return to_time_point(endTime);
    }
    return std::nullopt;
}

} // namespace


scenario::scenario parse_scenario(
    const boost::filesystem::path& scenarioFile,
    const std::unordered_map<simulator_index,
        simulator*>& simulators)
{
    boost::filesystem::ifstream i(scenarioFile);
    nlohmann::json j;
    i >> j;

    std::vector<scenario::event> events;
    defaults defaultOpts = parse_defaults(j);

    for (auto& event : j.at("events")) {
        auto trigger = event.at("time");
        auto triggerTime = trigger.get<double>();
        auto time = to_time_point(triggerTime);

        const auto& [index, simulator] =
            find_simulator(simulators, specified_or_default(event, "model", defaultOpts.model));
        auto varName =
            specified_or_default(event, "variable", defaultOpts.variable);
        const auto var =
            find_variable(simulator->model_description().variables, varName);

        auto mode = specified_or_default(event, "action", defaultOpts.action);
        bool isInput = is_input(var.causality);
        scenario::variable_action a = generate_action(event, mode, index, var.type, isInput, var.reference);
        events.emplace_back(scenario::event{time, a});
    }

    auto end = parse_end_time(j);
    return scenario::scenario{events, end};
}
} // namespace cse
