/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/slave_value_provider.hpp"

#include "cosim/error.hpp"

#include <mutex>

namespace cosim
{

namespace
{

template<typename T>
void get(
    gsl::span<const value_reference> variables,
    const std::unordered_map<value_reference, T>& samples,
    gsl::span<T> values)
{
    if (samples.empty()) {
        throw std::out_of_range("no samples available");
    }
    for (std::size_t i = 0; i < values.size(); i++) {
        value_reference valueRef = variables[i];
        values[i] = samples.at(valueRef);
    }
}

} // namespace

slave_value_provider::slave_value_provider(observable* observable)
    : observable_(observable)
{
    for (const auto& vd : observable->model_description().variables) {
        observable->expose_for_getting(vd.type, vd.reference);
        switch (vd.type) {
            case cosim::variable_type::real:
                realSamples_[vd.reference] = double();
                continue;
            case cosim::variable_type::integer:
                intSamples_[vd.reference] = int();
                continue;
            case cosim::variable_type::boolean:
                boolSamples_[vd.reference] = bool();
                continue;
            case cosim::variable_type::string:
                stringSamples_[vd.reference] = std::string();
                continue;
            case cosim::variable_type::enumeration:
                enumerationSamples_.emplace(vd.reference, std::nullopt);
                continue;
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
                COSIM_PANIC();
        }
        COSIM_PANIC();
    }
}

slave_value_provider::~slave_value_provider() noexcept = default;

void slave_value_provider::observe()
{
    std::lock_guard<std::mutex> lock(lock_);

    for (auto& [idx, value] : realSamples_) {
        value = observable_->get_real(idx);
    }
    for (auto& [idx, value] : intSamples_) {
        value = observable_->get_integer(idx);
    }
    for (auto& [idx, value] : boolSamples_) {
        value = observable_->get_boolean(idx);
    }
    for (auto& [idx, value] : stringSamples_) {
        value = observable_->get_string(idx);
    }
    for (auto& [idx, value] : enumerationSamples_) {
        value = observable_->get_variable_value(variable_type::enumeration, idx);
    }
}

variable_value slave_value_provider::get_variable_value(
    variable_type type, value_reference reference)
{
    if (type != variable_type::enumeration) {
        throw std::invalid_argument("Unsupported generic observed variable type");
    }
    std::lock_guard<std::mutex> lock(lock_);
    const auto& sample = enumerationSamples_.at(reference);
    if (!sample) throw std::out_of_range("no samples available");
    return *sample;
}

void slave_value_provider::get_real(gsl::span<const value_reference> variables, gsl::span<double> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<double>(variables, realSamples_, values);
}

void slave_value_provider::get_int(gsl::span<const value_reference> variables, gsl::span<int> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<int>(variables, intSamples_, values);
}

void slave_value_provider::get_boolean(gsl::span<const value_reference> variables, gsl::span<bool> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<bool>(variables, boolSamples_, values);
}

void slave_value_provider::get_string(gsl::span<const value_reference> variables, gsl::span<std::string> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<std::string>(variables, stringSamples_, values);
}

} // namespace cosim
