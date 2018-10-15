/**
 *  \file
 *  Utility functions for dealing with UUIDs.
 */
#ifndef CSE_UTILITY_UUID_HPP
#define CSE_UTILITY_UUID_HPP

#include <string>


namespace cse
{
namespace utility
{


/// Returns a randomly generated UUID.
std::string random_uuid() noexcept;


} // namespace utility
} // namespace cse
#endif // header guard
