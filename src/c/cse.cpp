#include <cse.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <cse/exception.hpp>
#include <cse/fmi/fmu.hpp>
#include <cse/fmi/importer.hpp>
#include <cse/log.hpp>

#include <cse/algorithm.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer.hpp>

#include <cse/hello_world.hpp>
#include <cse/ssp_parser.hpp>

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
    std::thread t;
    std::atomic<cse_execution_state> state;
    int error_code;
};

cse_execution* cse_execution_create(cse_time_point startTime, cse_duration stepSize)
{
    try {
        // No exceptions are possible right now, so try...catch and unique_ptr
        // are strictly unnecessary, but this will change soon enough.
        cse::log::set_global_output_level(cse::log::level::info);

        auto execution = std::make_unique<cse_execution>();

        execution->cpp_execution = std::make_unique<cse::execution>(
            cse::to_time_point(startTime),
            std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize, startTime)));
        execution->cpp_execution->enable_real_time_simulation();
        execution->error_code = CSE_ERRC_SUCCESS;
        execution->state = CSE_EXECUTION_STOPPED;

        return execution.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

cse_execution* cse_ssp_execution_create(const char* sspDir, cse_time_point startTime)
{
    try {
        auto execution = std::make_unique<cse_execution>();
        execution->cpp_execution = std::make_unique<cse::execution>(
            cse::load_ssp(sspDir, cse::to_time_point(startTime)));
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

struct cse_slave_s
{
    std::string address;
    std::shared_ptr<cse::slave> instance;
};

cse_slave* cse_local_slave_create(const char* fmuPath)
{
    try {
        const auto importer = cse::fmi::importer::create();
        const auto fmu = importer->import(fmuPath);
        auto slave = std::make_unique<cse_slave>();
        slave->instance = fmu->instantiate_slave("unnamed slave");
        // slave address not in use yet. Should be something else than a string.
        slave->address = "local";
        return slave.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

cse_slave_index cse_execution_add_slave(
    cse_execution* execution,
    cse_slave* slave)
{
    try {
        auto index = execution->cpp_execution->add_slave(cse::make_pseudo_async(slave->instance), "unnamed slave");
        return index;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

void cse_execution_step(cse_execution* execution)
{
    execution->cpp_execution->step(std::nullopt);
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

int cse_execution_start(cse_execution* execution)
{
    if (execution->t.joinable()) {
        return success;
    } else {
        try {
            execution->state = CSE_EXECUTION_RUNNING;
            execution->t = std::thread([execution]() {
                auto future = execution->cpp_execution->simulate_until(std::nullopt);
                future.get();
            });
            return success;
        } catch (...) {
            handle_current_exception();
            execution->state = CSE_EXECUTION_ERROR;
            return failure;
        }
    }
}

int cse_execution_stop(cse_execution* execution)
{
    try {
        execution->cpp_execution->stop_simulation();
        if (execution->t.joinable()) {
            execution->t.join();
        }
        execution->state = CSE_EXECUTION_STOPPED;
        return success;
    } catch (...) {
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
        status->current_time = cse::to_double_time_point(execution->cpp_execution->current_time());
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

struct cse_observer_s
{
    std::shared_ptr<cse::membuffer_observer> cpp_observer;
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

int cse_execution_slave_set_real(
    cse_execution* execution,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const double values[])
{
    try {
        const auto sim = execution->cpp_execution->get_simulator(slaveIndex);
        for (size_t i = 0; i < nv; i++) {
            sim->expose_for_setting(cse::variable_type::real, variables[i]);
            sim->set_real(variables[i], values[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_slave_set_integer(
    cse_execution* execution,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const int values[])
{
    try {
        const auto sim = execution->cpp_execution->get_simulator(slaveIndex);
        for (size_t i = 0; i < nv; i++) {
            sim->expose_for_setting(cse::variable_type::integer, variables[i]);
            sim->set_integer(variables[i], values[i]);
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int connect_variables(
    cse_execution* execution,
    cse::simulator_index outputSimulator,
    cse::variable_index outputVariable,
    cse::simulator_index inputSimulator,
    cse::variable_index inputVariable,
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
    cse_variable_index outputVariableIndex,
    cse_slave_index inputSlaveIndex,
    cse_variable_index inputVariableIndex)
{
    return connect_variables(execution, outputSlaveIndex, outputVariableIndex, inputSlaveIndex, inputVariableIndex, cse::variable_type::real);
}

int cse_execution_connect_integer_variables(
    cse_execution* execution,
    cse_slave_index outputSlaveIndex,
    cse_variable_index outputVariableIndex,
    cse_slave_index inputSlaveIndex,
    cse_variable_index inputVariableIndex)
{
    return connect_variables(execution, outputSlaveIndex, outputVariableIndex, inputSlaveIndex, inputVariableIndex, cse::variable_type::integer);
}

int cse_observer_slave_get_real(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    double values[])
{
    try {
        observer->cpp_observer->get_real(slave, gsl::make_span(variables, nv), gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_observer_slave_get_integer(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    int values[])
{
    try {
        observer->cpp_observer->get_integer(slave, gsl::make_span(variables, nv), gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

size_t cse_observer_slave_get_real_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_variable_index variableIndex,
    long fromStep,
    size_t nSamples,
    double values[],
    long long steps[])
{
    return observer->cpp_observer->get_real_samples(slave, variableIndex, fromStep, gsl::make_span(values, nSamples), gsl::make_span(steps, nSamples));
}

size_t cse_observer_slave_get_integer_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_variable_index variableIndex,
    long fromStep,
    size_t nSamples,
    int values[],
    long long steps[])
{
    return observer->cpp_observer->get_integer_samples(slave, variableIndex, fromStep, gsl::make_span(values, nSamples), gsl::make_span(steps, nSamples));
}

cse_observer* cse_membuffer_observer_create()
{
    auto observer = std::make_unique<cse_observer>();
    observer->cpp_observer = std::make_shared<cse::membuffer_observer>();

    return observer.release();
}

cse_observer_index cse_execution_add_observer(
    cse_execution* execution,
    cse_observer* observer)
{
    try {
        return execution->cpp_execution->add_observer(observer->cpp_observer);
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_hello_world(char* buffer, size_t size)
{
    const auto n = cse::hello_world(gsl::make_span(buffer, size - 1));
    buffer[n] = '\0';
    return cse::get_ultimate_answer();
}
