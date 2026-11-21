/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/v1/fmu.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/file_cache.hpp"
#include "cosim/fmi/enumeration.hpp"
#include "cosim/fmi/fmilib.h" // IWYU pragma: keep
#include "cosim/fmi/fmu.hpp"
#include "cosim/fmi/glue.hpp"
#include "cosim/fmi/importer.hpp"
#include "cosim/fmi/value_validation.hpp"
#include "cosim/fmi/windows.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/scalar_value_dispatch.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <gsl/span>
#include <gsl/span_ext>
#include <gsl/util>

#include <FMI/fmi_import_context.h>
#include <FMI1/fmi1_enums.h>
#include <FMI1/fmi1_functions.h>
#include <FMI1/fmi1_import.h>
#include <FMI1/fmi1_import_capabilities.h>
#include <FMI1/fmi1_import_capi.h>
#include <FMI1/fmi1_import_variable_list.h>
#include <FMI1/fmi1_types.h>
#include <FMI1/fmiPlatformTypes.h>
#include <JM/jm_types.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>


namespace cosim::fmi::v1
{

namespace
{

void warn_about_omitted_variables(
    const filesystem::path& path,
    const model_description& model)
{
    try {
        boost::property_tree::ptree xml;
        boost::property_tree::read_xml(path.string(), xml);
        const auto variables = xml.get_child_optional("fmiModelDescription.ModelVariables");
        if (!variables) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "Cannot check FMI 1.0 variable omissions: ModelVariables is missing";
            return;
        }

        std::unordered_map<std::string, std::size_t> imported;
        for (const auto& variable : model.variables) {
            ++imported[variable.name];
        }
        for (const auto& entry : *variables) {
            if (entry.first != "ScalarVariable") {
                continue;
            }
            const auto name = entry.second.get_optional<std::string>("<xmlattr>.name");
            if (!name || name->empty()) {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << "Cannot check an FMI 1.0 variable omission: variable name is missing";
                continue;
            }
            const auto found = imported.find(*name);
            if (found != imported.end() && found->second != 0) {
                --found->second;
            } else {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << "FMI Library did not import FMI 1.0 variable '" << *name
                    << "'; it is unavailable in the imported model";
            }
        }
    } catch (const boost::property_tree::ptree_error& exception) {
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Cannot check FMI 1.0 variable omissions: " << exception.what();
    }
}

void validate_enumeration_variable(
    const variable_description& description, const cosim::model_description& modelDescription)
{
    if (description.alias_kind == variable_alias_kind::negated_alias) {
        throw error(make_error_code(errc::unsupported_feature),
            "FMI 1.0 negated Enumeration alias '" + description.name +
                "' cannot be represented by its value reference");
    }
    const auto* definition = description.declared_type
        ? detail::find_type_definition(modelDescription, *description.declared_type)
        : nullptr;
    if (definition == nullptr ||
        definition->type != variable_type::enumeration ||
        definition->enumeration_items.empty()) {
        throw error(make_error_code(errc::bad_file),
            "FMI 1.0 Enumeration variable '" + description.name +
                "' has an invalid declared type");
    }
    if (description.start) {
        const auto start = std::get<enumeration_value>(*description.start);
        const auto item = boost::range::find_if(
            definition->enumeration_items,
            [start](const auto& candidate) {
                return candidate.value == start.value;
            });
        if (item == definition->enumeration_items.end() ||
            (description.min && start.value < std::get<enumeration_value>(*description.min).value) ||
            (description.max && start.value > std::get<enumeration_value>(*description.max).value)) {
            throw error(make_error_code(errc::bad_file),
                "FMI 1.0 Enumeration variable '" + description.name +
                    "' has an invalid start value");
        }
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
    , handle_{fmi1_import_parse_xml(importer_->fmilib_handle(), dir_->path().string().c_str())}
{
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            importer_->last_error_message());
    }
    bool initialized = false;
    const auto cleanup = gsl::finally([&] {
        if (!initialized) {
            fmi1_import_free(handle_);
        }
    });
    const auto fmuKind = fmi1_import_get_fmu_kind(handle_);
    if (fmuKind != fmi1_fmu_kind_enu_cs_standalone &&
        fmuKind != fmi1_fmu_kind_enu_cs_tool) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Not a co-simulation FMU");
    }

    modelDescription_.name = fmi1_import_get_model_name(handle_);
    modelDescription_.uuid = fmi1_import_get_GUID(handle_);
    modelDescription_.description = fmi1_import_get_description(handle_);
    modelDescription_.author = fmi1_import_get_author(handle_);
    modelDescription_.version = fmi1_import_get_model_version(handle_);
    auto* const capabilities = fmi1_import_get_capabilities(handle_);
    if (capabilities == nullptr) {
        throw error(make_error_code(errc::bad_file),
            "FMI 1.0 Co-Simulation capabilities are unavailable");
    }
    modelDescription_.capabilities.can_handle_variable_step =
        !(fmi1_import_get_canHandleVariableCommunicationStepSize(capabilities) == 0);
    modelDescription_.type_definitions = to_type_definitions(handle_);
    modelDescription_.unit_definitions = to_unit_definitions(handle_);
    auto* const varList = fmi1_import_get_variable_list(handle_);
    const auto freeVariableList = gsl::finally([&] {
        fmi1_import_free_variable_list(varList);
    });
    const auto varCount = fmi1_import_get_variable_list_size(varList);
    for (unsigned int i = 0; i < varCount; ++i) {
        auto* const var = fmi1_import_get_variable(varList, i);
        const auto description = to_variable_description(var);
        if (description.type == variable_type::enumeration) {
            validate_enumeration_variable(description, modelDescription_);
        }
        modelDescription_.variables.push_back(description);
    }
    warn_about_omitted_variables(dir_->path() / "modelDescription.xml", modelDescription_);
    read_fmi1_output_dependencies(modelDescription_, dir_->path() / "modelDescription.xml");
    initialized = true;
}


fmu::~fmu()
{
    fmi1_import_free(handle_);
}


fmi::fmi_version fmu::fmi_version() const
{
    return fmi::fmi_version::v1_0;
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


std::shared_ptr<v1::slave_instance> fmu::instantiate_v1_slave(
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
    const bool isSingleton = !(fmi1_import_get_canBeInstantiatedOnlyOncePerProcess(
                                   fmi1_import_get_capabilities(handle_)) == 0);
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


fmi1_import_t* fmu::fmilib_handle() const
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
    log_record(fmi1_status_t logStatus, std::string logMessage)
        : status{logStatus}
        , message(std::move(logMessage))
    { }
    fmi1_status_t status = fmi1_status_ok;
    std::string message;
};
std::unordered_map<std::string, log_record> g_logRecords; // NOLINT(bugprone-throwing-static-initialization,cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables): FMI callbacks share records across instances.
std::mutex g_logMutex; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): Synchronizes the shared callback records.

// NOLINTNEXTLINE(cert-dcl50-cpp,modernize-avoid-variadic-functions): FMI 1 fixes the callback signature.
void log_message(
    fmi1_component_t /*unused*/,
#ifndef LIBCOSIM_NO_FMI_LOGGING
    fmi1_string_t instanceName,
    fmi1_status_t status,
    fmi1_string_t category, // NOLINT(bugprone-easily-swappable-parameters): FMI 1 fixes the callback parameter order.
    fmi1_string_t message,
#else
    fmi1_string_t,
    fmi1_status_t,
    fmi1_string_t,
    fmi1_string_t,
#endif
    ...)
{
#ifndef LIBCOSIM_NO_FMI_LOGGING
    if (message == nullptr) {
        BOOST_LOG_SEV(log::logger(), log::error) << "FMI 1.0 log message is null";
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): FMI 1 supplies a variadic format.
    std::va_list args{};
    va_start(args, message);
    const auto msgLength = std::vsnprintf(nullptr, 0, message, args);
    va_end(args);
    if (msgLength < 0) {
        BOOST_LOG_SEV(log::logger(), log::error) << "Could not format FMI 1.0 log message";
        return;
    }
    auto msgBuffer = std::vector<char>(static_cast<std::size_t>(msgLength) + 1);
    va_start(args, message);
    const auto written = std::vsnprintf(msgBuffer.data(), msgBuffer.size(), message, args);
    va_end(args);
    if (written != msgLength) {
        BOOST_LOG_SEV(log::logger(), log::error) << "Could not format FMI 1.0 log message";
        return;
    }
    assert(msgBuffer.back() == '\0');

    std::string statusName = "unknown";
    log::severity_level logLevel = log::error;
    switch (status) {
        case fmi1_status_ok:
            statusName = "ok";
            logLevel = log::trace;
            break;
        case fmi1_status_warning:
            statusName = "warning";
            logLevel = log::warning;
            break;
        case fmi1_status_discard:
            // Don't know if this ever happens, but we should at least
            // print a debug message if it does.
            statusName = "discard";
            logLevel = log::debug;
            break;
        case fmi1_status_error:
            statusName = "error";
            logLevel = log::error;
            break;
        case fmi1_status_fatal:
            statusName = "fatal";
            logLevel = log::error;
            break;
        case fmi1_status_pending:
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

    g_logMutex.lock();
    g_logRecords[instanceName] =
        log_record{status, std::string(msgBuffer.data())};
    g_logMutex.unlock();
#endif
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
} // namespace


// NOTE: We have to re-parse the model description XML every time we
// instantiate a new slave because of a shortcoming in FMI Library.
// (In brief, fmi1_import_create_dllfmu() and fmi1_import_instantiate_slave()
// both store their results in the fmi1_import_t object created by
// fmi1_import_parse_xml().)
slave_instance::slave_instance(
    std::shared_ptr<v1::fmu> fmu,
    std::string_view instanceName)
    : fmu_{std::move(fmu)}
    , handle_{fmi1_import_parse_xml(fmu_->importer()->fmilib_handle(), fmu_->directory().string().c_str())}
    , instanceName_(instanceName)
{
    assert(!instanceName.empty());
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            fmu_->importer()->last_error_message());
    }

    fmi1_callback_functions_t callbacks;
    callbacks.allocateMemory = std::calloc;
    callbacks.freeMemory = std::free;
    callbacks.logger = log_message;
    callbacks.stepFinished = nullptr;

    if (fmi1_import_create_dllfmu(handle_, callbacks, 0) != jm_status_success) {
        static_cast<void>(fmu_->importer()->last_error_message());
        fmi1_import_free(handle_);
        throw error(
            make_error_code(errc::dl_load_error),
            fmu_->importer()->last_error_message());
    }

    const auto status = fmi1_import_instantiate_slave(
        handle_,
        instanceName_.c_str(),
        nullptr, // fmuLocation
        nullptr, // mimeType
        0, // timeout
        fmi1_false, // visible
        fmi1_false); // interactive
    if (status != jm_status_success) {
        fmi1_import_destroy_dllfmu(handle_);
        fmi1_import_free(handle_);
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


slave_instance::~slave_instance() noexcept
{
    if (simStarted_) {
        fmi1_import_terminate_slave(handle_);
    }
    fmi1_import_free_slave_instance(handle_);
    fmi1_import_destroy_dllfmu(handle_);
    fmi1_import_free(handle_);
}


void slave_instance::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> /*relativeTolerance*/)
{
    startTime_ = startTime;
    stopTime_ = stopTime;
}


void slave_instance::start_simulation()
{
    assert(!simStarted_);
    const auto status = fmi1_import_initialize_slave(
        handle_,
        to_double_time_point(startTime_),
        static_cast<fmi1_boolean_t>(stopTime_.has_value()),
        stopTime_ ? to_double_time_point(*stopTime_) : 0.0);
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    simStarted_ = true;
}


void slave_instance::end_simulation()
{
    assert(simStarted_);
    const auto status = fmi1_import_terminate_slave(handle_);
    simStarted_ = false;
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


step_result slave_instance::do_step(time_point currentT, duration deltaT)
{
    assert(simStarted_);
    const auto status = fmi1_import_do_step(
        handle_,
        to_double_time_point(currentT),
        to_double_duration(deltaT, currentT),
        1);
    if (status == fmi1_status_ok || status == fmi1_status_warning) {
        return step_result::complete;
    } else if (status == fmi1_status_discard) {
        return step_result::failed;
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
    const auto status = fmi1_import_get_real(
        handle_, variables.data(), variables.size(), values.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
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
    const auto status = fmi1_import_get_integer(
        handle_, variables.data(), variables.size(), values.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
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
    const auto& model = *fmu_->model_description();
    for (const auto reference : variables) {
        if (detail::find_variable_description(
                model, variable_type::enumeration, reference) == nullptr) {
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": value reference " + std::to_string(reference) +
                    " is not an Enumeration variable");
        }
    }
    std::vector<fmi1_integer_t> fmiValues(values.size());
    const auto status = fmi1_import_get_integer(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    std::vector<enumeration_value> staged;
    staged.reserve(values.size());
    auto reference = variables.begin();
    for (const auto fmiValue : fmiValues) {
        const auto value = enumeration_value{fmiValue};
        for (const auto& variable : model.variables) {
            if (variable.type == variable_type::enumeration &&
                variable.reference == *reference) {
                detail::validate_enumeration_value(model, variable, value, instanceName_);
            }
        }
        staged.push_back(value);
        ++reference;
    }
    boost::range::copy(staged, values.begin());
}


void slave_instance::get_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<bool> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }
    std::vector<fmi1_boolean_t> fmiValues(values.size());
    const auto status = fmi1_import_get_boolean(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    auto source = fmiValues.begin();
    for (auto& value : values) {
        value = (*source != fmi1_false);
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
    std::vector<fmi1_string_t> fmiValues(values.size());
    const auto status = fmi1_import_get_string(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
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
    const auto status = fmi1_import_set_real(
        handle_, variables.data(), variables.size(), values.data());
    if (status == fmi1_status_ok || status == fmi1_status_warning) {
        return;
    } else if (status == fmi1_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
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
    const auto status = fmi1_import_set_integer(
        handle_, variables.data(), variables.size(), values.data());
    if (status == fmi1_status_ok || status == fmi1_status_warning) {
        return;
    } else if (status == fmi1_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
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
    const auto& model = *fmu_->model_description();
    std::vector<fmi1_integer_t> fmiValues;
    fmiValues.reserve(values.size());
    auto reference = variables.begin();
    for (const auto value : values) {
        detail::validate_legacy_enumeration_assignment<fmi1_integer_t>(
            model, *reference, value, instanceName_, "1.0");
        fmiValues.push_back(static_cast<fmi1_integer_t>(value.value));
        ++reference;
    }
    const auto status = fmi1_import_set_integer(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status == fmi1_status_ok || status == fmi1_status_warning) {
        return;
    } else if (status == fmi1_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
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
    std::vector<fmi1_boolean_t> fmiValues(values.size());
    auto target = fmiValues.begin();
    for (const auto value : values) {
        *target = static_cast<fmi1_boolean_t>(value ? fmi1_true : fmi1_false);
        ++target;
    }
    const auto status = fmi1_import_set_boolean(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status == fmi1_status_ok || status == fmi1_status_warning) {
        return;
    } else if (status == fmi1_status_discard) {
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
    std::vector<fmi1_string_t> fmiValues(values.size());
    auto target = fmiValues.begin();
    for (const auto& value : values) {
        *target = value.c_str();
        ++target;
    }
    const auto status = fmi1_import_set_string(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status == fmi1_status_ok || status == fmi1_status_warning) {
        return;
    } else if (status == fmi1_status_discard) {
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
        if (variable.type == variable_type::enumeration) {
            enumeration_value value;
            get_enumeration_variables(
                gsl::make_span(&variable.reference, 1),
                gsl::make_span(&value, 1));
            result.emplace_back(enumeration_values{{value}});
        } else {
            auto scalar = slave::get_variable_values(gsl::make_span(&variable, 1));
            result.push_back(std::move(scalar.front()));
        }
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
            detail::validate_legacy_enumeration_assignment<fmi1_integer_t>(
                *fmu_->model_description(), reference, value, instanceName_, "1.0");
        });
    std::size_t index = 0;
    for (const auto variable : variables) {
        const auto& value = normalized.at(index++);
        if (variable.type == variable_type::enumeration) {
            if (value.type() != variable.type || !value.is_scalar()) {
                throw std::invalid_argument("Enumeration assignment type or shape mismatch");
            }
            const auto item = std::get<enumeration_values>(value.values()).values.at(0);
            set_enumeration_variables(
                gsl::make_span(&variable.reference, 1),
                gsl::make_span(&item, 1));
        } else {
            ::cosim::detail::set_legacy_scalar_value(*this, variable, value);
        }
    }
}


slave::state_index slave_instance::save_state()
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Getting and setting state not supported in FMI 1.0");
}


void slave_instance::save_state(state_index /*stateIndex*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Getting and setting state not supported in FMI 1.0");
}


void slave_instance::restore_state(state_index /*stateIndex*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Getting and setting state not supported in FMI 1.0");
}


void slave_instance::release_state(state_index /*stateIndex*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Getting and setting state not supported in FMI 1.0");
}


serialization::node slave_instance::export_state(state_index /*stateIndex*/) const
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Serializing and deserializing state not supported in FMI 1.0");
}


slave::state_index slave_instance::import_state(const serialization::node& /*exportedState*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Serializing and deserializing state not supported in FMI 1.0");
}


std::shared_ptr<v1::fmu> slave_instance::v1_fmu() const
{
    return fmu_;
}


fmi1_import_t* slave_instance::fmilib_handle() const
{
    return handle_;
}


} // namespace cosim::fmi::v1
