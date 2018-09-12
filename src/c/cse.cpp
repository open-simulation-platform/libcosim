#include <cse.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <cse/exception.hpp>
#include <cse/fmi/fmu.hpp>
#include <cse/fmi/importer.hpp>

#include <cse/hello_world.hpp>
#include <iostream>

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
    cse::time_duration currentTime;
    std::shared_ptr<cse::slave> slave;
    std::atomic<long> currentSteps;
    cse::time_duration stepSize;
    std::thread t;
    std::atomic<bool> shouldRun = false;
    std::atomic<cse_execution_state> state;
    int error_code;
};


cse_execution* cse_execution_create(cse_time_point startTime, cse_time_duration stepSize)
{
    try {
        // No exceptions are possible right now, so try...catch and unique_ptr
        // are strictly unnecessary, but this will change soon enough.
        auto execution = std::make_unique<cse_execution>();
        execution->currentTime = startTime;
        execution->stepSize = stepSize;
        execution->error_code = CSE_ERRC_SUCCESS;
        execution->state = CSE_EXECUTION_STOPPED;
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
        if (owned->slave) {
            owned->slave->end_simulation();
        }
        return success;
    } catch (...) {
        execution->state = CSE_EXECUTION_ERROR;
        execution->error_code = CSE_ERRC_UNSPECIFIED;
        handle_current_exception();
        return failure;
    }
}

int cse_execution_add_slave( // replaces cse_execution_add_slave_from_fmu()
    cse_execution* execution,
    cse_address* address,
    const char* name)
{
    try {
        if (execution->slave) {
            throw cse::error(
                make_error_code(cse::errc::unsupported_feature),
                "Only one slave may be added to an execution for the time being");
        }
        const auto importer = cse::fmi::importer::create();
        const auto fmu = importer->import(name);

        //address must be given to slave instance.
        //ignore unused variable for now
        (void)address;
        auto instance = fmu->instantiate_slave();
        instance->setup(
            "unnamed slave",
            "unnamed execution",
            execution->currentTime,
            cse::eternity,
            false,
            0.0);
        instance->start_simulation();
        execution->slave = instance;
        return /*slave index*/ 0;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


cse_slave_index cse_execution_add_slave_from_fmu(
    cse_execution* execution,
    const char* fmuPath)
{
    try {
        if (execution->slave) {
            throw cse::error(
                make_error_code(cse::errc::unsupported_feature),
                "Only one slave may be added to an execution for the time being");
        }
        const auto importer = cse::fmi::importer::create();
        const auto fmu = importer->import(fmuPath);
        auto instance = fmu->instantiate_slave();
        instance->setup(
            "unnamed slave",
            "unnamed execution",
            execution->currentTime,
            cse::eternity,
            false,
            0.0);
        instance->start_simulation();

        execution->slave = instance;
        return /*slave index*/ 0;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


int cse_execution_step(cse_execution* execution)
{
    try {
        const auto stepOK =
            !execution->slave ||
            execution->slave->do_step(execution->currentTime, execution->stepSize);
        if (!stepOK) {
            set_last_error(CSE_ERRC_STEP_TOO_LONG, "Time step too long");
            return failure;
        }
        execution->currentTime += execution->stepSize;
        execution->currentSteps++;
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


int cse_execution_step(cse_execution* execution, size_t numSteps)
{
    execution->state = CSE_EXECUTION_RUNNING;
    for (size_t i = 0 ; i < numSteps; i++)
    {
        if(cse_execution_step(execution) != success) {
            return failure;
        }
    }
    return success;
}

int cse_execution_start(cse_execution* execution)
{
    try {
        execution->shouldRun = true;
        execution->t = std::thread([execution]() {
            execution->state = CSE_EXECUTION_RUNNING;
            while (execution->shouldRun) {
                // TODO: Should be wrapped with some lock to prevent race conditions!
                cse_execution_step(execution, 1);

                // TODO: Add better time synchronization
                std::this_thread::sleep_for(std::chrono::duration<cse_time_duration>(execution->stepSize));
                std::cout << "Stepping!" << std::endl;
            }
        });

        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_stop(cse_execution* execution)
{
    try {
        execution->shouldRun = false;
        execution->t.join();
        execution->state = CSE_EXECUTION_STOPPED;
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_get_status(cse_execution* execution, cse_execution_status* status)
{
    try {
        status->error_code = execution->error_code;
        status->state = execution->state;
        status->current_time = execution->currentSteps*execution->stepSize;
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_slave_set_real(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    const double values[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        execution->slave->set_real_variables(
            gsl::make_span(variables, nv),
            gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_slave_get_real(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    double values[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        execution->slave->get_real_variables(
            gsl::make_span(variables, nv),
            gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_slave_set_integer(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    const int values[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        execution->slave->set_integer_variables(
            gsl::make_span(variables, nv),
            gsl::make_span(values, nv));
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

int cse_execution_slave_get_integer(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    int values[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        execution->slave->get_integer_variables(
            gsl::make_span(variables, nv),
            gsl::make_span(values, nv));
        return success;
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
