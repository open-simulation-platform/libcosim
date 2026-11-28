/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/osp_clock.hpp"

#include "cosim/exception.hpp"
#include "cosim/model_description.hpp"
#include "cosim/orchestration.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <gsl/span>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cosim::detail
{
namespace
{
constexpr value_reference input_reference = 1;
constexpr value_reference output_reference = 2;

class clock_source final : public slave, public clock_controller // NOLINT(misc-multiple-inheritance): The source implements both slave and Clock controller interfaces.
{
public:
    explicit clock_source(std::shared_ptr<const cosim::model_description> description)
        : description_(std::move(description))
    { }

    [[nodiscard]] cosim::model_description model_description() const override { return *description_; }
    clock_controller* get_clock_controller() noexcept override { return this; }
    event_controller* get_event_controller() noexcept override { return this; }
    void setup(time_point /*startTime*/, std::optional<time_point> /*stopTime*/, std::optional<double> /*relativeTolerance*/) override { }
    void start_simulation() override { }
    void end_simulation() override { }
    step_result do_step(time_point /*currentT*/, duration /*deltaT*/) override { return step_result::complete; }

    [[nodiscard]] std::optional<clock_description> resolved_input_clock_schedule(value_reference reference) const override
    {
        if (reference != input_reference) {
            throw std::out_of_range("Unknown OSP input Clock");
        }
        return description_->variables.front().clock;
    }

    clock_interval_update get_input_clock_interval(value_reference reference) override
    {
        const auto schedule = resolved_input_clock_schedule(reference).value(); // NOLINT(bugprone-unchecked-optional-access): The model always supplies the input Clock schedule.
        return {
            clock_interval_qualifier::unchanged,
            schedule.interval_decimal.value_or(0.0),
            schedule.interval_counter,
            schedule.resolution,
        };
    }

    [[nodiscard]] bool is_in_event_mode() const noexcept override { return eventMode_; }
    void enter_event_mode() override { eventMode_ = true; }
    void enter_step_mode() override
    {
        active_ = false;
        eventMode_ = false;
    }
    clock_update_result update_clock_state() override { return {}; }
    bool get_output_clock(value_reference reference) override
    {
        if (reference != output_reference) {
            throw std::out_of_range("Unknown OSP output Clock");
        }
        return active_;
    }
    void set_input_clock(value_reference reference, bool active) override
    {
        if (reference != input_reference) {
            throw std::out_of_range("Unknown OSP input Clock");
        }
        active_ = active;
    }
    variable_value get_clocked_variable(variable_type /*type*/, value_reference /*reference*/) override { no_data(); }
    void set_clocked_variable(variable_type /*type*/, value_reference /*reference*/, const variable_value& /*value*/) override { no_data(); }

    void get_real_variables(gsl::span<const value_reference> refs, gsl::span<double> /*values*/) const override { require_empty(refs); }
    void get_integer_variables(gsl::span<const value_reference> refs, gsl::span<int> /*values*/) const override { require_empty(refs); }
    void get_boolean_variables(gsl::span<const value_reference> refs, gsl::span<bool> /*values*/) const override { require_empty(refs); }
    void get_string_variables(gsl::span<const value_reference> refs, gsl::span<std::string> /*values*/) const override { require_empty(refs); }
    void set_real_variables(gsl::span<const value_reference> refs, gsl::span<const double> /*values*/) override { require_empty(refs); }
    void set_integer_variables(gsl::span<const value_reference> refs, gsl::span<const int> /*values*/) override { require_empty(refs); }
    void set_boolean_variables(gsl::span<const value_reference> refs, gsl::span<const bool> /*values*/) override { require_empty(refs); }
    void set_string_variables(gsl::span<const value_reference> refs, gsl::span<const std::string> /*values*/) override { require_empty(refs); }

    state_index save_state() override { no_state(); }
    void save_state(state_index /*stateIndex*/) override { no_state(); }
    void restore_state(state_index /*stateIndex*/) override { no_state(); }
    void release_state(state_index /*stateIndex*/) override { no_state(); }
    [[nodiscard]] serialization::node export_state(state_index /*stateIndex*/) const override { no_state(); }
    state_index import_state(const serialization::node& /*exportedState*/) override { no_state(); }

private:
    [[noreturn]] static void no_data() { throw std::logic_error("An OSP Clock has no data variables"); }
    static void require_empty(gsl::span<const value_reference> refs)
    {
        if (!refs.empty()) {
            no_data();
        }
    }
    [[noreturn]] static void no_state()
    {
        throw error(make_error_code(errc::unsupported_feature), "An OSP Clock cannot save simulator state");
    }
    std::shared_ptr<const cosim::model_description> description_;
    bool eventMode_ = false;
    bool active_ = false;
};

class clock_source_model final : public model
{
public:
    explicit clock_source_model(const clock_description& schedule)
    {
        auto description = std::make_shared<model_description>();
        description->name = "OSP Clock";
        description->uuid = "d60ec930-314d-4c28-aa59-dc11b3478b63";
        description->capabilities.can_save_state = false;
        variable_description input{};
        input.reference = input_reference;
        input.name = "in";
        input.type = variable_type::clock;
        input.causality = variable_causality::input;
        input.variability = variable_variability::discrete;
        input.clock = schedule;
        auto output = input;
        output.reference = output_reference;
        output.name = "out";
        output.causality = variable_causality::output;
        output.clock.emplace();
        output.clock->interval_variability = clock_interval_variability::triggered;
        output.clocks = {input_reference};
        description->variables = {std::move(input), std::move(output)};
        description_ = std::move(description);
    }

    [[nodiscard]] std::shared_ptr<const model_description> description() const noexcept override { return description_; }
    std::shared_ptr<slave> instantiate(std::string_view /*name*/) override
    {
        return std::make_shared<clock_source>(description_);
    }

private:
    std::shared_ptr<const model_description> description_;
};
} // namespace

std::shared_ptr<model> make_osp_clock_model(const clock_description& schedule)
{
    return std::make_shared<clock_source_model>(schedule);
}
} // namespace cosim::detail
