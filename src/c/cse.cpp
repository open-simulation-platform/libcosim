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
#include <mutex>

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
    std::atomic<cse::time_point> startTime;
    std::shared_ptr<cse::slave> slave;
    std::shared_ptr<cse_observer> observer;
    std::atomic<long> currentSteps;
    cse::time_duration stepSize;
    std::thread t;
    std::atomic<bool> shouldRun = false;
    std::atomic<cse_execution_state> state;
    int error_code;
};

cse::time_duration calculate_current_time(cse_execution* execution)
{
    return execution->startTime + execution->currentSteps * execution->stepSize;
}


cse_execution* cse_execution_create(cse_time_point startTime, cse_time_duration stepSize)
{
    try {
        // No exceptions are possible right now, so try...catch and unique_ptr
        // are strictly unnecessary, but this will change soon enough.
        auto execution = std::make_unique<cse_execution>();
        execution->startTime = startTime;
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
        slave->instance = fmu->instantiate_slave();
        // slave address not in use yet. Should be something else than a string.
        slave->address = "local";
        return slave.release();
    } catch (...) {
        handle_current_exception();
        return nullptr;
    }
}

int cse_execution_add_slave(
    cse_execution* execution,
    cse_slave* slave)
{
    try {
        if (execution->slave) {
            throw cse::error(
                make_error_code(cse::errc::unsupported_feature),
                "Only one slave may be added to an execution for the time being");
        }
        auto instance = slave->instance;
        instance->setup(
            "unnamed slave",
            "unnamed execution",
            execution->startTime + execution->currentSteps * execution->stepSize,
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

bool cse_observer_observe(cse_observer* observer, long currentStep);

int cse_execution_step(cse_execution* execution)
{
    try {
        const auto stepOK =
            !execution->slave ||
            execution->slave->do_step(calculate_current_time(execution), execution->stepSize);
        if (!stepOK) {
            set_last_error(CSE_ERRC_STEP_TOO_LONG, "Time step too long");
            return failure;
        }

        execution->currentSteps++;

        const auto observeOK =
            !execution->observer ||
            cse_observer_observe(execution->observer.get(), execution->currentSteps);
        if (!observeOK) {
            set_last_error(CSE_ERRC_UNSPECIFIED, "Observer failed to observe");
            return failure;
        }

        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


int cse_execution_step(cse_execution* execution, size_t numSteps)
{
    execution->state = CSE_EXECUTION_RUNNING;
    for (size_t i = 0; i < numSteps; i++) {
        if (cse_execution_step(execution) != success) {
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
                cse_execution_step(execution, 1);

                // TODO: Add better time synchronization
                std::this_thread::sleep_for(std::chrono::duration<cse_time_duration>(execution->stepSize));
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
        status->current_time = calculate_current_time(execution);
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

struct cse_observer_s
{
    std::map<long, std::vector<double>> realSamples;
    std::vector<int> intValues;
    std::vector<cse::variable_index> realIndexes;
    std::vector<cse::variable_index> intIndexes;
    std::mutex lock;
    std::shared_ptr<cse::slave> slave;
};

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


int cse_observer_slave_get_real(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    double values[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        std::lock_guard<std::mutex> lock(observer->lock);
        if (observer->realSamples.empty()) {
            throw std::out_of_range("no samples available");
        }
        auto lastEntry = observer->realSamples.rbegin();
        for (size_t i = 0; i < nv; i++) {
            auto it = std::find(observer->realIndexes.begin(), observer->realIndexes.end(), variables[i]);
            if (it != observer->realIndexes.end()) {
                size_t valueIndex = it - observer->realIndexes.begin();
                values[i] = lastEntry->second[valueIndex];
            }
        }
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
    long steps[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        std::lock_guard<std::mutex> lock(observer->lock);
        size_t samplesRead = 0;
        size_t valueIndex;
        auto variableIndexIt = std::find(observer->realIndexes.begin(), observer->realIndexes.end(), variableIndex);
        if (variableIndexIt != observer->realIndexes.end()) {
            valueIndex = variableIndexIt - observer->realIndexes.begin();
            auto sampleIt = observer->realSamples.find(fromStep);
            for (samplesRead = 0; samplesRead < nSamples; samplesRead++) {
                if (sampleIt != observer->realSamples.end()) {
                    steps[samplesRead] = sampleIt->first;
                    values[samplesRead] = sampleIt->second[valueIndex];
                    sampleIt++;
                } else {
                    break;
                }
            }
        }
        return samplesRead;

    } catch (...) {
        handle_current_exception();
        return 0;
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

int cse_observer_slave_get_integer(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    int values[])
{
    try {
        if (slave != 0) {
            throw std::out_of_range("Invalid slave index");
        }
        std::lock_guard<std::mutex> lock(observer->lock);
        for (size_t i = 0; i < nv; i++) {
            auto it = std::find(observer->intIndexes.begin(), observer->intIndexes.end(), variables[i]);
            if (it != observer->intIndexes.end()) {
                size_t valueIndex = it - observer->intIndexes.begin();
                values[i] = observer->intValues[valueIndex];
            }
        }
        return success;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


cse_observer* cse_membuffer_observer_create()
{
    auto observer = std::make_unique<cse_observer>();

    return observer.release();
}


int cse_execution_add_observer(
    cse_execution* execution,
    cse_observer* observer)
{
    try {
        if (execution->observer) {
            throw cse::error(
                make_error_code(cse::errc::unsupported_feature),
                "Only one observer may be added to an execution for the time being");
        }

        execution->observer = std::shared_ptr<cse_observer>(observer);

        return /*observer index*/ 0;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}


int cse_observer_add_slave(
    cse_observer* observer,
    cse_slave* slave)
{
    try {
        if (observer->slave) {
            throw cse::error(
                make_error_code(cse::errc::unsupported_feature),
                "Only one slave may be added to an observer for the time being");
        }
        for (cse::variable_description& vd : slave->instance->model_description().variables) {
            if (vd.type == cse::variable_type::real && vd.causality == cse::variable_causality::output) {
                observer->realIndexes.push_back(vd.index);
            }
            if (vd.type == cse::variable_type::integer && vd.causality == cse::variable_causality::output) {
                observer->intIndexes.push_back(vd.index);
            }
        }
        observer->slave = slave->instance;
        observer->intValues.resize(observer->intIndexes.size());
        cse_observer_observe(observer, 0);

        return /*slave index*/ 0;
    } catch (...) {
        handle_current_exception();
        return failure;
    }
}

bool cse_observer_observe(cse_observer* observer, long currentStep)
{
    try {
        if (observer->slave) {
            std::lock_guard<std::mutex> lock(observer->lock);
            observer->realSamples[currentStep].resize(observer->realIndexes.size());
            observer->slave->get_real_variables(
                gsl::make_span(observer->realIndexes),
                gsl::make_span(observer->realSamples[currentStep]));

            observer->slave->get_integer_variables(
                gsl::make_span(observer->intIndexes),
                gsl::make_span(observer->intValues));
        }
        return true;
    } catch (...) {
        handle_current_exception();
        return false;
    }
}

int cse_hello_world(char* buffer, size_t size)
{
    const auto n = cse::hello_world(gsl::make_span(buffer, size - 1));
    buffer[n] = '\0';
    return cse::get_ultimate_answer();
}
