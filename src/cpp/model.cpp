#include "cse/model.hpp"


namespace cse
{

variable_description find_variable(const model_description& description, const std::string& variable_name)
{
    for (const auto& variable : description.variables) {
        if (variable.name == variable_name) {
            return variable;
        }
    }
    throw std::invalid_argument("Can't find variable descriptor with name " + variable_name + " for model with name " + description.name);
}

std::vector<variable_description> find_variables_of_type(const model_description& description, variable_type type)
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
