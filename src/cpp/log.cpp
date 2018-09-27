#include <cse/log.hpp>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>

#include "cse/log/logger.hpp"


namespace cse
{
namespace log
{

void set_global_output_level(level lvl)
{
    boost::log::core::get()->set_filter(severity >= lvl);
}

} // namespace log
}
