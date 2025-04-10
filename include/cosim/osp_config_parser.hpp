/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_OSP_CONFIG_PARSER_HPP
#define LIBCOSIM_OSP_CONFIG_PARSER_HPP

#include <cosim/algorithm.hpp>
#include <cosim/model_description.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/time.hpp>

namespace cosim
{


/// The information contained in an OSP-IS system structure file.
struct osp_config
{
    /// The system structure
    cosim::system_structure system_structure;

    // The algorithm
    std::variant<cosim::fixed_step_algorithm_params, cosim::ecco_algorithm_params> algorithm_configuration;

    /// The default start time for a simulation
    time_point start_time;

    /// The optional end time for a simulation
    std::optional<time_point> end_time = std::nullopt;

    /// A set of default initial values
    variable_value_map initial_values;
};


/**
 *  Loads an OSP-IS system structure file.
 */
osp_config load_osp_config(
    const cosim::filesystem::path& configPath,
    cosim::model_uri_resolver& resolver);


} // namespace cosim
#endif // LIBCOSIM_OSP_CONFIG_PARSER_HPP
