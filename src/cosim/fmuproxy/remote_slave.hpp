/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMUPROXY_REMOTE_SLAVE_HPP
#define COSIM_FMUPROXY_REMOTE_SLAVE_HPP

#include <cosim/async_slave.hpp>
#include <cosim/fmuproxy/fmu_service.hpp>
#include <cosim/fmuproxy/thrift_state.hpp>
#include <cosim/model_description.hpp>
#include <cosim/time.hpp>

#include <string>

namespace cosim
{

namespace fmuproxy
{

class remote_slave : public slave
{

public:
    remote_slave(std::string instanceId,
        std::shared_ptr<thrift_state> state,
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

    ~remote_slave() override;

private:
    bool terminated_;
    std::string instanceId_;
    cosim::time_point startTime_;
    std::shared_ptr<cosim::fmuproxy::thrift_state> state_;
    std::shared_ptr<const cosim::model_description> modelDescription_;
};

} // namespace fmuproxy

} // namespace cosim

#endif
