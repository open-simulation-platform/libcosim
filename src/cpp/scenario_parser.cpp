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

std::pair<cse::simulator_index, cse::manipulable*> find_simulator(
    const std::unordered_map<simulator_index, manipulable*>& simulators,
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

    T value = event.value<T>("value", 0);
    if ("bias" == kind) {
        return [value](T original) { return original + value; };
    } else if ("override" == kind) {
        return [value](T /*original*/) { return value; };
    } else if ("transform" == kind) {
        T factor = event.at("factor").get<T>();
        T offset = event.at("offset").get<T>();
        return [factor, offset](T original) { return factor * original + offset; };
    }
    std::ostringstream oss;
    oss << "Can't process unrecognized modifier kind: " << kind;
    throw std::invalid_argument(oss.str());
}

template<typename T>
std::function<T(T, time_point)> generate_time_dependent_modifier(
    const std::string& kind,
    const nlohmann::json& event)
{
    if ("reset" == kind) {
        return nullptr;
    }

    // Ramp function y(x,t) = x + a*max(0,t), for t in seconds
    if ("ramp" == kind) {
        T factor = event.at("factor").get<T>();
        return [factor](T original, time_point timePoint) {
            auto t = std::chrono::time_point_cast<std::chrono::milliseconds>(timePoint).time_since_epoch().count()/1000.0;
            return static_cast<T>(original + factor*t); };
    }

    std::ostringstream oss;
    oss << "Can't process unrecognized modifier kind: " << kind;
    throw std::invalid_argument(oss.str());
}

cse::scenario::variable_action generate_action(
    const nlohmann::json& event,
    const std::string& mode,
    cse::simulator_index sim,
    cse::variable_id id,
    bool isInput,
    bool isTimeDependent)
{
    switch (id.type) {
        case cse::variable_type::real: {
            if (isTimeDependent) {
                auto f = generate_time_dependent_modifier<double>(mode, event);
                return cse::scenario::variable_action{
                    sim, id.reference, cse::scenario::time_dependent_real_modifier{f}, isInput, isTimeDependent};
            } else {
                auto f = generate_modifier<double>(mode, event);
                return cse::scenario::variable_action{
                    sim, id.reference, cse::scenario::real_modifier{f}, isInput};
            }
        }
        case cse::variable_type::integer: {
            if (isTimeDependent) {
                auto f = generate_time_dependent_modifier<int>(mode, event);
                return cse::scenario::variable_action{
                    sim, id.reference, cse::scenario::time_dependent_integer_modifier{f}, isInput, isTimeDependent};
            } else {
                auto f = generate_modifier<int>(mode, event);
                return cse::scenario::variable_action{
                    sim, id.reference, cse::scenario::integer_modifier{f}, isInput};
            }
        }
        case cse::variable_type::boolean: {
            auto f = generate_modifier<bool>(mode, event);
            return cse::scenario::variable_action{
                sim, id.reference, cse::scenario::boolean_modifier{f}, isInput};
        }
        default:
            std::ostringstream oss;
            oss << "No scenario action support for variable type: " << to_text(id.type);
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
        manipulable*>& simulators)
{
    boost::filesystem::ifstream i(scenarioFile);
    nlohmann::json j;
    i >> j;

    std::vector<scenario::event> events;
    std::vector<variable_id> tmpVars;

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
        bool isTimeDependent = false;

        auto id = variable_id{index, var.type, var.reference};

        if (event.find("action") != event.end()) {
            if (event.at("action") == "ramp") {
                tmpVars.emplace_back(id);
                isTimeDependent = true;
            }
            if (event.at("action") == "reset" && std::find(tmpVars.begin(), tmpVars.end(), id) != tmpVars.end()) {
                std::cout << "Time dependent var is being reset " << std::endl;
                isTimeDependent = true;
            }
        }

        scenario::variable_action a = generate_action(event, mode, index, id, isInput, isTimeDependent);
        events.emplace_back(scenario::event{time, a});
    }

    auto end = parse_end_time(j);
    return scenario::scenario{events, end};
}
} // namespace cse
