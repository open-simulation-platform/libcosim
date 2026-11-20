/**
 *  \file
 *  Log level control without Boost.Log in the interface.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_LOG_LEVEL_HPP
#define COSIM_LOG_LEVEL_HPP


namespace cosim
{
namespace log
{


/// Log levels, from most to least verbose.
enum class level
{
    trace,
    debug,
    info,
    warning,
    error,
    fatal
};


/**
 *  Sets the minimum level of the log messages that libcosim emits.
 *
 *  Equivalent to `set_global_output_level()` in `cosim/log/simple.hpp`,
 *  but without Boost.Log types in the interface, so it can be used from C
 *  and other languages.
 */
void set_output_level(level minimumLevel);


/**
 *  Sets up logging to the console, with a timestamp and the level of each
 *  message. Calling it more than once has no further effect.
 *
 *  Equivalent to `setup_simple_console_logging()` in `cosim/log/simple.hpp`.
 */
void setup_console_logging();


} // namespace log
} // namespace cosim
#endif // header guard
