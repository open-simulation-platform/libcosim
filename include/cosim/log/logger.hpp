/**
 *  \file
 *  Defines the logger for this library, based on Boost.Log.
 *
 *  For convenience, this header also includes some Boost.Log headers that
 *  provide useful short-hand logging macros (like `BOOST_LOG_SEV`).
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_LOG_LOGGER_HPP
#define COSIM_LOG_LOGGER_HPP

#include <fmt/core.h>

#include <memory>
#include <mutex>


namespace cosim
{
namespace log
{

class cosim_logger
{

public:
    ~cosim_logger();

    enum class level : int
    {
        trace,
        debug,
        info,
        warn,
        err,
        off
    };

    void set_level(level lvl);

    void log(level lvl, const std::string& msg);

    static cosim_logger& get_instance()
    {
        static cosim_logger logger;
        return logger;
    }

private:

    cosim_logger();

    struct impl;
    std::unique_ptr<impl> pimpl_;
};

void set_logging_level(cosim_logger::level lvl);

template<typename... Args>
inline void log(cosim_logger::level lvl, fmt::format_string<Args...> fmt, Args&&... args)
{
    cosim_logger::get_instance().log(lvl, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void trace(fmt::format_string<Args...> fmt, Args&&... args)
{
    cosim_logger::get_instance().log(cosim_logger::level::trace, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void debug(fmt::format_string<Args...> fmt, Args&&... args)
{
    cosim_logger::get_instance().log(cosim_logger::level::debug, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void info(fmt::format_string<Args...> fmt, Args&&... args)
{
    cosim_logger::get_instance().log(cosim_logger::level::info, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void warn(fmt::format_string<Args...> fmt, Args&&... args)
{
    cosim_logger::get_instance().log(cosim_logger::level::warn, fmt::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void err(fmt::format_string<Args...> fmt, Args&&... args)
{
    cosim_logger::get_instance().log(cosim_logger::level::err, fmt::format(fmt, std::forward<Args>(args)...));
}


} // namespace log
} // namespace cosim
#endif // COSIM_LOG_LOGGER_HPP
