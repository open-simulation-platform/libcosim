/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/scenario_parser.hpp"

#include "cosim/execution.hpp"
#include "cosim/manipulator/manipulator.hpp"
#include "cosim/model_description.hpp"
#include "cosim/scenario.hpp"
#include "cosim/time.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127 4275)
#endif
#include <yaml-cpp/yaml.h>
#ifdef _MSC_VER
#    pragma warning(pop)
#endif


namespace cosim
{

namespace
{

std::pair<cosim::simulator_index, cosim::manipulable*> find_simulator(
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

bool is_input(cosim::variable_causality causality)
{
    switch (causality) {
        case input:
        case parameter:
            return true;
        case calculated_parameter:
        case output:
            return false;
        case local:
        case structural_parameter:
        case independent:
            break;
    }
    std::ostringstream oss;
    oss << "No support for modifying a variable with causality: "
        << to_text(causality);
    throw std::invalid_argument(oss.str());
}

template<typename T>
std::function<T(T, duration)> generate_modifier(
    const std::string& kind,
    const YAML::Node& event)
{
    if ("reset" == kind) {
        return nullptr;
    }
    T value = event["value"].as<T>(); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
    if ("bias" == kind) {
        return [value](T original, duration) { return original + value; };
    } else if ("override" == kind) {
        return [value](T /*original*/, duration) { return value; };
    }
    std::ostringstream oss;
    oss << "Can't process unrecognized modifier kind: " << kind;
    throw std::invalid_argument(oss.str());
}

std::function<std::string(std::string_view, duration)> generate_string_modifier(
    const std::string& kind,
    const YAML::Node& event)
{
    if ("reset" == kind) {
        return nullptr;
    }
    auto value = event["value"].as<std::string>(); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
    if ("override" == kind) {
        return [value = std::move(value)](std::string_view /*original*/, duration) { return value; };
    }
    std::ostringstream oss;
    oss << "Can't process unsupported modifier kind: " << kind << " for type " << to_text(cosim::variable_type::string);
    throw std::invalid_argument(oss.str());
}

template<typename T, typename Converter>
std::vector<T> parse_values(const YAML::Node& node, Converter convert)
{
    std::vector<T> result;
    if (node.IsSequence()) {
        result.reserve(node.size());
        for (const auto& element : node) {
            result.push_back(convert(element));
        }
    } else {
        result.push_back(convert(node));
    }
    return result;
}

template<typename T>
T parse_signed_integer(const YAML::Node& node)
{
    const auto value = node.as<std::int64_t>();
    if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        throw std::out_of_range("Scenario integer value is out of range");
    }
    return static_cast<T>(value);
}

template<typename T>
T parse_unsigned_integer(const YAML::Node& node)
{
    const auto value = node.as<std::uint64_t>();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        throw std::out_of_range("Scenario integer value is out of range");
    }
    return static_cast<T>(value);
}

binary_value parse_binary(const YAML::Node& node)
{
    constexpr unsigned long max_byte_value = 0xff;
    const auto text = node.as<std::string>();
    if (text.size() % 2 != 0) {
        throw std::invalid_argument(
            "Scenario Binary values must contain an even number of hex digits");
    }
    binary_value result;
    result.data.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto byte = text.substr(i, 2);
        std::size_t parsed = 0;
        const auto value = std::stoul(byte, &parsed, 16);
        if (parsed != 2 || value > max_byte_value) {
            throw std::invalid_argument(
                "Scenario Binary values must use hexadecimal bytes");
        }
        result.data.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

variable_value parse_variable_value(
    const YAML::Node& node,
    variable_type type,
    const std::vector<std::uint64_t>& dimensions)
{
    switch (type) {
        case variable_type::real:
            return variable_value(
                real_values{
                    parse_values<double>(
                        node, [](const YAML::Node& value) {
                            return value.as<double>();
                        }),
                },
                dimensions);
        case variable_type::integer:
            return variable_value(
                integer_values{
                    parse_values<int>(
                        node, parse_signed_integer<int>),
                },
                dimensions);
        case variable_type::boolean:
            return variable_value(
                boolean_values{
                    parse_values<bool>(
                        node, [](const YAML::Node& value) {
                            return value.as<bool>();
                        }),
                },
                dimensions);
        case variable_type::string:
            return variable_value(
                string_values{
                    parse_values<std::string>(
                        node, [](const YAML::Node& value) {
                            return value.as<std::string>();
                        }),
                },
                dimensions);
        case variable_type::float32:
            return variable_value(
                float32_values{
                    parse_values<float>(
                        node, [](const YAML::Node& value) {
                            return value.as<float>();
                        }),
                },
                dimensions);
        case variable_type::int8:
            return variable_value(
                int8_values{
                    parse_values<std::int8_t>(
                        node, parse_signed_integer<std::int8_t>),
                },
                dimensions);
        case variable_type::uint8:
            return variable_value(
                uint8_values{
                    parse_values<std::uint8_t>(
                        node, parse_unsigned_integer<std::uint8_t>),
                },
                dimensions);
        case variable_type::int16:
            return variable_value(
                int16_values{
                    parse_values<std::int16_t>(
                        node, parse_signed_integer<std::int16_t>),
                },
                dimensions);
        case variable_type::uint16:
            return variable_value(
                uint16_values{
                    parse_values<std::uint16_t>(
                        node, parse_unsigned_integer<std::uint16_t>),
                },
                dimensions);
        case variable_type::uint32:
            return variable_value(
                uint32_values{
                    parse_values<std::uint32_t>(
                        node, parse_unsigned_integer<std::uint32_t>),
                },
                dimensions);
        case variable_type::int64:
            return variable_value(
                int64_values{
                    parse_values<std::int64_t>(
                        node, [](const YAML::Node& value) {
                            return value.as<std::int64_t>();
                        }),
                },
                dimensions);
        case variable_type::uint64:
            return variable_value(
                uint64_values{
                    parse_values<std::uint64_t>(
                        node, [](const YAML::Node& value) {
                            return value.as<std::uint64_t>();
                        }),
                },
                dimensions);
        case variable_type::enumeration:
            return variable_value(
                enumeration_values{
                    parse_values<enumeration_value>(
                        node, [](const YAML::Node& value) {
                            return enumeration_value{
                                value.as<std::int64_t>(),
                            };
                        }),
                },
                dimensions);
        case variable_type::binary:
            return variable_value(
                binary_values{parse_values<binary_value>(node, parse_binary)},
                dimensions);
        case variable_type::clock:
            throw std::invalid_argument(
                "Clock variables cannot be modified as data values");
    }
    throw std::invalid_argument("Unsupported scenario variable type");
}

cosim::scenario::variable_action generate_action(
    const YAML::Node& event,
    const std::string& mode,
    cosim::simulator_index sim,
    const variable_description& variable,
    bool isInput,
    cosim::value_reference var)
{
    if (!variable.dimensions.empty() ||
        (variable.type != variable_type::real &&
            variable.type != variable_type::integer &&
            variable.type != variable_type::boolean &&
            variable.type != variable_type::string)) {
        const auto dimensions = fixed_shape(variable);
        if (!dimensions) {
            throw std::invalid_argument(
                "Scenario actions require concrete variable dimensions");
        }
        if (mode == "reset") {
            return {
                sim,
                var,
                scenario::variable_modifier{variable.type, nullptr},
                isInput,
            };
        }
        if (mode != "override") {
            std::ostringstream message;
            message << "Scenario modifier " << mode
                    << " is not supported for complete values of type "
                    << variable.type;
            throw std::invalid_argument(message.str());
        }
        auto replacement =
            parse_variable_value(event["value"], variable.type, *dimensions); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        auto modifier = [replacement = std::move(replacement)](
                            const variable_value&,
                            duration) { return replacement; };
        return {
            sim,
            var,
            scenario::variable_modifier{
                variable.type,
                std::move(modifier),
            },
            isInput,
        };
    }

    switch (variable.type) {
        case cosim::variable_type::real: {
            const auto modifier = generate_modifier<double>(mode, event);
            return cosim::scenario::variable_action{
                sim,
                var,
                cosim::scenario::real_modifier{modifier},
                isInput,
            };
        }
        case cosim::variable_type::integer: {
            const auto modifier = generate_modifier<int>(mode, event);
            return cosim::scenario::variable_action{
                sim,
                var,
                cosim::scenario::integer_modifier{modifier},
                isInput,
            };
        }
        case cosim::variable_type::boolean: {
            const auto modifier = generate_modifier<bool>(mode, event);
            return cosim::scenario::variable_action{
                sim,
                var,
                cosim::scenario::boolean_modifier{modifier},
                isInput,
            };
        }
        case cosim::variable_type::string: {
            const auto modifier = generate_string_modifier(mode, event);
            return cosim::scenario::variable_action{
                sim,
                var,
                cosim::scenario::string_modifier{modifier},
                isInput,
            };
        }
        case cosim::variable_type::enumeration:
        case cosim::variable_type::float32:
        case cosim::variable_type::int8:
        case cosim::variable_type::uint8:
        case cosim::variable_type::int16:
        case cosim::variable_type::uint16:
        case cosim::variable_type::uint32:
        case cosim::variable_type::int64:
        case cosim::variable_type::uint64:
        case cosim::variable_type::binary:
        case cosim::variable_type::clock:
            break;
    }
    std::ostringstream oss;
    oss << "No scenario action support for variable type: "
        << to_text(variable.type);
    throw std::invalid_argument(oss.str());
}

struct defaults
{
    std::optional<std::string> model;
    std::optional<std::string> variable;
    std::optional<std::string> action;
};

std::optional<std::string> parse_element(
    const YAML::Node& node,
    const std::string& name)
{
    if (node[name]) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        return node[name].as<std::string>(); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
    } else {
        return std::nullopt;
    }
}

defaults parse_defaults(const YAML::Node& scenario)
{
    if (scenario["defaults"]) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        const auto node = scenario["defaults"]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        return defaults{
            parse_element(node, "model"),
            parse_element(node, "variable"),
            parse_element(node, "action"),
        };
    }
    return defaults{};
}

std::string specified_or_default(
    const YAML::Node& node,
    const std::string& name,
    std::optional<std::string> defaultOption)
{
    if (node[name]) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        return node[name].as<std::string>(); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
    } else if (defaultOption.has_value()) {
        return *defaultOption;
    }
    std::ostringstream oss;
    oss << "Option is not specified explicitly nor in defaults: " << name;
    throw std::invalid_argument(oss.str());
}

std::optional<cosim::time_point> parse_end_time(const YAML::Node& node)
{
    if (node["end"]) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        auto const endTime = node["end"].as<double>(); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        return to_time_point(endTime);
    }
    return std::nullopt;
}

} // namespace


scenario::scenario parse_scenario(
    const cosim::filesystem::path& scenarioFile,
    const std::unordered_map<simulator_index,
        manipulable*>& simulators)
{
    std::ifstream input(scenarioFile);
    if (!input) {
        std::ostringstream oss;
        oss << "Cannot load scenario. Failed to open file " << scenarioFile;
        throw std::system_error(errno, std::system_category(), oss.str());
    }
    YAML::Node scenarioNode = YAML::Load(input);

    std::vector<scenario::event> events;
    const defaults defaultOpts = parse_defaults(scenarioNode);

    for (const auto& event : scenarioNode["events"]) { // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        const auto trigger = event["time"]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): YAML mapping lookup.
        auto const triggerTime = trigger.as<double>();
        const auto time = to_time_point(triggerTime);

        const auto [index, simulator] =
            find_simulator(simulators, specified_or_default(event, "model", defaultOpts.model));
        const auto varName =
            specified_or_default(event, "variable", defaultOpts.variable);
        const auto variable = find_variable(simulator->model_description(), varName);
        if (!variable) {
            throw std::invalid_argument(
                "Can't find variable '" + varName + "' in model '" + simulator->name() + "'");
        }
        const auto& var = *variable;

        const auto mode = specified_or_default(event, "action", defaultOpts.action);
        bool const isInput = is_input(var.causality);
        const scenario::variable_action action =
            generate_action(event, mode, index, var, isInput, var.reference);
        events.emplace_back(scenario::event{time, action});
    }

    const auto end = parse_end_time(scenarioNode);
    return scenario::scenario{events, end};
}
} // namespace cosim
