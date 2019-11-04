
#include "cse/error.hpp"
#include "cse/exception.hpp"
#include "cse/connection/sum_connection.hpp"

namespace cse
{

sum_connection::sum_connection(const std::vector<variable_id>& sources, variable_id destination)
    : connection(sources, {destination})
{
    bool failed = false;
    std::ostringstream oss;
    if (destination.type != variable_type::real && destination.type != variable_type::integer) {
        failed = true;
        oss << "Cannot create a sum_connection to this variable: " << destination
            << ", type " << destination.type << " is not supported. ";
    }
    for (const auto id : sources) {
        if (id.type != destination.type) {
            failed = true;
            oss << "Mixing of variable types for sum_connection not supported: "
                << destination.type << " does not match " << id.type;
        }
        values_[id] = scalar_value_view();
    }
    if (failed) {
        throw error(make_error_code(errc::unsupported_feature), oss.str());
    }
}

void sum_connection::set_source_value(variable_id id, scalar_value_view value)
{
    values_.at(id) = value;
}

scalar_value_view sum_connection::get_destination_value(variable_id id)
{
    switch (id.type) {
        case variable_type::real: {
            double sum = 0.0;
            for (const auto& entry : values_) {
                sum += std::get<double>(entry.second);
            }
            return sum;
        }
        case variable_type::integer: {
            int sum = 0;
            for (const auto& entry : values_) {
                sum += std::get<int>(entry.second);
            }
            return sum;
        }
        default:
            CSE_PANIC();
    }
}

} // namespace cse
