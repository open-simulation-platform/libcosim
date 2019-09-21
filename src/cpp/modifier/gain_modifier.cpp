
#include "cse/modifier/gain_modifier.hpp"

#include <cse/error.hpp>

#include <sstream>

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
        return static_cast<int>(round(std::get<int>(value) * gain_));
    } else {
        std::ostringstream oss;
        oss << "gain_modifier does not support variable_type: " << type;
        CSE_PANIC_M(oss.str().c_str());
    }
}

} // namespace cse
