/**
 *  \file
 *  Includes the FMI Library header, locally disabling some compilation
 *  warnings for it.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_FMILIB_H
#define COSIM_FMI_FMILIB_H

#ifdef _MSC_VER
#    pragma warning(push, 0)
#endif
#ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunused-function"
#    pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <fmilib.h>

#ifdef _MSC_VER
#    pragma warning(pop)
#endif
#ifdef __GNUC__
#    pragma GCC diagnostic pop
#endif

#endif // header guard
