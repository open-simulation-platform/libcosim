#include "cse/scenario_parser.hpp"

#include <boost/filesystem/fstream.hpp>

#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127)
#endif
#include <yaml-cpp/yaml.h>
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

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

cse::variable_type find_variable_type(const std::string typestr)
{
    if (typestr == "real") {
        return variable_type::real;
    } else if (typestr == "integer") {
        return variable_type::integer;
    } else if (typestr == "boolean") {
        return variable_type::boolean;
    } else if (typestr == "string") {
        return variable_type::string;
    }
    std::ostringstream oss;
    oss << "Can't process unrecognized variable type: " << typestr;
    throw std::invalid_argument(oss.str());
}

cse::variable_causality find_causality(const std::string caus)
{
    if (caus == "output") {
        return variable_causality::output;
    } else if (caus == "input") {
        return variable_causality::input;
    } else if (caus == "parameter") {
        return variable_causality::parameter;
    } else if (caus == "calculatedParameter") {
        return variable_causality::calculated_parameter;
    } else if (caus == "local") {
        return variable_causality::local;
    }
    std::ostringstream oss;
    oss << "Can't process unrecognized variable causality: " << caus;
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

cse::variable_index find_variable_index(
    const std::vector<variable_description>& variables,
    const std::string& name,
    const cse::variable_type type,
    const cse::variable_causality causality)
{
    for (const auto& vd : variables) {
        if ((vd.name == name) && (vd.type == type) && (vd.causality == causality)) {
            return vd.index;
        }
    }
    std::ostringstream oss;
    oss << "Cannot find variable with name " << name
        << ", causality " << cse::to_text(causality)
        << " and type " << cse::to_text(type);
    throw std::invalid_argument(oss.str());
}

template<typename T>
std::function<T(T)> generate_modifier(
    const std::string& kind,
    const YAML::Node& event)
{
    if ("reset" == kind) {
        return nullptr;
    }
    T value = event["value"].as<T>();
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
    const YAML::Node& event,
    const std::string& mode,
    cse::simulator_index sim,
    cse::variable_type type,
    bool isInput,
    cse::variable_index var)
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
    std::optional<std::string> causality;
    std::optional<std::string> type;
    std::optional<std::string> action;
};

std::optional<std::string> parse_element(
    const YAML::Node& j,
    const std::string& name)
{
    if (j[name]) {
        return j[name].as<std::string>();
    } else {
        return std::nullopt;
    }
}

defaults parse_defaults(const YAML::Node& scenario)
{
    if (scenario["defaults"]) {
        auto j = scenario["defaults"];
        return defaults{
            parse_element(j, "model"),
            parse_element(j, "variable"),
            parse_element(j, "causality"),
            parse_element(j, "type"),
            parse_element(j, "action")};
    }
    return defaults{};
}

std::string specified_or_default(
    const YAML::Node& j,
    const std::string& name,
    std::optional<std::string> defaultOption)
{
    if (j[name]) {
        return j[name].as<std::string>();
    } else if (defaultOption.has_value()) {
        return *defaultOption;
    }
    std::ostringstream oss;
    oss << "Option is not specified explicitly nor in defaults: " << name;
    throw std::invalid_argument(oss.str());
}

std::optional<cse::time_point> parse_end_time(const YAML::Node& j)
{
    if (j["end"]) {
        auto endTime = j["end"].as<double>();
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
    YAML::Node j = YAML::Load(i);

    std::vector<scenario::event> events;
    defaults defaultOpts = parse_defaults(j);

    for (const auto& event : j["events"]) {
        auto trigger = event["time"];
        auto triggerTime = trigger.as<double>();
        auto time = to_time_point(triggerTime);

        const auto& [index, simulator] =
            find_simulator(simulators, specified_or_default(event, "model", defaultOpts.model));
        variable_type type =
            find_variable_type(specified_or_default(event, "type", defaultOpts.type));
        variable_causality causality =
            find_causality(specified_or_default(event, "causality", defaultOpts.causality));
        auto varName =
            specified_or_default(event, "variable", defaultOpts.variable);
        variable_index varIndex =
            find_variable_index(simulator->model_description().variables, varName, type, causality);

        auto mode = specified_or_default(event, "action", defaultOpts.action);
        bool isInput = is_input(causality);
        scenario::variable_action a = generate_action(event, mode, index, type, isInput, varIndex);
        events.emplace_back(scenario::event{time, a});
    }

    auto end = parse_end_time(j);
    return scenario::scenario{events, end};
}
} // namespace cse
