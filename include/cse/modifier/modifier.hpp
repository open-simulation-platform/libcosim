
#ifndef CSECORE_MODIFIER_MODIFIER_HPP
#define CSECORE_MODIFIER_MODIFIER_HPP

#include <cse/model.hpp>

#include <string_view>
#include <variant>

namespace cse
{

using scalar = std::variant<double, int, bool, std::string_view>;

class modifier
{
public:
    virtual scalar apply(variable_type, scalar value) = 0;
};

} // namespace cse

#endif
