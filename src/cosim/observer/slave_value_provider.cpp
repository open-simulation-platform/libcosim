/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/slave_value_provider.hpp"

#include "cosim/model_description.hpp"
#include "cosim/observer/observer.hpp"

#include <gsl/span>

#include <cstddef>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cosim
{

namespace
{

template<typename T, typename Values>
void get(
    gsl::span<const value_reference> variables,
    const std::map<value_reference, variable_value>& samples,
    gsl::span<T> values)
{
    if (samples.empty()) {
        throw std::out_of_range("no samples available");
    }
    for (std::size_t i = 0; i < values.size(); i++) {
        const auto& sample = samples.at(variables[i]); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): gsl::span checks its bounds.
        if (!sample.is_scalar()) {
            throw std::invalid_argument("Legacy scalar access does not support arrays");
        }
        values[i] = std::get<Values>(sample.values()).values.at(0); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): gsl::span checks its bounds.
    }
}

} // namespace

slave_value_provider::slave_value_provider(observable* observable)
    : observable_(observable)
{
    for (const auto& variable : observable->model_description().variables) {
        if (variable.type == variable_type::clock) {
            continue;
        }
        observable->expose_for_getting(variable.type, variable.reference);
        samples_.try_emplace(variable.type).first->second.emplace(variable.reference, variable_value{});
    }
}

slave_value_provider::~slave_value_provider() noexcept = default;

void slave_value_provider::observe()
{
    std::scoped_lock const lock(lock_);

    for (auto& [type, typedSamples] : samples_) {
        for (auto& [reference, value] : typedSamples) {
            value = observable_->get_variable_value(type, reference);
        }
    }
}

variable_value slave_value_provider::get_variable_value(
    variable_type type,
    value_reference reference)
{
    std::scoped_lock const lock(lock_);
    const auto& typedSamples = samples_.at(type);
    if (typedSamples.empty()) {
        throw std::out_of_range("no samples available");
    }
    return typedSamples.at(reference);
}

void slave_value_provider::get_real(gsl::span<const value_reference> variables, gsl::span<double> values)
{
    std::scoped_lock const lock(lock_);
    get<double, real_values>(variables, samples_.at(variable_type::real), values);
}

void slave_value_provider::get_int(gsl::span<const value_reference> variables, gsl::span<int> values)
{
    std::scoped_lock const lock(lock_);
    get<int, integer_values>(
        variables, samples_.at(variable_type::integer), values);
}

void slave_value_provider::get_boolean(gsl::span<const value_reference> variables, gsl::span<bool> values)
{
    std::scoped_lock const lock(lock_);
    get<bool, boolean_values>(
        variables, samples_.at(variable_type::boolean), values);
}

void slave_value_provider::get_string(gsl::span<const value_reference> variables, gsl::span<std::string> values)
{
    std::scoped_lock const lock(lock_);
    get<std::string, string_values>(
        variables, samples_.at(variable_type::string), values);
}

} // namespace cosim
