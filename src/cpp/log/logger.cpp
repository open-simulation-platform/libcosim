#include "cse/log/logger.hpp"


namespace cse
{
namespace log
{


logger_type& logger()
{
    static logger_type logger_{info};
    return logger_;
}


} // namespace log
} // namespace cse
