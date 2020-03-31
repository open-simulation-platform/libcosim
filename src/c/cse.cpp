#if defined(_WIN32) && !defined(NOMINMAX)
#    define NOMINMAX
#endif

#include <cse.h>
#include <cse/algorithm.hpp>
#include <cse/cse_config_parser.hpp>
#include <cse/exception.hpp>
#include <cse/execution.hpp>
#include <cse/fmi/fmu.hpp>
#include <cse/fmi/importer.hpp>
#include <cse/log/simple.hpp>
#include <cse/manipulator.hpp>
#include <cse/model.hpp>
#include <cse/observer.hpp>
#include <cse/orchestration.hpp>
#include <cse/ssp/ssp_loader.hpp>

#include <boost/fiber/future.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

namespace
{
constexpr int success = 0;
constexpr int failure = -1;

cse_errc cpp_to_c_error_code(std::error_code ec)
{
    if (ec == cse::errc::bad_file)
        return CSE_ERRC_BAD_FILE;
    else if (ec == cse::errc::unsupported_feature)
        return CSE_ERRC_UNSUPPORTED_FEATURE;
    else if (ec == cse::errc::dl_load_error)
        return CSE_ERRC_DL_LOAD_ERROR;
    else if (ec == cse::errc::model_error)
        return CSE_ERRC_MODEL_ERROR;
    else if (ec == cse::errc::simulation_error)
        return CSE_ERRC_SIMULATION_ERROR;
    else if (ec == cse::errc::zip_error)
        return CSE_ERRC_ZIP_ERROR;
    else if (ec.category() == std::generic_category()) {
        errno = ec.value();
        return CSE_ERRC_ERRNO;
    } else {
        return CSE_ERRC_UNSPECIFIED;
    }
}

// These hold information about the last reported error.
// They should only be set through `set_last_error()` and
// `handle_current_exception()`.
thread_local cse_errc g_lastErrorCode;
thread_local std::string g_lastErrorMessage;

// Sets the last error code and message directly.
void set_last_error(cse_errc ec, std::string message)
{
    g_lastErrorCode = ec;
    g_lastErrorMessage = std::move(message);
}

// Sets the last error code based on an `std::error_code`.
// If a message is specified it is used, otherwise the default
// message from `ec` is used.
void set_last_error(std::error_code ec, std::optional<std::string> message)
{
    set_last_error(
        cpp_to_c_error_code(ec),
        std::move(message).value_or(ec.message()));
}

// To be called from an exception handler (`catch` block).  Stores
// information about the current exception as an error code and message.
void handle_current_exception()
{
    try {
        throw;
    } catch (const cse::error& e) {
        set_last_error(e.code(), e.what());
    } catch (const std::system_error& e) {
        set_last_error(e.code(), e.what());
    } catch (const std::invalid_argument& e) {
        set_last_error(CSE_ERRC_INVALID_ARGUMENT, e.what());
    } catch (const std::out_of_range& e) {
        set_last_error(CSE_ERRC_OUT_OF_RANGE, e.what());
    } catch (const std::exception& e) {
        set_last_error(CSE_ERRC_UNSPECIFIED, e.what());
    } catch (...) {
        set_last_error(
            CSE_ERRC_UNSPECIFIED,
            "An exception of unknown type was thrown");
    }
}

constexpr cse_time_point to_integer_time_point(cse::time_point t)
{
    return t.time_since_epoch().count();
}

constexpr cse::duration to_duration(cse_duration nanos)
{
    return std::chrono::duration<cse::detail::clock::rep, cse::detail::clock::period>(nanos);
}

constexpr cse::time_point to_time_point(cse_time_point nanos)
{
    return cse::time_point(to_duration(nanos));
}

// A wrapper for `std::strncpy()` which ensures that `dest` is always
// null terminated.
void safe_strncpy(char* dest, const char* src, std::size_t count)
{
    std::strncpy(dest, src, count - 1);
    dest[count - 1] = '\0';
}

} // namespace


cse_errc cse_last_error_code()
{
    return g_lastErrorCode;
}

const char* cse_last_error_message()
{
    return g_lastErrorMessage.c_str();
}

struct cse_execution_s
{
    std::unique_ptr<cse::execution> cpp_execution;
    cse::entity_index_maps entity_maps;
    std::thread t;
    boost::fibers::future<bool> simulate_result;
    std::exception_ptr simulate_exception_ptr;
    std::atomic<cse_execution_state> state;
    int error_code;
};

cse_execution* cse_execution_create(cse_time_point startTime, cse_duration stepSize)
{
    try {
        // No exceptions are possible right now, so try...catch and unique_ptr
        // are strictly unnecessary, but this will change soon enough.
        auto execution = std::make_unique<cse_execution>();

        execution->cpp_execution = std::make_unique<cse::execution>(
            to_time_point(startTime),
            std::make_unique<cse::fixed_step_algorithm>(to_duration(stepSize)));
        execution->error_code = CSE_ERRC_SUCCESS;
        execution->state = CSE_EXECUTION_STOPPED;

        return execution.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

cse_execution* cse_config_execution_create(
    const char* configPath,
    bool startTimeDefined,
    cse_time_point startTime)
{
    try {
        auto execution = std::make_unique<cse_execution>();

        auto resolver = cse::default_model_uri_resolver();
        const auto config = cse::load_cse_config(configPath, *resolver);

        execution->cpp_execution = std::make_unique<cse::execution>(
            startTimeDefined ? to_time_point(startTime) : config.start_time,
            std::make_shared<cse::fixed_step_algorithm>(config.step_size));
        execution->entity_maps = cse::inject_system_structure(
            *execution->cpp_execution,
            config.system_structure,
            config.initial_values);

        return execution.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}


cse_execution* cse_ssp_execution_create(
    const char* sspDir,
    bool startTimeDefined,
    cse_time_point startTime)
{
    try {
        auto execution = std::make_unique<cse_execution>();

        cse::ssp_loader loader;
        const auto config = loader.load(sspDir);

        execution->cpp_execution = std::make_unique<cse::execution>(
            startTimeDefined ? to_time_point(startTime) : config.start_time,
            config.algorithm);
        execution->entity_maps = cse::inject_system_structure(
            *execution->cpp_execution,
            config.system_structure,
            config.parameter_sets.at(""));

        return execution.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

cse_execution* cse_ssp_fixed_step_execution_create(
    const char* sspDir,
    bool startTimeDefined,
    cse_time_point startTime,
    cse_duration stepSize)
{
    try {
        auto execution = std::make_unique<cse_execution>();

        cse::ssp_loader loader;
        const auto config = loader.load(sspDir);

        execution->cpp_execution = std::make_unique<cse::execution>(
            startTimeDefined ? to_time_point(startTime) : config.start_time,
            std::make_unique<cse::fixed_step_algorithm>(to_duration(stepSize)));
        execution->entity_maps = cse::inject_system_structure(
            *execution->cpp_execution,
            config.system_structure,
            config.parameter_sets.at(""));

        return execution.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

int cse_execution_destroy(cse_execution* execution)
{
    try {
        if (!execution) return success;
        const auto owned = std::unique_ptr<cse_execution>(execution);
        cse_execution_stop(execution);
        return success;
    } catch (...) {
        execution->state = CSE_EXECUTION_ERROR;
        execution->error_code = CSE_ERRC_UNSPECIFIED;
        handle_current_exception();
        return failure;
    }
}

size_t cse_execution_get_num_slaves(cse_execution* execution)
{
    return execution->entity_maps.simulators.size();
}

int cse_execution_get_slave_infos(cse_execution* execution, cse_slave_info infos[], size_t numSlaves)
{
    try {
        auto ids = execution->entity_maps.simulators;
        size_t slave = 0;
        for (const auto& [name, index] : ids) {
            safe_strncpy(infos[slave].name, name.c_str(), SLAVE_NAME_MAX_SIZE);
            infos[slave].index = index;
            if (++slave >= numSlaves) {
                break;
            }
        }
        return success;
    } catch (...) {
        execution->state = CSE_EXECUTION_ERROR;
        execution->error_code = CSE_ERRC_UNSPECIFIED;
        handle_current_exception();
        return failure;
    }
}

int cse_slave_get_num_variables(cse_execution* execution, cse_slave_index slave)
{
    try {
        return static_cast<int>(execution
                                    ->cpp_execution
                                    ->get_simulator(slave)
                                    ->model_description()
                                    .variables
                                    .size());
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_get_num_modified_variables(cse_execution* execution)
{
    return static_cast<int>(execution->cpp_execution->get_modified_variables().size());
}

cse_variable_variability to_variable_variability(const cse::variable_variability& vv)
{
    switch (vv) {
        case cse::variable_variability::constant:
            return CSE_VARIABLE_VARIABILITY_CONSTANT;
        case cse::variable_variability::continuous:
            return CSE_VARIABLE_VARIABILITY_CONTINUOUS;
        case cse::variable_variability::discrete:
            return CSE_VARIABLE_VARIABILITY_DISCRETE;
        case cse::variable_variability::fixed:
            return CSE_VARIABLE_VARIABILITY_FIXED;
        case cse::variable_variability::tunable:
            return CSE_VARIABLE_VARIABILITY_TUNABLE;
        default:
            throw std::invalid_argument("Invalid variable variability!");
    }
}

cse_variable_causality to_variable_causality(const cse::variable_causality& vc)
{
    switch (vc) {
        case cse::variable_causality::input:
            return CSE_VARIABLE_CAUSALITY_INPUT;
        case cse::variable_causality::output:
            return CSE_VARIABLE_CAUSALITY_OUTPUT;
        case cse::variable_causality::parameter:
            return CSE_VARIABLE_CAUSALITY_PARAMETER;
        case cse::variable_causality::calculated_parameter:
            return CSE_VARIABLE_CAUSALITY_CALCULATEDPARAMETER;
        case cse::variable_causality::local:
            return CSE_VARIABLE_CAUSALITY_LOCAL;
        default:
            throw std::invalid_argument("Invalid variable causality!");
    }
}

cse_variable_type to_c_variable_type(const cse::variable_type& vt)
{
    switch (vt) {
        case cse::variable_type::real:
            return CSE_VARIABLE_TYPE_REAL;
        case cse::variable_type::integer:
            return CSE_VARIABLE_TYPE_INTEGER;
        case cse::variable_type::boolean:
            return CSE_VARIABLE_TYPE_BOOLEAN;
        case cse::variable_type::string:
            return CSE_VARIABLE_TYPE_STRING;
        default:
            throw std::invalid_argument("Invalid variable type!");
    }
}

void translate_variable_description(const cse::variable_description& vd, cse_variable_description& cvd)
{
    safe_strncpy(cvd.name, vd.name.c_str(), SLAVE_NAME_MAX_SIZE);
    cvd.reference = vd.reference;
    cvd.type = to_c_variable_type(vd.type);
    cvd.causality = to_variable_causality(vd.causality);
    cvd.variability = to_variable_variability(vd.variability);
}

int cse_slave_get_variables(cse_execution* execution, cse_slave_index slave, cse_variable_description variables[], size_t numVariables)
{
    try {
        const auto vars = execution
                              ->cpp_execution
                              ->get_simulator(slave)
                              ->model_description()
                              .variables;
        size_t var = 0;
        for (; var < std::min(numVariables, vars.size()); var++) {
            translate_variable_description(vars.at(var), variables[var]);
        }
        return static_cast<int>(var);
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

struct cse_slave_s
{
    std::string address;
    std::string modelName;
    std::string instanceName;
    std::shared_ptr<cse::slave> instance;
};

cse_slave* cse_local_slave_create(const char* fmuPath, const char* instanceName)
{
    try {
        const auto importer = cse::fmi::importer::create();
        const auto fmu = importer->import(fmuPath);
        auto slave = std::make_unique<cse_slave>();
        slave->modelName = fmu->model_description()->name;
        slave->instanceName = std::string(instanceName);
        slave->instance = fmu->instantiate_slave(slave->instanceName);
        // slave address not in use yet. Should be something else than a string.
        slave->address = "local";
        return slave.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

int cse_execution_set_real_initial_value(cse_execution* execution, cse_slave_index slaveIndex, cse_value_reference vr, double value)
{
    try {
        execution->cpp_execution->set_real_initial_value(slaveIndex, vr, value);
    } catch (...) {
        handle_current_exception();
        return failure;
    }
    return success;
}

int cse_execution_set_integer_initial_value(cse_execution* execution, cse_slave_index slaveIndex, cse_value_reference vr, int value)
{
    try {
        execution->cpp_execution->set_integer_initial_value(slaveIndex, vr, value);
    } catch (...) {
        handle_current_exception();
        return failure;
    }
    return success;
}

int cse_execution_set_boolean_initial_value(cse_execution* execution, cse_slave_index slaveIndex, cse_value_reference vr, bool value)
{
    try {
        execution->cpp_execution->set_boolean_initial_value(slaveIndex, vr, value);
    } catch (...) {
        handle_current_exception();
        return failure;
    }
    return success;
}

int cse_execution_set_string_initial_value(cse_execution* execution, cse_slave_index slaveIndex, cse_value_reference vr, char* value)
{
    try {
        execution->cpp_execution->set_string_initial_value(slaveIndex, vr, value);
    } catch (...) {
        handle_current_exception();
        return failure;
    }
    return success;
}

int cse_local_slave_destroy(cse_slave* slave)
{
    try {
        if (!slave) return success;
        const auto owned = std::unique_ptr<cse_slave>(slave);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

cse_slave_index cse_execution_add_slave(
    cse_execution* execution,
    cse_slave* slave)
{
    try {
        auto index = execution->cpp_execution->add_slave(cse::make_background_thread_slave(slave->instance), slave->instanceName);
        execution->entity_maps.simulators[slave->instanceName] = index;
        return index;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

void cse_execution_step(cse_execution* execution)
{
    execution->cpp_execution->step();
}

int cse_execution_step(cse_execution* execution, size_t numSteps)
{
    if (execution->cpp_execution->is_running()) {
        return success;
    } else {
        execution->state = CSE_EXECUTION_RUNNING;
        for (size_t i = 0; i < numSteps; i++) {
            try {
                cse_execution_step(execution);
            } catch (...) {
                handle_current_exception();
                execution->state = CSE_EXECUTION_ERROR;
                return failure;
            }
        }
        execution->state = CSE_EXECUTION_STOPPED;
        return success;
    }
}

int cse_execution_simulate_until(cse_execution* execution, cse_time_point targetTime)
{
    if (execution->cpp_execution->is_running()) {
        set_last_error(CSE_ERRC_ILLEGAL_STATE, "Function 'cse_execution_simulate_until' may not be called while simulation is running!");
        return failure;
    } else {
        execution->state = CSE_EXECUTION_RUNNING;
        try {
            const bool notStopped = execution->cpp_execution->simulate_until(to_time_point(targetTime)).get();
            execution->state = CSE_EXECUTION_STOPPED;
            return notStopped;
        } catch (...) {
            handle_current_exception();
            execution->state = CSE_EXECUTION_ERROR;
            return failure;
        }
    }
}

int cse_execution_start(cse_execution* execution)
{
    if (execution->t.joinable()) {
        return success;
    } else {
        try {
            execution->state = CSE_EXECUTION_RUNNING;
            auto task = boost::fibers::packaged_task<bool()>([execution]() {
                return execution->cpp_execution->simulate_until(std::nullopt).get();
            });
            execution->simulate_result = task.get_future();
            execution->t = std::thread(std::move(task));
            return success;
        } catch (...) {
            handle_current_exception();
            execution->state = CSE_EXECUTION_ERROR;
            return failure;
        }
    }
}

void execution_async_health_check(cse_execution* execution)
{
    if (execution->simulate_result.valid()) {
        const auto status = execution->simulate_result.wait_for(std::chrono::duration<int64_t>());
        if (boost::fibers::future_status::ready == status) {
            if (auto ep = execution->simulate_result.get_exception_ptr()) {
                execution->simulate_exception_ptr = ep;
            }
        }
    }
    if (auto ep = execution->simulate_exception_ptr) {
        std::rethrow_exception(ep);
    }
}

int cse_execution_stop(cse_execution* execution)
{
    try {
        execution->cpp_execution->stop_simulation();
        if (execution->t.joinable()) {
            execution->simulate_result.get();
            execution->t.join();
        }
        execution->state = CSE_EXECUTION_STOPPED;
        return success;
    } catch (...) {
        execution->t.join();
        handle_current_exception();
        execution->state = CSE_EXECUTION_ERROR;
        return failure;
    }
}

int cse_execution_get_status(cse_execution* execution, cse_execution_status* status)
{
    try {
        status->error_code = execution->error_code;
        status->state = execution->state;
        status->current_time = to_integer_time_point(execution->cpp_execution->current_time());
        status->real_time_factor = execution->cpp_execution->get_measured_real_time_factor();
        status->real_time_factor_target = execution->cpp_execution->get_real_time_factor_target();
        status->is_real_time_simulation = execution->cpp_execution->is_real_time_simulation() ? 1 : 0;
        execution_async_health_check(execution);
        return success;
    } catch (...) {
        handle_current_exception();
        execution->error_code = cse_last_error_code();
        execution->state = CSE_EXECUTION_ERROR;
        status->error_code = execution->error_code;
        status->state = execution->state;
        return failure;
    }
}

int cse_execution_enable_real_time_simulation(cse_execution* execution)
{
    try {
        execution->cpp_execution->enable_real_time_simulation();
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_disable_real_time_simulation(cse_execution* execution)
{
    try {
        execution->cpp_execution->disable_real_time_simulation();
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_set_real_time_factor_target(cse_execution* execution, double realTimeFactor)
{
    try {
        execution->cpp_execution->set_real_time_factor_target(realTimeFactor);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

struct cse_observer_s
{
    std::shared_ptr<cse::observer> cpp_observer;
};

int cse_observer_destroy(cse_observer* observer)
{
    try {
        if (!observer) return success;
        const auto owned = std::unique_ptr<cse_observer>(observer);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int connect_variables(
    cse_execution* execution,
    cse::simulator_index outputSimulator,
    cse::value_reference outputVariable,
    cse::simulator_index inputSimulator,
    cse::value_reference inputVariable,
    cse::variable_type type)
{
    try {
        const auto outputId = cse::variable_id{outputSimulator, type, outputVariable};
        const auto inputId = cse::variable_id{inputSimulator, type, inputVariable};
        execution->cpp_execution->connect_variables(outputId, inputId);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_connect_real_variables(
    cse_execution* execution,
    cse_slave_index outputSlaveIndex,
    cse_value_reference outputValueReference,
    cse_slave_index inputSlaveIndex,
    cse_value_reference inputValueReference)
{
    return connect_variables(execution, outputSlaveIndex, outputValueReference, inputSlaveIndex, inputValueReference,
        cse::variable_type::real);
}

int cse_execution_connect_integer_variables(
    cse_execution* execution,
    cse_slave_index outputSlaveIndex,
    cse_value_reference outputValueReference,
    cse_slave_index inputSlaveIndex,
    cse_value_reference inputValueReference)
{
    return connect_variables(execution, outputSlaveIndex, outputValueReference, inputSlaveIndex, inputValueReference,
        cse::variable_type::integer);
}

int cse_observer_slave_get_real(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_value_reference variables[],
    size_t nv,
    double values[])
{
    try {
        const auto obs = std::dynamic_pointer_cast<cse::last_value_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a last_value_observer.");
        }
        obs->get_real(slave, gsl::make_span(variables, nv), gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_observer_slave_get_integer(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_value_reference variables[],
    size_t nv,
    int values[])
{
    try {
        const auto obs = std::dynamic_pointer_cast<cse::last_value_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a last_value_observer.");
        }
        obs->get_integer(slave, gsl::make_span(variables, nv), gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_observer_slave_get_boolean(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_value_reference variables[],
    size_t nv,
    bool values[])
{
    try {
        const auto obs = std::dynamic_pointer_cast<cse::last_value_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a last_value_observer.");
        }
        obs->get_boolean(slave, gsl::make_span(variables, nv), gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

// This holds string variable values.
// Must only be used with `cse_observer_slave_get_string()`.
thread_local std::vector<std::string> g_stringVariableBuffer;

int cse_observer_slave_get_string(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_value_reference variables[],
    size_t nv,
    const char* values[])
{
    try {
        const auto obs = std::dynamic_pointer_cast<cse::last_value_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a last_value_observer.");
        }
        g_stringVariableBuffer.clear();
        g_stringVariableBuffer.resize(nv);
        obs->get_string(slave, gsl::make_span(variables, nv), gsl::span<std::string>(g_stringVariableBuffer));
        for (size_t i = 0; i < nv; i++) {
            values[i] = g_stringVariableBuffer.at(i).c_str();
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int64_t cse_observer_slave_get_real_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_value_reference valueReference,
    cse_step_number fromStep,
    size_t nSamples,
    double values[],
    cse_step_number steps[],
    cse_time_point times[])
{
    try {
        std::vector<cse::time_point> timePoints(nSamples);
        const auto obs = std::dynamic_pointer_cast<cse::time_series_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        size_t samplesRead = obs->get_real_samples(slave, valueReference, fromStep,
            gsl::make_span(values, nSamples),
            gsl::make_span(steps, nSamples), timePoints);
        for (size_t i = 0; i < samplesRead; ++i) {
            times[i] = to_integer_time_point(timePoints[i]);
        }
        return samplesRead;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int64_t cse_observer_slave_get_real_synchronized_series(
    cse_observer* observer,
    cse_slave_index slave1,
    cse_value_reference valueReference1,
    cse_slave_index slave2,
    cse_value_reference valueReference2,
    cse_step_number fromStep,
    size_t nSamples,
    double values1[],
    double values2[])
{
    try {
        std::vector<cse::time_point> timePoints(nSamples);
        const auto obs = std::dynamic_pointer_cast<cse::time_series_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        size_t samplesRead = obs->get_synchronized_real_series(
            slave1,
            valueReference1,
            slave2,
            valueReference2,
            fromStep,
            gsl::make_span(values1, nSamples),
            gsl::make_span(values2, nSamples));
        return samplesRead;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int64_t cse_observer_slave_get_integer_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_value_reference valueReference,
    cse_step_number fromStep,
    size_t nSamples,
    int values[],
    cse_step_number steps[],
    cse_time_point times[])
{
    try {
        std::vector<cse::time_point> timePoints(nSamples);
        const auto obs = std::dynamic_pointer_cast<cse::time_series_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        size_t samplesRead = obs->get_integer_samples(slave, valueReference, fromStep,
            gsl::make_span(values, nSamples),
            gsl::make_span(steps, nSamples), timePoints);
        for (size_t i = 0; i < samplesRead; ++i) {
            times[i] = to_integer_time_point(timePoints[i]);
        }
        return samplesRead;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_observer_get_step_numbers_for_duration(
    cse_observer* observer,
    cse_slave_index slave,
    cse_duration duration,
    cse_step_number steps[])
{
    try {
        const auto obs = std::dynamic_pointer_cast<cse::time_series_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        obs->get_step_numbers(slave, to_duration(duration), gsl::make_span(steps, 2));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_observer_get_step_numbers(
    cse_observer* observer,
    cse_slave_index slave,
    cse_time_point begin,
    cse_time_point end,
    cse_step_number steps[])
{
    try {
        const auto obs = std::dynamic_pointer_cast<cse::time_series_provider>(observer->cpp_observer);
        if (!obs) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        obs->get_step_numbers(slave, to_time_point(begin), to_time_point(end),
            gsl::make_span(steps, 2));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

cse_observer* cse_last_value_observer_create()
{
    auto observer = std::make_unique<cse_observer>();
    observer->cpp_observer = std::make_shared<cse::last_value_observer>();
    return observer.release();
}

cse_observer* cse_file_observer_create(const char* logDir)
{
    auto observer = std::make_unique<cse_observer>();
    auto logPath = boost::filesystem::path(logDir);
    observer->cpp_observer = std::make_shared<cse::file_observer>(logPath);
    return observer.release();
}

cse_observer* cse_file_observer_create_from_cfg(const char* logDir, const char* cfgPath)
{
    auto observer = std::make_unique<cse_observer>();
    auto boostLogDir = boost::filesystem::path(logDir);
    auto boostCfgPath = boost::filesystem::path(cfgPath);
    observer->cpp_observer = std::make_shared<cse::file_observer>(boostLogDir, boostCfgPath);
    return observer.release();
}

cse_observer* cse_time_series_observer_create()
{
    auto observer = std::make_unique<cse_observer>();
    observer->cpp_observer = std::make_shared<cse::time_series_observer>();
    return observer.release();
}

cse_observer* cse_buffered_time_series_observer_create(size_t bufferSize)
{
    auto observer = std::make_unique<cse_observer>();
    observer->cpp_observer = std::make_shared<cse::time_series_observer>(bufferSize);
    return observer.release();
}

cse::variable_type to_cpp_variable_type(cse_variable_type type)
{
    switch (type) {
        case CSE_VARIABLE_TYPE_REAL:
            return cse::variable_type::real;
        case CSE_VARIABLE_TYPE_INTEGER:
            return cse::variable_type::integer;
        case CSE_VARIABLE_TYPE_BOOLEAN:
            return cse::variable_type::boolean;
        case CSE_VARIABLE_TYPE_STRING:
            return cse::variable_type::string;
        default:
            throw std::invalid_argument("Variable type not supported");
    }
}

int cse_observer_start_observing(cse_observer* observer, cse_slave_index slave, cse_variable_type type, cse_value_reference reference)
{
    try {
        const auto timeSeriesObserver = std::dynamic_pointer_cast<cse::time_series_observer>(observer->cpp_observer);
        if (!timeSeriesObserver) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        const auto variableId = cse::variable_id{slave, to_cpp_variable_type(type), reference};
        timeSeriesObserver->start_observing(variableId);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_observer_stop_observing(cse_observer* observer, cse_slave_index slave, cse_variable_type type, cse_value_reference reference)
{
    try {
        const auto timeSeriesObserver = std::dynamic_pointer_cast<cse::time_series_observer>(observer->cpp_observer);
        if (!timeSeriesObserver) {
            throw std::invalid_argument("Invalid observer! The provided observer must be a time_series_observer.");
        }
        const auto variableId = cse::variable_id{slave, to_cpp_variable_type(type), reference};
        timeSeriesObserver->stop_observing(variableId);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_add_observer(
    cse_execution* execution,
    cse_observer* observer)
{
    try {
        execution->cpp_execution->add_observer(observer->cpp_observer);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

struct cse_manipulator_s
{
    std::shared_ptr<cse::manipulator> cpp_manipulator;
};

cse_manipulator* cse_override_manipulator_create()
{
    auto manipulator = std::make_unique<cse_manipulator>();
    manipulator->cpp_manipulator = std::make_shared<cse::override_manipulator>();
    return manipulator.release();
}

int cse_manipulator_destroy(cse_manipulator* manipulator)
{
    try {
        if (!manipulator) return success;
        const auto owned = std::unique_ptr<cse_manipulator>(manipulator);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_add_manipulator(
    cse_execution* execution,
    cse_manipulator* manipulator)
{
    try {
        execution->cpp_execution->add_manipulator(manipulator->cpp_manipulator);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_manipulator_slave_set_real(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_value_reference variables[],
    size_t nv,
    const double values[])
{
    try {
        const auto man = std::dynamic_pointer_cast<cse::override_manipulator>(manipulator->cpp_manipulator);
        if (!man) {
            throw std::invalid_argument("Invalid manipulator!");
        }
        for (size_t i = 0; i < nv; i++) {
            man->override_real_variable(slaveIndex, variables[i], values[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_manipulator_slave_set_integer(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_value_reference variables[],
    size_t nv,
    const int values[])
{
    try {
        const auto man = std::dynamic_pointer_cast<cse::override_manipulator>(manipulator->cpp_manipulator);
        if (!man) {
            throw std::invalid_argument("Invalid manipulator!");
        }
        for (size_t i = 0; i < nv; i++) {
            man->override_integer_variable(slaveIndex, variables[i], values[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_manipulator_slave_set_boolean(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_value_reference variables[],
    size_t nv,
    const bool values[])
{
    try {
        const auto man = std::dynamic_pointer_cast<cse::override_manipulator>(manipulator->cpp_manipulator);
        if (!man) {
            throw std::invalid_argument("Invalid manipulator!");
        }
        for (size_t i = 0; i < nv; i++) {
            man->override_boolean_variable(slaveIndex, variables[i], values[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_manipulator_slave_set_string(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_value_reference variables[],
    size_t nv,
    const char* values[])
{
    try {
        const auto man = std::dynamic_pointer_cast<cse::override_manipulator>(manipulator->cpp_manipulator);
        if (!man) {
            throw std::invalid_argument("Invalid manipulator!");
        }
        for (size_t i = 0; i < nv; i++) {
            man->override_string_variable(slaveIndex, variables[i], values[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_manipulator_slave_reset(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    cse_variable_type type,
    const cse_value_reference variables[],
    size_t nv)
{
    try {
        const auto man = std::dynamic_pointer_cast<cse::override_manipulator>(manipulator->cpp_manipulator);
        if (!man) {
            throw std::invalid_argument("Invalid manipulator!");
        }
        cse::variable_type vt = to_cpp_variable_type(type);
        for (size_t i = 0; i < nv; i++) {
            man->reset_variable(slaveIndex, vt, variables[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

cse_manipulator* cse_scenario_manager_create()
{
    auto manipulator = std::make_unique<cse_manipulator>();
    manipulator->cpp_manipulator = std::make_shared<cse::scenario_manager>();
    return manipulator.release();
}

int cse_execution_load_scenario(
    cse_execution* execution,
    cse_manipulator* manipulator,
    const char* scenarioFile)
{
    try {
        auto time = execution->cpp_execution->current_time();
        const auto manager = std::dynamic_pointer_cast<cse::scenario_manager>(manipulator->cpp_manipulator);
        if (!manager) {
            throw std::invalid_argument("Invalid manipulator! The provided manipulator must be a scenario_manager.");
        }
        manager->load_scenario(scenarioFile, time);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_scenario_is_running(cse_manipulator* manipulator)
{
    try {
        const auto manager = std::dynamic_pointer_cast<cse::scenario_manager>(manipulator->cpp_manipulator);
        if (!manager) {
            throw std::invalid_argument("Invalid manipulator! The provided manipulator must be a scenario_manager.");
        }
        return manager->is_scenario_running();
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_scenario_abort(cse_manipulator* manipulator)
{
    try {
        const auto manager = std::dynamic_pointer_cast<cse::scenario_manager>(manipulator->cpp_manipulator);
        if (!manager) {
            throw std::invalid_argument("Invalid manipulator! The provided manipulator must be a scenario_manager.");
        }
        manager->abort_scenario();
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_get_modified_variables(cse_execution* execution, cse_variable_id ids[], size_t numVariables)
{
    try {
        auto modified_vars = execution->cpp_execution->get_modified_variables();
        size_t counter = 0;

        if (!modified_vars.empty()) {
            for (; counter < std::min(numVariables, modified_vars.size()); counter++) {
                ids[counter].slave_index = modified_vars[counter].simulator;
                ids[counter].type = to_c_variable_type(modified_vars[counter].type);
                ids[counter].value_reference = modified_vars[counter].reference;
            }
        }

        return static_cast<int>(counter);
    } catch (...) {
        execution->state = CSE_EXECUTION_ERROR;
        execution->error_code = CSE_ERRC_UNSPECIFIED;
        handle_current_exception();
        return failure;
    }
}


int cse_log_setup_simple_console_logging()
{
    try {
        cse::log::setup_simple_console_logging();
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


void cse_log_set_output_level(cse_log_severity_level level)
{
    switch (level) {
        case CSE_LOG_SEVERITY_TRACE:
            cse::log::set_global_output_level(cse::log::trace);
            break;
        case CSE_LOG_SEVERITY_DEBUG:
            cse::log::set_global_output_level(cse::log::debug);
            break;
        case CSE_LOG_SEVERITY_INFO:
            cse::log::set_global_output_level(cse::log::info);
            break;
        case CSE_LOG_SEVERITY_WARNING:
            cse::log::set_global_output_level(cse::log::warning);
            break;
        case CSE_LOG_SEVERITY_ERROR:
            cse::log::set_global_output_level(cse::log::error);
            break;
        case CSE_LOG_SEVERITY_FATAL:
            cse::log::set_global_output_level(cse::log::fatal);
            break;
        default:
            assert(false);
    }
}
