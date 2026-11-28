/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/manipulator/override_manipulator.hpp"

#include "cosim/execution.hpp"
#include "cosim/manipulator/manipulator.hpp"
#include "cosim/model_description.hpp"
#include "cosim/scenario.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/enumeration.hpp"
#include "cosim/utility/utility.hpp"

#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cosim
{

namespace
{

cosim::variable_description find_variable_description(
    const std::vector<variable_description>& variables,
    const cosim::variable_type type,
    const cosim::value_reference reference)
{
    for (const auto& description : variables) {
        if ((description.reference == reference) && (description.type == type)) {
            return description;
        }
    }
    std::ostringstream oss;
    oss << "Can't find variable with reference: " << reference
        << " and type: " << to_text(type);
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
        << to_text(causality) << ".";
    throw std::invalid_argument(oss.str());
}

} // namespace


void override_manipulator::simulator_added(
    simulator_index index,
    manipulable* sim,
    time_point /*currentTime*/)
{
    simulators_[index] = sim;
}

void override_manipulator::simulator_removed(simulator_index index, time_point /*unused*/)
{
    simulators_.erase(index);
}

void override_manipulator::step_commencing(time_point /*currentTime*/)
{
    std::scoped_lock const lock(lock_);
    if (actions_.empty()) {
        return;
    }
    for (const auto& action : actions_) {
        auto* sim = simulators_.at(action.simulator);
        // Keep the modifier copies: std::function targets may have observable copy behavior.
        // NOLINTBEGIN(performance-unnecessary-value-param)
        std::visit(
            visitor(
                [=](scenario::real_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::real, action.variable);
                        sim->set_real_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::real, action.variable);
                        sim->set_real_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::integer_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::integer, action.variable);
                        sim->set_integer_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::integer, action.variable);
                        sim->set_integer_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::boolean_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::boolean, action.variable);
                        sim->set_boolean_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::boolean, action.variable);
                        sim->set_boolean_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::string_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::string, action.variable);
                        sim->set_string_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::string, action.variable);
                        sim->set_string_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::variable_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(modifier.type, action.variable);
                        sim->set_variable_input_modifier(
                            modifier.type, action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(modifier.type, action.variable);
                        sim->set_variable_output_modifier(
                            modifier.type, action.variable, modifier.f);
                    }
                }),
            action.modifier);
        // NOLINTEND(performance-unnecessary-value-param)
    }
    actions_.clear();
}

void override_manipulator::add_action(
    simulator_index index,
    value_reference variable,
    variable_type type,
    const std::variant<
        scenario::real_modifier,
        scenario::integer_modifier,
        scenario::boolean_modifier,
        scenario::string_modifier,
        scenario::variable_modifier>& modifier)
{
    const auto* sim = simulators_.at(index);
    const auto description = find_variable_description(
        sim->model_description().variables, type, variable);
    bool const input = is_input(description.causality);

    std::scoped_lock const lock(lock_);
    actions_.emplace_back(scenario::variable_action{index, variable, modifier, input});
}

void override_manipulator::override_real_variable(
    simulator_index index,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Preserve the public API.
    value_reference variable,
    double value)
{
    const auto modifier = [value](double /*original*/, duration) { return value; };
    add_action(index, variable, variable_type::real, scenario::real_modifier{modifier});
}

void override_manipulator::override_integer_variable(
    simulator_index index,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Preserve the public API.
    value_reference variable,
    int value)
{
    const auto modifier = [value](int /*original*/, duration) { return value; };
    add_action(index, variable, variable_type::integer, scenario::integer_modifier{modifier});
}

void override_manipulator::override_boolean_variable(
    simulator_index index,
    value_reference variable,
    bool value)
{
    const auto modifier = [value](bool /*original*/, duration) { return value; };
    add_action(index, variable, variable_type::boolean, scenario::boolean_modifier{modifier});
}

void override_manipulator::override_string_variable(
    simulator_index index,
    value_reference variable,
    const std::string& value)
{
    const auto modifier = [value](std::string_view /*original*/, duration) { return value; };
    add_action(index, variable, variable_type::string, scenario::string_modifier{modifier});
}

void override_manipulator::override_variable(
    simulator_index index,
    variable_type type,
    value_reference variable,
    variable_value value)
{
    if (type == variable_type::clock) {
        throw std::invalid_argument(
            "Clock variables cannot be overridden as data values");
    }
    if (const auto normalized = normalize_enumeration_value(type, value)) {
        value = *normalized;
    }
    if (value.type() != type) {
        throw std::invalid_argument(
            "Override value type does not match the selected variable type");
    }
    auto modifier = [value = std::move(value)](
                        const variable_value&,
                        duration) { return value; };
    add_action(
        index,
        variable,
        type,
        scenario::variable_modifier{type, std::move(modifier)});
}

void override_manipulator::reset_variable(
    simulator_index index,
    variable_type type,
    value_reference variable)
{
    const auto description = find_variable_description(
        simulators_.at(index)->model_description().variables,
        type,
        variable);
    if (!description.dimensions.empty() &&
        (type == variable_type::real ||
            type == variable_type::integer ||
            type == variable_type::boolean ||
            type == variable_type::string)) {
        add_action(
            index,
            variable,
            type,
            scenario::variable_modifier{type, nullptr});
        return;
    }
    switch (type) {
        case variable_type::real:
            add_action(index, variable, variable_type::real, scenario::real_modifier{nullptr});
            return;
        case variable_type::integer:
            add_action(index, variable, variable_type::integer, scenario::integer_modifier{nullptr});
            return;
        case variable_type::boolean:
            add_action(index, variable, variable_type::boolean, scenario::boolean_modifier{nullptr});
            return;
        case variable_type::string:
            add_action(index, variable, variable_type::string, scenario::string_modifier{nullptr});
            return;
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
            add_action(
                index,
                variable,
                type,
                scenario::variable_modifier{type, nullptr});
            return;
        case variable_type::clock:
            throw std::invalid_argument(
                "Clock variables cannot be reset as data values");
    }
    throw std::invalid_argument("Unsupported variable type");
}

override_manipulator::~override_manipulator() noexcept = default;

} // namespace cosim
