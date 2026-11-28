/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_OSP_CLOCK_HPP
#define COSIM_OSP_CLOCK_HPP

#include "cosim/model_description.hpp"
#include "cosim/orchestration.hpp"

#include <memory>

namespace cosim::detail
{

/// Exposes an XML Clock as a scheduler input and a routable triggered output.
std::shared_ptr<model> make_osp_clock_model(const clock_description& schedule);

} // namespace cosim::detail
#endif
