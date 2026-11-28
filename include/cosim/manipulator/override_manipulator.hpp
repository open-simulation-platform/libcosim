/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_MANIPULATOR_OVERRIDE_MANIPULATOR_HPP
#define LIBCOSIM_MANIPULATOR_OVERRIDE_MANIPULATOR_HPP

#include <cosim/execution.hpp>
#include <cosim/manipulator/manipulator.hpp>
#include <cosim/model_description.hpp>
#include <cosim/scenario.hpp>
#include <cosim/time.hpp>

#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cosim
{

/**
 *  A manipulator implementation handling overrides of variable values.
 */
class override_manipulator : public manipulator // NOLINT(cppcoreguidelines-special-member-functions): Preserve existing interface copy and move behavior.
{
public:
    void simulator_added(simulator_index /*index*/, manipulable* /*sim*/, time_point /*unused*/) override;

    void simulator_removed(simulator_index /*index*/, time_point /*unused*/) override;

    void step_commencing(time_point currentTime) override;

    /// Override the value of a variable with type `real`
    void override_real_variable(simulator_index /*index*/, value_reference /*variable*/, double value);
    /// Override the value of a variable with type `integer`
    void override_integer_variable(simulator_index /*index*/, value_reference /*variable*/, int value);
    /// Override the value of a variable with type `boolean`
    void override_boolean_variable(simulator_index /*index*/, value_reference /*variable*/, bool value);
    /// Override the value of a variable with type `string`
    void override_string_variable(simulator_index /*index*/, value_reference /*variable*/, const std::string& value);
    /// Override one complete scalar or array value.
    void override_variable(
        simulator_index /*index*/,
        variable_type /*type*/,
        value_reference /*variable*/,
        variable_value value);
    /// Reset override of a variable
    void reset_variable(simulator_index /*index*/, variable_type /*type*/, value_reference /*variable*/);

    ~override_manipulator() noexcept override;

private:
    void add_action(
        simulator_index index,
        value_reference variable,
        variable_type type,
        const std::variant<
            scenario::real_modifier,
            scenario::integer_modifier,
            scenario::boolean_modifier,
            scenario::string_modifier,
            scenario::variable_modifier>& modifier);

    std::unordered_map<simulator_index, manipulable*> simulators_;
    std::vector<scenario::variable_action> actions_;
    std::mutex lock_;
};

} // namespace cosim

#endif
