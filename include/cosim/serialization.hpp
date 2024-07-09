/**
 *  \file
 *  Supporting functionality for serialization and persistence of simulation state.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_SERIALIZATION_HPP
#define COSIM_SERIALIZATION_HPP

#include <boost/property_tree/ptree.hpp>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <variant>
#include <vector>


namespace cosim
{
/// Supporting functionality for serialization and persistence of simulation state.
namespace serialization
{


/**
 *  The internal data type for `cosim::serialization::node`.
 *
 *  This is a `std::variant` of the types that can be stored in a node's data
 *  field.  This includes a variety of primitive types, plus the aggregate
 *  types `std::string` (for convenience) and `std::vector<std::byte>` (for
 *  convenience *and* memory-efficient storage of binary blobs).
 *
 *  A `nullptr` is used as the "empty" state, i.e., to indicate that the node
 *  stores no data.  This is the default if no other value has been set.
 */
using node_data = std::variant<
    std::nullptr_t, // represents "empty"
    bool,
    std::uint8_t,
    std::int8_t,
    std::uint16_t,
    std::int16_t,
    std::uint32_t,
    std::int32_t,
    std::uint64_t,
    std::int64_t,
    float,
    double,
    char,
    std::string,
    std::byte,
    std::vector<std::byte>
>;


/**
 *  A type for storing data intermediately before serialization.
 *
 *  This is a recursive, dynamic data type that can be used to store virtually
 *  any data structure in a generic yet type-safe manner.
 *
 *  It is just an alias for `boost::property_tree::basic_ptree<std::string, cosim::serialization::node_data>`.
 *  For guidance on its use, see the [Boost.PropertyTree](https://www.boost.org/doc/libs/release/doc/html/property_tree.html)
 *  documentation.
 *
 *  \note
 *      To make convenience functions like `node::get()` and `node::put()` work
 *      seamlessly, `cosim::serialization::node_data_translator` has been
 *      declared as the default translator for
 *      `cosim::serialization::node_data`.  However, we only recommend using
 *      these functions for primitive types, because they return by value
 *      rather than by reference.  In other words, a copy of the node data is
 *      made.  This could significantly impact performance in some cases if the
 *      node stores a large string or byte array.
 *      \code
 *      // Convenient, but always makes a copy
 *      auto thisIsACopy = myNode.get<std::vector<std::byte>>("huge_binary_blob");
 *
 *      // Uglier, but always gets a reference
 *      const auto& thisIsARef = std::get<std::vector<std::byte>>(myNode.get_child("huge_binary_blob").data());
 *      \endcode
 */
using node = boost::property_tree::basic_ptree<std::string, node_data>;


/// An implementation of Boost.PropertyTree's _Translator_ concept.
template<typename T>
struct node_data_translator
{
    using internal_type = node_data;
    using external_type = T;
    T get_value(const node_data& value) { return std::get<T>(value); }
    node_data put_value(const T& value) { return node_data(value); }
};

}} // namespace cosim::serialization


// Ordinarily, the following function would be in the `cosim::serialization`
// namespace and found by the compiler via ADL.  This doesn't work here,
// because `cosim::serialization::node` is just an alias for a type in the
// `boost::property_tree` namespace.
/**
 *  Writes the contents of `data` to the output stream `out` in a human-readable
 *  format.
 *
 *  This is meant for debugging purposes, not for serialization. There is no
 *  corresponding "read" function, nor is the output format designed to support
 *  round-trip information or type preservation.
 */
std::ostream& operator<<(std::ostream& out, const cosim::serialization::node& data);


// Make node_translator the default translator for property trees whose data
// type is node_data.
namespace boost
{
namespace property_tree
{
template<typename T>
struct translator_between<cosim::serialization::node_data, T>
{
    using type = cosim::serialization::node_data_translator<T>;
};
}} // namespace boost::property_tree

#endif // COSIM_SERIALIZATION_HPP
