/**
 *  \file
 *  Facilities for logging.
 */
#ifndef CSE_LOG_LOGGER_HPP
#define CSE_LOG_LOGGER_HPP

#include <boost/log/sources/severity_logger.hpp>
// These are just to introduce some convenient macros like BOOST_LOG_SEV:
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_feature.hpp>

#include <cse/log.hpp>


namespace cse
{
namespace log
{

using logger_type = boost::log::sources::severity_logger_mt<level>;

/// Returns the global logger.
logger_type& logger();

} // namespace log
} // namespace cse
#endif // header guard
