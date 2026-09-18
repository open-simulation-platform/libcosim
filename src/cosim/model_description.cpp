/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/model_description.hpp"

#include <iomanip>
#include <limits>
#include <stdexcept>
#include <type_traits>


namespace cosim
{

std::ostream& operator<<(std::ostream& stream, const binary_value& value)
{
    const auto flags = stream.flags();
    const auto fill = stream.fill();
    stream << std::hex << std::setfill('0');
    for (const auto byte : value.data) {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    stream.flags(flags);
    stream.fill(fill);
    return stream;
}


variable_type type_of(const scalar_value& value) noexcept
{
    static constexpr variable_type types[] = {
        variable_type::real,
        variable_type::integer,
        variable_type::boolean,
        variable_type::string,
        variable_type::float32,
        variable_type::int8,
        variable_type::uint8,
        variable_type::int16,
        variable_type::uint16,
        variable_type::uint32,
        variable_type::int64,
        variable_type::uint64,
        variable_type::enumeration,
        variable_type::binary};
    static_assert(
        sizeof(types) / sizeof(types[0]) == std::variant_size_v<scalar_value>);
    return types[value.index()];
}


std::size_t flat_size(const std::vector<std::uint64_t>& dimensions)
{
    // FMI arrays are stored as one flat, row-major sequence. Check every
    // multiplication before converting the declared shape to host size_t.
    std::size_t result = 1;
    for (const auto dimension : dimensions) {
        if (dimension >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::overflow_error("Array dimension does not fit in size_t");
        }
        const auto converted = static_cast<std::size_t>(dimension);
        if (converted != 0 &&
            result > std::numeric_limits<std::size_t>::max() / converted) {
            throw std::overflow_error("Array element count overflows size_t");
        }
        result *= converted;
    }
    return result;
}


variable_value::variable_value()
    : values_(real_values{{0.0}})
{
}


variable_value::variable_value(
    variable_value_storage values,
    std::vector<std::uint64_t> dimensions)
    : dimensions_(std::move(dimensions))
    , values_(std::move(values))
{
    // Keep shape and storage inseparable: generic transport must never expose
    // an array whose element count disagrees with its FMI dimensions.
    if (size() != flat_size(dimensions_)) {
        throw std::invalid_argument(
            "Variable value element count does not match its dimensions");
    }
}


variable_type variable_value::type() const noexcept
{
    return std::visit(
        [](const auto& values) {
            using values_type = std::decay_t<decltype(values)>;
            return values_type::type;
        },
        values_);
}


bool variable_value::is_scalar() const noexcept
{
    return dimensions_.empty();
}


std::size_t variable_value::size() const noexcept
{
    return std::visit(
        [](const auto& values) { return values.values.size(); },
        values_);
}


const std::vector<std::uint64_t>& variable_value::dimensions() const noexcept
{
    return dimensions_;
}


const variable_value_storage& variable_value::values() const noexcept
{
    return values_;
}


bool operator==(const variable_value& a, const variable_value& b)
{
    return a.dimensions() == b.dimensions() && a.values() == b.values();
}


bool operator!=(const variable_value& a, const variable_value& b)
{
    return !(a == b);
}


std::optional<variable_description> find_variable(const model_description& description, const std::string& variable_name)
{
    for (const auto& variable : description.variables) {
        if (variable.name == variable_name) {
            return variable;
        }
    }
    return std::nullopt;
}


std::optional<std::vector<std::uint64_t>>
fixed_shape(const variable_description& variable)
{
    // A shape that depends on a structural parameter is not stable enough for
    // an unconditional generic Get/Set operation until the parameter is
    // resolved.
    std::vector<std::uint64_t> dimensions;
    dimensions.reserve(variable.dimensions.size());
    for (const auto& dimension : variable.dimensions) {
        const auto fixed = std::get_if<fixed_dimension>(&dimension);
        if (fixed == nullptr) {
            return std::nullopt;
        }
        dimensions.push_back(fixed->size);
    }
    flat_size(dimensions);
    return dimensions;
}

} // namespace cosim
