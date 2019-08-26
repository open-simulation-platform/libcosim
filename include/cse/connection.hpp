#ifndef CSECORE_CONNECTION_HPP
#define CSECORE_CONNECTION_HPP

#include "exception.hpp"

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

/**
 * A class representing a one-to-one connection between two variables.
 *
 * Both variables are required to be of the same type.
 */
class scalar_connection : public connection
{

public:
    /// Constructor which takes one source and one destination variable.
    scalar_connection(variable_id source, variable_id destination);

    void set_source_value(variable_id, std::variant<double, int, bool, std::string_view> value) override;

    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id) override;

private:
    std::variant<double, int, bool, std::string_view> value_;
};

/**
 * A class representing a sum connection.
 *
 * The destination value is calculated as the sum of all source variable
 * values. Only valid when used with variables of type `real` or `integer`.
 * Mixing of variable types is not allowed.
 */
class sum_connection : public connection
{

public:
    /// Constructor which takes a number of source variables and one destination variable.
    sum_connection(const std::vector<variable_id>& sources, variable_id destination);

    void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) override;

    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id id) override;

private:
    std::unordered_map<variable_id, std::variant<double, int, bool, std::string_view>> values_;
};
} // namespace cse

#endif //CSECORE_CONNECTION_HPP
