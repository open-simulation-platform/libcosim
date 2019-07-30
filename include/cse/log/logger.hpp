/**
 *  \file
 *  Defines the logger for this library, based on Boost.Log.
 *
 *  For convenience, this header also includes some Boost.Log headers that
 *  provide useful short-hand logging macros (like `BOOST_LOG_SEV`).
 */
#ifndef CSE_LOG_LOGGER_HPP
#define CSE_LOG_LOGGER_HPP

#include <boost/log/expressions/keyword.hpp>
#include <boost/log/sources/record_ostream.hpp> // for convenience
#include <boost/log/sources/severity_feature.hpp> // for convenience
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>


namespace cse
{
namespace log
{


/**
 *  Severity levels for logged messages.
 *
 *  This is currently an alias of `boost::log::trivial::severity_level`,
 *  mainly to take advantage of the built-in support that this type has in
 *  Boost.Log.  However, this may change in the future, so it should be
 *  treated and used as if it were a separate `enum` whose enumerators are
 *  `trace`, `debug`, `info`, `warning`, `error`, and `fatal`.
 */
using severity_level = boost::log::trivial::severity_level;

constexpr severity_level trace = boost::log::trivial::trace;
constexpr severity_level debug = boost::log::trivial::debug;
constexpr severity_level info = boost::log::trivial::info;
constexpr severity_level warning = boost::log::trivial::warning;
constexpr severity_level error = boost::log::trivial::error;
constexpr severity_level fatal = boost::log::trivial::fatal;


/**
 *  Severity level keyword.
 *
 *  This keyword should be used when referring to the "Severity" attribute
 *  in filters and formatters.
 */
const auto severity = boost::log::trivial::severity;


/// The logger type used by this library, a thread-safe severity logger.
using logger_type = boost::log::sources::severity_logger_mt<severity_level>;


/// The logger used by this library.
logger_type& logger();


} // namespace log
} // namespace cse
#endif // header guard
