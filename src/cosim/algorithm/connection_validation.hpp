/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_ALGORITHM_CONNECTION_VALIDATION_HPP
#define COSIM_ALGORITHM_CONNECTION_VALIDATION_HPP

#include <cosim/model_description.hpp>

#include <stdexcept>

namespace cosim::detail
{

inline void validate_connection_types(variable_type output, variable_type input)
{
    if (output != input) {
        throw std::invalid_argument("Invalid variable connection: Variable types differ.");
    }
}

} // namespace cosim::detail

#endif
