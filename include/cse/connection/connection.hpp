
#ifndef CSECORE_CONNECTION_CONNECTION_HPP
#define CSECORE_CONNECTION_CONNECTION_HPP


#include "cse/execution.hpp"
#include "cse/model.hpp"

#include <gsl/span>

#include <string_view>
#include <variant>
#include <vector>

namespace cse
{

/**
 * A base class representing a connection between variables in a co-simulation.
 *
 * This will be used by a co-simulation algorithm to transfer values from
 * a number of source variables to a number of destination variables. Depending
 * on the implementation, a calculation of destination variable values based
 * on source variable values may occur.
 */
class connection
{
public:
    /// Returns the source variables of the connection.
    gsl::span<variable_id> get_sources()
    {
        return gsl::make_span(sources_);
    }

    /// Sets the value of a source variable.
    virtual void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) = 0;

    /// Returns the destination variables of the connection.
    gsl::span<variable_id> get_destinations()
    {
        return gsl::make_span(destinations_);
    }

    /// Returns the value of a destination variable.
    virtual std::variant<double, int, bool, std::string_view> get_destination_value(variable_id id) = 0;

protected:
    /// Base class constructor.
    connection(std::vector<variable_id> sources, std::vector<variable_id> destinations)
        : sources_(std::move(sources))
        , destinations_(std::move(destinations))
    {}
    /// Source variables.
    std::vector<variable_id> sources_;
    /// Destination variables.
    std::vector<variable_id> destinations_;
};

} // namespace cse

#endif
