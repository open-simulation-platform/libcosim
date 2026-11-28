/**
 *  \file
 *  Default limit for discrete-event iteration.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_EVENT_ITERATION_HPP
#define COSIM_EVENT_ITERATION_HPP

#include <cstddef>

namespace cosim
{

/// Default maximum number of iterations for each discrete-event operation.
inline constexpr std::size_t default_max_event_iterations = 1000;

/// Absolute and relative tolerances for coupled floating-point event values.
/// Both must be finite and nonnegative. Set both to zero for exact comparison.
struct event_value_tolerance
{
    double absolute = 1e-10;
    double relative = 1e-10;
};

inline constexpr event_value_tolerance default_event_value_tolerance{};

} // namespace cosim

#endif
