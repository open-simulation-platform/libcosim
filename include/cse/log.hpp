/**
 *  \file
 *  Facilities for logging control.
 */
#ifndef CSE_LOG_HPP
#define CSE_LOG_HPP

#include <ostream>


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


/// Returns a textual representation of `lvl`.
constexpr const char* to_text(level lvl) noexcept
{
    switch (lvl) {
        case level::trace: return "trace";
        case level::debug: return "debug";
        case level::info: return "info";
        case level::warning: return "warning";
        case level::error: return "error";
        case level::fatal: return "fatal";
        default: return nullptr;
    }
}


/// Writes a textual representation of `lvl` to `stream`.
inline std::ostream& operator<<(std::ostream& stream, level lvl)
{
    return stream << to_text(lvl);
}


/// Sets the global output log level.
void set_global_output_level(level lvl);


} // namespace log
} // namespace cse
#endif // header guard
