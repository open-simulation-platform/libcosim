#include "cse/function/utility.hpp"

#include <boost/lexical_cast.hpp>

#include <stdexcept>
#include <type_traits>


namespace cosim
{
namespace
{
template<typename T>
class replace_placeholder
{
public:
    replace_placeholder(
        const function_parameter_value_map& parameterValues,
        const std::vector<function_parameter_description>& parameterDescriptions)
        : parameterValues_(parameterValues)
        , parameterDescriptions_(parameterDescriptions)
    {}

    T operator()(const T& value) { return value; }

    T operator()(const function_parameter_placeholder& placeholder)
    {
        const auto index = placeholder.parameter_index;
        if (index < 0 ||
            static_cast<std::size_t>(index) >= parameterDescriptions_.size()) {
            throw std::out_of_range(
                "Invalid parameter index in placeholder: " +
                std::to_string(index));
        }
        const auto& descr = parameterDescriptions_[index];

        auto valueIt = parameterValues_.find(placeholder.parameter_index);
        if (valueIt == parameterValues_.end()) {
            return get_value(descr.default_value, descr);
        }
        const auto value = get_value(valueIt->second, descr);
        if constexpr (std::is_arithmetic_v<T>) {
            if (descr.min_value && value < std::get<T>(*descr.min_value)) {
                throw std::domain_error(
                    "Value of parameter '" + descr.name + "' too small: " +
                    boost::lexical_cast<std::string>(value));
            }
            if (descr.max_value && value > std::get<T>(*descr.max_value)) {
                throw std::domain_error(
                    "Value of parameter '" + descr.name + "' too small: " +
                    boost::lexical_cast<std::string>(value));
            }
        }
        return value;
    }

private:
    static T get_value(
        const function_parameter_value& v,
        const function_parameter_description& d)
    {
        if (const auto p = std::get_if<T>(&v)) return *p;
        throw std::logic_error(
            "Parameter '" + d.name + "': Illegal value type");
    }

    const function_parameter_value_map& parameterValues_;
    const std::vector<function_parameter_description>& parameterDescriptions_;
};
} // namespace


function_description substitute_function_parameters(
    const function_type_description& functionTypeDescription,
    const function_parameter_value_map& parameterValues)
{
    auto functionDescription = function_description(functionTypeDescription);
    for (auto& group : functionDescription.io_groups) {
        group.count = std::visit(
            replace_placeholder<int>(parameterValues, functionTypeDescription.parameters),
            group.count);

        for (auto& io : group.ios) {
            io.count = std::visit(
                replace_placeholder<int>(parameterValues, functionTypeDescription.parameters),
                io.count);
            io.type = std::visit(
                replace_placeholder<variable_type>(parameterValues, functionTypeDescription.parameters),
                io.type);
        }
    }
    return functionDescription;
}

} // namespace cse
