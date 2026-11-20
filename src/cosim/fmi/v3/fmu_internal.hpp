/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_V3_FMU_INTERNAL_HPP
#define COSIM_FMI_V3_FMU_INTERNAL_HPP

#include "cosim/fmi/fmilib.h"
#include "cosim/fmi/v3/fmu.hpp"

#include <cassert>
#include <condition_variable>


namespace cosim::fmi::v3
{

/**
 *  Owns the bridge between FMI-owned callback threads and an instance.
 *
 *  `acceptingCallbacks` is cleared before an instance is destroyed, then the
 *  destructor waits for `activeCallbacks` to reach zero. This makes the raw
 *  instance pointers safe for the duration of every callback without holding
 *  the instance mutex while user code runs.
 */
struct callback_context
{
    std::string instanceName;
    slave_instance* slaveInstance = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    bool acceptingCallbacks = true;
    std::size_t activeCallbacks = 0;
    std::exception_ptr callbackException;
    mutable std::mutex logMutex;
    mutable std::string lastLogMessage;
    mutable std::atomic_bool instancePoisoned{false};
    std::atomic_bool* sharedFatal = nullptr;
    mutable std::atomic_bool recoveryRequired{false};
    mutable std::atomic_bool terminationOnly{false};
    mutable std::atomic_bool errorTerminated{false};

    void record_status(fmi3_status_t status) const noexcept
    {
        if (status == fmi3_status_fatal) {
            mark_fatal();
        } else if (status == fmi3_status_error) {
            errorTerminated.store(true, std::memory_order_release);
            require_recovery();
        }
    }

    bool error_terminated() const noexcept
    {
        return errorTerminated.load(std::memory_order_acquire);
    }

    // Register an Intermediate Update callback only while the Co-Simulation
    // instance is still alive.
    slave_instance* begin_intermediate_callback()
    {
        std::scoped_lock const lock(mutex);
        if (!acceptingCallbacks || slaveInstance == nullptr) {
            return nullptr;
        }
        ++activeCallbacks;
        return slaveInstance;
    }

    // Wake destruction once the last callback has left user code.
    void end_callback() noexcept
    {
        std::scoped_lock const lock(mutex);
        assert(activeCallbacks > 0);
        --activeCallbacks;
        if (activeCallbacks == 0) {
            condition.notify_all();
        }
    }

    bool is_active_on_this_thread() const noexcept;

    // Preserve the first exception; the owning API rethrows it at its boundary.
    void capture_callback_exception(const std::exception_ptr& exception) noexcept
    {
        poison_instance();
        try {
            std::scoped_lock const lock(mutex);
            if (callbackException == nullptr) {
                callbackException = exception;
            }
        } catch (...) { // NOLINT(bugprone-empty-catch): The poison flag reports failure at the API boundary.
            // The poison flag still makes the failure visible to the caller.
        }
    }

    void rethrow_callback_exception()
    {
        std::exception_ptr exception;
        {
            std::scoped_lock const lock(mutex);
            exception = callbackException;
            callbackException = nullptr;
        }
        if (exception != nullptr) {
            std::rethrow_exception(exception);
        }
    }

    void poison_instance() const noexcept
    {
        instancePoisoned.store(true, std::memory_order_release);
    }

    void mark_fatal() const noexcept
    {
        poison_instance();
        if (sharedFatal != nullptr) {
            sharedFatal->store(true, std::memory_order_release);
        }
        recoveryRequired.store(false, std::memory_order_release);
        terminationOnly.store(false, std::memory_order_release);
    }

    void require_recovery(bool onlyTermination = false) const noexcept
    {
        recoveryRequired.store(true, std::memory_order_release);
        terminationOnly.store(onlyTermination, std::memory_order_release);
    }

    void clear_recovery() const noexcept
    {
        recoveryRequired.store(false, std::memory_order_release);
        terminationOnly.store(false, std::memory_order_release);
        errorTerminated.store(false, std::memory_order_release);
    }

    bool instance_poisoned() const noexcept
    {
        return instancePoisoned.load(std::memory_order_acquire);
    }

    bool fmu_returned_fatal() const noexcept
    {
        return sharedFatal != nullptr && sharedFatal->load(std::memory_order_acquire);
    }

    bool recovery_required() const noexcept
    {
        return recoveryRequired.load(std::memory_order_acquire);
    }

    bool termination_only() const noexcept
    {
        return terminationOnly.load(std::memory_order_acquire);
    }

    // Prevent new callbacks and wait for all FMU-owned callback threads.
    void deactivate()
    {
        std::unique_lock<std::mutex> lock(mutex);
        acceptingCallbacks = false;
        slaveInstance = nullptr;
        condition.wait(lock, [this] { return activeCallbacks == 0; });
    }
};

namespace adapter_detail
{

bool representable_time(double value);

void check_status(
    const callback_context* context,
    const std::string& operation,
    fmi3_status_t status);

enum class operation_access // NOLINT(performance-enum-size): Preserve the existing enum type.
{
    normal,
    diagnostic,
    recovery,
    terminate,
};

void check_available(
    const callback_context* context,
    const std::string& instanceName,
    const char* operation,
    operation_access access = operation_access::normal);

void check_lifecycle(
    bool condition,
    const std::string& instanceName,
    const char* operation);

void check_callback_reentrancy(
    const callback_context* context,
    const std::string& instanceName,
    const char* operation);

const variable_type_definition* find_type_definition(
    const model_description& model,
    const std::string& name);

std::vector<std::uint64_t> resolve_shape(
    const variable_description& variable,
    const std::unordered_map<value_reference, std::uint64_t>&
        structuralParameterValues,
    const std::string& instanceName);

} // namespace adapter_detail
} // namespace cosim::fmi::v3


#endif // COSIM_FMI_V3_FMU_INTERNAL_HPP
