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

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>


namespace cosim
{
/// Supporting functionality for serialization and persistence of simulation state.
namespace serialization
{

// This is a small trick to enable nodes to contain other nodes.  We
// forward-declare the `node` struct here, which allows us to use it in the
// declarations of the container types below. Thus, it can (indirectly) be
// used in the `node_base` type, which is the "actual" node type. Finally,
// we'll define `node` so that it inherits from `node_base`.
struct node;

/**
 *  An array of `cosim::serialization::node` objects.
 *
 *  This is used to enable a node to contain a sequence of other nodes.
 */
using array = std::vector<node>;

/**
 *  An associative array which maps strings to `cosim::serialization::node`
 *  objects.
 *
 *  This is used to enable a node to contain a dictionary of other nodes.
 */
using associative_array = std::unordered_map<std::string, node>;

/**
 *  An array of bytes.
 *
 *  This is used to enable a `cosim::serialization::node` to contain arbitrary
 *  binary data.
 */
using binary_blob = std::vector<std::byte>;


namespace detail
{
    // This is step 2 of the trick mentioned earlier, the type that actually
    // holds the node contents.
    using node_base = std::variant<
        bool,
        std::byte,
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
        std::string,
        array,
        associative_array,
        binary_blob
    >;
}


/**
 *  A recursive, dynamic data type that can be used to store structured data in
 *  a type-safe manner.
 *
 *  A `node` is essentially an `std::variant` which can hold the following
 *  types:
 *
 *  - `bool`
 *  - `std::byte`
 *  - `std::[u]int{8,16,32,64}_t`
 *  - `float` and `double`
 *  - `std::string`
 *  - `cosim::serialization::array`
 *  - `cosim::serialization::associative_array`
 *  - `cosim::serialization::binary_blob`
 *
 *  Its purpose is to be a generic representation of virtually any data
 *  structure, so that serialization to a variety of formats can be supported.
 */
struct node : public detail::node_base
{
    using detail::node_base::node_base;
};


/**
 *  Writes the contents of `data` to the output stream `out` in a human-readable
 *  format.
 *
 *  This is meant for debugging purposes, not for serialization. There is no
 *  corresponding "read" function, nor is the output format designed to support
 *  round-trip information or type preservation.
 */
std::ostream& operator<<(std::ostream& out, const node& data);


}} // namespace cosim::serialization
#endif // COSIM_SERIALIZATION_HPP
