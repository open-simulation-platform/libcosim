#include <cse/log.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include "cse/log/logger.hpp"


namespace cse
{
namespace log
{
namespace
{
// A keyword for referring to severity level in filters, formatters
// and such.
BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", level)


// Initialises the logging systems and returns a global
// logger source.
logger_type setup_logger()
{
    using namespace boost::log;
    add_common_attributes();

    // Set up a sink that writes to the console
    using text_sink = sinks::synchronous_sink<sinks::text_ostream_backend>;
    auto sink = boost::make_shared<text_sink>();
    sink->locked_backend()->add_stream(
        boost::shared_ptr<std::ostream>(&std::clog, [](void*) { /*delete nothing*/ }));

    sink->set_formatter(
        expressions::stream
        << expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%H:%M:%S.%f")
        << " [" << std::left << std::setw(7) << severity << "] "
        << expressions::smessage);
    core::get()->add_sink(sink);

    // Create and return logger
    return logger_type(level::info);
}
} // namespace


void set_global_output_level(level lvl)
{
    boost::log::core::get()->set_filter(severity >= lvl);
}


logger_type& logger()
{
    static logger_type logger_ = setup_logger();
    return logger_;
}


} // namespace log
} // namespace cse
