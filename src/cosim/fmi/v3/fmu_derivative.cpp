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

#include <boost/range/algorithm/copy.hpp>
#include <gsl/span>

#include <FMI3/fmi3PlatformTypes.h>
#include <FMI3/fmi3_function_types.h>
#include <FMI3/fmi3_import_capi.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>


namespace cosim::fmi::v3
{

using adapter_detail::check_available;
using adapter_detail::check_callback_reentrancy;
using adapter_detail::check_lifecycle;
using adapter_detail::check_status;
using adapter_detail::resolve_shape;

std::size_t slave_instance::resolved_derivative_value_count(
    gsl::span<const value_reference> variables) const
{
    std::size_t result = 0;
    for (const auto reference : variables) {
        const auto* variable = fmu_->find_variable(reference);
        if ((variable == nullptr) ||
            (variable->type != variable_type::real && variable->type != variable_type::float32)) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": derivative value reference " +
                    std::to_string(reference) +
                    " is not a floating-point variable");
        }
        const auto count = flat_size(
            resolve_shape(*variable, structuralParameterValues_, instanceName_));
        if (count > (std::numeric_limits<std::size_t>::max)() - result) { // NOLINT(readability-redundant-parentheses): Protect against the Windows max macro.
            throw error(
                make_error_code(errc::model_error),
                instanceName_ +
                    ": combined FMI 3 derivative value count overflows the "
                    "host size type");
        }
        result += count;
    }
    return result;
}

bool slave_instance::provides_directional_derivatives() const noexcept
{
    return providesDirectionalDerivatives_;
}


bool slave_instance::provides_adjoint_derivatives() const noexcept
{
    return providesAdjointDerivatives_;
}


bool slave_instance::provides_per_element_dependencies() const noexcept
{
    return providesPerElementDependencies_;
}


std::vector<element_dependency> slave_instance::get_variable_dependencies(
    value_reference variable) const
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "GetVariableDependencies");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(
        callbackContext_.get(), instanceName_, "GetVariableDependencies");
    if (!providesPerElementDependencies_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ +
                ": FMU does not provide runtime per-element dependencies");
    }

    const auto typed = [this](value_reference reference)
        -> std::optional<typed_value_reference> {
        const auto* found = fmu_->find_variable(reference);
        if (!found) {
            return std::nullopt;
        }
        return typed_value_reference{found->type, found->reference};
    };
    if (!typed(variable)) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": GetVariableDependencies value reference " +
                std::to_string(variable) + " does not identify a variable");
    }

    std::size_t count = 0;
    check_status(
        callbackContext_.get(),
        "GetNumberOfVariableDependencies",
        fmi3_import_get_number_of_variable_dependencies(
            handle_, variable, &count));
    std::vector<element_dependency> result;
    if (count == 0) {
        return result;
    }

    std::vector<std::size_t> dependentElements(count);
    std::vector<fmi3_value_reference_t> independents(count);
    std::vector<std::size_t> independentElements(count);
    std::vector<fmi3_dependency_kind_t> kinds(count);
    check_status(
        callbackContext_.get(),
        "GetVariableDependencies",
        fmi3_import_get_variable_dependencies(
            handle_,
            variable,
            dependentElements.data(),
            independents.data(),
            independentElements.data(),
            kinds.data(),
            count));

    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // Validate the FMU's raw value before reading it as an enum.
        std::underlying_type_t<fmi3_dependency_kind_t> rawKind{};
        std::memcpy(&rawKind, &kinds.at(i), sizeof(rawKind));
        dependency_kind kind = dependency_kind::dependent;
        switch (rawKind) {
            case 0: // fmi3Independent: no dependency
                continue;
            case fmi3_constant:
                kind = dependency_kind::constant;
                break;
            case fmi3_fixed:
                kind = dependency_kind::fixed;
                break;
            case fmi3_tunable:
                kind = dependency_kind::tunable;
                break;
            case fmi3_discrete:
                kind = dependency_kind::discrete;
                break;
            case fmi3_dependent:
                kind = dependency_kind::dependent;
                break;
            default:
                throw error(
                    make_error_code(errc::model_error),
                    instanceName_ +
                        ": GetVariableDependencies returned an invalid "
                        "dependency kind");
        }
        const auto independent = typed(independents.at(i));
        if (!independent) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ +
                    ": GetVariableDependencies returned unknown value "
                    "reference " +
                    std::to_string(independents.at(i)));
        }
        result.push_back(
            {
                dependentElements.at(i),
                *independent,
                independentElements.at(i),
                kind,
            });
    }
    return result;
}


// NOLINTNEXTLINE(readability-function-cognitive-complexity): Keep known and unknown eligibility rules together.
void slave_instance::check_derivative_variable_access(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): FMI distinguishes known and unknown reference spans.
    gsl::span<const value_reference> unknowns,
    gsl::span<const value_reference> knowns,
    const char* operation) const
{
    const bool initializing = state_ == state::initialization;
    const auto floatingPointVariable = [this](value_reference reference) {
        const auto* variable = fmu_->find_variable(reference);
        return variable && (variable->type == variable_type::real || variable->type == variable_type::float32)
            ? variable
            : nullptr;
    };

    for (const auto reference : unknowns) {
        const auto* variable = floatingPointVariable(reference);
        const bool eligible = (variable != nullptr) &&
            (initializing
                    ? fmu_->modelStructureInitialUnknowns_.count(reference) != 0
                    : (fmu_->modelStructureOutputs_.count(reference) != 0 &&
                          (variable->variability == variable_variability::continuous ||
                              variable->variability ==
                                  variable_variability::discrete)) ||
                        fmu_->modelStructureContinuousStateDerivatives_.count(
                            reference) != 0);
        if (!eligible) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": " + operation + " unknown value reference " +
                    std::to_string(reference) +
                    (initializing
                            ? " is not a floating-point InitialUnknown"
                            : " is not a floating-point Output or "
                              "ContinuousStateDerivative"));
        }
    }

    for (const auto reference : knowns) {
        const auto* variable = floatingPointVariable(reference);
        if (variable == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": " + operation + " known value reference " +
                    std::to_string(reference) +
                    " is not a floating-point variable");
        }

        const bool continuousState = fmu_->continuousStates_.count(reference) != 0;
        const bool discreteState =
            fmu_->modelStructureDiscreteStates_.count(reference) != 0;
        const bool eligible = initializing
            ? variable->causality == variable_causality::input ||
                variable->causality == variable_causality::independent ||
                variable->initial == variable_initial::exact
            : variable->causality == variable_causality::input ||
                variable->causality == variable_causality::parameter ||
                variable->causality == variable_causality::independent ||
                continuousState || discreteState;
        if (!eligible) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": " + operation + " known value reference " +
                    std::to_string(reference) +
                    (initializing
                            ? " is not an input, exact initial value, or "
                              "independent variable in Initialization Mode"
                            : " is not an input, continuous or clocked state, "
                              "parameter, or independent variable at a "
                              "communication point"));
        }
    }
}


std::uint32_t slave_instance::max_output_derivative_order() const noexcept
{
    return maxOutputDerivativeOrder_;
}


void slave_instance::get_output_derivatives(
    gsl::span<const value_reference> variables,
    gsl::span<const std::int32_t> orders,
    gsl::span<double> values) const
{
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(
        callbackContext_.get(), instanceName_, "GetOutputDerivatives",
        adapter_detail::operation_access::diagnostic);
    check_lifecycle(
        state_ == state::step || state_ == state::terminated,
        instanceName_,
        "GetOutputDerivatives");
    if (maxOutputDerivativeOrder_ == 0) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not provide output derivatives");
    }
    COSIM_INPUT_CHECK(!variables.empty());
    COSIM_INPUT_CHECK(variables.size() == orders.size());
    for (const auto order : orders) {
        // NOLINTNEXTLINE(readability-simplify-boolean-expr): Preserve input-check diagnostics.
        COSIM_INPUT_CHECK(
            order > 0 &&
            static_cast<std::uint32_t>(order) <= maxOutputDerivativeOrder_);
    }
    check_intermediate_access(variables, false);
    for (const auto reference : variables) {
        const auto* variable = fmu_->find_variable(reference);
        if ((variable == nullptr) ||
            (variable->type != variable_type::real && variable->type != variable_type::float32) ||
            variable->causality != variable_causality::output ||
            variable->variability != variable_variability::continuous) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": output derivative value reference " +
                    std::to_string(reference) +
                    " is not a continuous floating-point output");
        }
    }
    const auto valueCount = resolved_derivative_value_count(variables);
    COSIM_INPUT_CHECK(valueCount == values.size());
    const auto status = fmi3_import_get_output_derivatives(
        handle_,
        variables.data(),
        variables.size(),
        orders.data(),
        values.data(),
        values.size());
    check_status(callbackContext_.get(), "GetOutputDerivatives", status);
}


void slave_instance::get_directional_derivative(
    gsl::span<const value_reference> unknowns,
    gsl::span<const value_reference> knowns,
    gsl::span<const double> seed,
    gsl::span<double> sensitivity) const
{
    get_partial_derivative(false, unknowns, knowns, seed, sensitivity);
}


void slave_instance::get_adjoint_derivative(
    gsl::span<const value_reference> unknowns,
    gsl::span<const value_reference> knowns,
    gsl::span<const double> seed,
    gsl::span<double> sensitivity) const
{
    get_partial_derivative(true, unknowns, knowns, seed, sensitivity);
}

void slave_instance::get_partial_derivative(
    bool adjoint,
    gsl::span<const value_reference> unknowns,
    gsl::span<const value_reference> knowns,
    gsl::span<const double> seed,
    gsl::span<double> sensitivity) const
{
    const auto* const operation = adjoint ? "GetAdjointDerivative" : "GetDirectionalDerivative";
    check_callback_reentrancy(callbackContext_.get(), instanceName_, operation);
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(
        callbackContext_.get(), instanceName_, operation, adapter_detail::operation_access::diagnostic);
    check_lifecycle(
        state_ == state::initialization || state_ == state::step ||
            state_ == state::event || state_ == state::terminated,
        instanceName_,
        operation);
    if (!(adjoint ? providesAdjointDerivatives_ : providesDirectionalDerivatives_)) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not provide " +
                (adjoint ? "adjoint derivatives" : "directional derivatives"));
    }
    COSIM_INPUT_CHECK(!unknowns.empty());
    COSIM_INPUT_CHECK(!knowns.empty());
    check_derivative_variable_access(unknowns, knowns, operation);
    const auto seedCount = resolved_derivative_value_count(adjoint ? unknowns : knowns);
    const auto sensitivityCount = resolved_derivative_value_count(adjoint ? knowns : unknowns);
    COSIM_INPUT_CHECK(seedCount == seed.size());
    COSIM_INPUT_CHECK(sensitivityCount == sensitivity.size());
    const fmi3_float64_t emptySeed = 0.0;
    fmi3_float64_t emptySensitivity = 0.0;
    std::vector<fmi3_float64_t> stagedSensitivity(sensitivity.size());
    const auto getDerivative = adjoint
        ? fmi3_import_get_adjoint_derivative
        : fmi3_import_get_directional_derivative;
    const auto status = getDerivative(
        handle_,
        unknowns.data(),
        unknowns.size(),
        knowns.data(),
        knowns.size(),
        seed.empty() ? &emptySeed : seed.data(),
        seed.size(),
        stagedSensitivity.empty()
            ? &emptySensitivity
            : stagedSensitivity.data(),
        sensitivity.size());
    check_status(callbackContext_.get(), operation, status);
    if (!sensitivity.empty()) {
        boost::range::copy(
            stagedSensitivity,
            sensitivity.begin());
    }
}


} // namespace cosim::fmi::v3
