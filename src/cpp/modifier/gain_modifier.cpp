
#include "cse/modifier/gain_modifier.hpp"

#include <cse/error.hpp>

namespace cse
{

gain_modifier::gain_modifier(double gain)
    : gain_(gain)
{}

scalar gain_modifier::apply(variable_type type, scalar value)
{
    if (type == variable_type::real) {
        return std::get<double>(value) * gain_;
    } else if (type == variable_type::integer) {
        return std::get<int>(value) * gain_;
    } else {
        CSE_PANIC();
    }
}

} // namespace cse
