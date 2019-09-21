
#include "cse/connection/scalar_connection.hpp"

#include "cse/error.hpp"

namespace cse
{

scalar_connection::scalar_connection(variable_id source, variable_id destination)
    : connection({source}, {destination})
{
    CSE_INPUT_CHECK(source.type == destination.type);
    switch (source.type) {
        case variable_type::real:
            value_ = double();
            break;
        case variable_type::integer:
            value_ = int();
            break;
        case variable_type::boolean:
            value_ = bool();
            break;
        case variable_type::string:
            value_ = std::string_view();
            break;
        default:
            CSE_PANIC();
    }
}

void scalar_connection::set_source_value(variable_id, std::variant<double, int, bool, std::string_view> value)
{
    value_ = value;
}

std::variant<double, int, bool, std::string_view> scalar_connection::get_destination_value(variable_id)
{
    return value_;
}

} // namespace cse
