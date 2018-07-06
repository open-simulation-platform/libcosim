/**
 *  \file
 *  \brief Facilities for logging control.
 */
#ifndef CSE_LOG_HPP
#define CSE_LOG_HPP

namespace cse
{
namespace log
{


/// Severity levels for logged messages.
enum class level
{
    trace,
    debug,
    info,
    warning,
    error,
    fatal
};


}} // namespace
#endif // header guard
