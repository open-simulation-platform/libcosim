/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_OBSERVER_SLAVE_VALUE_PROVIDER_HPP
#define COSIM_OBSERVER_SLAVE_VALUE_PROVIDER_HPP

#include <cosim/execution.hpp>
#include <cosim/model_description.hpp>
#include <cosim/observer/observer.hpp>

#include <gsl/span>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cosim
{

class slave_value_provider
{

public:
    explicit slave_value_provider(observable* obs);
    ~slave_value_provider() noexcept;
    slave_value_provider(const slave_value_provider&) = delete;
    slave_value_provider& operator=(const slave_value_provider&) = delete;
    slave_value_provider(slave_value_provider&&) = delete;
    slave_value_provider& operator=(slave_value_provider&&) = delete;
    void observe();
    variable_value get_variable_value(variable_type type, value_reference reference);
    void get_real(gsl::span<const value_reference> variables, gsl::span<double> values);
    void get_int(gsl::span<const value_reference> variables, gsl::span<int> values);
    void get_boolean(gsl::span<const value_reference> variables, gsl::span<bool> values);
    void get_string(gsl::span<const value_reference> variables, gsl::span<std::string> values);

private:
    std::map<variable_type, std::map<value_reference, variable_value>> samples_;
    observable* observable_;
    std::mutex lock_;
};

} // namespace cosim

#endif // Header guard
