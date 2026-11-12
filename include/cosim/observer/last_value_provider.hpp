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
    /// Retrieves the latest observed owning scalar value.
    virtual variable_value get_variable_value(
        simulator_index sim, variable_type type, value_reference reference)
    {
        switch (type) {
            case variable_type::real: {
                double value;
                get_real(sim, gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
                return variable_value(real_values{{value}});
            }
            case variable_type::integer: {
                int value;
                get_integer(sim, gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
                return variable_value(integer_values{{value}});
            }
            case variable_type::boolean: {
                bool value;
                get_boolean(sim, gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
                return variable_value(boolean_values{{value}});
            }
            case variable_type::string: {
                std::string value;
                get_string(sim, gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
                return variable_value(string_values{{std::move(value)}});
            }
            case variable_type::enumeration:
            case variable_type::float32:
            case variable_type::int8:
            case variable_type::uint8:
            case variable_type::int16:
            case variable_type::uint16:
            case variable_type::uint32:
            case variable_type::int64:
            case variable_type::uint64:
            case variable_type::binary:
            case variable_type::clock:
                break;
        }
        throw std::logic_error("This last-value provider does not support the requested type");
    }

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

#endif // LIBCOSIM_LAST_VALUE_PROVIDER_HPP
