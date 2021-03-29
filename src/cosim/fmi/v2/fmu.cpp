/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/v2/fmu.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/fmi/fmilib.h"
#include "cosim/fmi/glue.hpp"
#include "cosim/fmi/importer.hpp"
#include "cosim/fmi/windows.hpp"
#include "cosim/log/logger.hpp"

#include <gsl/gsl_util>

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace cosim
{
namespace fmi
{
namespace v2
{

// =============================================================================
// fmu
// =============================================================================

namespace
{
void step_finished_placeholder(fmi2_component_environment_t, fmi2_status_t)
{
    BOOST_LOG_SEV(log::logger(), log::debug)
        << "FMU instance completed asynchronous step, "
           "but this feature is currently not supported";
}

struct log_record
{
    log_record() { }
    log_record(fmi2_status_t s, const std::string& m)
        : status{s}
        , message(m)
    { }
    fmi2_status_t status = fmi2_status_ok;
    std::string message;
};
std::unordered_map<std::string, log_record> g_logRecords;
std::mutex g_logMutex;

void log_message(
    fmi2_component_environment_t,
    fmi2_string_t instanceName,
    fmi2_status_t status,
    fmi2_string_t category,
    fmi2_string_t message,
    ...)
{
    std::va_list args;
        va_start(args, message);
    const auto msgLength = std::vsnprintf(nullptr, 0, message, args);
        va_end(args);
    auto msgBuffer = std::vector<char>(msgLength + 1);
        va_start(args, message);
    std::vsnprintf(msgBuffer.data(), msgBuffer.size(), message, args);
        va_end(args);
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
        << "[FMI status=" << statusName << ", category=" << category << "] "
        << msgBuffer.data();

    g_logMutex.lock();
    g_logRecords[instanceName] =
        log_record{status, std::string(msgBuffer.data())};
    g_logMutex.unlock();
}

log_record last_log_record(const std::string& instanceName)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    const auto it = g_logRecords.find(instanceName);
    if (it == g_logRecords.end()) {
        return log_record{};
    } else {
        // Note the use of c_str() here, to force the string to be copied.
        // The C++ standard now disallows copy-on-write, but some compilers
        // still use it, which could lead to problems in multithreaded
        // programs.
        return log_record{
            it->second.status,
            std::string(it->second.message.c_str())};
    }
}
} // namespace

fmu::fmu(
    std::shared_ptr<fmi::importer> importer,
    std::unique_ptr<file_cache::directory_ro> fmuDir)
    : importer_{importer}
    , dir_(std::move(fmuDir))
    , handle_{fmi2_import_parse_xml(importer->fmilib_handle(), dir_->path().string().c_str(), nullptr)}
{
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            importer->last_error_message());
    }
    const auto fmuKind = fmi2_import_get_fmu_kind(handle_);
    if (!(fmuKind & fmi2_fmu_kind_cs)) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Not a co-simulation FMU");
    }


    fmi2_callback_functions_t callbacks;
    callbacks.allocateMemory = std::calloc;
    callbacks.freeMemory = std::free;
    callbacks.logger = log_message;
    callbacks.stepFinished = step_finished_placeholder;
    callbacks.componentEnvironment = nullptr;

    if (fmi2_import_create_dllfmu(fmilib_handle(), fmi2_fmu_kind_cs, &callbacks) != jm_status_success) {
        const auto msg = importer_->last_error_message();
        fmi2_import_free(fmilib_handle());
        throw error(
            make_error_code(errc::dl_load_error),
            importer_->last_error_message());
    }

    modelDescription_.name = fmi2_import_get_model_name(handle_);
    modelDescription_.uuid = fmi2_import_get_GUID(handle_);
    modelDescription_.description = fmi2_import_get_description(handle_);
    modelDescription_.author = fmi2_import_get_author(handle_);
    modelDescription_.version = fmi2_import_get_model_version(handle_);
    const auto varList = fmi2_import_get_variable_list(handle_, 0);
    const auto _ = gsl::finally([&]() {
        fmi2_import_free_variable_list(varList);
    });
    const auto varCount = fmi2_import_get_variable_list_size(varList);
    for (unsigned int i = 0; i < varCount; ++i) {
        const auto var = fmi2_import_get_variable(varList, i);
        const auto vd = to_variable_description(var);
        if (variable_type::enumeration != vd.type) {
            modelDescription_.variables.push_back(vd);
        } else {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "FMI 2.0 Enumeration variable type not supported, variable with name "
                << vd.name << " will be ignored";
        }
    }
}


fmu::~fmu()
{
    fmi2_import_destroy_dllfmu(fmilib_handle());
    fmi2_import_free(handle_);
}


fmi::fmi_version fmu::fmi_version() const
{
    return fmi::fmi_version::v2_0;
}


std::shared_ptr<const cosim::model_description> fmu::model_description() const
{
    return std::shared_ptr<const cosim::model_description>(
        shared_from_this(),
        &modelDescription_);
}


std::shared_ptr<fmi::importer> fmu::importer() const
{
    return importer_;
}


namespace
{
void prune(std::vector<std::weak_ptr<slave_instance>>& instances)
{
    auto newEnd = std::remove_if(
        begin(instances),
        end(instances),
        [](const std::weak_ptr<slave_instance>& wp) { return wp.expired(); });
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
    const bool isSingleton = !!fmi2_import_get_capability(
        handle_,
        fmi2_cs_canBeInstantiatedOnlyOncePerProcess);
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


boost::filesystem::path fmu::directory() const
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

slave_instance::slave_instance(
    std::shared_ptr<v2::fmu> fmu,
    std::string_view instanceName)
    : fmu_{fmu}
    , instanceName_(instanceName)
{
    assert(!instanceName.empty());

    const auto rc = fmi2_import_instantiate(
        fmilib_handle(),
        instanceName_.c_str(),
        fmi2_cosimulation,
        nullptr, // fmuResourceLocation
        fmi2_false); // visible
    if (rc != jm_status_success) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


slave_instance::~slave_instance() noexcept
{
    if (simStarted_) {
        fmi2_import_terminate(fmilib_handle());
    }
    fmi2_import_free_instance(fmilib_handle());
}


void slave_instance::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    assert(!setupComplete_);
    const auto rcs = fmi2_import_setup_experiment(
        fmilib_handle(),
        relativeTolerance ? fmi2_true : fmi2_false,
        relativeTolerance ? *relativeTolerance : 0.0,
        to_double_time_point(startTime),
        stopTime ? fmi2_true : fmi2_false,
        stopTime ? to_double_time_point(*stopTime) : 0.0);
    if (rcs != fmi2_status_ok && rcs != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }

    const auto rce = fmi2_import_enter_initialization_mode(fmilib_handle());
    if (rce != fmi2_status_ok && rce != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }

    setupComplete_ = true;
}


void slave_instance::start_simulation()
{
    assert(setupComplete_);
    assert(!simStarted_);
    const auto rc = fmi2_import_exit_initialization_mode(fmilib_handle());
    if (rc != fmi2_status_ok && rc != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    simStarted_ = true;
}


void slave_instance::end_simulation()
{
    assert(simStarted_);
    const auto rc = fmi2_import_terminate(fmilib_handle());
    simStarted_ = false;
    if (rc != fmi2_status_ok && rc != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


step_result slave_instance::do_step(time_point currentT, duration deltaT)
{
    assert(simStarted_);
    const auto rc = fmi2_import_do_step(
        fmilib_handle(),
        to_double_time_point(currentT),
        to_double_duration(deltaT, currentT),
        fmi2_true);
    if (rc == fmi2_status_ok || rc == fmi2_status_warning) {
        return step_result::complete;
    } else if (rc == fmi2_status_discard) {
        return step_result::failed;
    } else if (rc == fmi2_status_pending) {
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
    if (variables.empty()) return;
    const auto status = fmi2_import_get_real(
        fmilib_handle(), variables.data(), variables.size(), values.data());
    if (status != fmi2_status_ok && status != fmi2_status_warning) {
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
    if (variables.empty()) return;
    const auto status = fmi2_import_get_integer(
        fmilib_handle(), variables.data(), variables.size(), values.data());
    if (status != fmi2_status_ok && status != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::get_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<bool> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    std::vector<fmi2_boolean_t> fmiValues(values.size());
    const auto status = fmi2_import_get_boolean(
        fmilib_handle(), variables.data(), variables.size(), fmiValues.data());
    if (status != fmi2_status_ok && status != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    for (int i = 0; i < values.size(); ++i) {
        values[i] = (fmiValues[i] != fmi2_false);
    }
}


void slave_instance::get_string_variables(
    gsl::span<const value_reference> variables,
    gsl::span<std::string> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    std::vector<fmi2_string_t> fmiValues(values.size());
    const auto status = fmi2_import_get_string(
        fmilib_handle(), variables.data(), variables.size(), fmiValues.data());
    if (status != fmi2_status_ok && status != fmi2_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    for (int i = 0; i < values.size(); ++i) {
        values[i] = (fmiValues[i] == nullptr) ? std::string()
                                              : std::string(fmiValues[i]);
    }
}


void slave_instance::set_real_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const double> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    const auto status = fmi2_import_set_real(
        fmilib_handle(), variables.data(), variables.size(), values.data());
    if (status == fmi2_status_ok || status == fmi2_status_warning) {
        return;
    } else if (status == fmi2_status_discard) {
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
    if (variables.empty()) return;
    const auto status = fmi2_import_set_integer(
        fmilib_handle(), variables.data(), variables.size(), values.data());
    if (status == fmi2_status_ok || status == fmi2_status_warning) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::set_boolean_variables(
    gsl::span<const value_reference> variables,
    gsl::span<const bool> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    std::vector<fmi2_boolean_t> fmiValues(values.size());
    for (int i = 0; i < values.size(); ++i) {
        fmiValues[i] =
            static_cast<fmi2_boolean_t>(values[i] ? fmi2_true : fmi2_false);
    }
    const auto status = fmi2_import_set_boolean(
        fmilib_handle(), variables.data(), variables.size(), fmiValues.data());
    if (status == fmi2_status_ok || status == fmi2_status_warning) {
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
    if (variables.empty()) return;
    std::vector<fmi2_string_t> fmiValues(values.size());
    for (int i = 0; i < values.size(); ++i) {
        fmiValues[i] = values[i].c_str();
    }
    const auto status = fmi2_import_set_string(
        fmilib_handle(), variables.data(), variables.size(), fmiValues.data());
    if (status == fmi2_status_ok || status == fmi2_status_warning) {
        return;
    } else if (status == fmi2_status_discard) {
        throw nonfatal_bad_value(last_log_record(instanceName_).message);
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


std::shared_ptr<v2::fmu> slave_instance::v2_fmu() const
{
    return fmu_;
}


fmi2_import_t* slave_instance::fmilib_handle() const
{
    return fmu_->fmilib_handle();
}


} // namespace v2
} // namespace fmi
} // namespace cosim
