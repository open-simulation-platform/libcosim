/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/log/logger.hpp"

#include "cosim/log/level.hpp"
#include "cosim/log/simple.hpp"

#include <mutex>


namespace cosim
{
namespace log
{


logger_type& logger()
{
    static logger_type logger_{info};
    return logger_;
}


void set_output_level(level minimumLevel)
{
    switch (minimumLevel) {
        case level::trace: set_global_output_level(trace); break;
        case level::debug: set_global_output_level(debug); break;
        case level::info: set_global_output_level(info); break;
        case level::warning: set_global_output_level(warning); break;
        case level::error: set_global_output_level(error); break;
        case level::fatal: set_global_output_level(fatal); break;
    }
}


void setup_console_logging()
{
    static std::once_flag once;
    std::call_once(once, [] { setup_simple_console_logging(); });
}


} // namespace log
} // namespace cosim
