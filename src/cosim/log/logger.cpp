#include "cosim/log/logger.hpp"


namespace cosim
{
namespace log
{


logger_type& logger()
{
    static logger_type logger_{info};
    return logger_;
}


} // namespace log
} // namespace cosim
