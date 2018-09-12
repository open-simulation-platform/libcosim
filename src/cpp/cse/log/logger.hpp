/**
 *  \file
 *  Facilities for logging.
 */
#ifndef CSE_LOG_LOGGER_HPP
#define CSE_LOG_LOGGER_HPP

#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <cse/log.hpp>


namespace cse
{
namespace log
{


BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(
    logger,
    boost::log::sources::severity_logger_mt<level>)
}
} // namespace cse
#endif // header guard
