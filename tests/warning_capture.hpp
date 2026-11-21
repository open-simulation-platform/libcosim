#pragma once

#include <cosim/log/logger.hpp>

#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/make_shared.hpp>

#include <sstream>
#include <string>

namespace
{
class warning_capture
{
    using sink_type = boost::log::sinks::synchronous_sink<
        boost::log::sinks::text_ostream_backend>;

public:
    explicit warning_capture(cosim::log::severity_level level = cosim::log::warning)
        : sink_(boost::make_shared<sink_type>())
    {
        sink_->locked_backend()->add_stream(
            boost::shared_ptr<std::ostream>(&stream_, boost::null_deleter()));
        sink_->set_filter(cosim::log::severity == level);
        sink_->set_formatter(boost::log::expressions::stream
            << boost::log::expressions::smessage);
        boost::log::core::get()->add_sink(sink_);
    }

    ~warning_capture()
    {
        boost::log::core::get()->remove_sink(sink_);
    }

    std::string messages() const { return stream_.str(); }

private:
    std::ostringstream stream_;
    boost::shared_ptr<sink_type> sink_;
};
} // namespace
