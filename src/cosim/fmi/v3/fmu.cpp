/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/fmu.hpp"

#include "fmu_internal.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/file_cache.hpp"
#include "cosim/fmi/fmilib.h" // IWYU pragma: keep
#include "cosim/fmi/glue.hpp"
#include "cosim/fmi/importer.hpp"
#include "cosim/fmi/v3/fmu.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/time.hpp"

#ifdef _WIN32
#    include "cosim/fmi/windows.hpp"
#endif

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <gsl/span>
#include <gsl/util>

#include <FMI/fmi_import_context.h>
#include <FMI3/fmi3PlatformTypes.h>
#include <FMI3/fmi3_enums.h>
#include <FMI3/fmi3_function_types.h>
#include <FMI3/fmi3_import.h>
#include <FMI3/fmi3_import_capi.h>
#include <FMI3/fmi3_import_variable.h>
#include <FMI3/fmi3_import_variable_list.h>
#include <FMI3/fmi3_types.h>
#include <JM/jm_types.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


namespace cosim::fmi::v3
{

struct callback_context;
using adapter_detail::check_available;
using adapter_detail::check_callback_reentrancy;
using adapter_detail::check_lifecycle;
using adapter_detail::check_status;
using adapter_detail::find_type_definition;
using adapter_detail::representable_time;

// Per-thread callback state is shared by the adapter's callback entry points.
static thread_local callback_context* active_callback_context = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static thread_local callback_context* active_step_context = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

bool callback_context::is_active_on_this_thread() const noexcept
{
    return active_callback_context == this;
}


class callback_scope
{
public:
    explicit callback_scope(callback_context* context) noexcept
    {
        active_callback_context = context;
    }

    callback_scope(const callback_scope&) = delete;
    callback_scope& operator=(const callback_scope&) = delete;
    callback_scope(callback_scope&&) = delete;
    callback_scope& operator=(callback_scope&&) = delete;

    ~callback_scope() noexcept
    {
        active_callback_context = previous_;
    }

private:
    callback_context* previous_ = active_callback_context;
};


namespace
{

const char* nonnull(const char* value)
{
    return value != nullptr ? value : "";
}


std::string take_log_message(const callback_context* context)
{
    if (context == nullptr) {
        return {};
    }
    std::scoped_lock const lock(context->logMutex);
    return std::exchange(context->lastLogMessage, {});
}


bool is_success(fmi3_status_t status)
{
    return status == fmi3_status_ok || status == fmi3_status_warning;
}


void release_instance(fmi3_import_t* handle, bool callFmu)
{
    if (callFmu) {
        fmi3_import_free_instance(handle);
    }
    fmi3_import_destroy_dllfmu(handle);
    if (!callFmu) {
        // FMILibrary 3.0.4 frees its instance name here; after destroying the
        // C API, free_instance cannot call into the failed or absent FMU.
        fmi3_import_free_instance(handle);
    }
    fmi3_import_free(handle);
}

} // namespace

bool adapter_detail::representable_time(double value)
{
    return std::isfinite(value) &&
        value >= to_double_time_point(time_point((duration::min)())) && // NOLINT(readability-redundant-parentheses): Protect against the Windows min macro.
        value < to_double_time_point(time_point((duration::max)())); // NOLINT(readability-redundant-parentheses): Protect against the Windows max macro.
}

namespace
{


std::string status_text(fmi3_status_t status)
{
    return nonnull(fmi3_status_to_string(status));
}


std::string status_message(
    const std::string& operation,
    fmi3_status_t status,
    const std::string& loggedMessage)
{
    auto message = operation + " returned FMI status " + status_text(status);
    if (!loggedMessage.empty()) {
        message += ": " + loggedMessage;
    }
    return message;
}


clock_interval_variability to_clock_interval_variability(
    cosim::clock_interval_variability variability)
{
    switch (variability) {
        case cosim::clock_interval_variability::unknown:
            return clock_interval_variability::unknown;
        case cosim::clock_interval_variability::constant:
            return clock_interval_variability::constant;
        case cosim::clock_interval_variability::fixed:
            return clock_interval_variability::fixed;
        case cosim::clock_interval_variability::tunable:
            return clock_interval_variability::tunable;
        case cosim::clock_interval_variability::changing:
            return clock_interval_variability::changing;
        case cosim::clock_interval_variability::countdown:
            return clock_interval_variability::countdown;
        case cosim::clock_interval_variability::triggered:
            return clock_interval_variability::triggered;
    }
    return clock_interval_variability::unknown;
}


clock_description to_clock_description(const variable_description& variable)
{
    const auto& clock = *variable.clock; // NOLINT(bugprone-unchecked-optional-access): The caller checks clock metadata before conversion.
    return {
        variable.name,
        variable.reference,
        variable.causality,
        to_clock_interval_variability(clock.interval_variability),
        clock.can_be_deactivated,
        clock.priority,
        clock.interval_decimal,
        clock.supports_fraction,
        clock.resolution,
        clock.shift_decimal,
        clock.interval_counter,
        clock.shift_counter,
    };
}


} // namespace

void adapter_detail::check_status(
    const callback_context* context,
    const std::string& operation,
    fmi3_status_t status)
{
    if (context != nullptr) {
        context->record_status(status);
    }
    const auto loggedMessage = take_log_message(context);
    if (status == fmi3_status_warning) {
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "[FMI instance=" << ((context != nullptr) ? context->instanceName : "<unknown>")
            << "] " << operation << " returned FMI warning";
    }
    if (!is_success(status)) {
        throw error(
            make_error_code(errc::model_error),
            status_message(operation, status, loggedMessage));
    }
}

void adapter_detail::check_available(
    const callback_context* context,
    const std::string& instanceName,
    const char* operation,
    operation_access access)
{
    if (context == nullptr) {
        return;
    }
    if (context->fmu_returned_fatal()) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": cannot call " + operation + " after the FMU returned fmi3Fatal");
    }
    if (context->instance_poisoned()) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": cannot call " + operation + " after an unrecoverable instance failure");
    }
    if (!context->recovery_required()) {
        return;
    }

    const bool allowed = context->error_terminated()
        ? access == operation_access::recovery || access == operation_access::diagnostic
        : access == operation_access::terminate ||
            (!context->termination_only() && access == operation_access::recovery);
    if (!allowed) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": cannot call " + operation + " until the FMU is reset or a saved FMU state is restored");
    }
}

void adapter_detail::check_lifecycle(
    bool condition,
    const std::string& instanceName,
    const char* operation)
{
    if (!condition) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": invalid lifecycle operation: " + operation);
    }
}

void adapter_detail::check_callback_reentrancy(
    const callback_context* context,
    const std::string& instanceName,
    const char* operation)
{
    // Calling back into the same instance from an FMU callback would recurse
    // through FMI's callback context and can invalidate the active operation.
    if (context->is_active_on_this_thread()) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName + ": " + operation + " cannot be called from an FMI callback");
    }
}

namespace
{


void log_message(
    void* environment,
    fmi3_status_t status,
    const char* category,
    const char* message) noexcept
try {
    auto* context = static_cast<callback_context*>(environment);
    const std::string_view instanceName = context != nullptr
        ? std::string_view(context->instanceName)
        : "<unknown>";
    const auto* const text = nonnull(message);
    if (context != nullptr && (status == fmi3_status_warning || status == fmi3_status_error || status == fmi3_status_fatal)) {
        std::scoped_lock const lock(context->logMutex);
        context->lastLogMessage = text;
    }

    log::severity_level logLevel = log::error;
    switch (status) {
        case fmi3_status_ok:
            logLevel = log::trace;
            break;
        case fmi3_status_warning:
            logLevel = log::warning;
            break;
        case fmi3_status_discard:
            logLevel = log::debug;
            break;
        case fmi3_status_error:
        case fmi3_status_fatal:
            logLevel = log::error;
            break;
    }

#ifndef LIBCOSIM_NO_FMI_LOGGING
    BOOST_LOG_SEV(log::logger(), logLevel)
        << "[FMI status=" << status_text(status)
        << ", instance=" << instanceName
        << ", category=" << nonnull(category) << "] "
        << text;
#endif

} catch (...) { // NOLINT(bugprone-empty-catch): Exceptions cannot cross the FMI callback boundary.
    // Logging failures must not unwind through FMU code.
}


template<typename T>
void prune(std::vector<std::weak_ptr<T>>& instances)
{
    const auto newEnd = std::remove_if(
        instances.begin(),
        instances.end(),
        [](const std::weak_ptr<T>& instance) { return instance.expired(); });
    instances.erase(newEnd, instances.end());
}


} // namespace

const variable_description* fmu::find_variable(value_reference reference) const
{
    const auto variable = variablesByReference_.find(reference);
    return variable == variablesByReference_.end() ? nullptr : variable->second;
}

const variable_description* fmu::find_variable(variable_type type, value_reference reference) const
{
    const auto* variable = find_variable(reference);
    return (variable != nullptr) && variable->type == type ? variable : nullptr;
}

const variable_type_definition* adapter_detail::find_type_definition(
    const model_description& model,
    const std::string& name)
{
    const auto definition = boost::range::find_if(
        model.type_definitions,
        [&name](const auto& candidate) { return candidate.name == name; });
    return definition == model.type_definitions.end() ? nullptr : &*definition;
}

namespace
{


void validate_enumeration_metadata(
    const model_description& model,
    const variable_description& variable)
{
    if (!variable.declared_type) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 3.0 Enumeration variable '" + variable.name +
                "' has no declared type");
    }
    const auto* definition =
        find_type_definition(model, *variable.declared_type);
    if (definition == nullptr || definition->type != variable_type::enumeration ||
        definition->enumeration_items.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 3.0 Enumeration variable '" + variable.name +
                "' references an invalid declared type");
    }

    const auto* minimum =
        variable.min ? std::get_if<enumeration_value>(&*variable.min) : nullptr;
    const auto* maximum =
        variable.max ? std::get_if<enumeration_value>(&*variable.max) : nullptr;
    if ((variable.min && minimum == nullptr) ||
        (variable.max && maximum == nullptr) ||
        (minimum != nullptr && maximum != nullptr &&
            minimum->value > maximum->value)) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 3.0 Enumeration variable '" + variable.name +
                "' has invalid bounds");
    }

    const auto validate_value = [&](enumeration_value value) {
        if ((minimum != nullptr && value.value < minimum->value) ||
            (maximum != nullptr && value.value > maximum->value)) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 3.0 Enumeration variable '" + variable.name +
                    "' has a start value outside its bounds");
        }
        const auto item = boost::range::find_if(
            definition->enumeration_items,
            [value](const auto& candidate) {
                return candidate.value == value.value;
            });
        if (item == definition->enumeration_items.end()) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 3.0 Enumeration variable '" + variable.name +
                    "' has an undeclared start value");
        }
    };

    if (variable.start) {
        const auto* value =
            std::get_if<enumeration_value>(&*variable.start);
        if (value == nullptr) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 3.0 Enumeration variable '" + variable.name +
                    "' has an invalid scalar start value");
        }
        validate_value(*value);
    }
    if (variable.array_start) {
        const auto* values =
            std::get_if<enumeration_values>(&variable.array_start->values());
        if (values == nullptr) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 3.0 Enumeration array '" + variable.name +
                    "' has an invalid start value");
        }
        for (const auto value : values->values) {
            validate_value(value);
        }
    }
}


std::unordered_map<value_reference, std::uint64_t>
initial_structural_parameter_values(const model_description& model)
{
    std::unordered_map<value_reference, std::uint64_t> result;
    for (const auto& variable : model.variables) {
        if (variable.type == variable_type::uint64 && variable.dimensions.empty() &&
            variable.start && (variable.causality == variable_causality::structural_parameter || variable.variability == variable_variability::constant)) {
            result.emplace(
                variable.reference,
                std::get<std::uint64_t>(*variable.start));
        }
    }
    return result;
}


} // namespace

std::vector<std::uint64_t> adapter_detail::resolve_shape(
    const variable_description& variable,
    const std::unordered_map<value_reference, std::uint64_t>&
        structuralParameterValues,
    const std::string& instanceName)
{
    // Resolve every dimension from the latest structural-parameter snapshot
    // so value counts match both the FMI call and the owning C++ container.
    std::vector<std::uint64_t> result;
    result.reserve(variable.dimensions.size());
    for (const auto& dimension : variable.dimensions) {
        if (const auto* const fixed = std::get_if<fixed_dimension>(&dimension)) {
            result.push_back(fixed->size);
            continue;
        }
        const auto reference =
            std::get<variable_dimension>(dimension).structural_parameter;
        const auto value = structuralParameterValues.find(reference);
        if (value == structuralParameterValues.end()) {
            throw error(
                make_error_code(errc::model_error),
                instanceName + ": no current value is available for structural "
                               "parameter " +
                    std::to_string(reference));
        }
        result.push_back(value->second);
    }
    try {
        flat_size(result);
    } catch (const std::overflow_error&) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": resolved shape of variable '" + variable.name + "' overflows the host size type");
    }
    return result;
}

namespace
{


void cache_model_structure_membership(
    fmi3_import_variable_list_t* variables,
    std::unordered_set<value_reference>& references,
    const char* listName)
{
    if (variables == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            std::string("Could not read FMI 3 ModelStructure ") + listName +
                " list");
    }
    const auto freeVariables = gsl::finally([variables] {
        fmi3_import_free_variable_list(variables);
    });
    for (std::size_t i = 0;
        i < fmi3_import_get_variable_list_size(variables);
        ++i) {
        references.insert(
            fmi3_import_get_variable_vr(
                fmi3_import_get_variable(variables, i)));
    }
}


} // namespace


// =============================================================================
// FMI 3 callbacks
// =============================================================================

// FMU-owned threads enter here without a C++ instance argument. The context
// takes a reference before dispatch and captures exceptions for the next
// caller-visible FMI operation.
void intermediate_update(
    void* environment,
    double intermediateUpdateTime,
    bool intermediateVariableSetRequested,
    bool intermediateVariableGetAllowed,
    bool intermediateStepFinished,
    bool canReturnEarly,
    bool* earlyReturnRequested,
    double* earlyReturnTime) noexcept
{
    auto* context = static_cast<callback_context*>(environment);
    if (context == nullptr) {
        return;
    }
    try {
        if (active_step_context != context) {
            throw error(make_error_code(errc::model_error),
                context->instanceName + ": intermediate update must run on the DoStep thread");
        }
        auto* instance = context->begin_intermediate_callback();
        if (instance == nullptr) {
            return;
        }
        const auto callbackFinished = gsl::finally([context] {
            context->end_callback();
        });
        const callback_scope callbackScope(context);
        instance->notify_intermediate_update(
            intermediateUpdateTime,
            intermediateVariableSetRequested,
            intermediateVariableGetAllowed,
            intermediateStepFinished,
            canReturnEarly,
            earlyReturnRequested,
            earlyReturnTime);
    } catch (...) {
        context->capture_callback_exception(std::current_exception());
        if (canReturnEarly && active_step_context == context &&
            (earlyReturnRequested != nullptr) && (earlyReturnTime != nullptr) && std::isfinite(intermediateUpdateTime)) {
            *earlyReturnRequested = (fmi3_true != 0);
            *earlyReturnTime = intermediateUpdateTime;
        }
    }
}


// =============================================================================
// fmu
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Keep import validation and cleanup in one exception boundary.
fmu::fmu(
    std::shared_ptr<fmi::importer> importer,
    std::unique_ptr<file_cache::directory_ro> fmuDir)
    : importer_{std::move(importer)}
    , dir_{std::move(fmuDir)}
    , handle_{
          fmi3_import_parse_xml(
              importer_->fmilib_handle(),
              dir_->path().string().c_str(),
              nullptr),
      }
{
    // Parse the FMI 3 model description once and retain only the bounded
    // Co-Simulation interface supported by this adapter.
    try {
        if (handle_ == nullptr) {
            throw error(
                make_error_code(errc::bad_file),
                importer_->last_error_message());
        }

        const auto fmuKind = fmi3_import_get_fmu_kind(handle_);
        if ((static_cast<unsigned>(fmuKind) & static_cast<unsigned>(fmi3_fmu_kind_cs)) == 0U) {
            fmi3_import_free(handle_);
            handle_ = nullptr;
            throw error(
                make_error_code(errc::unsupported_feature),
                "FMI 3 FMU does not implement Co-Simulation; Model Exchange "
                "and Scheduled Execution are not supported");
        }

        modelDescription_.name = nonnull(fmi3_import_get_model_name(handle_));
        modelDescription_.instantiation_token =
            nonnull(fmi3_import_get_instantiation_token(handle_));
        modelDescription_.description =
            nonnull(fmi3_import_get_description(handle_));
        modelDescription_.author = nonnull(fmi3_import_get_author(handle_));
        modelDescription_.version =
            nonnull(fmi3_import_get_model_version(handle_));

        modelDescription_.capabilities.can_handle_variable_step =
            !(fmi3_import_get_capability(
                  handle_, fmi3_cs_canHandleVariableCommunicationStepSize) == 0U);
        modelDescription_.capabilities.can_save_state =
            !(fmi3_import_get_capability(
                  handle_, fmi3_cs_canGetAndSetFMUState) == 0U);
        modelDescription_.capabilities.can_export_state =
            !(fmi3_import_get_capability(
                  handle_, fmi3_cs_canSerializeFMUState) == 0U);
        mightReturnEarlyFromDoStep_ =
            fmi3_import_get_capability(handle_, fmi3_cs_mightReturnEarlyFromDoStep) != 0;
        canReturnEarlyAfterIntermediateUpdate_ =
            fmi3_import_get_capability(handle_, fmi3_cs_canReturnEarlyAfterIntermediateUpdate) != 0;
        modelDescription_.capabilities.might_return_early =
            mightReturnEarlyFromDoStep_ || canReturnEarlyAfterIntermediateUpdate_;
        if (fmi3_import_get_capability(handle_, fmi3_cs_needsExecutionTool) != 0) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "FMI 3 FMU '" << modelDescription_.name
                << "' requires an external execution tool; libcosim does not launch it";
        }
        modelDescription_.type_definitions = to_type_definitions(handle_);
        for (const auto& definition : modelDescription_.type_definitions) {
            if (definition.type != variable_type::enumeration) {
                continue;
            }
            auto& items = enumItemsByType_[definition.name];
            items.reserve(definition.enumeration_items.size());
            for (const auto& item : definition.enumeration_items) {
                items.insert(item.value);
            }
        }
        modelDescription_.unit_definitions = to_unit_definitions(handle_);
        fmi3_array_start_counts arrayStartCounts;
        read_xml_metadata(modelDescription_.unit_definitions,
            dir_->path() / "modelDescription.xml", nullptr, &arrayStartCounts);

        auto* const variableList = fmi3_import_get_variable_list(handle_, 0);
        if (variableList == nullptr) {
            const auto message = importer_->last_error_message();
            fmi3_import_free(handle_);
            handle_ = nullptr;
            throw error(
                make_error_code(errc::bad_file),
                message.empty() ? "Could not read FMI 3 variable list" : message);
        }
        const auto freeVariableList = gsl::finally([&] {
            fmi3_import_free_variable_list(variableList);
        });

        std::unordered_map<value_reference, std::uint64_t>
            structuralParameterStarts;
        for (std::size_t i = 0;
            i < fmi3_import_get_variable_list_size(variableList);
            ++i) {
            auto* const variable = fmi3_import_get_variable(variableList, i);
            if (fmi3_import_get_variable_base_type(variable) == fmi3_base_type_uint64 &&
                (fmi3_import_variable_is_array(variable) == 0) &&
                (fmi3_import_get_variable_has_start(variable) != 0) &&
                (fmi3_import_get_variable_causality(variable) == fmi3_causality_enu_structural_parameter ||
                    fmi3_import_get_variable_variability(variable) == fmi3_variability_enu_constant)) {
                structuralParameterStarts.emplace(
                    fmi3_import_get_variable_vr(variable),
                    fmi3_import_get_uint64_variable_start(
                        fmi3_import_get_variable_as_uint64(variable)));
            }
        }

        for (std::size_t i = 0; i < fmi3_import_get_variable_list_size(variableList); ++i) {
            auto* const variable = fmi3_import_get_variable(variableList, i);
            modelDescription_.variables.push_back(to_variable_description(
                handle_,
                variable,
                structuralParameterStarts,
                arrayStartCounts));
        }
        for (const auto& variable : modelDescription_.variables) {
            if (variable.type == variable_type::enumeration) {
                validate_enumeration_metadata(modelDescription_, variable);
            }
        }
        read_model_structure(modelDescription_, handle_, dir_->path() / "modelDescription.xml");
        variablesByReference_.reserve(modelDescription_.variables.size());
        for (const auto& variable : modelDescription_.variables) {
            variablesByReference_.emplace(variable.reference, &variable);
            if (variable.derivative) {
                continuousStates_.insert(*variable.derivative);
            }
            if (variable.clock) {
                clockDescriptions_.push_back(to_clock_description(variable));
            }
        }

        cache_model_structure_membership(
            fmi3_import_get_outputs_list(handle_),
            modelStructureOutputs_,
            "Outputs");
        cache_model_structure_membership(
            fmi3_import_get_continuous_state_derivatives_list(handle_),
            modelStructureContinuousStateDerivatives_,
            "ContinuousStateDerivatives");
        cache_model_structure_membership(
            fmi3_import_get_clocked_states_list(handle_),
            modelStructureDiscreteStates_,
            "ClockedStates");
        cache_model_structure_membership(
            fmi3_import_get_initial_unknowns_list(handle_),
            modelStructureInitialUnknowns_,
            "InitialUnknowns");
    } catch (...) {
        if (handle_ != nullptr) {
            fmi3_import_free(handle_);
            handle_ = nullptr;
        }
        throw;
    }
}


fmu::~fmu()
{
    if (handle_ != nullptr) {
        fmi3_import_free(handle_);
    }
}


fmi::fmi_version fmu::fmi_version() const
{
    return fmi::fmi_version::v3_0;
}


std::shared_ptr<const cosim::model_description> fmu::model_description() const
{
    return {
        shared_from_this(),
        &modelDescription_};
}


std::shared_ptr<fmi::importer> fmu::importer() const
{
    return importer_;
}


std::shared_ptr<v3::slave_instance> fmu::instantiate_v3_slave(
    std::string_view instanceName,
    co_simulation_callbacks callbacks)
{
    COSIM_INPUT_CHECK(!instanceName.empty());
    if (fatal_.load(std::memory_order_acquire)) {
        throw error(
            make_error_code(errc::model_error),
            "FMU '" + modelDescription_.name + "' cannot be instantiated after an instance returned fmi3Fatal");
    }
    const auto fmuKind = fmi3_import_get_fmu_kind(handle_);
    if ((static_cast<unsigned>(fmuKind) & static_cast<unsigned>(fmi3_fmu_kind_cs)) == 0U) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "FMI 3 FMU does not implement Co-Simulation");
    }

#ifdef _WIN32
    if (!additionalDllSearchPath_) {
        additionalDllSearchPath_ =
            std::make_unique<detail::additional_path>(fmu_binaries_dir(dir_->path()));
    }
#endif

    prune(instances_);
    if ((fmi3_import_get_capability(
             handle_, fmi3_cs_canBeInstantiatedOnlyOncePerProcess) != 0U) &&
        !instances_.empty()) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "FMU '" + modelDescription_.name + "' can only be instantiated once");
    }

    auto instance = std::shared_ptr<v3::slave_instance>(
        new v3::slave_instance(
            shared_from_this(),
            instanceName,
            std::move(callbacks)));
    instances_.push_back(instance);
    return instance;
}


const std::vector<clock_description>& fmu::clock_descriptions() const
{
    return clockDescriptions_;
}


cosim::filesystem::path fmu::directory() const
{
    return dir_->path();
}


bool fmu::might_return_early_from_do_step() const noexcept
{
    return mightReturnEarlyFromDoStep_;
}

bool fmu::can_return_early_after_intermediate_update() const noexcept
{
    return canReturnEarlyAfterIntermediateUpdate_;
}

fmi3_import_t* fmu::fmilib_handle() const
{
    return handle_;
}


// =============================================================================
// slave_instance
// =============================================================================

void slave_instance::validate_clock_support(bool eventModeSupported) const
{
    if (!fmu_->clock_descriptions().empty() && !eventModeUsed_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ +
                (eventModeSupported
                        ? ": a Co-Simulation FMU with Clock variables requires Event Mode, "
                          "which was disabled for this instance"
                        : ": a Co-Simulation FMU with Clock variables must support Event Mode"));
    }
    for (const auto& clock : fmu_->clock_descriptions()) {
        const bool supportedTriggeredClock =
            clock.interval_variability == clock_interval_variability::triggered;
        const bool supportedDeclaredInputClock =
            clock.causality == variable_causality::input &&
            (clock.interval_variability == clock_interval_variability::constant ||
                clock.interval_variability == clock_interval_variability::fixed ||
                clock.interval_variability == clock_interval_variability::tunable) &&
            clock.interval_decimal && std::isfinite(*clock.interval_decimal) &&
            *clock.interval_decimal > 0.0;
        const bool configurableInputClockWithoutDefault =
            clock.causality == variable_causality::input &&
            (clock.interval_variability == clock_interval_variability::fixed ||
                clock.interval_variability == clock_interval_variability::tunable) &&
            !clock.interval_decimal;
        const bool dynamicInputClock =
            clock.causality == variable_causality::input &&
            (clock.interval_variability == clock_interval_variability::changing ||
                clock.interval_variability == clock_interval_variability::countdown);
        if (!supportedTriggeredClock && !supportedDeclaredInputClock &&
            !configurableInputClockWithoutDefault && !dynamicInputClock) {
            throw error(
                make_error_code(errc::unsupported_feature),
                instanceName_ + ": Clock " + std::to_string(clock.reference) +
                    " requires an unsupported dynamic interval or shift operation");
        }
    }
}


void slave_instance::prepare_intermediate_callbacks()
{
    if (!callbacks_.intermediate_update && !callbacks_.required_intermediate_variables.empty()) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": required intermediate variables need an intermediate-update callback");
    }
    if (callbacks_.intermediate_update && !providesIntermediateUpdate_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not provide Intermediate Update Mode");
    }

    if (callbacks_.intermediate_update && callbacks_.required_intermediate_variables.empty()) {
        for (const auto& variable : fmu_->model_description()->variables) {
            if (variable.intermediate_update) {
                callbacks_.required_intermediate_variables.push_back(variable.reference);
            }
        }
    }

    intermediateReferences_.reserve(callbacks_.required_intermediate_variables.size());
    for (const auto reference : callbacks_.required_intermediate_variables) {
        if (!intermediateReferences_.insert(reference).second) {
            throw error(
                make_error_code(errc::unsupported_feature),
                instanceName_ + ": duplicate intermediate-update value reference " + std::to_string(reference));
        }
        const auto* variable = fmu_->find_variable(reference);
        if ((variable == nullptr) || variable->variability != variable_variability::continuous || (variable->causality != variable_causality::input && variable->causality != variable_causality::output) || !variable->intermediate_update) {
            throw error(
                make_error_code(errc::unsupported_feature),
                instanceName_ + ": value reference " + std::to_string(reference) + " is not an eligible continuous intermediate-update variable");
        }
    }
}


slave_instance::slave_instance(
    std::shared_ptr<v3::fmu> fmu,
    std::string_view instanceName,
    co_simulation_callbacks callbacks)
    : fmu_{std::move(fmu)}
    , handle_{
          fmi3_import_parse_xml(
              fmu_->importer()->fmilib_handle(),
              fmu_->directory().string().c_str(),
              nullptr),
      }
    , callbacks_{std::move(callbacks)}
    , instanceName_{instanceName}
    , callbackContext_{std::make_unique<callback_context>()}
{
    assert(!instanceName.empty());
    callbackContext_->sharedFatal = &fmu_->fatal_;
    callbackContext_->instanceName = instanceName_;
    callbackContext_->slaveInstance = this;
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            fmu_->importer()->last_error_message());
    }

    if (fmi3_import_create_dllfmu(
            handle_,
            fmi3_fmu_kind_cs,
            callbackContext_.get(),
            &log_message) != jm_status_success) {
        const auto message = fmu_->importer()->last_error_message();
        fmi3_import_free(handle_);
        handle_ = nullptr;
        throw error(
            make_error_code(errc::dl_load_error),
            message);
    }
    bool constructionComplete = false;
    bool instanceCreated = false;
    const auto cleanupInstance = gsl::finally([this, &constructionComplete, &instanceCreated] {
        if (!constructionComplete && handle_ != nullptr) {
            release_instance(handle_, instanceCreated && !callbackContext_->fmu_returned_fatal());
            handle_ = nullptr;
        }
    });

    auto resourceDirectory = cosim::filesystem::absolute(fmu_->directory() / "resources");
    auto resourcePath = cosim::filesystem::is_directory(resourceDirectory)
        ? (resourceDirectory /= "").make_preferred().u8string()
        : std::string{};
    const bool eventModeSupported =
        fmi3_import_get_capability(handle_, fmi3_cs_hasEventMode) != 0;
    eventModeUsed_ = eventModeSupported && callbacks_.event_mode_used;
    earlyReturnAllowed_ = callbacks_.early_return_allowed &&
        (fmu_->might_return_early_from_do_step() ||
            fmu_->can_return_early_after_intermediate_update());
    providesIntermediateUpdate_ =
        fmi3_import_get_capability(
            handle_, fmi3_cs_providesIntermediateUpdate) != 0;
    providesDirectionalDerivatives_ =
        fmi3_import_get_capability(
            handle_, fmi3_cs_providesDirectionalDerivatives) != 0;
    providesAdjointDerivatives_ =
        fmi3_import_get_capability(
            handle_, fmi3_cs_providesAdjointDerivatives) != 0;
    providesPerElementDependencies_ =
        fmi3_import_get_capability(
            handle_, fmi3_cs_providesPerElementDependencies) != 0;
    maxOutputDerivativeOrder_ = fmi3_import_get_capability(
        handle_, fmi3_cs_maxOutputDerivativeOrder);
    validate_clock_support(eventModeSupported);
    prepare_intermediate_callbacks();

    const auto status = fmi3_import_instantiate_co_simulation(
        handle_,
        instanceName_.data(),
        resourcePath.empty() ? nullptr : resourcePath.data(),
        fmi3_false != 0,
        fmi3_true != 0,
        (eventModeUsed_ ? fmi3_true : fmi3_false) != 0,
        (earlyReturnAllowed_ ? fmi3_true : fmi3_false) != 0,
        callbacks_.required_intermediate_variables.empty()
            ? nullptr
            : callbacks_.required_intermediate_variables.data(),
        callbacks_.required_intermediate_variables.size(),
        callbacks_.intermediate_update ? &intermediate_update : nullptr);
    if (status != jm_status_success) {
        const auto message = take_log_message(callbackContext_.get());
        throw error(
            make_error_code(errc::model_error),
            message.empty() ? "Could not instantiate FMI 3 Co-Simulation FMU" : message);
    }
    instanceCreated = true;
    static_cast<void>(take_log_message(callbackContext_.get()));
    structuralParameterValues_ =
        initial_structural_parameter_values(*fmu_->model_description());
    constructionComplete = true;
}


slave_instance::~slave_instance() noexcept
{
    // Importer-side failures prevent further simulation but do not prevent
    // disposal. Only an FMI Fatal status prohibits calling back into the FMU.
    if (handle_ == nullptr) {
        return;
    }

    callbackContext_->deactivate();
    if (!callbackContext_->fmu_returned_fatal() && !callbackContext_->instance_poisoned() &&
        !callbackContext_->error_terminated() &&
        (state_ == state::step || state_ == state::event) && !terminationAttempted_) {
        terminationAttempted_ = true;
        const auto status = fmi3_import_terminate(handle_);
        callbackContext_->record_status(status);
    }
    if (!callbackContext_->fmu_returned_fatal()) {
        for (auto& state : savedStates_) {
            if (state.fmuState != nullptr) {
                const auto status = fmi3_import_free_fmu_state(handle_, &state.fmuState);
                if (!is_success(status)) {
                    callbackContext_->record_status(status);
                    if (status == fmi3_status_fatal) {
                        break;
                    }
                }
            }
        }
    }
    release_instance(handle_, !callbackContext_->fmu_returned_fatal());
}


void slave_instance::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    // EnterInitializationMode is the only transition that establishes the
    // time bounds later checked by every DoStep call.
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "EnterInitializationMode");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "EnterInitializationMode");
    check_lifecycle(
        state_ == state::instantiated,
        instanceName_,
        "EnterInitializationMode");
    if (stopTime && *stopTime < startTime) {
        throw std::invalid_argument("stopTime must not precede startTime");
    }
    const auto status = fmi3_import_enter_initialization_mode(
        handle_,
        (relativeTolerance ? fmi3_true : fmi3_false) != 0,
        relativeTolerance ? *relativeTolerance : 0.0,
        to_double_time_point(startTime),
        (stopTime ? fmi3_true : fmi3_false) != 0,
        stopTime ? to_double_time_point(*stopTime) : 0.0);
    check_status(callbackContext_.get(), "EnterInitializationMode", status);
    setupComplete_ = true;
    simStarted_ = false;
    state_ = state::initialization;
    currentTime_ = startTime;
    stopTime_ = stopTime;
    terminationRequested_ = false;
    eventHandlingRequested_ = false;
    nextEventTime_.reset();
    eventIterationComplete_ = false;
    activeClocks_.clear();
    activatedPeriodicClocks_.clear();
    changingClocksAwaitingInterval_.clear();
    resolvedInputClockSchedules_.clear();
    initialClockIntervals_.clear();
    pendingClockIntervals_.clear();
}

void slave_instance::set_max_event_iterations(std::size_t limit)
{
    COSIM_INPUT_CHECK(limit > 0);
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "SetMaxEventIterations");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetMaxEventIterations");
    check_lifecycle(!simStarted_, instanceName_, "SetMaxEventIterations");
    maxEventIterations_ = limit;
}

bool slave_instance::supports_automatic_step_replay() const noexcept
{
    return !callbacks_.intermediate_update;
}


void slave_instance::start_simulation()
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "ExitInitializationMode");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "ExitInitializationMode");
    check_lifecycle(
        state_ == state::initialization,
        instanceName_,
        "ExitInitializationMode");
    resolve_fixed_clock_schedules();
    resolve_changing_clock_schedules();
    const auto status = fmi3_import_exit_initialization_mode(handle_);
    check_status(callbackContext_.get(), "ExitInitializationMode", status);
    initialClockIntervals_.clear();
    simStarted_ = true;
    if (eventModeUsed_) {
        state_ = state::event;
        eventIterationComplete_ = false;
        activeClocks_.clear();
        activatedPeriodicClocks_.clear();
        changingClocksAwaitingInterval_.clear();
        if (!fmu_->clock_descriptions().empty()) {
            return;
        }
        handle_events();
    } else {
        state_ = state::step;
    }
}

void slave_instance::end_simulation()
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "Terminate");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "Terminate", adapter_detail::operation_access::terminate);
    check_lifecycle(
        state_ == state::step || state_ == state::event,
        instanceName_,
        "Terminate");
    terminationAttempted_ = true;
    const auto status = fmi3_import_terminate(handle_);
    check_status(callbackContext_.get(), "Terminate", status);
    callbackContext_->clear_recovery();
    simStarted_ = false;
    state_ = state::terminated;
    activeClocks_.clear();
    activatedPeriodicClocks_.clear();
}


step_result slave_instance::do_step(time_point currentT, duration deltaT)
{
    if (eventModeUsed_ || earlyReturnAllowed_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": Event Mode or early return requires an event-aware simulation algorithm");
    }
    const auto result = do_step_with_info(currentT, deltaT);
    if (result.result == step_result::failed) {
        return step_result::failed;
    }
    if (result.termination_requested) {
        return step_result::canceled;
    }
    if (result.early_return || result.event_handling_needed) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": the step requires an event-aware simulation algorithm");
    }
    return step_result::complete;
}


step_result_info slave_instance::do_step_with_info(
    time_point currentT,
    duration deltaT)
{
    // The FMU's internal time can differ from a completed communication point.
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "DoStep");
    // User callbacks may release the last external reference.
    const auto keepAlive = shared_from_this();
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "DoStep");
    check_lifecycle(
        state_ == state::step && !terminationRequested_ &&
            !eventHandlingRequested_,
        instanceName_,
        "DoStep");
    COSIM_INPUT_CHECK(deltaT > duration::zero());
    if (currentT != currentTime_) {
        throw error(
            make_error_code(errc::simulation_error),
            instanceName_ + ": DoStep current time does not match the FMU time");
    }
    if (stopTime_ && currentT + deltaT > *stopTime_) {
        throw error(
            make_error_code(errc::simulation_error),
            instanceName_ + ": DoStep would advance beyond the configured stop time");
    }

    fmi3_boolean_t eventHandlingNeeded = fmi3_false != 0;
    fmi3_boolean_t terminate = fmi3_false != 0;
    fmi3_boolean_t earlyReturn = fmi3_false != 0;
    fmi3_float64_t lastSuccessfulTime = to_double_time_point(currentT);
    intermediateState_ = intermediate_state{
        to_double_time_point(currentT),
        to_double_time_point(currentT + deltaT),
        to_double_time_point(currentT),
        false,
        false,
        false,
    };
    const auto clearIntermediateState = gsl::finally([this] {
        intermediateState_.reset();
    });
    auto* const previousStep = std::exchange(active_step_context, callbackContext_.get());
    const auto clearActiveStep = gsl::finally([previousStep] {
        active_step_context = previousStep;
    });
    const auto status = fmi3_import_do_step(
        handle_,
        to_double_time_point(currentT),
        to_double_duration(deltaT, currentT),
        (fmu_->model_description()->capabilities.can_save_state
                ? fmi3_false
                : fmi3_true) != 0,
        &eventHandlingNeeded,
        &terminate,
        &earlyReturn,
        &lastSuccessfulTime);
    callbackContext_->record_status(status);
    callbackContext_->rethrow_callback_exception();
    if (status == fmi3_status_discard) {
        static_cast<void>(take_log_message(callbackContext_.get()));
        return {
            step_result::failed,
            currentT,
            false,
            false,
            false,
            true,
        };
    }
    check_status(callbackContext_.get(), "DoStep", status);
    stepSetStarted_ = false;
    validate_step_endpoint(
        currentT, deltaT, lastSuccessfulTime,
        static_cast<int>(earlyReturn) != fmi3_false);
    if (static_cast<int>(eventHandlingNeeded) != fmi3_false && !eventModeUsed_) {
        callbackContext_->poison_instance();
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMI 3 DoStep requires Event Mode");
    }
    if (static_cast<int>(earlyReturn) != fmi3_false && !earlyReturnAllowed_) {
        callbackContext_->poison_instance();
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMI 3 DoStep returned early without negotiation");
    }
    if (static_cast<int>(earlyReturn) != fmi3_false && lastSuccessfulTime == to_double_time_point(currentT + deltaT)) {
        if (!warnedEndpointEarlyReturn_) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << instanceName_ << ": FMI 3 DoStep reported early return at its requested "
                                    "endpoint; treating it as a completed step";
            warnedEndpointEarlyReturn_ = true;
        }
        earlyReturn = (fmi3_false != 0);
    }
    const auto requestedEnd = currentT + deltaT;
    const auto reachedTime = lastSuccessfulTime == to_double_time_point(requestedEnd)
        ? requestedEnd
        : to_time_point(lastSuccessfulTime);
    // Only an early return changes the communication point. FMI also reports
    // the FMU's internal time, which can differ even on a completed step.
    currentTime_ = static_cast<int>(earlyReturn) != fmi3_false
        ? reachedTime
        : requestedEnd;
    terminationRequested_ = static_cast<int>(terminate) != fmi3_false;
    eventHandlingRequested_ = static_cast<int>(eventHandlingNeeded) != fmi3_false;
    if (terminationRequested_) {
        callbackContext_->require_recovery(true);
    }
    return {
        step_result::complete,
        reachedTime,
        static_cast<int>(earlyReturn) != fmi3_false,
        static_cast<int>(eventHandlingNeeded) != fmi3_false,
        terminationRequested_,
        false,
    };
}


void slave_instance::handle_events()
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "HandleEvents");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "HandleEvents");
    check_lifecycle(
        (state_ == state::step || state_ == state::event) &&
            !terminationRequested_,
        instanceName_,
        "HandleEvents");
    if (!eventModeUsed_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not support Event Mode");
    }
    if (state_ == state::step) {
        enter_event_mode();
    }
    if (!fmu_->clock_descriptions().empty()) {
        apply_pending_clock_intervals();
    }
    for (std::size_t iteration = 0; iteration < maxEventIterations_; ++iteration) {
        const auto update = update_discrete_states();
        if (update.terminate_simulation) {
            return;
        }
        if (!update.discrete_states_need_update) {
            enter_step_mode();
            return;
        }
    }
    throw error(
        make_error_code(errc::simulation_error),
        instanceName_ + ": FMI 3 Event Mode did not converge within the iteration limit");
}

event_handling_status slave_instance::get_event_handling_status() const noexcept
{
    return {terminationRequested_, nextEventTime_};
}


void slave_instance::enter_configuration_mode()
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "EnterConfigurationMode");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "EnterConfigurationMode");
    check_lifecycle(
        (state_ == state::instantiated || state_ == state::step) &&
            !terminationRequested_ && !eventHandlingRequested_,
        instanceName_,
        "EnterConfigurationMode");
    if (state_ == state::step) {
        const auto& variables = fmu_->model_description()->variables;
        const auto hasTunableStructuralParameter = boost::algorithm::any_of(
            variables, [](const auto& variable) {
                return variable.causality == variable_causality::structural_parameter &&
                    variable.variability == variable_variability::tunable;
            });
        if (!hasTunableStructuralParameter) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": Reconfiguration Mode requires a tunable structural parameter");
        }
    }
    const auto status = fmi3_import_enter_configuration_mode(handle_);
    check_status(callbackContext_.get(), "EnterConfigurationMode", status);
    stateBeforeConfiguration_ = state_;
    state_ = state::configuration;
}


void slave_instance::exit_configuration_mode()
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "ExitConfigurationMode");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "ExitConfigurationMode");
    check_lifecycle(
        state_ == state::configuration,
        instanceName_,
        "ExitConfigurationMode");
    const auto status = fmi3_import_exit_configuration_mode(handle_);
    check_status(callbackContext_.get(), "ExitConfigurationMode", status);
    state_ = stateBeforeConfiguration_;
    refresh_structural_parameters();
}


void slave_instance::enter_event_mode()
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "EnterEventMode");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "EnterEventMode");
    check_lifecycle(
        state_ == state::step && !terminationRequested_,
        instanceName_,
        "EnterEventMode");
    if (!eventModeUsed_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU was instantiated without Event Mode");
    }
    const auto status = fmi3_import_enter_event_mode(handle_);
    check_status(callbackContext_.get(), "EnterEventMode", status);
    state_ = state::event;
    eventIterationComplete_ = false;
    activeClocks_.clear();
    activatedPeriodicClocks_.clear();
}


slave_instance::discrete_state_update slave_instance::update_discrete_states()
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "UpdateDiscreteStates");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "UpdateDiscreteStates");
    check_lifecycle(state_ == state::event, instanceName_, "UpdateDiscreteStates");
    fmi3_boolean_t discreteStatesNeedUpdate = fmi3_false != 0;
    fmi3_boolean_t terminateSimulation = fmi3_false != 0;
    fmi3_boolean_t nominalsChanged = fmi3_false != 0;
    fmi3_boolean_t valuesChanged = fmi3_false != 0;
    fmi3_boolean_t nextEventDefined = fmi3_false != 0;
    fmi3_float64_t nextEventTime = 0.0;
    const auto status = fmi3_import_update_discrete_states(
        handle_,
        &discreteStatesNeedUpdate,
        &terminateSimulation,
        &nominalsChanged,
        &valuesChanged,
        &nextEventDefined,
        &nextEventTime);
    check_status(callbackContext_.get(), "UpdateDiscreteStates", status);
    // FMI Clock-associated variables are inaccessible after this call until
    // an associated Clock is genuinely activated again.
    activeClocks_.clear();
    eventIterationComplete_ = static_cast<int>(discreteStatesNeedUpdate) == fmi3_false;
    if (static_cast<int>(nextEventDefined) != fmi3_false) {
        if (!representable_time(nextEventTime) ||
            nextEventTime < to_double_time_point(currentTime_)) {
            callbackContext_->require_recovery();
            throw error(
                make_error_code(errc::simulation_error),
                instanceName_ + ": FMI 3 reported an invalid next event time");
        }
        nextEventTime_ = to_time_point(nextEventTime);
    } else {
        nextEventTime_.reset();
    }
    if (static_cast<int>(terminateSimulation) != fmi3_false) {
        terminationRequested_ = true;
        callbackContext_->require_recovery(true);
    }
    return {
        static_cast<int>(discreteStatesNeedUpdate) != fmi3_false,
        static_cast<int>(terminateSimulation) != fmi3_false,
        static_cast<int>(nominalsChanged) != fmi3_false,
        static_cast<int>(valuesChanged) != fmi3_false,
        static_cast<int>(nextEventDefined) != fmi3_false
            ? std::optional<time_point>(to_time_point(nextEventTime))
            : std::nullopt,
    };
}


void slave_instance::enter_step_mode()
{
    check_callback_reentrancy(
        callbackContext_.get(), instanceName_, "EnterStepMode");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "EnterStepMode");
    check_lifecycle(state_ == state::event, instanceName_, "EnterStepMode");
    if (!eventIterationComplete_) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": UpdateDiscreteStates must complete before entering Step Mode");
    }
    if (!changingClocksAwaitingInterval_.empty()) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": query the next interval of each activated changing Clock before entering Step Mode");
    }
    if (terminationRequested_) {
        throw error(
            make_error_code(errc::simulation_error),
            instanceName_ + ": cannot enter Step Mode after termination was requested");
    }
    const auto status = fmi3_import_enter_step_mode(handle_);
    check_status(callbackContext_.get(), "EnterStepMode", status);
    state_ = state::step;
    eventHandlingRequested_ = false;
    stepSetStarted_ = false;
    eventIterationComplete_ = false;
    activeClocks_.clear();
    activatedPeriodicClocks_.clear();
}


void slave_instance::set_debug_logging(
    bool enabled, gsl::span<const std::string_view> categories)
{
    constexpr auto operation = "SetDebugLogging";
    check_callback_reentrancy(callbackContext_.get(), instanceName_, operation);
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, operation, adapter_detail::operation_access::diagnostic);

    std::vector<fmi3_string_t> names;
    names.reserve(categories.size());
    const auto count = fmi3_import_get_log_categories_num(handle_);
    for (const auto category : categories) {
        fmi3_string_t match = nullptr;
        for (std::size_t i = 0; i < count; ++i) {
            const auto* const candidate = fmi3_import_get_log_category(handle_, i);
            if ((candidate != nullptr) && category == candidate) {
                match = candidate;
                break;
            }
        }
        if (match == nullptr) {
            throw std::invalid_argument(
                instanceName_ + ": undeclared FMI 3 log category '" + std::string(category) + "'");
        }
        names.push_back(match);
    }

    const auto status = fmi3_import_set_debug_logging(
        handle_, (enabled ? fmi3_true : fmi3_false) != 0, names.size(),
        names.empty() ? nullptr : names.data());
    check_status(callbackContext_.get(), operation, status);
}


void slave_instance::reset()
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "Reset");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "Reset", adapter_detail::operation_access::recovery);
    check_lifecycle(
        (state_ != state::instantiated || callbackContext_->recovery_required()) && !intermediateState_,
        instanceName_,
        "Reset");
    const auto status = fmi3_import_reset(handle_);
    check_status(callbackContext_.get(), "Reset", status);
    callbackContext_->clear_recovery();
    setupComplete_ = false;
    simStarted_ = false;
    state_ = state::instantiated;
    stateBeforeConfiguration_ = state::instantiated;
    currentTime_ = time_point{};
    stopTime_.reset();
    terminationRequested_ = false;
    eventHandlingRequested_ = false;
    terminationAttempted_ = false;
    stepSetStarted_ = false;
    nextEventTime_.reset();
    eventIterationComplete_ = false;
    activeClocks_.clear();
    activatedPeriodicClocks_.clear();
    changingClocksAwaitingInterval_.clear();
    resolvedInputClockSchedules_.clear();
    initialClockIntervals_.clear();
    pendingClockIntervals_.clear();
    refresh_structural_parameters();
}


bool slave_instance::event_mode_used() const noexcept
{
    return eventModeUsed_;
}


bool slave_instance::early_return_allowed() const noexcept
{
    return earlyReturnAllowed_;
}


bool slave_instance::provides_intermediate_update() const noexcept
{
    return providesIntermediateUpdate_;
}


std::optional<time_point> slave_instance::next_event_time() const noexcept
{
    return nextEventTime_;
}


void slave_instance::notify_intermediate_update(
    double intermediateUpdateTime,
    bool intermediateVariableSetRequested,
    bool intermediateVariableGetAllowed,
    bool intermediateStepFinished,
    bool canReturnEarly,
    bool* earlyReturnRequested,
    double* earlyReturnTime)
{
    // The callback window is step-local. Reset per-callback Get/Set tracking
    // so permission and ordering checks cannot leak into the next update.
    std::scoped_lock const lock(mutex_);
    if (!intermediateState_) {
        throw error(
            make_error_code(errc::model_error),
            instanceName_ + ": intermediate update callback occurred outside DoStep");
    }
    if (!std::isfinite(intermediateUpdateTime) || intermediateUpdateTime < intermediateState_->startTime || intermediateUpdateTime > intermediateState_->endTime || intermediateUpdateTime < intermediateState_->lastCompletedTime) {
        throw error(
            make_error_code(errc::simulation_error),
            instanceName_ + ": FMI 3 intermediate update time is outside the step");
    }
    if (earlyReturnRequested != nullptr) {
        *earlyReturnRequested = (fmi3_false != 0);
    }
    if (earlyReturnTime != nullptr) {
        *earlyReturnTime = intermediateUpdateTime;
    }

    if (intermediateStepFinished) {
        intermediateState_->lastCompletedTime = intermediateUpdateTime;
    }
    intermediateState_->variableSetRequested = intermediateVariableSetRequested;
    intermediateState_->variableGetAllowed = intermediateVariableGetAllowed;
    intermediateState_->setStarted = false;

    if (callbacks_.intermediate_update) {
        intermediate_update_info info{
            to_time_point(intermediateUpdateTime),
            intermediateVariableSetRequested,
            intermediateVariableGetAllowed,
            intermediateStepFinished,
            canReturnEarly,
            false,
            to_time_point(intermediateUpdateTime),
        };
        callbacks_.intermediate_update(*this, info);
        if (info.early_return_requested) {
            if (!canReturnEarly) {
                throw error(
                    make_error_code(errc::simulation_error),
                    instanceName_ + ": intermediate callback requested an early return "
                                    "when the FMU did not permit it");
            }
            const auto requestedTime = to_double_time_point(info.early_return_time);
            if (!std::isfinite(requestedTime) ||
                requestedTime < intermediateState_->startTime ||
                requestedTime > intermediateState_->endTime) {
                throw error(
                    make_error_code(errc::simulation_error),
                    instanceName_ + ": intermediate callback returned an invalid early-return time");
            }
            if (earlyReturnRequested != nullptr) {
                *earlyReturnRequested = (fmi3_true != 0);
            }
            if (earlyReturnTime != nullptr) {
                *earlyReturnTime = requestedTime;
            }
        }
    }
}


void slave_instance::check_intermediate_access(
    gsl::span<const value_reference> variables,
    bool setting) const
{
    if (!intermediateState_) {
        return;
    }
    for (const auto reference : variables) {
        if (intermediateReferences_.count(reference) == 0) {
            throw error(
                make_error_code(errc::unsupported_feature),
                instanceName_ + ": value reference " + std::to_string(reference) + " was not declared as an intermediate-update variable");
        }
        const auto* variable = fmu_->find_variable(reference);
        if (variable == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": intermediate-update value reference " + std::to_string(reference) + " is not defined");
        }
        if (setting) {
            if (variable->causality != variable_causality::input || variable->variability != variable_variability::continuous) {
                throw error(
                    make_error_code(errc::model_error),
                    instanceName_ + ": value reference " + std::to_string(reference) + " is not a continuous intermediate input");
            }
        } else if (variable->causality != variable_causality::output) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": value reference " + std::to_string(reference) + " is not an intermediate output");
        }
    }
    if (setting) {
        if (!intermediateState_->variableSetRequested) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": FMI 3 intermediate Set is not permitted");
        }
    } else {
        if (!intermediateState_->variableGetAllowed) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": FMI 3 intermediate Get is not permitted");
        }
        if (intermediateState_->setStarted) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": intermediate Get must precede intermediate Set");
        }
    }
}


void slave_instance::validate_step_endpoint(
    time_point currentT,
    duration deltaT,
    double lastSuccessfulTime,
    bool communicationStopped) const
{
    COSIM_INPUT_CHECK(deltaT > duration::zero());
    const auto currentTime = to_double_time_point(currentT);
    const auto requestedEnd = to_double_time_point(currentT + deltaT);
    const auto minimumTime = to_double_time_point(time_point((duration::min)())); // NOLINT(readability-redundant-parentheses): Protect against the Windows min macro.
    const auto maximumTime = to_double_time_point(time_point((duration::max)())); // NOLINT(readability-redundant-parentheses): Protect against the Windows max macro.
    if (!std::isfinite(lastSuccessfulTime) ||
        lastSuccessfulTime < minimumTime || lastSuccessfulTime >= maximumTime ||
        (communicationStopped &&
            (lastSuccessfulTime < currentTime || lastSuccessfulTime > requestedEnd))) {
        callbackContext_->poison_instance();
        throw error(
            make_error_code(errc::simulation_error),
            instanceName_ + ": FMI 3 returned an invalid lastSuccessfulTime");
    }
}


std::shared_ptr<v3::fmu> slave_instance::v3_fmu() const
{
    return fmu_;
}


fmi3_import_t* slave_instance::fmilib_handle() const
{
    return handle_;
}


} // namespace cosim::fmi::v3
