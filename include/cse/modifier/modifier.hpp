
#ifndef CSECORE_MODIFIER_MODIFIER_HPP
#define CSECORE_MODIFIER_MODIFIER_HPP

#include <cse/model.hpp>

#include <string_view>
#include <variant>

namespace cse
{

using scalar = std::variant<double, int, bool, std::string_view>;

/// Interface for modifying the data passing through a connection
class modifier
{
public:
    /***
     * The purpose of this function is to return a modified version of the passed in value.
     * Used in conjunction with a connection in order to modify data passing through it.
     */
    virtual scalar apply(variable_type, scalar value) = 0;
};

} // namespace cse

#endif
