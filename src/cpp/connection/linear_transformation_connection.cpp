#include <cse/connection/linear_transformation_connection.hpp>
#include <cse/error.hpp>

namespace cse
{

linear_transformation_connection::linear_transformation_connection(variable_id source, variable_id destination, double offset, double factor)
    : scalar_connection(source, destination)
    , offset_(offset)
    , factor_(factor)
{
    CSE_INPUT_CHECK(source.type == variable_type::real);
}

void linear_transformation_connection::set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value)
{
    scalar_connection::set_source_value(id, std::get<double>(value) * factor_ + offset_);
}

} // namespace cse
