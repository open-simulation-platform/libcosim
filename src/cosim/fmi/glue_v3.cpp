/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "glue_internal.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/fmi/glue.hpp"
#include "cosim/model_description.hpp"

#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/min_element.hpp>
#include <gsl/narrow>
#include <gsl/span>
#include <gsl/span_ext>
#include <gsl/util>

#include <FMI/fmi_import_context.h>
#include <FMI3/fmi3_enums.h>
#include <FMI3/fmi3_import.h>
#include <FMI3/fmi3_import_type.h>
#include <FMI3/fmi3_import_unit.h>
#include <FMI3/fmi3_import_variable.h>
#include <FMI3/fmi3_import_variable_list.h>
#include <FMI3/fmi3_types.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


namespace cosim::fmi
{

using glue_detail::nonnull;
using glue_detail::optional_string;

variable_type to_variable_type(fmi3_base_type_enu_t type)
{
    switch (type) {
        case fmi3_base_type_float64: return variable_type::real;
        case fmi3_base_type_float32: return variable_type::float32;
        case fmi3_base_type_int64: return variable_type::int64;
        case fmi3_base_type_int32: return variable_type::integer;
        case fmi3_base_type_int16: return variable_type::int16;
        case fmi3_base_type_int8: return variable_type::int8;
        case fmi3_base_type_uint64: return variable_type::uint64;
        case fmi3_base_type_uint32: return variable_type::uint32;
        case fmi3_base_type_uint16: return variable_type::uint16;
        case fmi3_base_type_uint8: return variable_type::uint8;
        case fmi3_base_type_bool: return variable_type::boolean;
        case fmi3_base_type_binary: return variable_type::binary;
        case fmi3_base_type_clock: return variable_type::clock;
        case fmi3_base_type_str: return variable_type::string;
        case fmi3_base_type_enum: return variable_type::enumeration;
    }
    COSIM_PANIC();
}

variable_causality to_variable_causality(fmi3_causality_enu_t causality)
{
    switch (causality) {
        case fmi3_causality_enu_structural_parameter:
            return variable_causality::structural_parameter;
        case fmi3_causality_enu_parameter:
            return variable_causality::parameter;
        case fmi3_causality_enu_calculated_parameter:
            return variable_causality::calculated_parameter;
        case fmi3_causality_enu_input:
            return variable_causality::input;
        case fmi3_causality_enu_output:
            return variable_causality::output;
        case fmi3_causality_enu_independent:
            return variable_causality::independent;
        case fmi3_causality_enu_local:
        case fmi3_causality_enu_unknown:
            return variable_causality::local;
    }
    COSIM_PANIC();
}

variable_variability to_variable_variability(fmi3_variability_enu_t variability)
{
    switch (variability) {
        case fmi3_variability_enu_constant: return variable_variability::constant;
        case fmi3_variability_enu_fixed: return variable_variability::fixed;
        case fmi3_variability_enu_tunable: return variable_variability::tunable;
        case fmi3_variability_enu_discrete: return variable_variability::discrete;
        case fmi3_variability_enu_continuous: return variable_variability::continuous;
        case fmi3_variability_enu_unknown:
            return variable_variability::discrete;
    }
    COSIM_PANIC();
}

namespace
{
std::optional<variable_initial> to_variable_initial(fmi3_initial_enu_t initial)
{
    switch (initial) {
        case fmi3_initial_enu_exact: return variable_initial::exact;
        case fmi3_initial_enu_approx: return variable_initial::approximate;
        case fmi3_initial_enu_calculated: return variable_initial::calculated;
        case fmi3_initial_enu_unknown: return std::nullopt;
    }
    COSIM_PANIC();
}


clock_interval_variability to_clock_interval_variability(
    fmi3_interval_variability_enu_t variability)
{
    switch (variability) {
        case fmi3_interval_variability_unknown:
            return clock_interval_variability::unknown;
        case fmi3_interval_variability_constant:
            return clock_interval_variability::constant;
        case fmi3_interval_variability_fixed:
            return clock_interval_variability::fixed;
        case fmi3_interval_variability_tunable:
            return clock_interval_variability::tunable;
        case fmi3_interval_variability_changing:
            return clock_interval_variability::changing;
        case fmi3_interval_variability_countdown:
            return clock_interval_variability::countdown;
        case fmi3_interval_variability_triggered:
            return clock_interval_variability::triggered;
    }
    return clock_interval_variability::unknown;
}


clock_description to_clock_description(fmi3_import_clock_variable_t* clock)
{
    // Clock metadata is copied from the FMI Library object so callers can
    // inspect scheduling capabilities without retaining parser-owned pointers.
    clock_description result;
    result.interval_variability = to_clock_interval_variability(
        fmi3_import_get_clock_variable_interval_variability(clock));
    result.can_be_deactivated =
        static_cast<int>(fmi3_import_get_clock_variable_can_be_deactivated(clock)) != fmi3_false;
    if (static_cast<int>(fmi3_import_get_clock_variable_has_priority(clock)) != fmi3_false) {
        result.priority = fmi3_import_get_clock_variable_priority(clock);
    }
    result.supports_fraction =
        static_cast<int>(fmi3_import_get_clock_variable_supports_fraction(clock)) != fmi3_false;
    if (static_cast<int>(fmi3_import_get_clock_variable_has_interval_decimal(clock)) != fmi3_false) {
        result.interval_decimal =
            fmi3_import_get_clock_variable_interval_decimal(clock);
        result.shift_decimal =
            fmi3_import_get_clock_variable_shift_decimal(clock);
    }
    if (static_cast<int>(fmi3_import_get_clock_variable_has_interval_counter(clock)) != fmi3_false) {
        result.interval_counter =
            fmi3_import_get_clock_variable_interval_counter(clock);
        result.shift_counter =
            fmi3_import_get_clock_variable_shift_counter(clock);
    }
    if (static_cast<int>(fmi3_import_get_clock_variable_has_resolution(clock)) != fmi3_false) {
        result.resolution = fmi3_import_get_clock_variable_resolution(clock);
    }
    return result;
}


clock_description to_clock_description(fmi3_import_clock_typedef_t* clock)
{
    clock_description result;
    result.interval_variability = to_clock_interval_variability(
        fmi3_import_get_clock_type_interval_variability(clock));
    result.can_be_deactivated =
        static_cast<int>(fmi3_import_get_clock_type_can_be_deactivated(clock)) != fmi3_false;
    if (static_cast<int>(fmi3_import_get_clock_type_has_priority(clock)) != fmi3_false) {
        result.priority = fmi3_import_get_clock_type_priority(clock);
    }
    result.supports_fraction =
        static_cast<int>(fmi3_import_get_clock_type_supports_fraction(clock)) != fmi3_false;
    if (static_cast<int>(fmi3_import_get_clock_type_has_interval_decimal(clock)) != fmi3_false) {
        result.interval_decimal =
            fmi3_import_get_clock_type_interval_decimal(clock);
        result.shift_decimal = fmi3_import_get_clock_type_shift_decimal(clock);
    }
    if (static_cast<int>(fmi3_import_get_clock_type_has_interval_counter(clock)) != fmi3_false) {
        result.interval_counter =
            fmi3_import_get_clock_type_interval_counter(clock);
        result.shift_counter = fmi3_import_get_clock_type_shift_counter(clock);
    }
    if (static_cast<int>(fmi3_import_get_clock_type_has_resolution(clock)) != fmi3_false) {
        result.resolution = fmi3_import_get_clock_type_resolution(clock);
    }
    return result;
}


std::vector<variable_dimension_description> to_dimensions(
    fmi3_import_variable_t* variable)
{
    // FMI 3 dimensions are either literal sizes or value references to
    // UInt64 structural parameters; accepting both would make the initial
    // shape ambiguous.
    auto* const list = fmi3_import_get_variable_dimension_list(variable);
    std::vector<variable_dimension_description> result;
    const auto count = fmi3_import_get_dimension_list_size(list);
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto* const dimension = fmi3_import_get_dimension(list, i);
        const bool hasStart =
            fmi3_import_get_dimension_has_start(dimension) != 0;
        const bool hasReference =
            fmi3_import_get_dimension_has_vr(dimension) != 0;
        if (hasStart == hasReference) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 3 array dimension must define exactly one of start or "
                "valueReference");
        }
        if (hasStart) {
            result.emplace_back(
                fixed_dimension{fmi3_import_get_dimension_start(dimension)});
        } else {
            result.emplace_back(variable_dimension{
                fmi3_import_get_dimension_vr(dimension),
            });
        }
    }
    return result;
}


std::vector<std::uint64_t> resolve_initial_shape(
    const std::vector<variable_dimension_description>& dimensions,
    const std::unordered_map<value_reference, std::uint64_t>&
        structuralParameterStarts)
{
    // Resolve structural dimensions once during import. Runtime shape changes
    // are handled by the FMU lifecycle and are not silently folded into a
    // stale generic value.
    std::vector<std::uint64_t> result;
    result.reserve(dimensions.size());
    for (const auto& dimension : dimensions) {
        if (const auto* const fixed = std::get_if<fixed_dimension>(&dimension)) {
            result.push_back(fixed->size);
        } else {
            const auto reference =
                std::get<variable_dimension>(dimension).structural_parameter;
            const auto start = structuralParameterStarts.find(reference);
            if (start == structuralParameterStarts.end()) {
                std::ostringstream message;
                message << "FMI 3 array dimension references UInt64 structural "
                           "parameter "
                        << reference << " without a scalar start value";
                throw error(
                    make_error_code(errc::bad_file),
                    message.str());
            }
            result.push_back(start->second);
        }
    }
    flat_size(result);
    return result;
}


template<typename Values, typename Getter>
variable_value make_values(
    std::size_t count,
    const std::vector<std::uint64_t>& dimensions,
    Getter getter)
{
    Values result;
    result.values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.values.push_back(getter(i));
    }
    return variable_value(std::move(result), dimensions);
}


template<typename T>
const T* checked_start_pointer(
    const T* values,
    std::size_t count,
    const char* type)
{
    if (values == nullptr && count != 0) {
        std::ostringstream message;
        message << "FMI 3 " << type
                << " array start has a null value pointer";
        throw error(make_error_code(errc::bad_file), message.str());
    }
    return values;
}

template<typename T>
gsl::span<const T> checked_start_array(
    const T* values,
    std::size_t count,
    const char* type)
{
    return gsl::make_span(checked_start_pointer(values, count, type), count);
}

template<typename T>
const T& start_array_at(gsl::span<const T> values, std::size_t index)
{
    return gsl::at(values, gsl::narrow<gsl::index>(index));
}


binary_value copy_binary(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr && size != 0) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 3 Binary start value has a null data pointer");
    }
    binary_value result;
    if (size != 0) {
        const auto bytes = gsl::make_span(data, size);
        result.data.assign(bytes.begin(), bytes.end());
    }
    return result;
}


std::optional<scalar_value> to_scalar_start(
    fmi3_import_variable_t* variable,
    variable_type type)
{
    if (fmi3_import_get_variable_has_start(variable) == 0) {
        return std::nullopt;
    }
    switch (type) {
        case variable_type::real:
            return fmi3_import_get_float64_variable_start(
                fmi3_import_get_variable_as_float64(variable));
        case variable_type::float32:
            return fmi3_import_get_float32_variable_start(
                fmi3_import_get_variable_as_float32(variable));
        case variable_type::int64:
            return fmi3_import_get_int64_variable_start(
                fmi3_import_get_variable_as_int64(variable));
        case variable_type::integer:
            return static_cast<int>(fmi3_import_get_int32_variable_start(
                fmi3_import_get_variable_as_int32(variable)));
        case variable_type::int16:
            return fmi3_import_get_int16_variable_start(
                fmi3_import_get_variable_as_int16(variable));
        case variable_type::int8:
            return fmi3_import_get_int8_variable_start(
                fmi3_import_get_variable_as_int8(variable));
        case variable_type::uint64:
            return fmi3_import_get_uint64_variable_start(
                fmi3_import_get_variable_as_uint64(variable));
        case variable_type::uint32:
            return fmi3_import_get_uint32_variable_start(
                fmi3_import_get_variable_as_uint32(variable));
        case variable_type::uint16:
            return fmi3_import_get_uint16_variable_start(
                fmi3_import_get_variable_as_uint16(variable));
        case variable_type::uint8:
            return fmi3_import_get_uint8_variable_start(
                fmi3_import_get_variable_as_uint8(variable));
        case variable_type::boolean:
            return static_cast<int>(fmi3_import_get_boolean_variable_start(
                       fmi3_import_get_variable_as_boolean(variable))) != fmi3_false;
        case variable_type::string:
            // FMI Library returns FMU/parser-owned character storage; copy it
            // before the metadata object outlives the import representation.
            return std::string(nonnull(fmi3_import_get_string_variable_start(
                fmi3_import_get_variable_as_string(variable))));
        case variable_type::enumeration:
            return enumeration_value{
                fmi3_import_get_enum_variable_start(
                    fmi3_import_get_variable_as_enum(variable)),
            };
        case variable_type::binary: {
            auto* const binary =
                fmi3_import_get_variable_as_binary(variable);
            return copy_binary(
                fmi3_import_get_binary_variable_start(binary),
                fmi3_import_get_binary_variable_start_size(binary));
        }
        case variable_type::clock:
            return std::nullopt;
    }
    COSIM_PANIC();
}


std::optional<variable_value> to_array_start(
    fmi3_import_variable_t* variable,
    variable_type type,
    const std::vector<std::uint64_t>& dimensions,
    std::size_t startCount)
{
    if (fmi3_import_get_variable_has_start(variable) == 0) {
        return std::nullopt;
    }
    const auto count = flat_size(dimensions);
    const bool broadcast = startCount == 1 &&
        type != variable_type::string && type != variable_type::binary;
    if (startCount != count && (!broadcast || count <= 0)) {
        throw error(make_error_code(errc::bad_file),
            "FMI 3 start count does not match array dimensions for '" +
                std::string(nonnull(fmi3_import_get_variable_name(variable))) + "'");
    }
    const auto index = [broadcast](std::size_t elementIndex) { return broadcast ? 0 : elementIndex; };
    switch (type) {
        case variable_type::real: {
            const auto values = checked_start_array(
                fmi3_import_get_float64_variable_start_array(
                    fmi3_import_get_variable_as_float64(variable)),
                startCount,
                "Float64");
            return make_values<real_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::float32: {
            const auto values = checked_start_array(
                fmi3_import_get_float32_variable_start_array(
                    fmi3_import_get_variable_as_float32(variable)),
                startCount,
                "Float32");
            return make_values<float32_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::int64: {
            const auto values = checked_start_array(
                fmi3_import_get_int64_variable_start_array(
                    fmi3_import_get_variable_as_int64(variable)),
                startCount,
                "Int64");
            return make_values<int64_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::integer: {
            const auto values = checked_start_array(
                fmi3_import_get_int32_variable_start_array(
                    fmi3_import_get_variable_as_int32(variable)),
                startCount,
                "Int32");
            return make_values<integer_values>(
                count,
                dimensions,
                [=](std::size_t elementIndex) { return static_cast<int>(start_array_at(values, index(elementIndex))); });
        }
        case variable_type::int16: {
            const auto values = checked_start_array(
                fmi3_import_get_int16_variable_start_array(
                    fmi3_import_get_variable_as_int16(variable)),
                startCount,
                "Int16");
            return make_values<int16_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::int8: {
            const auto values = checked_start_array(
                fmi3_import_get_int8_variable_start_array(
                    fmi3_import_get_variable_as_int8(variable)),
                startCount,
                "Int8");
            return make_values<int8_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::uint64: {
            const auto values = checked_start_array(
                fmi3_import_get_uint64_variable_start_array(
                    fmi3_import_get_variable_as_uint64(variable)),
                startCount,
                "UInt64");
            return make_values<uint64_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::uint32: {
            const auto values = checked_start_array(
                fmi3_import_get_uint32_variable_start_array(
                    fmi3_import_get_variable_as_uint32(variable)),
                startCount,
                "UInt32");
            return make_values<uint32_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::uint16: {
            const auto values = checked_start_array(
                fmi3_import_get_uint16_variable_start_array(
                    fmi3_import_get_variable_as_uint16(variable)),
                startCount,
                "UInt16");
            return make_values<uint16_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::uint8: {
            const auto values = checked_start_array(
                fmi3_import_get_uint8_variable_start_array(
                    fmi3_import_get_variable_as_uint8(variable)),
                startCount,
                "UInt8");
            return make_values<uint8_values>(
                count, dimensions, [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)); });
        }
        case variable_type::boolean: {
            const auto values = checked_start_array(
                fmi3_import_get_boolean_variable_start_array(
                    fmi3_import_get_variable_as_boolean(variable)),
                startCount,
                "Boolean");
            return make_values<boolean_values>(
                count,
                dimensions,
                [=](std::size_t elementIndex) { return start_array_at(values, index(elementIndex)) != fmi3_false; });
        }
        case variable_type::string: {
            // Copy every FMU-owned String pointer while the importer still
            // owns the model description and its temporary storage.
            const auto values = checked_start_array(
                fmi3_import_get_string_variable_start_array(
                    fmi3_import_get_variable_as_string(variable)),
                startCount,
                "String");
            return make_values<string_values>(
                count,
                dimensions,
                [=](std::size_t elementIndex) { return std::string(nonnull(start_array_at(values, elementIndex))); });
        }
        case variable_type::enumeration: {
            const auto values = checked_start_array(
                fmi3_import_get_enum_variable_start_array(
                    fmi3_import_get_variable_as_enum(variable)),
                startCount,
                "Enumeration");
            return make_values<enumeration_values>(
                count,
                dimensions,
                [=](std::size_t elementIndex) { return enumeration_value{start_array_at(values, index(elementIndex))}; });
        }
        case variable_type::binary: {
            // Binary arrays have one byte buffer and one size per element;
            // validate both arrays before constructing owning values.
            auto* const binary =
                fmi3_import_get_variable_as_binary(variable);
            const auto* values = checked_start_pointer(
                fmi3_import_get_binary_variable_start_array(binary),
                startCount,
                "Binary");
            const auto* sizes = checked_start_pointer(
                fmi3_import_get_binary_variable_start_array_sizes(binary),
                startCount,
                "Binary size");
            const auto binaryCount =
                fmi3_import_get_binary_variable_start_array_size(binary);
            if (binaryCount != count) {
                throw error(
                    make_error_code(errc::bad_file),
                    "FMI 3 Binary start count does not match array dimensions");
            }
            const auto valueSpan = gsl::make_span(values, startCount);
            const auto sizeSpan = gsl::make_span(sizes, startCount);
            return make_values<binary_values>(
                count,
                dimensions,
                [=](std::size_t elementIndex) { return copy_binary(start_array_at(valueSpan, elementIndex), start_array_at(sizeSpan, elementIndex)); });
        }
        case variable_type::clock:
            return std::nullopt;
    }
    COSIM_PANIC();
}


void add_aliases(
    variable_description& result,
    fmi3_import_variable_t* variable)
{
    if (static_cast<int>(fmi3_import_get_variable_has_alias(variable)) == fmi3_false) {
        return;
    }
    auto* const aliases = fmi3_import_get_variable_alias_list(variable);
    const auto count = fmi3_import_get_alias_variable_list_size(aliases);
    result.aliases.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto* const alias = fmi3_import_get_alias(aliases, i);
        variable_alias converted;
        converted.name = nonnull(fmi3_import_get_alias_variable_name(alias));
        if (static_cast<int>(fmi3_import_get_alias_variable_has_description(alias)) != fmi3_false) {
            converted.description =
                nonnull(fmi3_import_get_alias_variable_description(alias));
        }
        auto* const displayUnit =
            fmi3_import_get_alias_variable_display_unit(alias);
        if (displayUnit != nullptr) {
            converted.display_unit =
                nonnull(fmi3_import_get_display_unit_name(displayUnit));
        }
        result.aliases.push_back(std::move(converted));
    }
}


void add_clock_references(
    variable_description& result,
    fmi3_import_t* fmu,
    fmi3_import_variable_t* variable)
{
    if (static_cast<int>(fmi3_import_get_variable_is_clocked(variable)) == fmi3_false) {
        return;
    }
    auto* const clocks = fmi3_import_get_variable_clocks(fmu, variable);
    if (clocks == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            "Could not read FMI 3 variable Clock references");
    }
    const auto freeClocks =
        gsl::finally([&] { fmi3_import_free_variable_list(clocks); });
    const auto count = fmi3_import_get_variable_list_size(clocks);
    result.clocks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.clocks.push_back(fmi3_import_get_variable_vr(
            fmi3_import_get_variable(clocks, i)));
    }
}


void add_type_specific_metadata(
    variable_description& result,
    fmi3_import_variable_t* variable)
{
    switch (result.type) {
        case variable_type::real: {
            auto* const typed =
                fmi3_import_get_variable_as_float64(variable);
            result.quantity = optional_string(
                fmi3_import_get_float64_variable_quantity(typed));
            auto* const unit = fmi3_import_get_float64_variable_unit(typed);
            if (unit != nullptr) {
                result.unit = nonnull(fmi3_import_get_unit_name(unit));
            }
            auto* const display =
                fmi3_import_get_float64_variable_display_unit(typed);
            if (display != nullptr) {
                result.display_unit =
                    nonnull(fmi3_import_get_display_unit_name(display));
            }
            result.min = fmi3_import_get_float64_variable_min(typed);
            result.max = fmi3_import_get_float64_variable_max(typed);
            result.nominal = fmi3_import_get_float64_variable_nominal(typed);
            result.relative_quantity =
                static_cast<int>(fmi3_import_get_float64_variable_relative_quantity(typed)) != fmi3_false;
            result.unbounded =
                static_cast<int>(fmi3_import_get_float64_variable_unbounded(typed)) != fmi3_false;
            result.reinit =
                static_cast<int>(fmi3_import_get_float64_variable_reinit(typed)) != fmi3_false;
            auto* const derivative =
                fmi3_import_get_float64_variable_derivative_of(typed);
            if (derivative != nullptr) {
                result.derivative = fmi3_import_get_variable_vr(
                    reinterpret_cast<fmi3_import_variable_t*>(derivative)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): FMILibrary represents typed variables as opaque pointers.
            }
            return;
        }
        case variable_type::float32: {
            auto* const typed =
                fmi3_import_get_variable_as_float32(variable);
            result.quantity = optional_string(
                fmi3_import_get_float32_variable_quantity(typed));
            auto* const unit = fmi3_import_get_float32_variable_unit(typed);
            if (unit != nullptr) {
                result.unit = nonnull(fmi3_import_get_unit_name(unit));
            }
            auto* const display =
                fmi3_import_get_float32_variable_display_unit(typed);
            if (display != nullptr) {
                result.display_unit =
                    nonnull(fmi3_import_get_display_unit_name(display));
            }
            result.min = fmi3_import_get_float32_variable_min(typed);
            result.max = fmi3_import_get_float32_variable_max(typed);
            result.nominal = fmi3_import_get_float32_variable_nominal(typed);
            result.relative_quantity =
                static_cast<int>(fmi3_import_get_float32_variable_relative_quantity(typed)) != fmi3_false;
            result.unbounded =
                static_cast<int>(fmi3_import_get_float32_variable_unbounded(typed)) != fmi3_false;
            result.reinit =
                static_cast<int>(fmi3_import_get_float32_variable_reinit(typed)) != fmi3_false;
            auto* const derivative =
                fmi3_import_get_float32_variable_derivative_of(typed);
            if (derivative != nullptr) {
                result.derivative = fmi3_import_get_variable_vr(
                    reinterpret_cast<fmi3_import_variable_t*>(derivative)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): FMILibrary represents typed variables as opaque pointers.
            }
            return;
        }
#define COSIM_INTEGER_VARIABLE_METADATA(ENUM_VALUE, PREFIX, CAST)                                             \
    case variable_type::ENUM_VALUE: {                                                                         \
        const auto typed = fmi3_import_get_variable_as_##PREFIX(variable);                                    \
        result.quantity = optional_string(                                                                    \
            fmi3_import_get_##PREFIX##_variable_quantity(typed));                                             \
        result.min = CAST(fmi3_import_get_##PREFIX##_variable_min(typed)); /* NOLINT(misc-include-cleaner) */ \
        result.max = CAST(fmi3_import_get_##PREFIX##_variable_max(typed)); /* NOLINT(misc-include-cleaner) */ \
        return;                                                                                               \
    }
            COSIM_INTEGER_VARIABLE_METADATA(int64, int64, static_cast<std::int64_t>)
            COSIM_INTEGER_VARIABLE_METADATA(integer, int32, static_cast<int>)
            COSIM_INTEGER_VARIABLE_METADATA(int16, int16, static_cast<std::int16_t>)
            COSIM_INTEGER_VARIABLE_METADATA(int8, int8, static_cast<std::int8_t>)
            COSIM_INTEGER_VARIABLE_METADATA(uint64, uint64, static_cast<std::uint64_t>)
            COSIM_INTEGER_VARIABLE_METADATA(uint32, uint32, static_cast<std::uint32_t>)
            COSIM_INTEGER_VARIABLE_METADATA(uint16, uint16, static_cast<std::uint16_t>)
            COSIM_INTEGER_VARIABLE_METADATA(uint8, uint8, static_cast<std::uint8_t>)
#undef COSIM_INTEGER_VARIABLE_METADATA
        case variable_type::enumeration: {
            auto* const typed = fmi3_import_get_variable_as_enum(variable);
            result.quantity = optional_string(
                fmi3_import_get_enum_variable_quantity(typed));
            result.min = enumeration_value{
                fmi3_import_get_enum_variable_min(typed),
            };
            result.max = enumeration_value{
                fmi3_import_get_enum_variable_max(typed),
            };
            return;
        }
        case variable_type::binary: {
            auto* const typed = fmi3_import_get_variable_as_binary(variable);
            result.mime_type = std::string(
                nonnull(fmi3_import_get_binary_variable_mime_type(typed)));
            if (static_cast<int>(fmi3_import_get_binary_variable_has_max_size(typed)) != fmi3_false) {
                result.max_size =
                    fmi3_import_get_binary_variable_max_size(typed);
            }
            return;
        }
        case variable_type::clock:
            result.clock = to_clock_description(
                fmi3_import_get_variable_as_clock(variable));
            return;
        case variable_type::boolean:
        case variable_type::string:
            return;
    }
    COSIM_PANIC();
}


void add_float_type_metadata(
    variable_type_definition& result,
    fmi3_import_float_typedef_t* type)
{
    if (result.type == variable_type::real) {
        result.min = fmi3_import_get_float64_type_min(type);
        result.max = fmi3_import_get_float64_type_max(type);
        result.nominal = fmi3_import_get_float64_type_nominal(type);
        result.relative_quantity =
            fmi3_import_get_float64_type_is_relative_quantity(type) != 0;
        result.unbounded =
            fmi3_import_get_float64_type_is_unbounded(type) != 0;
        auto* const unit = fmi3_import_get_float64_type_unit(type);
        if (unit != nullptr) {
            result.unit = nonnull(fmi3_import_get_unit_name(unit));
        }
        auto* const display = fmi3_import_get_float64_type_display_unit(type);
        if (display != nullptr) {
            result.display_unit =
                nonnull(fmi3_import_get_display_unit_name(display));
        }
    } else {
        result.min = fmi3_import_get_float32_type_min(type);
        result.max = fmi3_import_get_float32_type_max(type);
        result.nominal = fmi3_import_get_float32_type_nominal(type);
        result.relative_quantity =
            fmi3_import_get_float32_type_is_relative_quantity(type) != 0;
        result.unbounded =
            fmi3_import_get_float32_type_is_unbounded(type) != 0;
        auto* const unit = fmi3_import_get_float32_type_unit(type);
        if (unit != nullptr) {
            result.unit = nonnull(fmi3_import_get_unit_name(unit));
        }
        auto* const display = fmi3_import_get_float32_type_display_unit(type);
        if (display != nullptr) {
            result.display_unit =
                nonnull(fmi3_import_get_display_unit_name(display));
        }
    }
}


void add_integer_type_metadata(
    variable_type_definition& result,
    fmi3_import_int_typedef_t* type)
{
    switch (result.type) {
        case variable_type::int64:
            result.min = static_cast<std::int64_t>(
                fmi3_import_get_int64_type_min(type));
            result.max = static_cast<std::int64_t>(
                fmi3_import_get_int64_type_max(type));
            return;
        case variable_type::integer:
            result.min =
                static_cast<int>(fmi3_import_get_int32_type_min(type));
            result.max =
                static_cast<int>(fmi3_import_get_int32_type_max(type));
            return;
        case variable_type::int16:
            result.min = static_cast<std::int16_t>(
                fmi3_import_get_int16_type_min(type));
            result.max = static_cast<std::int16_t>(
                fmi3_import_get_int16_type_max(type));
            return;
        case variable_type::int8:
            result.min = static_cast<std::int8_t>(
                fmi3_import_get_int8_type_min(type));
            result.max = static_cast<std::int8_t>(
                fmi3_import_get_int8_type_max(type));
            return;
        case variable_type::uint64:
            result.min = static_cast<std::uint64_t>(
                fmi3_import_get_uint64_type_min(type));
            result.max = static_cast<std::uint64_t>(
                fmi3_import_get_uint64_type_max(type));
            return;
        case variable_type::uint32:
            result.min = static_cast<std::uint32_t>(
                fmi3_import_get_uint32_type_min(type));
            result.max = static_cast<std::uint32_t>(
                fmi3_import_get_uint32_type_max(type));
            return;
        case variable_type::uint16:
            result.min = static_cast<std::uint16_t>(
                fmi3_import_get_uint16_type_min(type));
            result.max = static_cast<std::uint16_t>(
                fmi3_import_get_uint16_type_max(type));
            return;
        case variable_type::uint8:
            result.min = static_cast<std::uint8_t>(
                fmi3_import_get_uint8_type_min(type));
            result.max = static_cast<std::uint8_t>(
                fmi3_import_get_uint8_type_max(type));
            return;
        case variable_type::real:
        case variable_type::float32:
        case variable_type::boolean:
        case variable_type::string:
        case variable_type::enumeration:
        case variable_type::binary:
        case variable_type::clock:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}


void add_enumeration_type_metadata(
    variable_type_definition& result,
    fmi3_import_enumeration_typedef_t* enumeration)
{
    if (enumeration == nullptr || result.name.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 3.0 Enumeration declared type is incomplete");
    }
    const auto itemCount = fmi3_import_get_enum_type_size(enumeration);
    if (itemCount == 0) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 3.0 Enumeration declared type '" + result.name +
                "' has no items");
    }

    std::unordered_set<std::string> itemNames;
    std::unordered_set<std::int64_t> itemValues;
    result.enumeration_items.reserve(itemCount);
    for (std::size_t item = 1; item <= itemCount; ++item) {
        const auto* const name = nonnull(
            fmi3_import_get_enum_type_item_name(enumeration, item));
        const auto value = static_cast<std::int64_t>(
            fmi3_import_get_enum_type_item_value(enumeration, item));
        if (*name == '\0' || !itemNames.emplace(name).second ||
            !itemValues.emplace(value).second) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 3.0 Enumeration declared type '" +
                    result.name +
                    "' contains duplicate or empty item metadata");
        }
        result.enumeration_items.push_back(
            {
                name,
                value,
                nonnull(fmi3_import_get_enum_type_item_description(
                    enumeration, item)),
            });
    }
    const auto minItem = boost::range::min_element(
        result.enumeration_items,
        [](const auto& lhs, const auto& rhs) {
            return lhs.value < rhs.value;
        });
    const auto maxItem = boost::range::max_element(
        result.enumeration_items,
        [](const auto& lhs, const auto& rhs) {
            return lhs.value < rhs.value;
        });
    // FMI Library exposes these type bounds through an unsigned
    // API; derive signed bounds from the validated item values.
    result.min = enumeration_value{minItem->value};
    result.max = enumeration_value{maxItem->value};
}
} // namespace

variable_description to_variable_description(
    fmi3_import_t* fmu,
    fmi3_import_variable_t* fmiVariable,
    const std::unordered_map<value_reference, std::uint64_t>&
        structuralParameterStarts,
    const fmi3_array_start_counts& arrayStartCounts)
{
    assert(fmu != nullptr);
    assert(fmiVariable != nullptr);

    // Preserve exact FMI 3 types and resolved dimensions in the generic model;
    // later typed access uses them to reject incompatible references/counts.
    variable_description result;
    result.name = nonnull(fmi3_import_get_variable_name(fmiVariable));
    result.reference = fmi3_import_get_variable_vr(fmiVariable);
    result.type =
        to_variable_type(fmi3_import_get_variable_base_type(fmiVariable));
    result.causality =
        to_variable_causality(fmi3_import_get_variable_causality(fmiVariable));
    result.variability =
        to_variable_variability(fmi3_import_get_variable_variability(fmiVariable));
    result.intermediate_update =
        static_cast<int>(fmi3_import_get_variable_intermediate_update(fmiVariable)) != fmi3_false;
    if (static_cast<int>(fmi3_import_get_variable_has_description(fmiVariable)) != fmi3_false) {
        result.description =
            nonnull(fmi3_import_get_variable_description(fmiVariable));
    }
    result.initial =
        to_variable_initial(fmi3_import_get_variable_initial(fmiVariable));
    if (auto* const declared =
            fmi3_import_get_variable_declared_type(fmiVariable)) {
        result.declared_type = nonnull(fmi3_import_get_type_name(declared));
    }
    result.dimensions = to_dimensions(fmiVariable);
    const auto resolvedDimensions =
        resolve_initial_shape(result.dimensions, structuralParameterStarts);
    if (result.dimensions.empty()) {
        result.start = to_scalar_start(fmiVariable, result.type);
    } else if (fmi3_import_get_variable_has_start(fmiVariable) != 0) {
        const auto count = arrayStartCounts.find(result.name);
        if (count == arrayStartCounts.end()) {
            throw error(make_error_code(errc::bad_file),
                "Cannot verify the FMI 3 array start for '" + result.name + "'");
        }
        result.array_start =
            to_array_start(fmiVariable, result.type, resolvedDimensions, count->second);
    }
    if (auto* const previous =
            fmi3_import_get_variable_previous(fmiVariable)) {
        result.previous = fmi3_import_get_variable_vr(previous);
    }
    result.can_handle_multiple_set_per_time_instant =
        static_cast<int>(fmi3_import_get_variable_can_handle_multiple_set_per_time_instant(
            fmiVariable)) != fmi3_false;
    add_aliases(result, fmiVariable);
    add_clock_references(result, fmu, fmiVariable);
    add_type_specific_metadata(result, fmiVariable);
    return result;
}


std::vector<variable_type_definition>
to_type_definitions(fmi3_import_t* fmu)
{
    assert(fmu != nullptr);
    auto* const list = fmi3_import_get_type_definition_list(fmu);
    const auto count = fmi3_import_get_type_definition_list_size(list);
    std::vector<variable_type_definition> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto* const type = fmi3_import_get_typedef(list, i);
        variable_type_definition converted;
        converted.name = nonnull(fmi3_import_get_type_name(type));
        converted.description =
            nonnull(fmi3_import_get_type_description(type));
        converted.type = to_variable_type(fmi3_import_get_base_type(type));
        converted.quantity =
            optional_string(fmi3_import_get_type_quantity(type));
        bool handled = false;
        switch (converted.type) {
            case variable_type::real:
            case variable_type::float32:
                add_float_type_metadata(
                    converted, fmi3_import_get_type_as_float(type));
                handled = true;
                break;
            case variable_type::int64:
            case variable_type::integer:
            case variable_type::int16:
            case variable_type::int8:
            case variable_type::uint64:
            case variable_type::uint32:
            case variable_type::uint16:
            case variable_type::uint8:
                add_integer_type_metadata(
                    converted, fmi3_import_get_type_as_int(type));
                handled = true;
                break;
            case variable_type::enumeration: {
                add_enumeration_type_metadata(
                    converted, fmi3_import_get_type_as_enum(type));
                handled = true;
                break;
            }
            case variable_type::binary: {
                auto* const binary =
                    fmi3_import_get_type_as_binary(type);
                converted.mime_type = std::string(
                    nonnull(fmi3_import_get_binary_type_mime_type(binary)));
                if (static_cast<int>(fmi3_import_get_binary_type_has_max_size(binary)) != fmi3_false) {
                    converted.max_size =
                        fmi3_import_get_binary_type_max_size(binary);
                }
                handled = true;
                break;
            }
            case variable_type::clock:
                converted.clock = to_clock_description(
                    fmi3_import_get_type_as_clock(type));
                handled = true;
                break;
            case variable_type::boolean:
            case variable_type::string:
                handled = true;
                break;
        }
        if (!handled) {
            COSIM_PANIC();
        }
        result.push_back(std::move(converted));
    }
    return result;
}


std::vector<unit_definition> to_unit_definitions(fmi3_import_t* fmu)
{
    assert(fmu != nullptr);
    auto* const list = fmi3_import_get_unit_definition_list(fmu);
    const auto count = fmi3_import_get_unit_definition_list_size(list);
    std::vector<unit_definition> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto* const unit = fmi3_import_get_unit(list, i);
        const auto exponents = gsl::make_span(
            fmi3_import_get_SI_unit_exponents(unit), fmi3_SI_base_units_Num);
        unit_definition converted;
        converted.name = nonnull(fmi3_import_get_unit_name(unit));
        converted.kilogram = gsl::at(exponents, fmi3_SI_base_unit_kg);
        converted.metre = gsl::at(exponents, fmi3_SI_base_unit_m);
        converted.second = gsl::at(exponents, fmi3_SI_base_unit_s);
        converted.ampere = gsl::at(exponents, fmi3_SI_base_unit_A);
        converted.kelvin = gsl::at(exponents, fmi3_SI_base_unit_K);
        converted.mole = gsl::at(exponents, fmi3_SI_base_unit_mol);
        converted.candela = gsl::at(exponents, fmi3_SI_base_unit_cd);
        converted.radian = gsl::at(exponents, fmi3_SI_base_unit_rad);
        converted.factor = fmi3_import_get_SI_unit_factor(unit);
        converted.offset = fmi3_import_get_SI_unit_offset(unit);
        const auto displayCount =
            fmi3_import_get_unit_display_unit_list_size(unit);
        converted.display_units.reserve(displayCount);
        for (std::size_t displayIndex = 0;
            displayIndex < displayCount;
            ++displayIndex) {
            auto* const display =
                fmi3_import_get_unit_display_unit(unit, displayIndex);
            converted.display_units.push_back({
                nonnull(fmi3_import_get_display_unit_name(display)),
                fmi3_import_get_display_unit_factor(display),
                fmi3_import_get_display_unit_offset(display),
                fmi3_import_get_display_unit_inverse(display) != 0,
            });
        }
        result.push_back(std::move(converted));
    }
    return result;
}

} // namespace cosim::fmi
