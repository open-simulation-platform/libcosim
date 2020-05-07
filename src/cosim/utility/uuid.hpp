/**
 *  \file
 *  Utility functions for dealing with UUIDs.
 */
#ifndef COSIM_UTILITY_UUID_HPP
#define COSIM_UTILITY_UUID_HPP

#include <string>


namespace cosim
{
namespace utility
{


/// Returns a randomly generated UUID.
std::string random_uuid() noexcept;


} // namespace utility
} // namespace cosim
#endif // header guard
