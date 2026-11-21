/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/algorithm/fixed_step_algorithm.hpp"

#include "clock_router.hpp"
#include "event_router.hpp"
#include "step_batch.hpp"

#include "cosim/algorithm/algorithm.hpp"
#include "cosim/algorithm/connection_validation.hpp"
#include "cosim/algorithm/simulator.hpp"
#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/execution.hpp"
#include "cosim/function/function.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/thread_pool.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/range/algorithm/count_if.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/range/numeric.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace cosim
{
namespace
{

int calculate_decimation_factor(
    std::string_view name,
    duration baseStepSize,
    duration simulatorStepSize)
{
    if (simulatorStepSize == duration::zero()) {
        return 1;
    }
    const auto result = std::div(simulatorStepSize.count(), baseStepSize.count());
    const int factor = std::max<int>(1, static_cast<int>(result.quot));
    if (result.rem > 0 || result.quot < 1) {
        const duration actualStepSize = baseStepSize * factor;
        const auto startTime = time_point();
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Effective step size for " << name
            << " will be " << to_double_duration(actualStepSize, startTime) << " s"
            << " instead of configured value "
            << to_double_duration(simulatorStepSize, startTime) << " s";
    }
    return factor;
}

} // namespace


class fixed_step_algorithm::impl
{
public:
    explicit impl(duration baseStepSize, std::optional<unsigned int> workerThreadCount)
        : baseStepSize_(baseStepSize)
        , pool_(std::min(workerThreadCount.value_or(max_threads_), max_threads_))
    {
        COSIM_INPUT_CHECK(baseStepSize.count() > 0);
        clockRouter_.set_event_budget(eventRouter_.budget());
    }

    explicit impl(fixed_step_algorithm_params params, std::optional<unsigned int> workerThreadCount)
        : impl(params.stepSize, workerThreadCount)
    {
        eventRouter_.set_max_event_iterations(params.max_event_iterations);
        set_event_value_tolerance(params.event_value_tolerance);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(simulator_index index, simulator* sim, duration stepSizeHint)
    {
        assert(simulators_.count(index) == 0);
        const auto description = std::make_shared<const model_description>(sim->model_description());
        simulators_[index].sim = sim;
        simulators_[index].capabilities = description->capabilities;
        simulators_[index].decimationFactor =
            calculate_decimation_factor(sim->name(), baseStepSize_, stepSizeHint);
        clockRouter_.add_simulator(index, sim, description);
        eventRouter_.add_simulator(index, sim, description);
    }

    void remove_simulator(simulator_index index)
    {
        if (const auto found = simulators_.find(index); found != simulators_.end()) {
            release_checkpoint(found->second);
        }
        clockRouter_.remove_simulator(index);
        eventRouter_.remove_simulator(index);
        simulators_.erase(index);
        disconnect_simulator_variables(index);
    }

    void add_function(function_index index, function* fun)
    {
        assert(functions_.count(index) == 0);
        functions_[index].fun = fun;
        eventRouter_.add_function(index, fun);
    }

    void connect_variables(variable_id output, variable_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        if (output.type == variable_type::clock ||
            input.type == variable_type::clock) {
            clockRouter_.connect_clock(output, input);
            return;
        }
        auto& sourceSimInfo = simulators_.at(output.simulator);
        const auto& targetSimInfo = simulators_.at(input.simulator);
        sourceSimInfo.sim->expose_for_getting(output.type, output.reference);
        targetSimInfo.sim->expose_for_setting(input.type, input.reference);
        const bool clocked = clockRouter_.connect_data(output, input);
        if (!clocked) {
            eventRouter_.connect_data(output, input);
        }
        sourceSimInfo.outgoingSimConnections.push_back({output, input, clocked});
    }

    void connect_variables(variable_id output, function_io_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        auto& simInfo = simulators_.at(output.simulator);
        simInfo.sim->expose_for_getting(output.type, output.reference);
        eventRouter_.connect_function(output, input);
        simInfo.outgoingFunConnections.push_back({output, input});
    }

    void connect_variables(function_io_id output, variable_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        auto& funInfo = functions_.at(output.function);
        const auto& simInfo = simulators_.at(input.simulator);
        simInfo.sim->expose_for_setting(input.type, input.reference);
        eventRouter_.connect_function(output, input);
        funInfo.outgoingSimConnections.push_back({output, input});
        update_function_decimation_factor(funInfo);
    }

    void connect_variables(function_io_id output, function_io_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        auto& funInfo = functions_.at(output.function);
        eventRouter_.connect_function(output, input);
        funInfo.outgoingFunConnections.push_back({output, input});
    }

    void disconnect_variable(variable_id input)
    {
        eventRouter_.disconnect(input);
        clockRouter_.disconnect(input);
        for (auto& simulator : simulators_) {
            auto& conns = simulator.second.outgoingSimConnections;
            const auto found = boost::range::find_if(
                conns,
                [input](const auto& connection) { return connection.target == input; });
            if (found != conns.end()) {
                conns.erase(found);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
    }

    void disconnect_variable(function_io_id input)
    {
        eventRouter_.disconnect(input);
        for (auto& simulator : simulators_) {
            auto& conns = simulator.second.outgoingFunConnections;
            const auto found = boost::range::find_if(
                conns,
                [input](const auto& connection) { return connection.target == input; });
            if (found != conns.end()) {
                conns.erase(found);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
        for (auto& function : functions_) {
            auto& conns = function.second.outgoingFunConnections;
            const auto found = boost::range::find_if(
                conns,
                [input](const auto& connection) { return connection.target == input; });
            if (found != conns.end()) {
                conns.erase(found);
                return;
            }
        }
    }

    void setup(time_point startTime, std::optional<time_point> stopTime)
    {
        startTime_ = startTime;
        stopTime_ = stopTime;
    }

    void initialize()
    {
        if (initializationFailed_) {
            throw error(make_error_code(errc::simulation_error),
                "The algorithm cannot continue after failed initialization; recreate the execution");
        }
        try {
            initialize_impl();
        } catch (...) {
            initializationFailed_ = true;
            throw;
        }
    }

    void initialize_impl()
    {
        initialized_ = true;
        gridMode_ = false;
        eventRouter_.warn_about_mixed_capability_connections(clockRouter_);
        for (const auto& entry : simulators_) {
            auto* simulator = entry.second.sim;
            pool_.submit([this, simulator] {
                simulator->set_max_event_iterations(eventRouter_.budget().limit());
                simulator->setup(startTime_, stopTime_, std::nullopt);
            });
        }
        pool_.wait_for_tasks_to_finish();

        // Run N iterations of the simulators' and functions' step/calculation
        // procedures, where N is the number of simulators in the system,
        // to propagate initial values.
        for (std::size_t i = 0; i < simulators_.size() + functions_.size(); ++i) {
            for (const auto& entry : simulators_) {
                auto* simulator = entry.second.sim;
                pool_.submit([simulator] {
                    simulator->do_iteration();
                });
            }
            pool_.wait_for_tasks_to_finish();
            calculate_and_transfer(true);
        }


        for (const auto& entry : simulators_) {
            auto* simulator = entry.second.sim;
            pool_.submit([simulator] {
                simulator->start_simulation();
            });
        }
        pool_.wait_for_tasks_to_finish();
        if (boost::algorithm::any_of(simulators_, [](const auto& entry) {
                return entry.second.sim->get_event_handling_status().termination_requested;
            })) {
            throw error(make_error_code(errc::simulation_error),
                "A simulator requested termination during initialization");
        }
        eventRouter_.initialize(startTime_, baseStepSize_, clockRouter_);
        transferTime_ = startTime_;
        calculate_and_transfer(false);
        warn_about_unrewindable_event_simulators();
    }

    algorithm::macro_step_result do_step_with_info(time_point currentT)
    {
        check_step_usable(clockRouter_, initializationFailed_, recoveryRequired_);
        clockRouter_.validate_pending_intervals(currentT);
        try {
            return do_step_with_info_impl(currentT);
        } catch (...) {
            recoveryRequired_ = true;
            throw;
        }
    }

    void validate_decimation() const
    {
        for (const auto& entry : simulators_) {
            if (entry.second.decimationFactor != 1 &&
                entry.second.sim->get_clock_controller() != nullptr) {
                throw error(make_error_code(errc::unsupported_feature),
                    entry.second.sim->name() +
                        ": a simulator with the FMI 3 Clock capability cannot use "
                        "simulator step-size decimation");
            }
        }
        if (boost::algorithm::any_of(simulators_, [](const auto& entry) {
                return entry.second.sim->get_event_controller() == nullptr &&
                    entry.second.sim->get_event_handling_status().next_event_time.has_value();
            })) {
            throw error(make_error_code(errc::unsupported_feature),
                "Time-event scheduling by simulators without Event Mode cannot be "
                "combined with simulator step-size decimation");
        }
    }

    struct normal_step_results
    {
        std::unordered_set<simulator_index> finished;
        bool earlyReturn = false;
        bool terminationRequested = false;
        std::unordered_set<simulator_index> eventSources;
        std::optional<time_point> earlyReturnEndpoint;
        std::size_t activeSimulatorCount = 0;
    };

    void run_normal_steps(step_batch& batch, time_point currentT,
        duration requestedStepSize, normal_step_results& result)
    {
        for (auto& entry : simulators_) {
            auto* info = &entry.second;
            if (stepCounter_ % info->decimationFactor == 0) {
                ++result.activeSimulatorCount;
                batch.submit(*info->sim, info->stepInfo, currentT,
                    requestedStepSize * info->decimationFactor);
            }
        }
        ++stepCounter_;
        for (auto& [idx, info] : simulators_) {
            if (stepCounter_ % info.decimationFactor == 0) {
                result.finished.insert(idx);
            }
        }
        batch.join();
        for (const auto& [idx, info] : simulators_) {
            if (stepCounter_ - 1 < 0 || (stepCounter_ - 1) % info.decimationFactor != 0) {
                continue;
            }
            result.earlyReturn = result.earlyReturn || info.stepInfo.early_return;
            result.terminationRequested =
                result.terminationRequested || info.stepInfo.termination_requested;
            if (info.stepInfo.event_handling_needed) {
                result.eventSources.insert(idx);
            }
            if (info.stepInfo.early_return) {
                if (result.earlyReturnEndpoint && *result.earlyReturnEndpoint != info.stepInfo.last_successful_time) {
                    throw error(
                        make_error_code(errc::simulation_error),
                        "Simulators returned different early-return endpoints");
                }
                result.earlyReturnEndpoint = info.stepInfo.last_successful_time;
            }
        }
    }

    void validate_normal_step(bool hasDecimation, time_point currentT,
        duration requestedStepSize, const normal_step_results& result) const
    {
        if (hasDecimation && !result.eventSources.empty()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Event handling cannot be combined with simulator step-size decimation");
        }
        if (result.earlyReturn || result.terminationRequested) {
            const auto unsupportedDecimation = boost::algorithm::any_of(
                simulators_,
                [](const auto& simulator) {
                    return simulator.second.decimationFactor != 1;
                });
            if (result.activeSimulatorCount != simulators_.size() || unsupportedDecimation) {
                throw error(
                    make_error_code(errc::unsupported_feature),
                    "Early return or termination cannot be combined with "
                    "simulator step-size decimation");
            }
            const auto expectedEndpoint =
                result.earlyReturnEndpoint.value_or(currentT + requestedStepSize);
            for (const auto& [idx, info] : simulators_) {
                const auto endpoint =
                    info.stepInfo.early_return
                    ? info.stepInfo.last_successful_time
                    : currentT + requestedStepSize;
                if (endpoint != expectedEndpoint) {
                    throw error(
                        make_error_code(errc::simulation_error),
                        "Simulators returned different step endpoints");
                }
            }
        }
    }

    algorithm::macro_step_result do_step_with_info_impl(time_point currentT)
    {
        const bool hasDecimation = has_decimation();
        if (hasDecimation) {
            validate_decimation();
        }
        const bool coordinateEarlyReturns = simulators_.size() > 1 &&
            boost::algorithm::any_of(simulators_, [](auto& entry) {
                return might_return_early(entry.second);
            });
        if ((hasDecimation && has_event_simulator()) || coordinateEarlyReturns) {
            return do_decimated_event_step(currentT);
        }
        leave_decimated_event_grid(currentT);
        gridMode_ = false;
        if (eventRouter_.prepare_clock_step(currentT, baseStepSize_, clockRouter_)) {
            return {duration::zero(), {}, false, true};
        }
        if (interruptedEnd_ && *interruptedEnd_ <= currentT) {
            interruptedEnd_.reset();
        }
        const auto plannedStepSize = interruptedEnd_
            ? std::min(baseStepSize_, *interruptedEnd_ - currentT)
            : baseStepSize_;
        const auto plannedEnd = currentT + plannedStepSize;
        auto requestedStepSize = clockRouter_.cap_step(currentT, plannedStepSize);
        if (eventRouter_.prepare_step_inputs(currentT, requestedStepSize, clockRouter_)) {
            return {duration::zero(), {}, false, true};
        }
        const bool clockStepCapped = requestedStepSize != baseStepSize_;
        if (clockStepCapped &&
            boost::algorithm::any_of(
                simulators_,
                [](const auto& simulator) {
                    return simulator.second.decimationFactor != 1;
                })) {
            throw error(
                make_error_code(errc::unsupported_feature),
                "Fixed-step Clock events cannot be combined with simulator "
                "step-size decimation");
        }
        step_batch batch(pool_);
        normal_step_results result;
        run_normal_steps(batch, currentT, requestedStepSize, result);
        validate_normal_step(hasDecimation, currentT, requestedStepSize, result);

        const auto stepSize = result.earlyReturnEndpoint
            ? *result.earlyReturnEndpoint - currentT
            : requestedStepSize;
        const auto endTime = currentT + stepSize;
        const bool timeEvent = boost::algorithm::any_of(simulators_,
            [currentT, endTime](const auto& entry) {
                const auto next = entry.second.sim->get_event_handling_status().next_event_time;
                return next && *next > currentT && *next == endTime;
            });
        if (!hasDecimation && endTime < plannedEnd &&
            (result.earlyReturn || timeEvent) && !interruptedEnd_) {
            interruptedEnd_ = plannedEnd;
        }
        if (interruptedEnd_ && endTime >= *interruptedEnd_) {
            interruptedEnd_.reset();
        }
        if (!result.terminationRequested) {
            result.terminationRequested = eventRouter_.finish_step_events(
                endTime, baseStepSize_, clockRouter_, result.eventSources);
        }
        if (!result.terminationRequested) {
            eventRouter_.require_step_progress(currentT, endTime);
            calculate_and_transfer(false);
        }

        return {
            stepSize,
            std::move(result.finished),
            result.earlyReturn || clockStepCapped,
            result.terminationRequested,
        };
    }

    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT)
    {
        auto result = do_step_with_info(currentT);
        return {result.step_size, std::move(result.finished)};
    }

    serialization::node export_current_state() const
    {
        if (initializationFailed_) {
            throw error(make_error_code(errc::simulation_error),
                "The algorithm cannot continue after failed initialization; recreate the execution");
        }
        if (recoveryRequired_) {
            throw error(make_error_code(errc::simulation_error),
                "Cannot save algorithm state after a failed step; restore a consistent execution snapshot or recreate the execution");
        }
        if (clockRouter_.termination_requested()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Saving terminated Clock coordination is not supported");
        }
        check_event_function_state();
        auto exportedState = serialization::node();
        exportedState.put("type", std::string("fixed_step_algorithm"));
        exportedState.put("step_counter", stepCounter_);
        if (interruptedEnd_) {
            exportedState.put<std::int64_t>(
                "interrupted_end_ticks", interruptedEnd_->time_since_epoch().count());
        }
        exportedState.put_child("event_router", eventRouter_.export_state());
        if (clockRouter_.has_clock_support()) {
            exportedState.put_child("clock_router", clockRouter_.export_state());
        }
        if (gridStateActive_) {
            exportedState.put_child("decimation", export_decimation_state());
        }
        return exportedState;
    }

    void import_state(const serialization::node& exportedState)
    {
        if (initializationFailed_) {
            throw error(make_error_code(errc::simulation_error),
                "The algorithm cannot continue after failed initialization; recreate the execution");
        }
        if (clockRouter_.termination_requested()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Restoring terminated Clock coordination is not supported");
        }
        check_event_function_state();
        try {
            if (exportedState.get<std::string>("type") != "fixed_step_algorithm") {
                throw std::exception();
            }
            const auto stepCounter = exportedState.get<std::int64_t>("step_counter");
            if (stepCounter < 0) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized fixed-step counter is invalid");
            }
            const auto interruptedTicks =
                exportedState.get_optional<std::int64_t>("interrupted_end_ticks");
            std::optional<time_point> interruptedEnd;
            if (interruptedTicks) {
                interruptedEnd = time_point(duration(*interruptedTicks));
                if (*interruptedEnd <= startTime_) {
                    throw error(make_error_code(errc::bad_file),
                        "The serialized fixed-step interrupted endpoint is invalid");
                }
            }
            auto restoredClockRouter = clockRouter_;
            auto restoredDecimation = parse_decimation_state(exportedState);
            const auto clockState = exportedState.get_child_optional("clock_router");
            if (clockRouter_.has_clock_support()) {
                if (!clockState) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "The snapshot does not contain Clock coordination state");
                }
                restoredClockRouter.import_state(*clockState);
            } else if (clockState) {
                throw error(make_error_code(errc::bad_file),
                    "The snapshot contains incompatible Clock coordination state");
            }
            if (const auto eventState = exportedState.get_child_optional("event_router")) {
                eventRouter_.import_state(*eventState);
            } else {
                if (boost::algorithm::any_of(simulators_, [](const auto& entry) {
                        return entry.second.sim->get_event_controller() != nullptr;
                    })) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "The snapshot does not contain event coordination state");
                }
                eventRouter_.reset_state();
            }
            clockRouter_ = std::move(restoredClockRouter);
            stepCounter_ = stepCounter;
            interruptedEnd_ = interruptedEnd;
            apply_decimation_state(std::move(restoredDecimation));
            recoveryRequired_ = false;
        } catch (const error&) {
            throw;
        } catch (...) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized algorithm state is invalid or corrupt");
        }
    }

    void set_stepsize_decimation_factor(cosim::simulator_index index, int factor)
    {
        COSIM_INPUT_CHECK(factor > 0);
        simulators_.at(index).decimationFactor = factor;
    }

    void set_max_event_iterations(std::size_t limit)
    {
        if (initialized_) {
            throw error(make_error_code(errc::simulation_error),
                "The event iteration limit must be configured before initialization");
        }
        eventRouter_.set_max_event_iterations(limit);
    }

    void set_event_value_tolerance(cosim::event_value_tolerance tolerance)
    {
        if (initialized_) {
            throw error(make_error_code(errc::simulation_error),
                "The event value tolerance must be configured before initialization");
        }
        eventRouter_.set_event_value_tolerance(tolerance);
        clockRouter_.set_event_value_tolerance(tolerance);
    }

    cosim::event_value_tolerance event_value_tolerance() const noexcept
    {
        return eventRouter_.value_tolerance();
    }

    duration base_step_size() const noexcept { return baseStepSize_; }
    std::size_t worker_thread_count() const noexcept { return pool_.numWorkerThreads(); }
    std::size_t max_event_iterations() const noexcept { return eventRouter_.budget().limit(); }


private:
    void check_event_function_state() const
    {
        if (!functions_.empty() &&
            boost::algorithm::any_of(simulators_, [](const auto& entry) {
                return entry.second.sim->get_event_controller() != nullptr;
            })) {
            throw error(make_error_code(errc::unsupported_feature),
                "Saving or restoring an event-aware algorithm with functions is not supported");
        }
    }

    struct connection_ss
    {
        variable_id source;
        variable_id target;
        bool clocked = false;
    };

    struct connection_sf
    {
        variable_id source;
        function_io_id target;
    };

    struct connection_fs
    {
        function_io_id source;
        variable_id target;
    };

    struct connection_ff
    {
        function_io_id source;
        function_io_id target;
    };

    struct simulator_info
    {
        simulator* sim = nullptr;
        int decimationFactor = 1;
        cosim::step_result_info stepInfo;
        std::vector<connection_ss> outgoingSimConnections;
        std::vector<connection_sf> outgoingFunConnections;

        // State used only when event-capable simulators are decimated.
        // `reached` is the time the simulator has been stepped to, which can
        // be ahead of the co-simulation time.
        std::optional<time_point> reached;
        bool eventPending = false;
        simulator_capabilities capabilities;
        std::optional<simulator::state_index> checkpointIndex;
        time_point checkpointTime;
        bool checkpointLive = false;
    };

    struct function_info
    {
        function* fun = nullptr;
        int decimationFactor = 1;
        std::vector<connection_fs> outgoingSimConnections;
        std::vector<connection_ff> outgoingFunConnections;
    };

    void disconnect_simulator_variables(simulator_index index)
    {
        for (auto& simulator : simulators_) {
            auto& connections = simulator.second.outgoingSimConnections;
            const auto newEnd = boost::range::remove_if(
                connections,
                [index](const auto& connection) { return connection.target.simulator == index; });
            connections.erase(newEnd, connections.end());
        }
        for (auto& function : functions_) {
            auto& connections = function.second.outgoingSimConnections;
            const auto newEnd = boost::range::remove_if(
                connections,
                [index](const auto& connection) { return connection.target.simulator == index; });
            connections.erase(newEnd, connections.end());
        }
    }

    void update_function_decimation_factor(function_info& info)
    {
        info.decimationFactor = boost::accumulate(
            info.outgoingSimConnections,
            1,
            [&](int current, const auto& connection) {
                return std::lcm(
                    current,
                    simulators_.at(connection.target.simulator).decimationFactor);
            });
    }

    bool has_decimation() const
    {
        return boost::algorithm::any_of(simulators_, [](const auto& entry) {
            return entry.second.decimationFactor != 1;
        });
    }

    static bool is_event_simulator(const simulator_info& info)
    {
        return info.sim->get_event_controller() != nullptr;
    }

    bool has_event_simulator() const
    {
        return boost::algorithm::any_of(simulators_, [](const auto& entry) {
            return is_event_simulator(entry.second);
        });
    }

    static bool can_save_state(const simulator_info& info)
    {
        return info.capabilities.can_save_state;
    }

    static bool might_return_early(const simulator_info& info)
    {
        return info.capabilities.might_return_early;
    }

    static bool is_coordinated_simulator(const simulator_info& info)
    {
        return is_event_simulator(info) || might_return_early(info);
    }

    static time_point reached_time(const simulator_info& info)
    {
        assert(info.reached.has_value());
        return *info.reached; // NOLINT(bugprone-unchecked-optional-access): Grid participants are initialized before their reached times are used.
    }

    static void release_checkpoint(simulator_info& info) noexcept
    {
        info.checkpointLive = false;
        if (!info.checkpointIndex) {
            return;
        }
        try {
            info.sim->release_state(*info.checkpointIndex);
        } catch (...) { // NOLINT(bugprone-empty-catch): Cleanup must not throw when a simulator is already unusable.
            // The simulator may already be unusable; nothing else to release.
        }
        info.checkpointIndex.reset();
    }

    void warn_about_unrewindable_event_simulators()
    {
        if (!has_decimation()) {
            return;
        }
        const auto eventSimulators = boost::range::count_if(
            simulators_, [](const auto& entry) {
                return is_event_simulator(entry.second);
            });
        if (eventSimulators < 2) {
            return;
        }
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (is_event_simulator(info) && !can_save_state(info)) {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << info.sim->name()
                    << " cannot save and restore its state, so it cannot be"
                    << " rewound to an event of another simulator."
                    << " Such events may be lost while simulator step-size"
                    << " decimation is used.";
            }
        }
    }

    // Time points on the grid of a simulator with the given decimation factor.
    bool on_anchor(time_point time, int factor) const
    {
        return (time - startTime_).count() % (factor * baseStepSize_.count()) == 0;
    }

    time_point next_anchor(time_point time, int factor) const
    {
        const auto period = factor * baseStepSize_.count();
        return startTime_ + duration((((time - startTime_).count() / period) + 1) * period);
    }

    time_point ceil_anchor(time_point time, int factor) const
    {
        return on_anchor(time, factor) ? time : next_anchor(time, factor);
    }

    // The end of the next co-simulation step: the next base step boundary,
    // or the point where an event-capable simulator has already been stepped to,
    // whichever comes first, further limited by known events.
    duration grid_step_size(time_point currentT) const
    {
        auto limit = next_anchor(currentT, 1);
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (is_coordinated_simulator(info) && info.reached && *info.reached > currentT) {
                limit = std::min(limit, *info.reached);
            }
        }
        return clockRouter_.cap_step(currentT, limit - currentT);
    }

    // Leaves the event-aware decimation mode. All simulators must be at the
    // co-simulation time, which must be on the base step grid.
    void leave_decimated_event_grid(time_point currentT)
    {
        if (!gridStateActive_) {
            return;
        }
        const bool ahead = boost::algorithm::any_of(simulators_, [&](const auto& entry) {
            return entry.second.reached && *entry.second.reached > currentT;
        });
        if (ahead || !on_anchor(currentT, 1)) {
            throw error(make_error_code(errc::unsupported_feature),
                "Simulator step-size decimation cannot be disabled while a simulator is "
                "ahead of the co-simulation time or the time is off the base step grid");
        }
        for (auto& entry : simulators_) {
            release_checkpoint(entry.second);
            entry.second.reached.reset();
            entry.second.eventPending = false;
        }
        stepCounter_ = (currentT - startTime_).count() / baseStepSize_.count();
        gridStateActive_ = false;
    }

    struct grid_step
    {
        simulator_info* info;
        time_point target;
        bool checkpoint;
    };

    // Saves the state of `info.sim` right before a step, if possible.
    // `info.checkpointLive` tells whether a checkpoint was taken.
    static void take_checkpoint(simulator_info& info, time_point currentT, duration deltaT)
    {
        info.checkpointLive = false;
        if (!save_step_checkpoint(*info.sim, can_save_state(info), deltaT, info.checkpointIndex)) {
            return;
        }
        info.checkpointTime = currentT;
        info.checkpointLive = true;
    }

    // Steps the given simulators in parallel from `currentT`, and records the
    // time they reached and whether they need event handling.
    void run_grid_steps(std::vector<grid_step>& steps, time_point currentT)
    {
        step_batch batch(pool_);
        for (const auto& step : steps) {
            auto* info = step.info;
            if (step.target <= currentT) {
                info->stepInfo = {};
                info->stepInfo.last_successful_time = currentT;
                info->checkpointLive = false;
                continue;
            }
            batch.submit(*info->sim, [info, currentT, target = step.target, checkpoint = step.checkpoint] {
                const auto deltaT = target - currentT;
                if (checkpoint) {
                    take_checkpoint(*info, currentT, deltaT);
                } else {
                    info->checkpointLive = false;
                }
                info->stepInfo = info->sim->do_step_with_info(currentT, deltaT);
                if (info->stepInfo.result != step_result::complete) {
                    throw error(make_error_code(errc::simulation_error), "Step not complete");
                }
            });
        }
        batch.join();
        for (const auto& step : steps) {
            auto& info = *step.info;
            if (info.stepInfo.termination_requested && has_decimation()) {
                throw error(make_error_code(errc::unsupported_feature),
                    info.sim->name() +
                        ": termination requests cannot be combined with simulator "
                        "step-size decimation of event-aware simulators");
            }
            info.reached = step_endpoint(info.stepInfo, std::max(step.target, currentT));
            info.eventPending = info.stepInfo.event_handling_needed;
        }
    }

    // Restores `info.sim` to its checkpoint and steps it to `horizon`.
    // Returns false if there is no usable checkpoint.
    static bool rewind_to(simulator_info& info, time_point horizon)
    {
        if (!info.checkpointLive || !info.checkpointIndex) {
            return false;
        }
        info.sim->restore_state(*info.checkpointIndex);
        info.checkpointLive = false;
        info.stepInfo = {};
        info.stepInfo.last_successful_time = horizon;
        if (horizon > info.checkpointTime) {
            info.stepInfo = info.sim->do_step_with_info(
                info.checkpointTime, horizon - info.checkpointTime);
            const auto endpoint = step_endpoint(info.stepInfo, horizon);
            if (info.stepInfo.result != step_result::complete ||
                info.stepInfo.termination_requested || endpoint != horizon) {
                throw error(make_error_code(errc::simulation_error),
                    info.sim->name() + ": repeating a step up to an event time gave a different result");
            }
        }
        info.reached = horizon;
        info.eventPending = info.stepInfo.event_handling_needed;
        return true;
    }

    // Rewinds participants stepped past an event when a checkpoint is usable.
    // Others stay ahead and receive the event at their next communication point.
    void repair_overshoot(time_point horizon)
    {
        std::vector<simulator_info*> overshoot;
        for (auto& entry : simulators_) {
            auto& info = entry.second;
            if (info.reached && *info.reached > horizon) {
                overshoot.push_back(&info);
            }
        }
        if (overshoot.empty()) {
            return;
        }

        std::vector<char> rewound(overshoot.size(), 0);
        step_batch batch(pool_);
        for (std::size_t i = 0; i < overshoot.size(); ++i) {
            auto* info = overshoot.at(i);
            batch.submit(*info->sim, [info, &rewound, i, horizon] {
                rewound.at(i) = rewind_to(*info, horizon) ? 1 : 0;
            });
        }
        batch.join();
        for (std::size_t i = 0; i < overshoot.size(); ++i) {
            if (rewound.at(i) != 0) {
                continue;
            }
            BOOST_LOG_SEV(log::logger(), log::warning)
                << overshoot.at(i)->sim->name() << " could not be rewound to the event at "
                << to_double_time_point(horizon) << " s and has already been stepped to "
                << to_double_time_point(*overshoot.at(i)->reached) // NOLINT(bugprone-unchecked-optional-access): Overshoot entries were selected only when reached was set.
                << " s; the event reaches it at its next communication point.";
        }
    }

    std::unordered_set<simulator_index> initialize_grid_participants(time_point currentT)
    {
        gridMode_ = true;
        gridStateActive_ = true;
        for (auto& entry : simulators_) {
            auto& info = entry.second;
            if (!info.reached) {
                info.reached = info.decimationFactor == 1
                    ? currentT
                    : ceil_anchor(currentT, info.decimationFactor);
            }
            if (*info.reached < currentT) {
                throw error(make_error_code(errc::simulation_error),
                    info.sim->name() + ": the simulator is behind the co-simulation time");
            }
        }

        std::unordered_set<simulator_index> participants;
        for (const auto& entry : simulators_) {
            if (reached_time(entry.second) == currentT) {
                participants.insert(entry.first);
            }
        }
        return participants;
    }

    struct grid_event_times
    {
        std::optional<time_point> timeEvent;
        std::optional<time_point> event;
    };

    static void note_event_time(std::optional<time_point>& target, time_point time)
    {
        if (!target || time < *target) {
            target = time;
        }
    }

    grid_event_times find_grid_events(time_point currentT) const
    {
        grid_event_times result;
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (!is_coordinated_simulator(info)) {
                continue;
            }
            const auto next = info.sim->get_event_handling_status().next_event_time;
            if (next && *next > currentT) {
                note_event_time(result.timeEvent, *next);
                note_event_time(result.event, *next);
            }
            if (reached_time(info) > currentT &&
                (info.eventPending || !on_anchor(reached_time(info), info.decimationFactor))) {
                note_event_time(result.event, reached_time(info));
            }
        }
        return result;
    }

    struct grid_step_plan
    {
        std::vector<grid_step> rewindable;
        std::vector<grid_step> unrewindable;
        std::vector<grid_step> others;
    };

    grid_step_plan plan_grid_steps(time_point currentT, time_point stepEnd, // NOLINT(bugprone-easily-swappable-parameters): Start and end are distinct grid boundaries.
        const std::optional<time_point>& knownEvent)
    {
        const auto targetFor = [&](const simulator_info& info) {
            if (info.decimationFactor == 1) {
                return stepEnd;
            }
            auto target = next_anchor(currentT, info.decimationFactor);
            if (is_coordinated_simulator(info) && knownEvent && *knownEvent < target) {
                target = *knownEvent;
            }
            return target;
        };

        // A simulator needs a checkpoint only if another can stop before it.
        std::vector<std::pair<simulator_index, time_point>> earliestStops;
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (!is_coordinated_simulator(info)) {
                continue;
            }
            auto earliest = reached_time(info);
            if (!might_return_early(info)) {
                earliest = reached_time(info) == currentT
                    ? targetFor(info)
                    : next_anchor(reached_time(info), info.decimationFactor);
            }
            earliestStops.emplace_back(entry.first, earliest);
        }
        const auto needsCheckpoint = [&](simulator_index self, time_point target) {
            return boost::algorithm::any_of(
                earliestStops, [&](const auto& stop) {
                    return stop.first != self && stop.second < target;
                });
        };

        grid_step_plan plan;
        for (auto& entry : simulators_) {
            auto& info = entry.second;
            if (reached_time(info) != currentT) {
                continue;
            }
            const auto target = targetFor(info);
            if (!is_coordinated_simulator(info)) {
                const bool checkpoint =
                    needsCheckpoint(entry.first, target) && can_save_state(info);
                plan.others.push_back({&info, target, checkpoint});
                continue;
            }
            const bool checkpoint = needsCheckpoint(entry.first, target);
            if (!checkpoint || can_save_state(info)) {
                plan.rewindable.push_back({&info, target, checkpoint});
            } else {
                plan.unrewindable.push_back({&info, target, false});
            }
        }
        return plan;
    }

    static void note_grid_stops(
        const std::vector<grid_step>& steps, std::optional<time_point>& eventTime)
    {
        for (const auto& step : steps) {
            if (step.info->stepInfo.early_return) {
                note_event_time(eventTime, step.info->stepInfo.last_successful_time);
            } else if (step.info->stepInfo.event_handling_needed ||
                step.info->stepInfo.termination_requested) {
                note_event_time(eventTime, step.target);
            }
        }
    }

    time_point run_grid_plan(time_point currentT, time_point stepEnd, // NOLINT(bugprone-easily-swappable-parameters): Start and end are distinct grid boundaries.
        const grid_event_times& known, grid_step_plan& plan)
    {
        std::optional<time_point> eventTime;
        run_grid_steps(plan.rewindable, currentT);
        note_grid_stops(plan.rewindable, eventTime);
        if (eventTime) {
            for (auto& step : plan.unrewindable) {
                step.target = std::min(step.target, *eventTime);
            }
        }
        run_grid_steps(plan.unrewindable, currentT);
        note_grid_stops(plan.unrewindable, eventTime);

        auto horizon = eventTime;
        if (known.timeEvent && *known.timeEvent <= stepEnd) {
            note_event_time(horizon, *known.timeEvent);
        }
        if (known.event && *known.event <= stepEnd) {
            note_event_time(horizon, *known.event);
        }
        const auto endT = (eventTime && *eventTime < stepEnd) ? *eventTime : stepEnd;
        if (endT > currentT) {
            for (auto& step : plan.others) {
                if (step.info->decimationFactor == 1) {
                    step.target = endT;
                }
            }
            run_grid_steps(plan.others, currentT);
            for (const auto& step : plan.others) {
                if (step.info->stepInfo.early_return) {
                    throw error(make_error_code(errc::unsupported_feature),
                        step.info->sim->name() +
                            ": early return cannot be combined with simulator step-size "
                            "decimation of event-aware simulators");
                }
            }
        }
        if (horizon) {
            repair_overshoot(*horizon);
        }
        return endT;
    }

    // A co-simulation step used when simulators with different step sizes
    // and event-capable simulators are mixed. Event-capable simulators step
    // first so that the others can be limited to the first event time. A
    // simulator that is stepped past that time is rewound and stepped again
    // if it can save its state, and otherwise it keeps its result.
    algorithm::macro_step_result do_decimated_event_step(time_point currentT)
    {
        const auto participants = initialize_grid_participants(currentT);
        if (eventRouter_.prepare_clock_step(currentT, baseStepSize_, clockRouter_, &participants)) {
            return {duration::zero(), {}, false, true};
        }
        auto stepSize = grid_step_size(currentT);
        if (eventRouter_.process_inputs(currentT, stepSize, clockRouter_, true, &participants)) {
            return {duration::zero(), {}, false, true};
        }
        if (eventRouter_.settle_step_size(
                currentT,
                stepSize,
                clockRouter_,
                [this, currentT](duration) { return grid_step_size(currentT); },
                &participants)) {
            return {duration::zero(), {}, false, true};
        }
        clockRouter_.stage_current_unclocked_data(participants);
        const auto stepEnd = currentT + stepSize;

        // The first event time that is already known: a scheduled time event, or
        // a simulator that was stopped by an event ahead of the current time.
        const auto known = find_grid_events(currentT);
        auto plan = plan_grid_steps(currentT, stepEnd, known.event);
        const auto endT = run_grid_plan(currentT, stepEnd, known, plan);

        std::unordered_set<simulator_index> atEnd;
        std::unordered_set<simulator_index> eventSources;
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (reached_time(info) != endT) {
                continue;
            }
            atEnd.insert(entry.first);
            if (info.eventPending) {
                eventSources.insert(entry.first);
            }
        }
        bool terminated = false;
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (!info.stepInfo.termination_requested) {
                continue;
            }
            if (!info.reached || *info.reached != endT) {
                throw error(make_error_code(errc::unsupported_feature),
                    info.sim->name() +
                        ": a termination request cannot be coordinated while the simulator "
                        "is ahead of the event time");
            }
            terminated = true;
        }
        if (!terminated) {
            terminated = eventRouter_.finish_step_events(
                endT, baseStepSize_, clockRouter_, eventSources, &atEnd);
        }
        for (const auto index : atEnd) {
            simulators_.at(index).eventPending = false;
        }
        if (!terminated) {
            eventRouter_.require_step_progress(currentT, endT);
        }
        transferTime_ = endT;
        if (!terminated) {
            calculate_and_transfer(false);
        }

        std::unordered_set<simulator_index> finished;
        for (const auto& entry : simulators_) {
            if (reached_time(entry.second) == endT) {
                finished.insert(entry.first);
            }
        }
        ++stepCounter_;
        return {
            endT - currentT,
            std::move(finished),
            endT - currentT != baseStepSize_,
            terminated,
        };
    }

    struct decimation_snapshot // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
    {
        bool active = false;
        std::unordered_map<simulator_index, std::pair<std::optional<std::int64_t>, bool>> entries;
    };

    serialization::node export_decimation_state() const
    {
        serialization::node result;
        for (const auto& entry : simulators_) {
            const auto& info = entry.second;
            if (info.checkpointLive && info.reached && *info.reached > transferTime_) {
                throw error(make_error_code(errc::unsupported_feature),
                    info.sim->name() +
                        ": the state cannot be exported while the simulator is ahead of the "
                        "co-simulation time and relies on a rewind checkpoint");
            }
            serialization::node child;
            if (info.reached) {
                child.put<std::int64_t>("reached_ticks", info.reached->time_since_epoch().count());
            }
            child.put("event_pending", info.eventPending);
            result.put_child(std::to_string(entry.first), child);
        }
        return result;
    }

    decimation_snapshot parse_decimation_state(const serialization::node& exportedState) const
    {
        decimation_snapshot snapshot;
        const auto state = exportedState.get_child_optional("decimation");
        if (!state) {
            return snapshot;
        }
        snapshot.active = true;
        for (const auto& entry : simulators_) {
            const auto child = state->get_child_optional(std::to_string(entry.first));
            if (!child) {
                throw error(make_error_code(errc::bad_file),
                    "The snapshot does not contain decimation state for every simulator");
            }
            std::optional<std::int64_t> ticks;
            if (const auto value = child->get_optional<std::int64_t>("reached_ticks")) {
                ticks = *value;
            }
            snapshot.entries.emplace(
                entry.first,
                std::make_pair(ticks, child->get<bool>("event_pending")));
        }
        return snapshot;
    }

    void apply_decimation_state(decimation_snapshot snapshot)
    {
        gridStateActive_ = snapshot.active;
        for (auto& entry : simulators_) {
            auto& info = entry.second;
            info.checkpointLive = false;
            info.reached.reset();
            info.eventPending = false;
            if (!snapshot.active) {
                continue;
            }
            const auto& restored = snapshot.entries.at(entry.first);
            if (restored.first) {
                info.reached = time_point(duration(*restored.first));
            }
            info.eventPending = restored.second;
        }
    }

    bool on_cadence(std::int64_t factor) const
    {
        if (factor == 1) {
            return true;
        }
        if (!gridMode_) {
            return stepCounter_ % factor == 0;
        }
        return (transferTime_ - startTime_).count() %
            (factor * baseStepSize_.count()) ==
            0;
    }

    bool at_communication_point(const simulator_info& info) const
    {
        if (!gridMode_) {
            return stepCounter_ % info.decimationFactor == 0;
        }
        return !info.reached || *info.reached == transferTime_;
    }

    void calculate_and_transfer(bool includeClockedData)
    {
        // Transfer the outputs from simulators that have finished their
        // individual time steps within the current co-simulation time step.
        for (const auto& simulator : simulators_) {
            if (at_communication_point(simulator.second)) {
                transfer_variables(
                    simulator.second.outgoingSimConnections,
                    includeClockedData);
                transfer_variables(simulator.second.outgoingFunConnections);
            }
        }
        calculate_and_transfer_functions();
    }

    void calculate_and_transfer_functions()
    {
        std::unordered_map<function_index, std::size_t> indegrees;
        for (const auto& function : functions_) {
            indegrees.emplace(function.first, 0);
        }
        for (const auto& function : functions_) {
            for (const auto& connection : function.second.outgoingFunConnections) {
                ++indegrees.at(connection.target.function);
            }
        }

        std::vector<function_index> ready;
        for (const auto& entry : indegrees) {
            if (entry.second == 0) {
                ready.push_back(entry.first);
            }
        }
        std::size_t processed = 0;
        while (!ready.empty()) {
            const auto index = ready.back();
            ready.pop_back();
            const auto& info = functions_.at(index);
            if (on_cadence(info.decimationFactor)) {
                info.fun->calculate();
                transfer_variables(info.outgoingSimConnections);
                transfer_variables(info.outgoingFunConnections);
            }
            ++processed;
            for (const auto& connection : info.outgoingFunConnections) {
                auto& indegree = indegrees.at(connection.target.function);
                if (--indegree == 0) {
                    ready.push_back(connection.target.function);
                }
            }
        }
        if (processed != functions_.size()) {
            throw std::logic_error(
                "Cyclic function-to-function connections are not supported");
        }
    }

    void transfer_variables(
        const std::vector<connection_ss>& connections,
        bool includeClockedData = true)
    {
        for (const auto& connection : connections) {
            if (connection.clocked && !includeClockedData) {
                continue;
            }
            const auto sdf = simulators_.at(connection.source.simulator).decimationFactor;
            const auto tdf = simulators_.at(connection.target.simulator).decimationFactor;
            if (on_cadence(std::lcm(sdf, tdf))) {
                transfer_variable(connection);
            }
        }
    }

    void transfer_variables(const std::vector<connection_sf>& connections)
    {
        for (const auto& connection : connections) {
            const auto sdf = simulators_.at(connection.source.simulator).decimationFactor;
            const auto tdf = functions_.at(connection.target.function).decimationFactor;
            if (on_cadence(std::lcm(sdf, tdf))) {
                transfer_variable(connection);
            }
        }
    }

    void transfer_variables(const std::vector<connection_fs>& connections)
    {
        for (const auto& connection : connections) {
            const auto sdf = functions_.at(connection.source.function).decimationFactor;
            const auto tdf = simulators_.at(connection.target.simulator).decimationFactor;
            if (on_cadence(std::lcm(sdf, tdf))) {
                transfer_variable(connection);
            }
        }
    }

    void transfer_variables(const std::vector<connection_ff>& connections)
    {
        for (const auto& connection : connections) {
            const auto sdf = functions_.at(connection.source.function).decimationFactor;
            const auto tdf = functions_.at(connection.target.function).decimationFactor;
            if (on_cadence(std::lcm(sdf, tdf))) {
                transfer_variable(connection);
            }
        }
    }

    void transfer_variable(const connection_ss& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = simulators_.at(connection.source.simulator)
                               .sim->get_variable_value(
                                   connection.source.type, connection.source.reference);
        simulators_.at(connection.target.simulator)
            .sim->set_variable_value(
                connection.target.type, connection.target.reference, value);
    }

    void transfer_variable(const connection_sf& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = simulators_.at(connection.source.simulator)
                               .sim->get_variable_value(
                                   connection.source.type, connection.source.reference);
        functions_.at(connection.target.function)
            .fun->set_variable_value(connection.target.reference, value);
    }

    void transfer_variable(const connection_fs& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = functions_.at(connection.source.function)
                               .fun->get_variable_value(connection.source.reference);
        simulators_.at(connection.target.simulator)
            .sim->set_variable_value(
                connection.target.type, connection.target.reference, value);
    }

    void transfer_variable(const connection_ff& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = functions_.at(connection.source.function)
                               .fun->get_variable_value(connection.source.reference);
        functions_.at(connection.target.function)
            .fun->set_variable_value(connection.target.reference, value);
    }

    // Algorithm parameters
    const duration baseStepSize_;
    std::optional<time_point> interruptedEnd_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    unsigned int max_threads_ = std::thread::hardware_concurrency() - 1;

    // System structure
    std::unordered_map<simulator_index, simulator_info> simulators_;
    std::unordered_map<function_index, function_info> functions_;
    clock_router clockRouter_;
    event_router eventRouter_;

    // Simulation state
    std::int64_t stepCounter_ = 0;
    bool initialized_ = false;
    bool initializationFailed_ = false;
    bool recoveryRequired_ = false;

    // State of the event-aware decimation mode, where simulators can be at
    // different times and time steps need not be multiples of the base step.
    bool gridMode_ = false;
    bool gridStateActive_ = false;
    time_point transferTime_;

    // Other
    utility::thread_pool pool_;
};


fixed_step_algorithm::fixed_step_algorithm(duration baseStepSize, std::optional<unsigned int> workerThreadCount)
    : pimpl_(std::make_unique<impl>(baseStepSize, workerThreadCount))
{
}

fixed_step_algorithm::fixed_step_algorithm(fixed_step_algorithm_params params, std::optional<unsigned int> workerThreadCount)
    : pimpl_(std::make_unique<impl>(params, workerThreadCount))
{
}


fixed_step_algorithm::~fixed_step_algorithm() noexcept = default;


fixed_step_algorithm::fixed_step_algorithm(fixed_step_algorithm&& other) noexcept
    : pimpl_(std::move(other.pimpl_))
{
}


fixed_step_algorithm& fixed_step_algorithm::operator=(
    fixed_step_algorithm&& other) noexcept
{
    pimpl_ = std::move(other.pimpl_);
    return *this;
}


void fixed_step_algorithm::add_simulator(
    simulator_index index,
    simulator* sim,
    duration stepSizeHint)
{
    pimpl_->add_simulator(index, sim, stepSizeHint);
}


void fixed_step_algorithm::remove_simulator(simulator_index index)
{
    pimpl_->remove_simulator(index);
}


void fixed_step_algorithm::add_function(function_index index, function* fun)
{
    pimpl_->add_function(index, fun);
}

void fixed_step_algorithm::connect_variables(variable_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void fixed_step_algorithm::connect_variables(variable_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

void fixed_step_algorithm::connect_variables(function_io_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void fixed_step_algorithm::connect_variables(
    function_io_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

void fixed_step_algorithm::disconnect_variable(variable_id input)
{
    pimpl_->disconnect_variable(input);
}

void fixed_step_algorithm::disconnect_variable(function_io_id input)
{
    pimpl_->disconnect_variable(input);
}


void fixed_step_algorithm::setup(
    time_point startTime,
    std::optional<time_point> stopTime)
{
    pimpl_->setup(startTime, stopTime);
}


void fixed_step_algorithm::initialize()
{
    pimpl_->initialize();
}


std::pair<duration, std::unordered_set<simulator_index>> fixed_step_algorithm::do_step(
    time_point currentT)
{
    return pimpl_->do_step(currentT);
}

algorithm::macro_step_result fixed_step_algorithm::do_step_with_info(
    time_point currentT)
{
    return pimpl_->do_step_with_info(currentT);
}

serialization::node fixed_step_algorithm::export_current_state() const
{
    return pimpl_->export_current_state();
}

void fixed_step_algorithm::import_state(const serialization::node& exportedState)
{
    pimpl_->import_state(exportedState);
}

void fixed_step_algorithm::set_stepsize_decimation_factor(cosim::simulator_index simulator, int factor)
{
    pimpl_->set_stepsize_decimation_factor(simulator, factor);
}

void fixed_step_algorithm::set_max_event_iterations(std::size_t limit)
{
    pimpl_->set_max_event_iterations(limit);
}

duration fixed_step_algorithm::base_step_size() const noexcept { return pimpl_->base_step_size(); }
std::size_t fixed_step_algorithm::worker_thread_count() const noexcept { return pimpl_->worker_thread_count(); }
std::size_t fixed_step_algorithm::max_event_iterations() const noexcept { return pimpl_->max_event_iterations(); }

void fixed_step_algorithm::set_event_value_tolerance(cosim::event_value_tolerance tolerance)
{
    pimpl_->set_event_value_tolerance(tolerance);
}

cosim::event_value_tolerance fixed_step_algorithm::event_value_tolerance() const noexcept
{
    return pimpl_->event_value_tolerance();
}


} // namespace cosim
