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

#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/time.hpp"
#include <cosim/algorithm.hpp>
#include <cosim/slave.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>


namespace cosim
{

/**
 * Adapts an owning slave instance to the algorithm-facing simulator API.
 * Exposed values and modifiers are cached; Event and Clock operations are
 * forwarded only when the slave exposes the corresponding controller.
 */
class slave_simulator : public simulator
{
public:
    slave_simulator(std::shared_ptr<slave> slave, std::string_view name);

    ~slave_simulator() noexcept override;

    slave_simulator(const slave_simulator&) = delete;
    slave_simulator& operator=(const slave_simulator&) = delete;

    slave_simulator(slave_simulator&&) noexcept;
    slave_simulator& operator=(slave_simulator&&) noexcept;

    /// Returns the underlying owning slave.
    [[nodiscard]] std::shared_ptr<slave> get_slave() const;

    // `observable` methods
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] cosim::model_description model_description() const override;

    void expose_for_getting(variable_type type, value_reference ref) override;
    [[nodiscard]] double get_real(value_reference reference) const override;
    [[nodiscard]] int get_integer(value_reference reference) const override;
    [[nodiscard]] bool get_boolean(value_reference reference) const override;
    [[nodiscard]] std::string_view get_string(value_reference reference) const override;
    [[nodiscard]] variable_value get_variable_value(
        variable_type type,
        value_reference reference) const override;

    // `simulator` methods
    clock_controller* get_clock_controller() noexcept override;
    event_controller* get_event_controller() noexcept override;
    void set_max_event_iterations(std::size_t limit) override;
    void begin_input_event_coordination(time_point currentTime) override;
    void defer_input_events(time_point currentTime) override;
    bool prepare_input_events(
        duration deltaT,
        bool evaluateUnstagedModifiers = true) override;
    bool reevaluate_input_modifiers(duration deltaT) override;
    void apply_event_inputs() override;
    void apply_coupled_event_inputs() override;
    [[nodiscard]] bool has_applicable_event_inputs() const override;
    void finish_input_event_coordination() override;
    void refresh_event_values() override;
    [[nodiscard]] variable_value get_event_output_value(variable_type type, value_reference ref) const override;
    void stage_clocked_input(
        variable_type type, value_reference ref, const variable_value& value) override;
    void expose_for_setting(variable_type type, value_reference ref) override;
    void signal_input_discontinuity(variable_type type, value_reference ref) override;
    void set_input_sample_discontinuity_policy(
        variable_type type, value_reference ref, bool enabled) override;
    void set_input_modifier_replay_safe(
        variable_type type, value_reference ref, bool enabled) override;
    void set_real(value_reference reference, double value) override;
    void set_integer(value_reference reference, int value) override;
    void set_boolean(value_reference reference, bool value) override;
    void set_string(value_reference reference, std::string_view value) override;
    void set_variable_value(
        variable_type type,
        value_reference reference,
        const variable_value& value) override;

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
    void set_variable_input_modifier(
        variable_type type,
        value_reference reference,
        variable_modifier modifier) override;
    void set_variable_output_modifier(
        variable_type type,
        value_reference reference,
        variable_modifier modifier) override;

    [[nodiscard]] std::unordered_set<value_reference>& get_modified_real_variables() const override;
    [[nodiscard]] std::unordered_set<value_reference>& get_modified_integer_variables() const override;
    [[nodiscard]] std::unordered_set<value_reference>& get_modified_boolean_variables() const override;
    [[nodiscard]] std::unordered_set<value_reference>& get_modified_string_variables() const override;
    [[nodiscard]] bool has_variable_modifiers() const override;
    [[nodiscard]] bool supports_automatic_step_replay() const override;
    [[nodiscard]] std::vector<typed_value_reference> get_modified_variables() const;

    void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    void do_iteration() override;

    void start_simulation() override;

    bool apply_pending_step_inputs(duration deltaT) override;

    step_result do_step(
        time_point currentT,
        duration deltaT) override;

    step_result_info do_step_with_info(
        time_point currentT,
        duration deltaT) override;

    void handle_events() override;

    [[nodiscard]] event_handling_status get_event_handling_status() const noexcept override;

    state_index save_state() override;
    void save_state(state_index stateIndex) override;
    void restore_state(state_index stateIndex) override;
    void release_state(state_index stateIndex) override;
    [[nodiscard]] serialization::node export_state(state_index stateIndex) const override;
    state_index import_state(const serialization::node& exportedState) override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cosim
#endif // COSIM_SLAVE_SIMULATOR_HPP
