/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_SCALAR_VALUE_DISPATCH_HPP
#define COSIM_SCALAR_VALUE_DISPATCH_HPP

#include "cosim/model_description.hpp"
#include <cosim/slave.hpp>


namespace cosim::detail
{

/// Dispatches one validated scalar value to the corresponding legacy setter.
void set_legacy_scalar_value(
    slave& target,
    typed_value_reference variable,
    const variable_value& value);

} // namespace cosim::detail

#endif
