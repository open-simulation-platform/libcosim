/**
 *  \file
 *  Includes the FMI Library header, locally disabling some compilation
 *  warnings for it.
 */
#ifndef CSE_FMI_FMILIB_H
#define CSE_FMI_FMILIB_H

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
