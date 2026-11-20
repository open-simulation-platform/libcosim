/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "fmu_internal.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/fmi/v3/fmu.hpp"
#include "cosim/model_description.hpp"
#include "cosim/utility/enumeration.hpp"

#include <boost/container/small_vector.hpp>
#include <boost/container/vector.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <gsl/narrow>
#include <gsl/span>
#include <gsl/span_ext>
#include <gsl/util>

#include <FMI3/fmi3PlatformTypes.h>
#include <FMI3/fmi3_import_capi.h>
#include <FMI3/fmi3_types.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


namespace cosim::fmi::v3
{

using adapter_detail::check_available;
using adapter_detail::check_status;
using adapter_detail::operation_access;
using adapter_detail::resolve_shape;

namespace
{

template<typename Values>
struct whole_value_operations;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): This list generates both specializations and switch cases.
#define LIBCOSIM_FMI3_WHOLE_VALUE_TYPES(X)          \
    X(real, real_values, real)                      \
    X(integer, integer_values, integer)             \
    X(boolean, boolean_values, boolean)             \
    X(string, string_values, string)                \
    X(enumeration, enumeration_values, enumeration) \
    X(float32, float32_values, float32)             \
    X(int8, int8_values, int8)                      \
    X(uint8, uint8_values, uint8)                   \
    X(int16, int16_values, int16)                   \
    X(uint16, uint16_values, uint16)                \
    X(uint32, uint32_values, uint32)                \
    X(int64, int64_values, int64)                   \
    X(uint64, uint64_values, uint64)                \
    X(binary, binary_values, binary)

#define LIBCOSIM_FMI3_WHOLE_VALUE_OPERATIONS(TYPE, VALUES, ACCESSOR)             \
    template<>                                                                   \
    struct whole_value_operations<VALUES>                                        \
    {                                                                            \
        using values_type = VALUES;                                              \
        static constexpr auto get = &slave_instance::get_##ACCESSOR##_variables; \
        static constexpr auto set = &slave_instance::set_##ACCESSOR##_variables; \
    };
LIBCOSIM_FMI3_WHOLE_VALUE_TYPES(LIBCOSIM_FMI3_WHOLE_VALUE_OPERATIONS)
#undef LIBCOSIM_FMI3_WHOLE_VALUE_OPERATIONS

template<typename Visitor>
decltype(auto) visit_whole_value_type(variable_type type, const std::string& instanceName, const Visitor& visit)
{
    switch (type) {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Generates a case for each value type.
#define LIBCOSIM_FMI3_VISIT_WHOLE_VALUE(TYPE, VALUES, ACCESSOR) \
    case variable_type::TYPE: return visit(whole_value_operations<VALUES>{});
        LIBCOSIM_FMI3_WHOLE_VALUE_TYPES(LIBCOSIM_FMI3_VISIT_WHOLE_VALUE)
#undef LIBCOSIM_FMI3_VISIT_WHOLE_VALUE
        case variable_type::clock: break;
    }
    throw error(make_error_code(errc::unsupported_feature),
        instanceName + ": Clock values require the FMI 3 clock API");
}
#undef LIBCOSIM_FMI3_WHOLE_VALUE_TYPES

// Keep small batches allocation-free and keep the staging local so nested
// calls cannot overwrite another call's FMI buffers.
constexpr std::size_t small_value_buffer_capacity = 8;
template<typename T>
using value_buffer = boost::container::small_vector<T, small_value_buffer_capacity>;

const variable_value* converted_or_original(
    const std::vector<std::optional<variable_value>>& conversions,
    std::size_t index,
    const variable_value* original)
{
    if (!conversions.empty()) {
        const auto& converted = conversions.at(index);
        if (converted) {
            return &*converted;
        }
    }
    return original;
}

} // namespace


void slave_instance::validate_enumeration_values(
    gsl::span<const value_reference> variables,
    gsl::span<const enumeration_value> values,
    const std::unordered_map<value_reference, std::uint64_t>&
        structuralParameterValues,
    const char* operation) const
{
    std::size_t offset = 0;
    for (const auto reference : variables) {
        const auto* variable =
            fmu_->find_variable(variable_type::enumeration, reference);
        if (variable == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": " + operation +
                    " received a non-Enumeration value reference");
        }
        const auto count = flat_size(
            resolve_shape(*variable, structuralParameterValues, instanceName_));
        if (count > values.size() - (std::min)(offset, values.size())) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": " + operation +
                    " received too few Enumeration values");
        }
        const auto* minimum =
            variable->min ? std::get_if<enumeration_value>(&*variable->min) : nullptr;
        const auto* maximum =
            variable->max ? std::get_if<enumeration_value>(&*variable->max) : nullptr;
        const auto& declaredTypes = fmu_->enumItemsByType_;
        const auto items = variable->declared_type
            ? declaredTypes.find(*variable->declared_type)
            : declaredTypes.end();
        if (items == declaredTypes.end()) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": " + operation +
                    " received an Enumeration without a declared type");
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto value = gsl::at(values, gsl::narrow<gsl::index>(offset + i));
            if ((minimum != nullptr && value.value < minimum->value) ||
                (maximum != nullptr && value.value > maximum->value)) {
                throw error(
                    make_error_code(errc::model_error),
                    instanceName_ + ": " + operation +
                        " received an Enumeration value outside the bounds of '" +
                        variable->name + "'");
            }
            if (items->second.count(value.value) == 0) {
                throw error(
                    make_error_code(errc::model_error),
                    instanceName_ + ": " + operation +
                        " received an undeclared Enumeration value for '" +
                        variable->name + "'");
            }
        }
        offset += count;
    }
    if (offset != values.size()) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": " + operation +
                " received too many Enumeration values");
    }
}

std::vector<std::uint64_t> slave_instance::resolved_shape(
    variable_type type,
    value_reference variable) const
{
    // FMI 3 passes flattened value arrays, while libcosim exposes one
    // variable at a time. Resolve structural dimensions before allocating the
    // destination so the C++ and FMI counts cannot diverge.
    std::scoped_lock const lock(mutex_);
    const auto* description = fmu_->find_variable(type, variable);
    if (description == nullptr) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": value reference " + std::to_string(variable) + " is not a " + to_text(type) + " variable");
    }
    return resolve_shape(
        *description, structuralParameterValues_, instanceName_);
}


void slave_instance::refresh_structural_parameters()
{
    std::vector<value_reference> references;
    for (const auto& variable : fmu_->model_description()->variables) {
        if (variable.type == variable_type::uint64 && variable.causality == variable_causality::structural_parameter && variable.dimensions.empty()) {
            references.push_back(variable.reference);
        }
    }
    if (references.empty()) {
        return;
    }

    std::vector<std::uint64_t> values(references.size());
    const auto status = fmi3_import_get_uint64(
        handle_,
        references.data(),
        references.size(),
        values.data(),
        values.size());
    check_status(callbackContext_.get(), "GetUInt64", status);
    auto value = values.begin();
    for (const auto reference : references) {
        structuralParameterValues_.insert_or_assign(reference, *value);
        ++value;
    }
}


#define COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(NAME, VALUE_TYPE, VARIABLE_TYPE, FMI_FUNCTION, OPERATION)    \
    void slave_instance::get_##NAME##_variables(                                                         \
        gsl::span<const value_reference> variables,                                                      \
        gsl::span<VALUE_TYPE> values) const                                                              \
    {                                                                                                    \
        const std::scoped_lock lock(mutex_);                                                             \
        callbackContext_->rethrow_callback_exception();                                                  \
        check_available(callbackContext_.get(), instanceName_, OPERATION, operation_access::diagnostic); \
        if (variables.empty()) {                                                                         \
            COSIM_INPUT_CHECK(values.empty());                                                           \
            return;                                                                                      \
        }                                                                                                \
        check_intermediate_access(variables, false);                                                     \
        const auto valueCount = check_variable_access(                                                   \
            variables, VARIABLE_TYPE, false);                                                            \
        COSIM_INPUT_CHECK(valueCount == values.size());                                                  \
        const auto status = FMI_FUNCTION(                                                                \
            handle_, variables.data(), variables.size(), values.data(), values.size());                  \
        check_status(callbackContext_.get(), OPERATION, status);                                         \
    }

COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    real, double, variable_type::real, fmi3_import_get_float64, "GetFloat64")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    float32, float, variable_type::float32, fmi3_import_get_float32, "GetFloat32")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    int8, std::int8_t, variable_type::int8, fmi3_import_get_int8, "GetInt8")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    uint8, std::uint8_t, variable_type::uint8, fmi3_import_get_uint8, "GetUInt8")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    int16, std::int16_t, variable_type::int16, fmi3_import_get_int16, "GetInt16")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    uint16, std::uint16_t, variable_type::uint16, fmi3_import_get_uint16, "GetUInt16")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    uint32, std::uint32_t, variable_type::uint32, fmi3_import_get_uint32, "GetUInt32")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    int64, std::int64_t, variable_type::int64, fmi3_import_get_int64, "GetInt64")
COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER(
    uint64, std::uint64_t, variable_type::uint64, fmi3_import_get_uint64, "GetUInt64")

#undef COSIM_DEFINE_FMI3_CS_NUMERIC_GETTER


void slave_instance::get_integer_variables(
    gsl::span<const value_reference> variables,
    gsl::span<int> values) const
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetInt32", operation_access::diagnostic);
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, false);
    const auto valueCount =
        check_variable_access(variables, variable_type::integer, false);
    COSIM_INPUT_CHECK(valueCount == values.size());
    value_buffer<fmi3_int32_t> fmiValues(values.size());
    const auto status = fmi3_import_get_int32(
        handle_, variables.data(), variables.size(), fmiValues.data(), fmiValues.size());
    check_status(callbackContext_.get(), "GetInt32", status);
    auto source = fmiValues.begin();
    for (auto& value : values) {
        value = static_cast<int>(*source); // NOLINT(readability-redundant-casting): FMI typedefs vary by platform.
        ++source;
    }
}


void slave_instance::get_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<bool> values) const
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetBoolean", operation_access::diagnostic);
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, false);
    const auto valueCount =
        check_variable_access(variables, variable_type::boolean, false);
    COSIM_INPUT_CHECK(valueCount == values.size());
    value_buffer<fmi3_boolean_t> fmiValues(values.size());
    const auto status = fmi3_import_get_boolean(
        handle_, variables.data(), variables.size(), fmiValues.data(), values.size());
    check_status(callbackContext_.get(), "GetBoolean", status);
    auto source = fmiValues.begin();
    for (auto& value : values) {
        value = static_cast<int>(*source) != fmi3_false;
        ++source;
    }
}


void slave_instance::get_string_variables(
    gsl::span<const value_reference> variables,
    gsl::span<std::string> values) const
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetString", operation_access::diagnostic);
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, false);
    const auto valueCount =
        check_variable_access(variables, variable_type::string, false);
    COSIM_INPUT_CHECK(valueCount == values.size());
    value_buffer<fmi3_string_t> fmiValues(values.size());
    const auto status = fmi3_import_get_string(
        handle_, variables.data(), variables.size(), fmiValues.data(), fmiValues.size());
    check_status(callbackContext_.get(), "GetString", status);
    auto source = fmiValues.begin();
    for (auto& value : values) {
        value = *source == nullptr ? std::string() : std::string(*source);
        ++source;
    }
}


void slave_instance::get_binary_variables(
    gsl::span<const value_reference> variables,
    gsl::span<binary_value> values) const
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetBinary", operation_access::diagnostic);
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, false);
    const auto valueCount =
        check_variable_access(variables, variable_type::binary, false);
    COSIM_INPUT_CHECK(valueCount == values.size());
    value_buffer<std::size_t> sizes(values.size());
    value_buffer<fmi3_binary_t> fmiValues(values.size());
    const auto status = fmi3_import_get_binary(
        handle_,
        variables.data(),
        variables.size(),
        sizes.data(),
        fmiValues.data(),
        fmiValues.size());
    check_status(callbackContext_.get(), "GetBinary", status);
    auto size = sizes.begin();
    auto source = fmiValues.begin();
    for (auto& value : values) {
        if (*size > 0 && *source == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": GetBinary returned a null pointer for a nonempty value");
        }
        if (*size == 0) {
            value.data.clear();
        } else {
            const auto bytes = gsl::make_span(*source, *size);
            value.data.assign(bytes.begin(), bytes.end());
        }
        ++size;
        ++source;
    }
}


void slave_instance::get_enumeration_variables(
    gsl::span<const value_reference> variables,
    gsl::span<enumeration_value> values) const
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetInt64", operation_access::diagnostic);
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, false);
    const auto valueCount =
        check_variable_access(variables, variable_type::enumeration, false);
    COSIM_INPUT_CHECK(valueCount == values.size());
    value_buffer<fmi3_int64_t> fmiValues(values.size());
    const auto status = fmi3_import_get_int64(
        handle_,
        variables.data(),
        variables.size(),
        fmiValues.data(),
        fmiValues.size());
    check_status(callbackContext_.get(), "GetInt64", status);
    value_buffer<enumeration_value> stagedValues;
    stagedValues.reserve(fmiValues.size());
    for (const auto value : fmiValues) {
        stagedValues.push_back(enumeration_value{value});
    }
    validate_enumeration_values(
        variables,
        gsl::make_span(stagedValues),
        structuralParameterValues_,
        "GetInt64");
    boost::range::copy(stagedValues, values.begin());
}


#define COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(NAME, VALUE_TYPE, VARIABLE_TYPE, FMI_FUNCTION, OPERATION) \
    void slave_instance::set_##NAME##_variables(                                                      \
        gsl::span<const value_reference> variables,                                                   \
        gsl::span<const VALUE_TYPE> values)                                                           \
    {                                                                                                 \
        const std::scoped_lock lock(mutex_);                                                          \
        callbackContext_->rethrow_callback_exception();                                               \
        check_available(callbackContext_.get(), instanceName_, OPERATION);                            \
        if (variables.empty()) {                                                                      \
            COSIM_INPUT_CHECK(values.empty());                                                        \
            return;                                                                                   \
        }                                                                                             \
        check_intermediate_access(variables, true);                                                   \
        const auto valueCount = check_variable_access(                                                \
            variables, VARIABLE_TYPE, true);                                                          \
        COSIM_INPUT_CHECK(valueCount == values.size());                                               \
        if (intermediateState_) {                                                                     \
            intermediateState_->setStarted = true;                                                    \
        }                                                                                             \
        const auto status = FMI_FUNCTION(                                                             \
            handle_, variables.data(), variables.size(), values.data(), values.size());               \
        check_status(callbackContext_.get(), OPERATION, status);                                      \
        if (state_ == state::step && !intermediateState_) {                                           \
            stepSetStarted_ = true;                                                                   \
        }                                                                                             \
    }

COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    real, double, variable_type::real, fmi3_import_set_float64, "SetFloat64")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    float32, float, variable_type::float32, fmi3_import_set_float32, "SetFloat32")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    int8, std::int8_t, variable_type::int8, fmi3_import_set_int8, "SetInt8")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    uint8, std::uint8_t, variable_type::uint8, fmi3_import_set_uint8, "SetUInt8")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    int16, std::int16_t, variable_type::int16, fmi3_import_set_int16, "SetInt16")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    uint16, std::uint16_t, variable_type::uint16, fmi3_import_set_uint16, "SetUInt16")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    uint32, std::uint32_t, variable_type::uint32, fmi3_import_set_uint32, "SetUInt32")
COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER(
    int64, std::int64_t, variable_type::int64, fmi3_import_set_int64, "SetInt64")

#undef COSIM_DEFINE_FMI3_CS_NUMERIC_SETTER


void slave_instance::set_integer_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const int> values)
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetInt32");
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, true);
    const auto valueCount =
        check_variable_access(variables, variable_type::integer, true);
    COSIM_INPUT_CHECK(valueCount == values.size());
    if (intermediateState_) {
        intermediateState_->setStarted = true;
    }
    static_assert(std::is_same_v<fmi3_int32_t, int>);
    const auto status = fmi3_import_set_int32(
        handle_, variables.data(), variables.size(), values.data(), values.size());
    check_status(callbackContext_.get(), "SetInt32", status);
    if (state_ == state::step && !intermediateState_) {
        stepSetStarted_ = true;
    }
}


void slave_instance::set_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const bool> values)
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetBoolean");
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, true);
    const auto valueCount =
        check_variable_access(variables, variable_type::boolean, true);
    COSIM_INPUT_CHECK(valueCount == values.size());
    if (intermediateState_) {
        intermediateState_->setStarted = true;
    }
    static_assert(std::is_same_v<fmi3_boolean_t, bool>);
    const auto status = fmi3_import_set_boolean(
        handle_, variables.data(), variables.size(), values.data(), values.size());
    check_status(callbackContext_.get(), "SetBoolean", status);
    if (state_ == state::step && !intermediateState_) {
        stepSetStarted_ = true;
    }
}


void slave_instance::set_string_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const std::string> values)
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetString");
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, true);
    const auto valueCount =
        check_variable_access(variables, variable_type::string, true);
    COSIM_INPUT_CHECK(valueCount == values.size());
    if (intermediateState_) {
        intermediateState_->setStarted = true;
    }
    value_buffer<fmi3_string_t> fmiValues(values.size());
    auto target = fmiValues.begin();
    for (const auto& value : values) {
        *target = value.c_str();
        ++target;
    }
    const auto status = fmi3_import_set_string(
        handle_, variables.data(), variables.size(), fmiValues.data(), fmiValues.size());
    check_status(callbackContext_.get(), "SetString", status);
    if (state_ == state::step && !intermediateState_) {
        stepSetStarted_ = true;
    }
}


void slave_instance::set_uint64_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const std::uint64_t> values)
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetUInt64");
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, true);
    const auto valueCount =
        check_variable_access(variables, variable_type::uint64, true);
    COSIM_INPUT_CHECK(valueCount == values.size());
    if (intermediateState_) {
        intermediateState_->setStarted = true;
    }
    // Every offset is resolved against the shape before the FMI call.
    std::vector<std::pair<value_reference, std::uint64_t>> dimensionUpdates;
    std::size_t offset = 0;
    for (const auto reference : variables) {
        const auto* description = fmu_->find_variable(variable_type::uint64, reference);
        assert(description != nullptr);
        if (description->dimensions.empty()) {
            if (description->causality == variable_causality::structural_parameter) {
                dimensionUpdates.emplace_back(
                    reference, gsl::at(values, gsl::narrow<gsl::index>(offset)));
            }
            ++offset;
        } else {
            offset += flat_size(resolve_shape(*description, structuralParameterValues_, instanceName_));
        }
    }
    const auto status = fmi3_import_set_uint64(
        handle_,
        variables.data(),
        variables.size(),
        values.data(),
        values.size());
    check_status(callbackContext_.get(), "SetUInt64", status);

    for (const auto& [reference, value] : dimensionUpdates) {
        structuralParameterValues_.insert_or_assign(reference, value);
    }
    if (state_ == state::step && !intermediateState_) {
        stepSetStarted_ = true;
    }
}


void slave_instance::set_binary_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const binary_value> values)
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetBinary");
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, true);
    const auto valueCount =
        check_variable_access(variables, variable_type::binary, true);
    COSIM_INPUT_CHECK(valueCount == values.size());
    if (intermediateState_) {
        intermediateState_->setStarted = true;
    }
    value_buffer<std::size_t> sizes(values.size());
    value_buffer<fmi3_binary_t> fmiValues(values.size());
    auto size = sizes.begin();
    auto target = fmiValues.begin();
    for (const auto& value : values) {
        *size = value.data.size();
        *target = value.data.data();
        ++size;
        ++target;
    }
    const auto status = fmi3_import_set_binary(
        handle_,
        variables.data(),
        variables.size(),
        sizes.data(),
        fmiValues.data(),
        fmiValues.size());
    check_status(callbackContext_.get(), "SetBinary", status);
    if (state_ == state::step && !intermediateState_) {
        stepSetStarted_ = true;
    }
}


void slave_instance::set_enumeration_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const enumeration_value> values)
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetInt64");
    if (variables.empty()) {
        COSIM_INPUT_CHECK(values.empty());
        return;
    }
    check_intermediate_access(variables, true);
    const auto valueCount =
        check_variable_access(variables, variable_type::enumeration, true);
    COSIM_INPUT_CHECK(valueCount == values.size());
    validate_enumeration_values(
        variables,
        values,
        structuralParameterValues_,
        "SetInt64");
    if (intermediateState_) {
        intermediateState_->setStarted = true;
    }
    value_buffer<fmi3_int64_t> fmiValues(values.size());
    auto target = fmiValues.begin();
    for (const auto& value : values) {
        *target = value.value;
        ++target;
    }
    const auto status = fmi3_import_set_int64(
        handle_,
        variables.data(),
        variables.size(),
        fmiValues.data(),
        fmiValues.size());
    check_status(callbackContext_.get(), "SetInt64", status);
    if (state_ == state::step && !intermediateState_) {
        stepSetStarted_ = true;
    }
}


std::vector<variable_value> slave_instance::get_variable_values(
    gsl::span<const typed_value_reference> variables) const
{
    // Whole-value access owns every returned element. This is especially
    // important for FMI String, whose getter pointers remain FMU-owned.
    std::vector<variable_value> result;
    result.reserve(variables.size());
    for (const auto variable : variables) {
        auto shape = resolved_shape(variable.type, variable.reference);
        const auto valueCount = flat_size(shape);
        visit_whole_value_type(variable.type, instanceName_, [&](auto operations) {
            using values_type = typename decltype(operations)::values_type;
            const auto reference = gsl::make_span(&variable.reference, 1);
            values_type values;
            if constexpr (values_type::type == variable_type::boolean) {
                boost::container::vector<bool> buffer(valueCount);
                (this->*operations.get)(reference, gsl::make_span(buffer));
                values.values.assign(buffer.begin(), buffer.end());
            } else {
                values.values.resize(valueCount);
                (this->*operations.get)(reference, gsl::make_span(values.values));
            }
            result.emplace_back(std::move(values), std::move(shape));
        });
    }
    return result;
}


// NOLINTNEXTLINE(readability-function-cognitive-complexity): Validate the entire batch before performing any writes.
void slave_instance::set_variable_values(
    gsl::span<const typed_value_reference> variables,
    gsl::span<const variable_value> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetVariable");
    std::optional<decltype(structuralParameterValues_)> projectedParameters;
    const auto activeParameters = [&]() -> const auto& {
        return projectedParameters ? *projectedParameters : structuralParameterValues_;
    };
    std::vector<std::optional<variable_value>> conversions;
    std::size_t index = 0;
    auto original = values.begin();
    for (const auto variable : variables) {
        if (auto normalized = normalize_enumeration_value(variable.type, *original)) {
            if (conversions.empty()) {
                conversions.resize(values.size());
            }
            conversions.at(index) = std::move(normalized);
        }
        const auto& value = *converted_or_original(conversions, index, &*original);
        COSIM_INPUT_CHECK(value.type() == variable.type);
        if (variable.type == variable_type::clock) {
            throw error(make_error_code(errc::unsupported_feature),
                instanceName_ + ": Clock values require the FMI 3 clock API");
        }
        const auto references = gsl::make_span(&variable.reference, 1);
        check_intermediate_access(references, true);
        check_variable_access(references, variable.type, true, activeParameters());
        const auto* description = fmu_->find_variable(variable.type, variable.reference);
        assert(description != nullptr);
        COSIM_INPUT_CHECK(
            value.dimensions() == resolve_shape(*description, activeParameters(), instanceName_));
        if (variable.type == variable_type::enumeration) {
            const auto& items = std::get<enumeration_values>(value.values()).values;
            validate_enumeration_values(
                references, gsl::make_span(items), activeParameters(), "SetInt64");
        }
        if (variable.type == variable_type::uint64 &&
            description->causality == variable_causality::structural_parameter &&
            description->dimensions.empty()) {
            if (!projectedParameters) {
                projectedParameters.emplace(structuralParameterValues_);
            }
            projectedParameters->insert_or_assign(
                variable.reference,
                std::get<uint64_values>(value.values()).values.at(0));
        }
        ++index;
        ++original;
    }
    index = 0;
    original = values.begin();
    for (const auto variable : variables) {
        const auto& value = *converted_or_original(conversions, index, &*original);
        visit_whole_value_type(variable.type, instanceName_, [&](auto operations) {
            using values_type = typename decltype(operations)::values_type;
            const auto reference = gsl::make_span(&variable.reference, 1);
            const auto& elements = std::get<values_type>(value.values()).values;
            if constexpr (values_type::type == variable_type::boolean) {
                const boost::container::vector<bool> buffer(elements.begin(), elements.end());
                (this->*operations.set)(reference, gsl::make_span(buffer));
            } else {
                (this->*operations.set)(reference, gsl::make_span(elements));
            }
        });
        ++index;
        ++original;
    }
}


std::size_t slave_instance::check_variable_access(
    gsl::span<const value_reference> variables,
    variable_type type,
    bool setting) const
{
    return check_variable_access(variables, type, setting, structuralParameterValues_);
}


// NOLINTNEXTLINE(readability-function-cognitive-complexity): Keep lifecycle and shape checks in their batch-validation order.
std::size_t slave_instance::check_variable_access(
    gsl::span<const value_reference> variables,
    variable_type type,
    bool setting,
    const std::unordered_map<value_reference, std::uint64_t>& structuralParameterValues) const
{
    // Step Mode and Intermediate Update Mode have different ordering and
    // permission rules; validate them before resolving any typed value span.
    if (!setting && !callbackContext_->error_terminated() && state_ == state::configuration) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": FMI 3 Get is not allowed in Configuration or Reconfiguration Mode");
    }
    if (!intermediateState_ && state_ == state::step && !setting && stepSetStarted_ &&
        !callbackContext_->error_terminated()) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": Step Mode Get must precede all Set calls at a communication point");
    }

    const variable_description* firstArray = nullptr;
    for (const auto reference : variables) {
        const auto* description = fmu_->find_variable(type, reference);
        if (description == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": value reference " + std::to_string(reference) + " is not a " + to_text(type) + " variable");
        }
        if (!description->dimensions.empty() && firstArray == nullptr) {
            firstArray = description;
        }

        if (intermediateState_) {
            continue;
        }

        if (!description->clocks.empty() && !callbackContext_->error_terminated()) {
            check_clocked_variable_access(
                *description, setting ? "SetVariable" : "GetVariable");
        }
        if (!setting) {
            continue;
        }

        bool allowed = false;
        switch (state_) {
            case state::instantiated:
                allowed = description->variability != variable_variability::constant && (description->initial == variable_initial::exact || description->initial == variable_initial::approximate);
                break;
            case state::configuration:
                allowed =
                    description->causality == variable_causality::structural_parameter &&
                    (description->variability == variable_variability::tunable ||
                        (stateBeforeConfiguration_ == state::instantiated &&
                            description->variability == variable_variability::fixed));
                break;
            case state::initialization:
                allowed =
                    description->variability != variable_variability::constant && description->initial == variable_initial::exact;
                break;
            case state::step:
            case state::event:
                allowed = description->causality == variable_causality::input || (description->causality == variable_causality::parameter && description->variability == variable_variability::tunable);
                break;
            case state::terminated:
                allowed = false;
                break;
        }
        if (!allowed) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": value reference " + std::to_string(reference) + " cannot be set in the current lifecycle state");
        }
    }
    if (firstArray == nullptr) {
        return variables.size();
    }
    if (variables.size() == 1) {
        return flat_size(adapter_detail::resolve_shape(*firstArray, structuralParameterValues, instanceName_));
    }
    // Validate access to the full batch before resolving any dynamic dimensions.
    std::size_t result = 0;
    for (const auto reference : variables) {
        const auto* variable = fmu_->find_variable(type, reference);
        const auto count = flat_size(resolve_shape(*variable, structuralParameterValues, instanceName_));
        if (count > (std::numeric_limits<std::size_t>::max)() - result) { // NOLINT(readability-redundant-parentheses): Protect against the Windows max macro.
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": combined FMI 3 value count overflows the host size type");
        }
        result += count;
    }
    return result;
}


} // namespace cosim::fmi::v3
