/**
 *  \file
 *  Defines the logger for this library.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_LOG_LOGGER_HPP
#define COSIM_LOG_LOGGER_HPP

#include <fmt/core.h>


namespace cosim
{
namespace log
{

enum class level : int
{
    trace,
    debug,
    info,
    warn,
    err,
    off
};

void set_logging_level(level lvl);

void log(level lvl, const std::string& msg);

template<typename... Args>
inline void log(level lvl, fmt::format_string<Args...> fmt, Args&&... args)
{
    log(lvl, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void trace(fmt::format_string<Args...> fmt, Args&&... args)
{
    log(level::trace, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void debug(fmt::format_string<Args...> fmt, Args&&... args)
{
    log(level::debug, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void info(fmt::format_string<Args...> fmt, Args&&... args)
{
    log(level::info, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void warn(fmt::format_string<Args...> fmt, Args&&... args)
{
    log(level::warn, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void err(fmt::format_string<Args...> fmt, Args&&... args)
{
    log(level::err, fmt::format(fmt, std::forward<Args>(args)...));
}


} // namespace log
} // namespace cosim
#endif // COSIM_LOG_LOGGER_HPP
