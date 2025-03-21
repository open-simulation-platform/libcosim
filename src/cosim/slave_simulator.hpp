/**
 *  \file
 *  Defines `cosim::slave_simulator`
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_SLAVE_SIMULATOR_HPP
#define COSIM_SLAVE_SIMULATOR_HPP

#include <cosim/algorithm.hpp>
#include <cosim/slave.hpp>

#include <memory>
#include <string_view>


namespace cosim
{

class slave_simulator : public simulator
{
public:
    slave_simulator(std::shared_ptr<slave> slave, std::string_view name);

    ~slave_simulator() noexcept;

    slave_simulator(const slave_simulator&) = delete;
    slave_simulator& operator=(const slave_simulator&) = delete;

    slave_simulator(slave_simulator&&) noexcept;
    slave_simulator& operator=(slave_simulator&&) noexcept;

    // `observable` methods
    std::string name() const override;
    cosim::model_description model_description() const override;

    void expose_for_getting(variable_type type, value_reference ref) override;
    double get_real(value_reference reference) const override;
    int get_integer(value_reference reference) const override;
    bool get_boolean(value_reference reference) const override;
    std::string_view get_string(value_reference reference) const override;

    // `simulator` methods
    void expose_for_setting(variable_type type, value_reference ref) override;
    void set_real(value_reference reference, double value) override;
    void set_integer(value_reference reference, int value) override;
    void set_boolean(value_reference reference, bool value) override;
    void set_string(value_reference reference, std::string_view value) override;

    void set_real_input_modifier(
        value_reference reference,
        std::function<double(double, duration)> modifier) override;
    void set_integer_input_modifier(
        value_reference reference,
        std::function<int(int, duration)> modifier) override;
    void set_boolean_input_modifier(
        value_reference reference,
        std::function<bool(bool, duration)> modifier) override;
    void set_string_input_modifier(
        value_reference reference,
        std::function<std::string(std::string_view, duration)> modifier) override;
    void set_real_output_modifier(
        value_reference reference,
        std::function<double(double, duration)> modifier) override;
    void set_integer_output_modifier(
        value_reference reference,
        std::function<int(int, duration)> modifier) override;
    void set_boolean_output_modifier(
        value_reference reference,
        std::function<bool(bool, duration)> modifier) override;
    void set_string_output_modifier(
        value_reference reference,
        std::function<std::string(std::string_view, duration)> modifier) override;

    std::unordered_set<value_reference>& get_modified_real_variables() const override;
    std::unordered_set<value_reference>& get_modified_integer_variables() const override;
    std::unordered_set<value_reference>& get_modified_boolean_variables() const override;
    std::unordered_set<value_reference>& get_modified_string_variables() const override;

    void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    void do_iteration() override;

    void start_simulation() override;

    step_result do_step(
        time_point currentT,
        duration deltaT) override;

    state_index save_state() override;
    void save_state(state_index stateIndex) override;
    void restore_state(state_index stateIndex) override;
    void release_state(state_index stateIndex) override;
    serialization::node export_state(state_index stateIndex) const override;
    state_index import_state(const serialization::node& exportedState) override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cosim
#endif // COSIM_SLAVE_SIMULATOR_HPP
