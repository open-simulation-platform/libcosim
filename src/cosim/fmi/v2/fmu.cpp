/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/v2/fmu.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/file_cache.hpp"
#include "cosim/fmi/enumeration.hpp"
#include "cosim/fmi/fmilib.h" // IWYU pragma: keep
#include "cosim/fmi/fmu.hpp"
#include "cosim/fmi/glue.hpp"
#include "cosim/fmi/importer.hpp"
#include "cosim/fmi/value_validation.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/scalar_value_dispatch.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#ifdef _WIN32
#    include "cosim/fmi/windows.hpp"
#endif

#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <gsl/span>
#include <gsl/span_ext>
#include <gsl/util>

#include <FMI/fmi_import_context.h>
#include <FMI2/fmi2TypesPlatform.h>
#include <FMI2/fmi2_enums.h>
#include <FMI2/fmi2_function_types.h>
#include <FMI2/fmi2_import.h>
#include <FMI2/fmi2_import_capi.h>
#include <FMI2/fmi2_import_variable.h>
#include <FMI2/fmi2_import_variable_list.h>
#include <FMI2/fmi2_types.h>
#include <JM/jm_types.h>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


namespace cosim::fmi::v2
{

namespace
{
void apply_enumeration_metadata(
    variable_description& description,
    const cosim::model_description& modelDescription,
    const fmi2_enumeration_restrictions& enumerationRestrictions)
{
    if (!description.declared_type) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 Enumeration variable '" + description.name +
                "' has no declared type");
    }
    const auto typeDefinition = boost::range::find_if(
        modelDescription.type_definitions,
        [&description](const auto& definition) {
            return definition.name == *description.declared_type;
        });
    if (typeDefinition == modelDescription.type_definitions.end() ||
        typeDefinition->type != variable_type::enumeration ||
        typeDefinition->enumeration_items.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 Enumeration variable '" + description.name +
                "' references an invalid declared type");
    }
    description.min = typeDefinition->min;
    description.max = typeDefinition->max;
    const auto restrictions = enumerationRestrictions.find(description.name);
    if (restrictions != enumerationRestrictions.end()) {
        if (restrictions->second.min) {
            description.min = enumeration_value{*restrictions->second.min};
        }
        if (restrictions->second.max) {
            description.max = enumeration_value{*restrictions->second.max};
        }
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): value() preserves the missing-bound exception.
    const auto minimum = std::get<enumeration_value>(description.min.value()).value;
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): value() preserves the missing-bound exception.
    const auto maximum = std::get<enumeration_value>(description.max.value()).value;
    if (minimum > maximum) {
        throw error(make_error_code(errc::bad_file),
            "FMI 2.0 Enumeration variable '" + description.name +
                "' has inverted bounds");
    }
    if (description.start) {
        const auto* start =
            std::get_if<enumeration_value>(&*description.start);
        if (start == nullptr) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 2.0 Enumeration variable '" + description.name +
                    "' has an invalid start value");
        }
        const auto item = std::find_if(
            typeDefinition->enumeration_items.begin(),
            typeDefinition->enumeration_items.end(),
            [start](const auto& candidate) {
                return candidate.value == start->value;
            });
        if (item == typeDefinition->enumeration_items.end() ||
            (description.min && start->value < std::get<enumeration_value>(*description.min).value) ||
            (description.max && start->value > std::get<enumeration_value>(*description.max).value)) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 2.0 Enumeration variable '" + description.name +
                    "' has an undeclared start value");
        }
    }
}

void read_derivative_reference(
    variable_description& description,
    fmi2_import_variable_t* variable,
    const std::unordered_map<fmi2_import_real_variable_t*, fmi2_import_variable_t*>& realVariables)
{
    auto* const real = fmi2_import_get_variable_as_real(variable);
    if (auto* const state = fmi2_import_get_real_variable_derivative_of(real)) {
        const auto target = realVariables.find(state);
        if (target == realVariables.end()) {
            throw error(make_error_code(errc::bad_file),
                "FMI 2.0 derivative variable '" + description.name +
                    "' references an unknown state");
        }
        description.derivative = fmi2_import_get_variable_vr(target->second);
    }
}
} // namespace

// =============================================================================
// fmu
// =============================================================================

fmu::fmu(
    std::shared_ptr<fmi::importer> importer,
    std::unique_ptr<file_cache::directory_ro> fmuDir)
    : importer_{std::move(importer)}
    , dir_(std::move(fmuDir))
    , handle_{fmi2_import_parse_xml(importer_->fmilib_handle(), dir_->path().string().c_str(), nullptr)}
{
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            importer_->last_error_message());
    }
    bool initialized = false;
    const auto cleanup = gsl::finally([&] {
        if (!initialized) {
            fmi2_import_free(handle_);
        }
    });
    const auto fmuKind = fmi2_import_get_fmu_kind(handle_);
    if ((static_cast<unsigned int>(fmuKind) &
            static_cast<unsigned int>(fmi2_fmu_kind_cs)) == 0U) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Not a co-simulation FMU");
    }

    modelDescription_.name = fmi2_import_get_model_name(handle_);
    modelDescription_.uuid = fmi2_import_get_GUID(handle_);
    modelDescription_.description = fmi2_import_get_description(handle_);
    modelDescription_.author = fmi2_import_get_author(handle_);
    modelDescription_.version = fmi2_import_get_model_version(handle_);
    modelDescription_.capabilities.can_handle_variable_step = !(fmi2_import_get_capability(
                                                                    handle_,
                                                                    fmi2_cs_canHandleVariableCommunicationStepSize) == 0U);
    modelDescription_.capabilities.can_save_state = !(fmi2_import_get_capability(
                                                          handle_,
                                                          fmi2_cs_canGetAndSetFMUstate) == 0U);
    modelDescription_.capabilities.can_export_state = !(fmi2_import_get_capability(
                                                            handle_,
                                                            fmi2_cs_canSerializeFMUstate) == 0U);
    modelDescription_.type_definitions = to_type_definitions(handle_);
    modelDescription_.unit_definitions = to_unit_definitions(handle_);
    fmi2_enumeration_restrictions enumerationRestrictions;
    read_xml_metadata(
        modelDescription_.unit_definitions,
        dir_->path() / "modelDescription.xml",
        &enumerationRestrictions);
    auto* const varList = fmi2_import_get_variable_list(handle_, 0);
    if (varList == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 model has no readable ScalarVariable list");
    }
    const auto freeVariableList = gsl::finally([&] {
        fmi2_import_free_variable_list(varList);
    });
    const auto varCount = fmi2_import_get_variable_list_size(varList);
    std::unordered_map<fmi2_import_real_variable_t*, fmi2_import_variable_t*>
        realVariables;
    for (unsigned int i = 0; i < varCount; ++i) {
        auto* const var = fmi2_import_get_variable(varList, i);
        if (var != nullptr &&
            fmi2_import_get_variable_base_type(var) == fmi2_base_type_real) {
            if (auto* const real = fmi2_import_get_variable_as_real(var)) {
                realVariables.emplace(real, var);
            }
        }
    }
    for (unsigned int i = 0; i < varCount; ++i) {
        auto* const var = fmi2_import_get_variable(varList, i);
        if (var == nullptr) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 2.0 model contains an invalid ScalarVariable");
        }
        auto description = to_variable_description(var);
        if (description.type == variable_type::real) {
            read_derivative_reference(description, var, realVariables);
        }
        if (description.type == variable_type::enumeration) {
            apply_enumeration_metadata(description, modelDescription_, enumerationRestrictions);
        }
        modelDescription_.variables.push_back(description);
    }
    read_output_dependencies(modelDescription_, handle_, dir_->path() / "modelDescription.xml");
    initialized = true;
}


fmu::~fmu()
{
    fmi2_import_free(handle_);
}


fmi::fmi_version fmu::fmi_version() const
{
    return fmi::fmi_version::v2_0;
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


namespace
{
void prune(std::vector<std::weak_ptr<slave_instance>>& instances)
{
    auto const newEnd = boost::range::remove_if(
        instances,
        [](const std::weak_ptr<slave_instance>& instance) { return instance.expired(); });
    instances.erase(newEnd, end(instances));
}
} // namespace


std::shared_ptr<v2::slave_instance> fmu::instantiate_v2_slave(
    std::string_view instanceName)
{
    COSIM_INPUT_CHECK(!instanceName.empty());
#ifdef _WIN32
    if (!additionalDllSearchPath_) {
        additionalDllSearchPath_ =
            std::make_unique<detail::additional_path>(fmu_binaries_dir(dir_->path()));
    }
#endif
    prune(instances_);
    const bool isSingleton = !(fmi2_import_get_capability(
                                   handle_,
                                   fmi2_cs_canBeInstantiatedOnlyOncePerProcess) == 0U);
    if (isSingleton && !instances_.empty()) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "FMU '" + modelDescription_.name + "' can only be instantiated once");
    }
    auto instance = std::shared_ptr<slave_instance>(
        new slave_instance(shared_from_this(), instanceName));
    instances_.push_back(instance);
    return instance;
}


cosim::filesystem::path fmu::directory() const
{
    return dir_->path();
}


fmi2_import_t* fmu::fmilib_handle() const
{
    return handle_;
}


// =============================================================================
// slave_instance
// =============================================================================

namespace
{
struct log_record
{
    log_record() = default;
    log_record(fmi2_status_t logStatus, std::string logMessage)
        : status{logStatus}
        , message(std::move(logMessage))
    { }
    fmi2_status_t status = fmi2_status_ok;
    std::string message;
};
std::unordered_map<std::string, log_record> g_logRecords; // NOLINT(bugprone-throwing-static-initialization,cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables): FMI callbacks share records across instances.
std::mutex g_logMutex; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): Synchronizes the shared callback records.

// NOLINTNEXTLINE(cert-dcl50-cpp,modernize-avoid-variadic-functions): FMI 2 fixes the callback signature.
void log_message(
    fmi2_component_environment_t /*unused*/,
#ifndef LIBCOSIM_NO_FMI_LOGGING
    fmi2_string_t instanceName,
    fmi2_status_t status,
    fmi2_string_t category, // NOLINT(bugprone-easily-swappable-parameters): FMI 2 fixes the callback parameter order.
    fmi2_string_t message,
#else
    fmi2_string_t,
    fmi2_status_t,
    fmi2_string_t,
    fmi2_string_t,
#endif
    ...) noexcept
try {
#ifndef LIBCOSIM_NO_FMI_LOGGING
    if (message == nullptr) {
        BOOST_LOG_SEV(log::logger(), log::error) << "FMI 2.0 log message is null";
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): FMI 2 supplies a variadic format.
    std::va_list args{};
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay): va_list is an array on some platforms.
    va_start(args, message);
    const auto msgLength = std::vsnprintf(nullptr, 0, message, args);
    va_end(args);
    // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    if (msgLength < 0) {
        BOOST_LOG_SEV(log::logger(), log::error) << "Could not format FMI 2.0 log message";
        return;
    }
    auto msgBuffer = std::vector<char>(static_cast<std::size_t>(msgLength) + 1);
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay): va_list is an array on some platforms.
    va_start(args, message);
    const auto written = std::vsnprintf(msgBuffer.data(), msgBuffer.size(), message, args);
    va_end(args);
    // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    if (written != msgLength) {
        BOOST_LOG_SEV(log::logger(), log::error) << "Could not format FMI 2.0 log message";
        return;
    }
    assert(msgBuffer.back() == '\0');

    std::string statusName = "unknown";
    log::severity_level logLevel = log::error;
    switch (status) {
        case fmi2_status_ok:
            statusName = "ok";
            logLevel = log::trace;
            break;
        case fmi2_status_warning:
            statusName = "warning";
            logLevel = log::warning;
            break;
        case fmi2_status_discard:
            // Don't know if this ever happens, but we should at least
            // print a debug message if it does.
            statusName = "discard";
            logLevel = log::debug;
            break;
        case fmi2_status_error:
            statusName = "error";
            logLevel = log::error;
            break;
        case fmi2_status_fatal:
            statusName = "fatal";
            logLevel = log::error;
            break;
        case fmi2_status_pending:
            // Don't know if this ever happens, but we should at least
            // print a debug message if it does.
            statusName = "pending";
            logLevel = log::debug;
            break;
    }
    BOOST_LOG_SEV(log::logger(), logLevel)
        << "[FMI status=" << statusName
        << ", category=" << ((category != nullptr) ? category : "") << "] "
        << msgBuffer.data();

    std::scoped_lock const lock(g_logMutex);
    g_logRecords[(instanceName != nullptr) ? instanceName : ""] =
        log_record{status, std::string(msgBuffer.data())};
#endif
} catch (...) { // NOLINT(bugprone-empty-catch): Preserve the existing non-throwing FMI callback boundary.
    // Logging failures must not unwind through FMU code.
}

log_record last_log_record(const std::string& instanceName)
{
    std::scoped_lock const lock(g_logMutex);
    const auto record = g_logRecords.find(instanceName);
    if (record == g_logRecords.end()) {
        return log_record{};
    } else {
        // Note the use of c_str() here, to force the string to be copied.
        // The C++ standard now disallows copy-on-write, but some compilers
        // still use it, which could lead to problems in multithreaded
        // programs.
        return log_record{
            record->second.status,
            std::string(record->second.message.c_str())}; // NOLINT(readability-redundant-string-cstr)
    }
}


std::string status_message(
    const std::string& instanceName,
    const char* operation,
    fmi2_status_t status)
{
    auto message =
        instanceName + ": " + operation + " returned " +
        fmi2_status_to_string(status);
    const auto logMessage = last_log_record(instanceName).message;
    if (!logMessage.empty()) {
        message += ": " + logMessage;
    }
    return message;
}


using detail::find_variable_description;

void validate_enumeration_references(
    fmi2_import_t* handle,
    const model_description& model,
    gsl::span<const value_reference> variables,
    const std::string& instanceName)
{
    for (const auto reference : variables) {
        if (fmi2_import_get_variable_by_vr(
                handle, fmi2_base_type_enum, reference) == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName + ": value reference " +
                    std::to_string(reference) + " is not an Enumeration variable");
        }
        if (find_variable_description(
                model, variable_type::enumeration, reference) == nullptr) {
            throw error(
                make_error_code(errc::model_error),
                instanceName + ": value reference " +
                    std::to_string(reference) +
                    " has no Enumeration metadata");
        }
    }
}
} // namespace


// NOTE: We have to re-parse the model description XML every time we
// instantiate a new slave because of a shortcoming in FMI Library.
// (In brief, fmi2_import_create_dllfmu() and fmi2_import_instantiate()
// both store their results in the fmi2_import_t object created by
// fmi2_import_parse_xml().)
slave_instance::slave_instance(
    std::shared_ptr<v2::fmu> fmu,
    std::string_view instanceName)
    : fmu_{std::move(fmu)}
    , handle_{fmi2_import_parse_xml(fmu_->importer()->fmilib_handle(), fmu_->directory().string().c_str(), nullptr)}
    , instanceName_(instanceName)
{
    assert(!instanceName.empty());
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            fmu_->importer()->last_error_message());
    }
    cache_directional_derivative_metadata();

    fmi2_callback_functions_t callbacks;
    callbacks.allocateMemory = std::calloc;
    callbacks.freeMemory = std::free;
    callbacks.logger = log_message;
    callbacks.stepFinished = nullptr;
    callbacks.componentEnvironment = nullptr;

    if (fmi2_import_create_dllfmu(handle_, fmi2_fmu_kind_cs, &callbacks) != jm_status_success) {
        static_cast<void>(fmu_->importer()->last_error_message());
        fmi2_import_free(handle_);
        throw error(
            make_error_code(errc::dl_load_error),
            fmu_->importer()->last_error_message());
    }
    providesDirectionalDerivatives_ = !(fmi2_import_get_capability(
                                            handle_,
                                            fmi2_cs_providesDirectionalDerivatives) == 0U);

    const auto status = fmi2_import_instantiate(
        handle_,
        instanceName_.c_str(),
        fmi2_cosimulation,
        nullptr, // fmuResourceLocation
        fmi2_false); // visible
    if (status != jm_status_success) {
        fmi2_import_destroy_dllfmu(handle_);
        fmi2_import_free(handle_);
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


slave_instance::~slave_instance() noexcept
{
    if (!fmu_->fatal_.load() && cleanupState_ == cleanup_state::step_pending &&
        accept_status(fmi2_import_cancel_step(handle_))) {
        cleanupState_ = cleanup_state::step_interrupted;
    }
    if (!fmu_->fatal_.load() && cleanupState_ == cleanup_state::normal &&
        lifecycleState_ == lifecycle_state::step) {
        accept_status(fmi2_import_terminate(handle_));
    }
    if (cleanupState_ != cleanup_state::step_pending) {
        for (auto& state : savedStates_) {
            if (fmu_->fatal_.load()) {
                break;
            }
            if (state.fmuState != nullptr) {
                accept_status(fmi2_import_free_fmu_state(handle_, &state.fmuState));
            }
        }
        if (!fmu_->fatal_.load()) {
            fmi2_import_free_instance(handle_);
        }
    }
    fmi2_import_destroy_dllfmu(handle_);
    fmi2_import_free(handle_);
}


template<typename Status>
bool slave_instance::accept_status(Status status) const noexcept
{
    if (status == fmi2_status_fatal) {
        fmu_->fatal_.store(true);
    } else if (status == fmi2_status_error) {
        cleanupState_ = cleanup_state::error;
    }
    return status == fmi2_status_ok || status == fmi2_status_warning;
}


void slave_instance::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    assert(lifecycleState_ == lifecycle_state::instantiated);
    const auto rcs = fmi2_import_setup_experiment(
        handle_,
        relativeTolerance ? fmi2_true : fmi2_false,
        relativeTolerance ? *relativeTolerance : 0.0,
        to_double_time_point(startTime),
        stopTime ? fmi2_true : fmi2_false,
        stopTime ? to_double_time_point(*stopTime) : 0.0);
    if (!accept_status(rcs)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }

    const auto rce = fmi2_import_enter_initialization_mode(handle_);
    if (!accept_status(rce)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }

    lifecycleState_ = lifecycle_state::initialization;
}


void slave_instance::start_simulation()
{
    assert(lifecycleState_ == lifecycle_state::initialization);
    const auto status = fmi2_import_exit_initialization_mode(handle_);
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    lifecycleState_ = lifecycle_state::step;
}


void slave_instance::end_simulation()
{
    assert(lifecycleState_ == lifecycle_state::step);
    const auto status = fmi2_import_terminate(handle_);
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    lifecycleState_ = lifecycle_state::terminated;
}


step_result slave_instance::do_step(time_point currentT, duration deltaT)
{
    assert(lifecycleState_ == lifecycle_state::step);
    const auto status = fmi2_import_do_step(
        handle_,
        to_double_time_point(currentT),
        to_double_duration(deltaT, currentT),
        fmi2_true);
    if (accept_status(status)) {
        cleanupState_ = cleanup_state::normal;
        return step_result::complete;
    } else if (status == fmi2_status_discard) {
        cleanupState_ = cleanup_state::step_interrupted;
        return step_result::failed;
    } else if (status == fmi2_status_pending) {
        cleanupState_ = cleanup_state::step_pending;
        throw error(
            make_error_code(errc::unsupported_feature),
            "Slave performs time step asynchronously");
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::get_real_variables(
    gsl::span<const value_reference> variables,
    gsl::span<double> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    const auto status = fmi2_import_get_real(
        handle_, variables.data(), variables.size(), values.data());
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            status_message(instanceName_, "GetReal", status));
    }
}


void slave_instance::get_integer_variables(
    gsl::span<const value_reference> variables,
    gsl::span<int> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    const auto status = fmi2_import_get_integer(
        handle_, variables.data(), variables.size(), values.data());
    if (!accept_status(status)) {
        auto message = status_message(instanceName_, "GetInteger", status);
        message += " (value references:";
        for (const auto reference : variables) {
            message += " " + std::to_string(reference);
        }
        message += ')';
        throw error(
            make_error_code(errc::model_error),
            message);
    }
}


void slave_instance::get_enumeration_variables(
    gsl::span<const value_reference> variables,
    gsl::span<enumeration_value> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    validate_enumeration_references(
        handle_,
        *fmu_->model_description(),
        variables,
        instanceName_);

    std::vector<int> fmiValues(values.size());
    const auto status = fmi2_import_get_integer(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    const auto& model = *fmu_->model_description();
    std::vector<enumeration_value> stagedValues;
    stagedValues.reserve(values.size());
    auto reference = variables.begin();
    for (const auto fmiValue : fmiValues) {
        const auto value = enumeration_value{
            static_cast<std::int64_t>(fmiValue),
        };
        detail::validate_enumeration_reference_value(
            model, *reference, value, instanceName_);
        stagedValues.push_back(value);
        ++reference;
    }
    boost::range::copy(stagedValues, values.begin());
}


void slave_instance::get_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<bool> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    std::vector<fmi2_boolean_t> fmiValues(values.size());
    const auto status = fmi2_import_get_boolean(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    auto source = fmiValues.begin();
    for (auto& value : values) {
        value = (*source != fmi2_false);
        ++source;
    }
}


void slave_instance::get_string_variables(
    gsl::span<const value_reference> variables,
    gsl::span<std::string> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    std::vector<fmi2_string_t> fmiValues(values.size());
    const auto status = fmi2_import_get_string(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    auto source = fmiValues.begin();
    for (auto& value : values) {
        value = (*source == nullptr) ? std::string() : std::string(*source);
        ++source;
    }
}


void slave_instance::set_real_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const double> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    const auto status = fmi2_import_set_real(
        handle_, variables.data(), variables.size(), values.data());
    if (accept_status(status)) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            status_message(instanceName_, "SetReal", status));
    }
}


void slave_instance::set_integer_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const int> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    const auto status = fmi2_import_set_integer(
        handle_, variables.data(), variables.size(), values.data());
    if (accept_status(status)) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            status_message(instanceName_, "SetInteger", status));
    }
}


void slave_instance::set_enumeration_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const enumeration_value> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    validate_enumeration_references(
        handle_,
        *fmu_->model_description(),
        variables,
        instanceName_);

    const auto& model = *fmu_->model_description();
    std::vector<int> fmiValues;
    fmiValues.reserve(values.size());
    auto reference = variables.begin();
    for (const auto value : values) {
        detail::validate_legacy_enumeration_assignment<int>(
            model, *reference, value, instanceName_, "2.0");
        fmiValues.push_back(static_cast<int>(value.value));
        ++reference;
    }

    const auto status = fmi2_import_set_integer(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (accept_status(status)) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            status_message(instanceName_, "SetEnumeration", status));
    }
}


void slave_instance::set_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const bool> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    std::vector<fmi2_boolean_t> fmiValues(values.size());
    auto target = fmiValues.begin();
    for (const auto value : values) {
        *target = static_cast<fmi2_boolean_t>(value ? fmi2_true : fmi2_false);
        ++target;
    }
    const auto status = fmi2_import_set_boolean(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (accept_status(status)) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::set_string_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const std::string> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    std::vector<fmi2_string_t> fmiValues(values.size());
    auto target = fmiValues.begin();
    for (const auto& value : values) {
        *target = value.c_str();
        ++target;
    }
    const auto status = fmi2_import_set_string(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (accept_status(status)) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


std::vector<variable_value> slave_instance::get_variable_values(
    gsl::span<const typed_value_reference> variables) const
{
    std::vector<variable_value> result;
    result.reserve(variables.size());
    for (const auto variable : variables) {
        switch (variable.type) {
            case variable_type::real: {
                double value = 0.0;
                get_real_variables(
                    gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(real_values{{value}});
                continue;
            }
            case variable_type::integer: {
                int value = 0;
                get_integer_variables(
                    gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(integer_values{{value}});
                continue;
            }
            case variable_type::boolean: {
                bool value = false;
                get_boolean_variables(
                    gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(boolean_values{{value}});
                continue;
            }
            case variable_type::string: {
                std::string value;
                get_string_variables(
                    gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(string_values{{std::move(value)}});
                continue;
            }
            case variable_type::enumeration: {
                enumeration_value value;
                get_enumeration_variables(
                    gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(enumeration_values{{value}});
                continue;
            }
            case variable_type::float32:
            case variable_type::int8:
            case variable_type::uint8:
            case variable_type::int16:
            case variable_type::uint16:
            case variable_type::uint32:
            case variable_type::int64:
            case variable_type::uint64:
            case variable_type::binary:
            case variable_type::clock:
                break;
        }
        throw error(
            make_error_code(errc::unsupported_feature),
            std::string("Generic variable access is not supported for ") +
                to_text(variable.type));
    }
    return result;
}


void slave_instance::set_variable_values(
    gsl::span<const typed_value_reference> variables,
    gsl::span<const variable_value> values)
{
    const auto normalized = detail::prevalidate_legacy_values(
        *fmu_->model_description(), instanceName_, variables, values,
        [this](value_reference reference, enumeration_value value) {
            validate_enumeration_references(
                handle_, *fmu_->model_description(),
                gsl::make_span(&reference, 1), instanceName_);
            detail::validate_legacy_enumeration_assignment<int>(
                *fmu_->model_description(), reference, value, instanceName_, "2.0");
        });
    std::size_t index = 0;
    for (const auto variable : variables) {
        const auto& value = normalized.at(index++);
        if (variable.type == variable_type::enumeration) {
            const auto scalar =
                std::get<enumeration_values>(value.values()).values.at(0);
            set_enumeration_variables(
                gsl::make_span(&variable.reference, 1),
                gsl::make_span(&scalar, 1));
        } else {
            ::cosim::detail::set_legacy_scalar_value(*this, variable, value);
        }
    }
}


slave::state_index slave_instance::save_state()
{
    saved_state currentState;
    copy_current_state(currentState);
    return store_new_state(currentState);
}


void slave_instance::save_state(state_index stateIndex)
{
    checked_saved_state(stateIndex);
    copy_current_state(savedStates_.at(stateIndex));
}


void slave_instance::restore_state(state_index stateIndex)
{
    const auto& state = checked_saved_state(stateIndex);
    const auto status = fmi2_import_set_fmu_state(handle_, state.fmuState);
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    lifecycleState_ = state.lifecycleState;
    cleanupState_ = cleanup_state::normal;
}


void slave_instance::release_state(state_index stateIndex)
{
    checked_saved_state(stateIndex);
    auto& state = savedStates_.at(stateIndex);
    const auto status = fmi2_import_free_fmu_state(handle_, &state.fmuState);
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    state = {};
    savedStatesFreelist_.push(stateIndex);
}


// IMPORTANT:
// Since the cosim::fmi::v2::slave_instance class may evolve over time, and the
// state that needs to be serialized may change, we need some versioning.
// Increment this number whenever the "exported state" changes form, and
// always consider whether backwards compatibility measures are warranted.
constexpr std::int32_t export_scheme_version = 1;


serialization::node slave_instance::export_state(state_index stateIndex) const
{
    const auto& savedState = checked_saved_state(stateIndex);

    // Check that the FMU supports state serialization
    if (!fmu_->model_description()->capabilities.can_export_state) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not support state serialization");
    }

    // Get size of serialized FMU state
    std::size_t fmuStateSize = 0;
    const auto sizeStatus = fmi2_import_serialized_fmu_state_size(
        handle_, savedState.fmuState, &fmuStateSize);
    if (!accept_status(sizeStatus)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }

    // Serialize FMU state
    auto serializedFMUState = std::vector<std::byte>(fmuStateSize);
    const auto status = fmi2_import_serialize_fmu_state(
        handle_, savedState.fmuState,
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): FMI 2 expects a byte pointer.
        reinterpret_cast<fmi2_byte_t*>(serializedFMUState.data()), fmuStateSize);
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }

    // Create the exported state
    serialization::node exportedState;
    exportedState.put("scheme_version", export_scheme_version);
    exportedState.put("fmu_uuid", fmu_->model_description()->uuid);
    exportedState.put("serialized_fmu_state", serializedFMUState);
    exportedState.put(
        "lifecycle_state",
        static_cast<std::int32_t>(savedState.lifecycleState));
    return exportedState;
}


slave::state_index slave_instance::import_state(
    const serialization::node& exportedState)
{
    saved_state savedState;
    try {
        // First some sanity checks
        const auto schemeVersion = exportedState.get<std::int32_t>("scheme_version");
        if (schemeVersion != export_scheme_version) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ + "' uses an incompatible scheme");
        }
        const auto fmuUUID = std::get<std::string>(
            exportedState.get_child("fmu_uuid").data());
        if (fmuUUID != fmu_->model_description()->uuid) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ + "' was created with a different FMU");
        }
        if (fmi2_import_get_capability(handle_, fmi2_cs_canSerializeFMUstate) == 0U) {
            throw error(
                make_error_code(errc::unsupported_feature),
                instanceName_ + ": FMU does not support state deserialization");
        }

        // Validate wrapper state before asking the FMU to allocate a state.
        const auto serializedLifecycleState =
            exportedState.get<std::int32_t>("lifecycle_state");
        switch (serializedLifecycleState) {
            case static_cast<std::int32_t>(lifecycle_state::instantiated):
                savedState.lifecycleState = lifecycle_state::instantiated;
                break;
            case static_cast<std::int32_t>(lifecycle_state::initialization):
                savedState.lifecycleState = lifecycle_state::initialization;
                break;
            case static_cast<std::int32_t>(lifecycle_state::step):
                savedState.lifecycleState = lifecycle_state::step;
                break;
            case static_cast<std::int32_t>(lifecycle_state::terminated):
                savedState.lifecycleState = lifecycle_state::terminated;
                break;
            default:
                throw error(
                    make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + instanceName_ + "' contains an invalid lifecycle state");
        }

        const auto& serializedFMUState = std::get<std::vector<std::byte>>(
            exportedState.get_child("serialized_fmu_state").data());
        const auto status = fmi2_import_de_serialize_fmu_state(
            handle_,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): FMI 2 expects a byte pointer.
            reinterpret_cast<const fmi2_byte_t*>(serializedFMUState.data()),
            serializedFMUState.size(),
            &savedState.fmuState);
        if (!accept_status(status)) {
            throw error(
                make_error_code(errc::model_error),
                last_log_record(instanceName_).message);
        }
    } catch (const boost::property_tree::ptree_bad_path&) {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ + "' is invalid or corrupt");
    } catch (const std::bad_variant_access&) {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ + "' is invalid or corrupt");
    }
    return store_new_state(savedState);
}


std::shared_ptr<v2::fmu> slave_instance::v2_fmu() const
{
    return fmu_;
}


fmi2_import_t* slave_instance::fmilib_handle() const
{
    return handle_;
}


bool slave_instance::provides_directional_derivatives() const noexcept
{
    return providesDirectionalDerivatives_;
}


void slave_instance::get_directional_derivative(
    gsl::span<const value_reference> unknowns,
    gsl::span<const value_reference> knowns,
    gsl::span<const double> seed,
    gsl::span<double> sensitivity) const
{
    if (!providesDirectionalDerivatives_) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not provide directional derivatives");
    }
    if (lifecycleState_ != lifecycle_state::initialization &&
        lifecycleState_ != lifecycle_state::step &&
        lifecycleState_ != lifecycle_state::terminated) {
        throw error(
            make_error_code(errc::invalid_operation),
            instanceName_ +
                ": GetDirectionalDerivative can only be called in lifecycle states "
                "initialization (1), step (2), or terminated (3); current lifecycle "
                "state: " +
                std::to_string(static_cast<std::int32_t>(lifecycleState_)));
    }
    COSIM_INPUT_CHECK(!unknowns.empty());
    COSIM_INPUT_CHECK(!knowns.empty());
    COSIM_INPUT_CHECK(seed.size() == knowns.size());
    COSIM_INPUT_CHECK(sensitivity.size() == unknowns.size());

    validate_derivative_unknowns(unknowns);
    validate_derivative_knowns(knowns);

    std::vector<double> stagedSensitivity(sensitivity.size());
    const auto status = fmi2_import_get_directional_derivative(
        handle_,
        knowns.data(),
        knowns.size(),
        unknowns.data(),
        unknowns.size(),
        seed.data(),
        stagedSensitivity.data());
    if (!accept_status(status)) {
        auto message =
            instanceName_ + ": GetDirectionalDerivative returned " +
            fmi2_status_to_string(status);
        const auto logMessage = last_log_record(instanceName_).message;
        if (!logMessage.empty()) {
            message += ": " + logMessage;
        }
        throw error(
            make_error_code(errc::model_error),
            message);
    }
    boost::range::copy(
        stagedSensitivity,
        sensitivity.begin());
}


void slave_instance::validate_derivative_unknowns(
    gsl::span<const value_reference> unknowns) const
{
    const auto contains = [](const auto& references, value_reference reference) {
        return std::find(
                   references.begin(),
                   references.end(),
                   reference) != references.end();
    };
    for (const auto reference : unknowns) {
        auto* const variable = fmi2_import_get_variable_by_vr(
            handle_, fmi2_base_type_real, reference);
        const auto variability = variable == nullptr
            ? fmi2_variability_enu_unknown
            : fmi2_import_get_variability(variable);
        const bool eligibleOutput =
            contains(outputReferences_, reference) &&
            (variability == fmi2_variability_enu_continuous ||
                variability == fmi2_variability_enu_discrete);
        const bool validUnknown =
            lifecycleState_ == lifecycle_state::initialization
            ? contains(initialUnknownReferences_, reference)
            : eligibleOutput ||
                contains(derivativeReferences_, reference);
        if (variable == nullptr || !validUnknown) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": directional derivative unknown value reference " +
                    std::to_string(reference) +
                    " is not a mode-appropriate Real ModelStructure entry");
        }
    }
}


void slave_instance::validate_derivative_knowns(
    gsl::span<const value_reference> knowns) const
{
    for (const auto reference : knowns) {
        auto* const variable = fmi2_import_get_variable_by_vr(
            handle_, fmi2_base_type_real, reference);
        bool validKnown = false;
        if (variable != nullptr) {
            const auto causality = fmi2_import_get_causality(variable);
            validKnown =
                causality == fmi2_causality_enu_input ||
                causality == fmi2_causality_enu_independent;
            if (lifecycleState_ == lifecycle_state::initialization) {
                validKnown =
                    validKnown ||
                    fmi2_import_get_initial(variable) ==
                        fmi2_initial_enu_exact;
            } else {
                validKnown =
                    validKnown ||
                    boost::range::find(continuousStateReferences_, reference) !=
                        continuousStateReferences_.end();
            }
        }
        if (!validKnown) {
            throw error(
                make_error_code(errc::model_error),
                instanceName_ + ": directional derivative known value reference " +
                    std::to_string(reference) +
                    " is not a mode-appropriate Real known");
        }
    }
}


const slave_instance::saved_state& slave_instance::checked_saved_state(state_index stateIndex) const
{
    const auto& state = savedStates_.at(stateIndex);
    if (state.fmuState == nullptr) {
        throw std::out_of_range("FMI 2 saved state has been released");
    }
    return state;
}


void slave_instance::copy_current_state(saved_state& state)
{
    if (!fmu_->model_description()->capabilities.can_save_state) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not support state saving");
    }
    const auto status = fmi2_import_get_fmu_state(handle_, &state.fmuState);
    if (!accept_status(status)) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    state.lifecycleState = lifecycleState_;
}


void slave_instance::cache_directional_derivative_metadata()
{
    std::vector<fmi2_import_real_variable_t*> continuousStateVariables;
    const auto cacheList =
        [&continuousStateVariables](
            fmi2_import_variable_list_t* list,
            std::vector<value_reference>& references,
            bool cacheContinuousStates) {
            if (list == nullptr) {
                return;
            }
            const auto freeList = gsl::finally([list] {
                fmi2_import_free_variable_list(list);
            });
            const auto count = fmi2_import_get_variable_list_size(list);
            references.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                auto* const variable = fmi2_import_get_variable(list, i);
                references.push_back(fmi2_import_get_variable_vr(variable));
                if (cacheContinuousStates) {
                    auto* const realVariable =
                        fmi2_import_get_variable_as_real(variable);
                    if (realVariable != nullptr) {
                        auto* const stateVariable =
                            fmi2_import_get_real_variable_derivative_of(
                                realVariable);
                        if (stateVariable != nullptr) {
                            continuousStateVariables.push_back(stateVariable);
                        }
                    }
                }
            }
        };

    cacheList(
        fmi2_import_get_outputs_list(handle_),
        outputReferences_,
        false);
    cacheList(
        fmi2_import_get_derivatives_list(handle_),
        derivativeReferences_,
        true);
    cacheList(
        fmi2_import_get_initial_unknowns_list(handle_),
        initialUnknownReferences_,
        false);

    auto* const variables = fmi2_import_get_variable_list(handle_, 0);
    if (variables == nullptr) {
        return;
    }
    const auto freeVariables = gsl::finally([variables] {
        fmi2_import_free_variable_list(variables);
    });
    const auto variableCount = fmi2_import_get_variable_list_size(variables);
    for (std::size_t i = 0; i < variableCount; ++i) {
        auto* const variable = fmi2_import_get_variable(variables, i);
        auto* const realVariable = fmi2_import_get_variable_as_real(variable);
        if (realVariable != nullptr &&
            boost::range::find(
                continuousStateVariables,
                realVariable) != continuousStateVariables.end()) {
            continuousStateReferences_.push_back(
                fmi2_import_get_variable_vr(variable));
        }
    }
}


slave::state_index slave_instance::store_new_state(saved_state state)
{
    const auto releaseOnFailure = gsl::finally([this, &state] {
        if (state.fmuState != nullptr && !fmu_->fatal_.load()) {
            accept_status(fmi2_import_free_fmu_state(handle_, &state.fmuState));
        }
    });
    if (savedStatesFreelist_.empty()) {
        savedStates_.push_back(state);
        state.fmuState = nullptr;
        return static_cast<state_index>(savedStates_.size() - 1);
    }
    const auto stateIndex = savedStatesFreelist_.front();
    savedStates_.at(stateIndex) = state;
    savedStatesFreelist_.pop();
    state.fmuState = nullptr;
    return stateIndex;
}


} // namespace cosim::fmi::v2
