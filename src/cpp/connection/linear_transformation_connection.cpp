#include <cse/connection/linear_transformation_connection.hpp>

#include <cse/error.hpp>

cse::linear_transformation_connection::linear_transformation_connection(variable_id source, variable_id destination, double offset, double factor)
    : connection({source}, {destination})
    , offset_(offset)
    , factor_(factor)
{
    CSE_INPUT_CHECK(source.type == cse::variable_type::real || source.type == cse::variable_type::integer);
}

void cse::linear_transformation_connection::set_source_value(cse::variable_id id, std::variant<double, int, bool, std::string_view> value)
{

}
std::variant<double, int, bool, std::string_view> cse::linear_transformation_connection::get_destination_value(cse::variable_id id)
{
    return std::variant<double, int, bool, std::string_view>();
}
