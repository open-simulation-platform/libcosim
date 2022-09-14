/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/log/logger.hpp"

#ifndef SPDLOG_FMT_EXTERNAL
#define SPDLOG_FMT_EXTERNAL
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace cosim
{
namespace log
{

namespace
{

spdlog::level::level_enum convert(level lvl)
{
    if (lvl == level::trace) {
        return spdlog::level::trace;
    } else if (lvl == level::debug) {
        return spdlog::level::debug;
    } else if (lvl == level::info) {
        return spdlog::level::info;
    } else if (lvl == level::warn) {
        return spdlog::level::warn;
    } else if (lvl == level::err) {
        return spdlog::level::err;
    } else if (lvl == level::off) {
        return spdlog::level::off;
    } else {
        throw std::runtime_error("Invalid log level");
    }
}

struct logger
{

    void set_level(level lvl)
    {
        logger_->set_level(convert(lvl));
    }

    void log(level lvl, std::string_view msg)
    {
        logger_->log(convert(lvl), msg);
    }

    static logger& get_instance()
    {
        static logger l;
        return l;
    }

private:
    std::shared_ptr<spdlog::logger> logger_;

    logger()
        : logger_(spdlog::stdout_color_mt("cosim"))
    { }
};

} // namespace

void set_logging_level(level lvl)
{
    logger::get_instance().set_level(lvl);
}

void log(level lvl, std::string_view msg)
{
    logger::get_instance().log(lvl, msg);
}

} // namespace log
} // namespace cosim
