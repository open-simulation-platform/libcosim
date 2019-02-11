
#ifndef CSECORE_LAST_VALUE_PROVIDER_HPP
#define CSECORE_LAST_VALUE_PROVIDER_HPP

#include <cse/model.hpp>
#include <cse/observer/observer.hpp>

#include <gsl/span>

namespace cse
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
        gsl::span<const variable_index> variables,
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
        gsl::span<const variable_index> variables,
        gsl::span<int> values) = 0;
};

} // namespace cse

#endif //CSECORE_LAST_VALUE_PROVIDER_HPP
