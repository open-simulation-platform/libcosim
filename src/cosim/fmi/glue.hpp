/**
 *  \file
 *  Conversions between FMI variable attributes and "our" attributes.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_GLUE_HPP
#define COSIM_FMI_GLUE_HPP

#include "cosim/fmi/fmilib.h"
#include "cosim/model_description.hpp"


namespace cosim
{
namespace fmi
{


/// Converts an FMI 1.0 base type to "our" data type.
variable_type to_variable_type(fmi1_base_type_enu_t t);


/// Converts an FMI 2.0 base type to "our" data type.
variable_type to_variable_type(fmi2_base_type_enu_t t);


/**
 *  Converts an FMI 1.0 variable causality to "our" corresponding causality.
 *
 *  The causality mapping is not unique, so the variable's variability is also
 *  needed.
 */
variable_causality to_variable_causality(
    fmi1_causality_enu_t c,
    fmi1_variability_enu_t v);


/// Converts an FMI 2.0 variable causality to "our" corresponding causality.
variable_causality to_variable_causality(fmi2_causality_enu_t c);


/// Converts an FMI 1.0 variable variability to "our" corresponding variability.
variable_variability to_variable_variability(fmi1_variability_enu_t v);


/// Converts an FMI 2.0 variable variability to "our" corresponding variability.
variable_variability to_variable_variability(fmi2_variability_enu_t v);


/// Converts an FMI 1.0 variable description to a `variable_description` object.
variable_description to_variable_description(fmi1_import_variable_t* fmiVariable);


/// Converts an FMI 2.0 variable description to a `variable_description` object.
variable_description to_variable_description(fmi2_import_variable_t* fmiVariable);


} // namespace fmi
} // namespace cosim
#endif // header guard
