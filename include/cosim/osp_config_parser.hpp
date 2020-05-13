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

#include <cosim/model.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/system_structure.hpp>

#include <boost/filesystem/path.hpp>


namespace cosim
{


struct osp_config
{
    cosim::system_structure system_structure;
    time_point start_time;
    duration step_size;
    variable_value_map initial_values;
};

osp_config load_osp_config(
    const boost::filesystem::path& configPath,
    cosim::model_uri_resolver& resolver);


} // namespace cosim
#endif //LIBCOSIM_OSP_CONFIG_PARSER_HPP
