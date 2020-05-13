/**
*   This Source Code Form is subject to the terms of the Mozilla Public
*   License, v. 2.0. If a copy of the MPL was not distributed with this
*   file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "cosim/model.hpp"


namespace cosim
{

const variable_description find_variable(const model_description& description, const std::string& variable_name)
{
    for (const auto& variable : description.variables) {
        if (variable.name == variable_name) {
            return variable;
        }
    }
    throw std::invalid_argument("Can't find variable descriptor with name " + variable_name + " for model with name " + description.name);
}

} // namespace cosim
