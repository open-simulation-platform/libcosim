#include <cse/connection/linear_transformation_connection.hpp>
#include <cse/error.hpp>

namespace cse
{

linear_transformation_connection::linear_transformation_connection(variable_id source, variable_id destination, double offset, double factor)
    : scalar_connection(source, destination)
    , offset_(offset)
    , factor_(factor)
{
    CSE_INPUT_CHECK(source.type == variable_type::real || source.type == variable_type::integer);
}

void linear_transformation_connection::set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value)
{
    switch (id.type) {
        case variable_type::real:
            value_ = std::get<double>(value) * factor_ + offset_;
            break;
        case variable_type::integer:
            value_ = std::get<int>(value) * static_cast<int>(factor_) + static_cast<int>(offset_);
            break;
        default:
            CSE_PANIC();
    }
}

} // namespace cse
