/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_LAST_VALUE_PROVIDER_HPP
#define LIBCOSIM_LAST_VALUE_PROVIDER_HPP

#include <cosim/model_description.hpp>
#include <cosim/observer/observer.hpp>

#include <gsl/span>

#include <string>

namespace cosim
{

/**
 *  An interface for last-value providers.
 *
 *  The methods in this interface represent ways to extract data from an
 *  observer providing the last observed values for a range of variables.
 */
class last_value_provider : public observer
{
public:
    /**
     * Retrieves the latest observed values for a range of real variables.
     *
     * \param [in] sim index of the simulator
     * \param [in] variables the variable indices to retrieve values for
     * \param [out] values a collection where the observed values will be stored
     */
    virtual void get_real(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<double> values) = 0;

    /**
     * Retrieves the latest observed values for a range of integer variables.
     *
     * \param [in] sim index of the simulator
     * \param [in] variables the variable indices to retrieve values for
     * \param [out] values a collection where the observed values will be stored
     */
    virtual void get_integer(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<int> values) = 0;

    /**
     * Retrieves the latest observed values for a range of boolean variables.
     *
     * \param [in] sim index of the simulator
     * \param [in] variables the variable indices to retrieve values for
     * \param [out] values a collection where the observed values will be stored
     */
    virtual void get_boolean(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<bool> values) = 0;

    /**
     * Retrieves the latest observed values for a range of string variables.
     *
     * \param [in] sim index of the simulator
     * \param [in] variables the variable indices to retrieve values for
     * \param [out] values a collection where the observed values will be stored
     */
    virtual void get_string(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<std::string> values) = 0;
};

} // namespace cosim

#endif //LIBCOSIM_LAST_VALUE_PROVIDER_HPP
