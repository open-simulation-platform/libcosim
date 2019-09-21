#include "cse/connection.hpp"

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
    type_ = destination.type;
}

void scalar_connection::set_source_value(variable_id, std::variant<double, int, bool, std::string_view> value)
{
    value_ = value;
    for (std::shared_ptr<modifier> modifier : modifiers_) {
        value_ = modifier->apply(type_, value_);
    }
}

std::variant<double, int, bool, std::string_view> scalar_connection::get_destination_value(variable_id)
{
    return value_;
}

void scalar_connection::add_modifier(const std::shared_ptr<modifier>& modifier)
{
    modifiers_.push_back(modifier);
}


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
        values_[id] = std::variant<double, int, bool, std::string_view>();
    }
    if (failed) {
        throw error(make_error_code(errc::unsupported_feature), oss.str());
    }
}

void sum_connection::set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value)
{
    values_.at(id) = value;
}

std::variant<double, int, bool, std::string_view> sum_connection::get_destination_value(variable_id id)
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
