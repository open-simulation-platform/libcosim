#include "cse/observer/configurable_log_observer.hpp"

#include "cse/error.hpp"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

#include <map>
#include <mutex>
#include <vector>

namespace cse
{

configurable_log_observer::configurable_log_observer(boost::filesystem::path& logPath, int rate, int limit)
    : logPath_(logPath)
    , rate_(rate)
    , limit_(limit)
{
}

configurable_log_observer::~configurable_log_observer() = default;

} // namespace cse
