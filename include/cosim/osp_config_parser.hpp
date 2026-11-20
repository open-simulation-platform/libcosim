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
#include <cosim/execution.hpp>
#include <cosim/model_description.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/time.hpp>

#include <memory>

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


/**
 *  Creates a ready-to-run execution from an OSP-IS system structure file.
 *
 *  This loads the configuration with `load_osp_config()`, creates the
 *  algorithm it specifies (fixed-step or ECCO), creates an execution
 *  starting at the configured start time, and adds the system structure
 *  and initial values with `inject_system_structure()`.
 *
 *  \param configPath
 *      Path to an OSP-IS configuration file, or to a directory that
 *      contains a file named `OspSystemStructure.xml`.
 *  \param options
 *      Overrides for start time, worker threads, model resolver and
 *      algorithm. `parameter_set` is ignored.
 */
configured_execution load_execution_from_osp_config(
    const cosim::filesystem::path& configPath,
    const execution_load_options& options = {});


} // namespace cosim
#endif // LIBCOSIM_OSP_CONFIG_PARSER_HPP
