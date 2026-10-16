/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_PROXY_REMOTE_SLAVE_HPP
#define COSIM_PROXY_REMOTE_SLAVE_HPP

#include <cosim/model_description.hpp>
#include <cosim/slave.hpp>
#include <cosim/time.hpp>

#include <proxyfmu/fmi/slave.hpp>

#include <string>

namespace cosim
{

namespace proxy
{

class remote_slave : public slave
{

public:
    remote_slave(std::unique_ptr<proxyfmu::fmi::slave> slave,
        std::shared_ptr<const cosim::model_description> modelDescription);

    cosim::model_description model_description() const override;

    void setup(time_point startTime, std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    void start_simulation() override;

    void end_simulation() override;

    cosim::step_result do_step(time_point currentT, duration deltaT) override;

    void get_real_variables(gsl::span<const value_reference> variables, gsl::span<double> values) const override;

    void get_integer_variables(gsl::span<const value_reference> variables, gsl::span<int> values) const override;

    void get_boolean_variables(gsl::span<const value_reference> variables, gsl::span<bool> values) const override;

    void get_string_variables(gsl::span<const value_reference> variables,
        gsl::span<std::string> values) const override;

    void set_real_variables(gsl::span<const value_reference> variables, gsl::span<const double> values) override;

    void set_integer_variables(gsl::span<const value_reference> variables, gsl::span<const int> values) override;

    void set_boolean_variables(gsl::span<const value_reference> variables, gsl::span<const bool> values) override;

    void set_string_variables(gsl::span<const value_reference> variables,
        gsl::span<const std::string> values) override;

    /// Retrieves variables of every type in a single call to the remote slave.
    void get_variables(const variable_references& variables, variable_values& values) const override;

    /// Sets variables of every type in a single call to the remote slave.
    void set_variables(const variable_references& variables, const variable_values_view& values) override;

    // Keep the convenience overloads from `slave` visible.
    using slave::get_variables;
    using slave::set_variables;

    /**
     *  Sets variables, performs a time step and retrieves variables in a
     *  single call to the remote slave.
     *
     *  \throws cosim::error
     *      With `errc::model_error` if the remote slave reports a failure in
     *      any part of the sequence, or returns the wrong number of values.
     */
    step_result set_variables_do_step_get_variables(
        const variable_references& setVariables,
        const variable_values_view& setValues,
        time_point currentT,
        duration deltaT,
        const variable_references& getVariables,
        variable_values& getValues) override;

    state_index save_state() override;

    void save_state(state_index stateIndex) override;

    void restore_state(state_index stateIndex) override;

    void release_state(state_index stateIndex) override;

    serialization::node export_state(state_index stateIndex) const override;

    state_index import_state(const serialization::node& exportedState) override;

    ~remote_slave() override;

private:
    bool terminated_;
    cosim::time_point startTime_;
    std::unique_ptr<proxyfmu::fmi::slave> slave_;
    std::shared_ptr<const cosim::model_description> modelDescription_;
};

} // namespace proxy

} // namespace cosim

#endif
