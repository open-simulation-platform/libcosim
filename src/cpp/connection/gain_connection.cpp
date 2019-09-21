
#include "cse/connection/gain_connection.hpp"

void cse::gain_connection::set_source_value(cse::variable_id id, std::variant<double, int, bool, std::string_view> value)
{
}
std::variant<double, int, bool, std::string_view> cse::gain_connection::get_destination_value(cse::variable_id id)
{
    return std::variant<double, int, bool, std::string_view>();
}
