#include "uuid.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace cosim
{
namespace utility
{


std::string random_uuid() noexcept
{
    boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}


} // namespace utility
} // namespace cse
