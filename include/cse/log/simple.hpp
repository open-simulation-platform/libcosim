/**
 *  \file
 *  Simple and convenient functions for setting up and controlling logging.
 */
#ifndef CSE_LOG_SIMPLE_HPP
#define CSE_LOG_SIMPLE_HPP

#include <cse/log/logger.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>

#include <iomanip>
#include <ios>


namespace cse
{
namespace log
{


/**
 *  Convenience function that sets the global output log level.
 *
 *  This function adds the following global filter: `severity >= lvl`
 */
inline void set_global_output_level(severity_level lvl)
{
    boost::log::core::get()->set_filter(severity >= lvl);
}


/**
 *  Convenience function that makes a simple log record formatter which includes
 *  time stamp, severity and message.
 */
inline auto simple_formatter()
{
    using namespace boost::log;
    return expressions::stream
        << expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%H:%M:%S.%f")
        << " [" << std::left << std::setw(7) << severity << "] "
        << expressions::smessage;
}


/**
 *  Convenience function that sets up simple console logging.
 *
 *  This function performs the following operations:
 *
 *  1. Calls `boost::log::add_common_attributes()`.
 *  2. Calls `boost::log::add_console_log()`.
 *  3. Sets the formatter of the console sink to `simple_formatter()`. 
 */
inline void setup_simple_console_logging()
{
    boost::log::add_common_attributes();
    const auto sink = boost::log::add_console_log();
    sink->set_formatter(simple_formatter());
}


} // namespace log
} // namespace cse
#endif // header guard
