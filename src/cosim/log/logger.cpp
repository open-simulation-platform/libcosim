/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/log/logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace cosim
{
namespace log
{

namespace
{

spdlog::level::level_enum convert(cosim_logger::level lvl)
{
    if (lvl == cosim_logger::level::trace) {
        return spdlog::level::trace;
    } else if (lvl == cosim_logger::level::debug) {
        return spdlog::level::debug;
    } else if (lvl == cosim_logger::level::info) {
        return spdlog::level::info;
    } else if (lvl == cosim_logger::level::warn) {
        return spdlog::level::warn;
    } else if (lvl == cosim_logger::level::err) {
        return spdlog::level::err;
    } else if (lvl == cosim_logger::level::off) {
        return spdlog::level::off;
    } else {
        throw std::runtime_error("Invalid log level");
    }
}

std::shared_ptr<spdlog::logger> logger() {
    auto get = spdlog::get("cosim");
    if (get) return get;
    return spdlog::stdout_color_mt("cosim");
}

} // namespace

struct cosim_logger::impl
{

    impl()
        : logger_(logger())
    { }

    ~impl() = default;

    void set_level(level lvl)
    {
        if (lvl == cosim_logger::level::trace) {
            logger_->set_level(spdlog::level::trace);
        } else if (lvl == cosim_logger::level::debug) {
            logger_->set_level(spdlog::level::debug);
        } else if (lvl == cosim_logger::level::info) {
            logger_->set_level(spdlog::level::info);
        } else if (lvl == cosim_logger::level::warn) {
            logger_->set_level(spdlog::level::warn);
        } else if (lvl == cosim_logger::level::err) {
            logger_->set_level(spdlog::level::err);
        } else if (lvl == cosim_logger::level::off) {
            logger_->set_level(spdlog::level::off);
        } else {
            throw std::runtime_error("Invalid log level");
        }
    }

    void log(cosim_logger::level lvl, fmt::basic_string_view<char> fmt)
    {
        logger_->log(convert(lvl), fmt);
    }


private:
    std::shared_ptr<spdlog::logger> logger_;
};

void cosim_logger::set_level(level lvl)
{
    pimpl_->set_level(lvl);
}

void cosim_logger::log(cosim_logger::level lvl, const std::string& msg)
{
    pimpl_->log(lvl, msg);
}

cosim_logger::cosim_logger(): pimpl_(std::make_unique<impl>()) {}
cosim_logger::~cosim_logger() = default;

} // namespace log
} // namespace cosim
