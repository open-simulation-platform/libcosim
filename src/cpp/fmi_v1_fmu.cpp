#include <cse/fmi/v1/fmu.hpp>

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

#include <gsl/gsl_util>

#include "cse/error.hpp"
#include "cse/fmi/fmilib.h"
#include "cse/fmi/glue.hpp"
#include "cse/log/logger.hpp"
#include <cse/exception.hpp>
#include <cse/fmi/importer.hpp>

#ifdef _WIN32
#    include "cse/fmi/windows.hpp"
#endif


namespace cse
{
namespace fmi
{
namespace v1
{

// =============================================================================
// fmu
// =============================================================================

fmu::fmu(
    std::shared_ptr<fmi::importer> importer,
    const std::filesystem::path& fmuDir)
    : importer_{importer}
    , dir_(fmuDir)
    , handle_{fmi1_import_parse_xml(importer->fmilib_handle(), fmuDir.string().c_str())}
{
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            importer->last_error_message());
    }
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
    const auto varList = fmi1_import_get_variable_list(handle_);
    const auto _ = gsl::finally([&]() {
        fmi1_import_free_variable_list(varList);
    });
    const auto varCount = fmi1_import_get_variable_list_size(varList);
    for (unsigned int i = 0; i < varCount; ++i) {
        const auto var = fmi1_import_get_variable(varList, i);
        modelDescription_.variables.push_back(to_variable_description(var));
    }
}


fmu::~fmu()
{
    fmi1_import_free(handle_);
}


fmi::fmi_version fmu::fmi_version() const
{
    return fmi::fmi_version::v1_0;
}


std::shared_ptr<const cse::model_description> fmu::model_description() const
{
    return std::shared_ptr<const cse::model_description>(
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


std::shared_ptr<v1::slave_instance> fmu::instantiate_v1_slave()
{
#ifdef _WIN32
    if (!additionalDllSearchPath_) {
        additionalDllSearchPath_ =
            std::make_unique<detail::additional_path>(fmu_binaries_dir(dir_));
    }
#endif
    prune(instances_);
    const bool isSingleton = !!fmi1_import_get_canBeInstantiatedOnlyOncePerProcess(
        fmi1_import_get_capabilities(handle_));
    if (isSingleton && !instances_.empty()) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "FMU can only be instantiated once");
    }
    auto instance =
        std::shared_ptr<slave_instance>(new slave_instance(shared_from_this()));
    instances_.push_back(instance);
    return instance;
}


std::filesystem::path fmu::directory() const
{
    return dir_;
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
void step_finished_placeholder(fmi1_component_t, fmi1_status_t)
{
    BOOST_LOG_SEV(log::logger(), log::level::debug)
        << "FMU instance completed asynchronous step, "
           "but this feature is currently not supported";
}

struct log_record
{
    log_record() {}
    log_record(fmi1_status_t s, const std::string& m)
        : status{s}
        , message(m)
    {}
    fmi1_status_t status = fmi1_status_ok;
    std::string message;
};
std::unordered_map<std::string, log_record> g_logRecords;
std::mutex g_logMutex;

void log_message(
    fmi1_component_t,
    fmi1_string_t instanceName,
    fmi1_status_t status,
    fmi1_string_t category,
    fmi1_string_t message,
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
    log::level logLevel = log::level::error;
    switch (status) {
        case fmi1_status_ok:
            statusName = "ok";
            logLevel = log::level::trace;
            break;
        case fmi1_status_warning:
            statusName = "warning";
            logLevel = log::level::warning;
            break;
        case fmi1_status_discard:
            // Don't know if this ever happens, but we should at least
            // print a debug message if it does.
            statusName = "discard";
            logLevel = log::level::debug;
            break;
        case fmi1_status_error:
            statusName = "error";
            logLevel = log::level::error;
            break;
        case fmi1_status_fatal:
            statusName = "fatal";
            logLevel = log::level::error;
            break;
        case fmi1_status_pending:
            // Don't know if this ever happens, but we should at least
            // print a debug message if it does.
            statusName = "pending";
            logLevel = log::level::debug;
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


slave_instance::slave_instance(std::shared_ptr<v1::fmu> fmu)
    : fmu_{fmu}
    , handle_{fmi1_import_parse_xml(fmu->importer()->fmilib_handle(), fmu->directory().string().c_str())}
{
    if (handle_ == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            fmu->importer()->last_error_message());
    }

    fmi1_callback_functions_t callbacks;
    callbacks.allocateMemory = std::calloc;
    callbacks.freeMemory = std::free;
    callbacks.logger = log_message;
    callbacks.stepFinished = step_finished_placeholder;

    if (fmi1_import_create_dllfmu(handle_, callbacks, false) != jm_status_success) {
        const auto msg = fmu->importer()->last_error_message();
        fmi1_import_free(handle_);
        throw error(
            make_error_code(errc::dl_load_error),
            fmu->importer()->last_error_message());
    }
}


slave_instance::~slave_instance() noexcept
{
    if (setupComplete_) {
        if (simStarted_) {
            fmi1_import_terminate_slave(handle_);
        }
        fmi1_import_free_slave_instance(handle_);
    }
    fmi1_import_destroy_dllfmu(handle_);
    fmi1_import_free(handle_);
}


void slave_instance::setup(
    std::string_view slaveName,
    std::string_view /*executionName*/,
    time_point startTime,
    time_point stopTime,
    bool /*adaptiveStepSize*/,
    double /*relativeTolerance*/)
{
    assert(!setupComplete_);
    const auto instanceName = std::string(slaveName);
    const auto rc = fmi1_import_instantiate_slave(
        handle_,
        instanceName.c_str(),
        nullptr,
        nullptr,
        0,
        fmi1_false,
        fmi1_false);
    if (rc != jm_status_success) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName).message);
    }
    setupComplete_ = true;

    instanceName_ = std::move(instanceName);
    startTime_ = startTime;
    stopTime_ = stopTime;
}


void slave_instance::start_simulation()
{
    assert(setupComplete_);
    assert(!simStarted_);
    const auto rc = fmi1_import_initialize_slave(
        handle_,
        startTime_,
        stopTime_ != eternity,
        stopTime_);
    if (rc != fmi1_status_ok && rc != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    simStarted_ = true;
}


void slave_instance::end_simulation()
{
    assert(simStarted_);
    const auto rc = fmi1_import_terminate_slave(handle_);
    simStarted_ = false;
    if (rc != fmi1_status_ok && rc != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


bool slave_instance::do_step(time_point currentT, time_duration deltaT)
{
    assert(simStarted_);
    const auto rc = fmi1_import_do_step(handle_, currentT, deltaT, true);
    if (rc == fmi1_status_ok || rc == fmi1_status_warning) {
        return true;
    } else if (rc == fmi1_status_discard) {
        return false;
    } else {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::get_real_variables(
    gsl::span<const variable_index> variables,
    gsl::span<double> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    const auto status = fmi1_import_get_real(
        handle_, variables.data(), variables.size(), values.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::get_integer_variables(
    gsl::span<const variable_index> variables,
    gsl::span<int> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    const auto status = fmi1_import_get_integer(
        handle_, variables.data(), variables.size(), values.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
}


void slave_instance::get_boolean_variables(
    gsl::span<const variable_index> variables,
    gsl::span<bool> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    std::vector<fmi1_boolean_t> fmiValues(values.size());
    const auto status = fmi1_import_get_boolean(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
        throw error(
            make_error_code(errc::model_error),
            last_log_record(instanceName_).message);
    }
    for (int i = 0; i < values.size(); ++i) {
        values[i] = (fmiValues[i] != fmi1_false);
    }
}


void slave_instance::get_string_variables(
    gsl::span<const variable_index> variables,
    gsl::span<std::string> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    std::vector<fmi1_string_t> fmiValues(values.size());
    const auto status = fmi1_import_get_string(
        handle_, variables.data(), variables.size(), fmiValues.data());
    if (status != fmi1_status_ok && status != fmi1_status_warning) {
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
    gsl::span<const variable_index> variables,
    gsl::span<const double> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
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
    gsl::span<const variable_index> variables,
    gsl::span<const int> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
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


void slave_instance::set_boolean_variables(
    gsl::span<const variable_index> variables,
    gsl::span<const bool> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    std::vector<fmi1_boolean_t> fmiValues(values.size());
    for (int i = 0; i < values.size(); ++i) {
        fmiValues[i] =
            static_cast<fmi1_boolean_t>(values[i] ? fmi1_true : fmi1_false);
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
    gsl::span<const variable_index> variables,
    gsl::span<const std::string> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    std::vector<fmi1_string_t> fmiValues(values.size());
    for (int i = 0; i < values.size(); ++i) {
        fmiValues[i] = values[i].c_str();
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


std::shared_ptr<v1::fmu> slave_instance::v1_fmu() const
{
    return fmu_;
}


fmi1_import_t* slave_instance::fmilib_handle() const
{
    return handle_;
}


} // namespace v1
} // namespace fmi
} // namespace cse
