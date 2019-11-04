
#ifndef CSECORE_CONNECTION_SCALAR_CONNECTION_HPP
#define CSECORE_CONNECTION_SCALAR_CONNECTION_HPP

#include <cse/connection/connection.hpp>

namespace cse
{

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

    void set_source_value(variable_id, scalar_value_view value) override;

    scalar_value_view get_destination_value(variable_id) override;

protected:
    scalar_value_view value_;
};

} // namespace cse

#endif
