#include "cse/function/utility.hpp"


namespace cse
{
namespace
{
template<typename T>
class replace_placeholder
{
public:
    replace_placeholder(
        const std::unordered_map<int, function_parameter_value>& parameterValues)
        : parameterValues_(parameterValues)
    {}

    T operator()(const T& value) { return value; }

    T operator()(const function_parameter_placeholder& placeholder)
    {
        return std::get<T>(parameterValues_.at(placeholder.parameter_index));
    }

private:
    const std::unordered_map<int, function_parameter_value>& parameterValues_;
};
} // namespace


function_description substitute_function_parameters(
    const function_type_description& functionTypeDescription,
    const function_parameter_value_map& parameterValues)
{
    auto functionDescription = function_description(functionTypeDescription);
    for (auto& group : functionDescription.io_groups) {
        group.count = std::visit(replace_placeholder<int>(parameterValues), group.count);

        for (auto& io : group.ios) {
            io.count = std::visit(replace_placeholder<int>(parameterValues), io.count);
            io.type = std::visit(replace_placeholder<variable_type>(parameterValues), io.type);
        }
    }
    return functionDescription;
}

} // namespace cse
