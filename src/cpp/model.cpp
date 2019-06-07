#include "cse/model.hpp"


namespace cse
{

const variable_description find_variable(const model_description& description, const std::string& variable_name, variable_type type, variable_causality causality)
{
    for (const auto& variable : description.variables) {
        if (variable.name == variable_name && variable.type == type && variable.causality == causality) {
            return variable;
        }
    }
    throw std::invalid_argument("Can't find variable descriptor with name " + variable_name);
}

const std::vector<variable_description> find_variables_of_type(const model_description& description, variable_type type)
{
    std::vector<variable_description> vars;

    for (const auto& variable : description.variables) {
        if (variable.type == type) {
            vars.push_back(variable);
        }
    }
    return vars;
}
} // namespace cse
