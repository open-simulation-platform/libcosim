
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

    void set_source_value(variable_id, std::variant<double, int, bool, std::string_view> value) override;

    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id) override;

private:
    std::variant<double, int, bool, std::string_view> value_;
};

} // namespace cse

#endif
