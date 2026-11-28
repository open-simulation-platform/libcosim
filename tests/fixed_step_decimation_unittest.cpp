#define BOOST_TEST_MODULE cosim::fixed_step_algorithm event decimation unittest
#include "mock_slave.hpp"
#include "warning_capture.hpp"

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/error.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/function/vector_sum.hpp>
#include <cosim/slave_simulator.hpp>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr cosim::duration base_step{std::chrono::seconds(1)};

cosim::time_point at_ms(long long ms)
{
    return cosim::time_point(std::chrono::milliseconds(ms));
}

long long to_ms(cosim::duration d)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

long long to_ms(cosim::time_point t)
{
    return to_ms(t.time_since_epoch());
}

struct step_record
{
    cosim::time_point start;
    cosim::duration size;
    double realIn;
};

// Formats steps as "<start>+<size>" in milliseconds, e.g. "0+1000 1000+500".
std::string describe(const std::vector<step_record>& steps)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) out << ' ';
        out << to_ms(steps[i].start) << '+' << to_ms(steps[i].size);
    }
    return out.str();
}

// A slave that records its steps and counts its state saves and restores.
class recording_slave : public mock_slave
{
public:
    explicit recording_slave(
        bool canSaveState,
        std::function<double(cosim::time_point, cosim::duration, double)> realOp = nullptr)
        : mock_slave(std::move(realOp))
        , canSaveState_(canSaveState)
    {
    }

    cosim::model_description model_description() const override
    {
        auto description = mock_slave::model_description();
        description.capabilities.can_save_state = canSaveState_;
        return description;
    }

    cosim::step_result do_step(cosim::time_point currentT, cosim::duration deltaT) override
    {
        record(currentT, deltaT);
        return mock_slave::do_step(currentT, deltaT);
    }

    state_index save_state() override
    {
        require_state_saving();
        ++saves;
        return mock_slave::save_state();
    }

    void save_state(state_index overwriteState) override
    {
        require_state_saving();
        ++saves;
        mock_slave::save_state(overwriteState);
    }

    void restore_state(state_index state) override
    {
        ++restores;
        mock_slave::restore_state(state);
    }

    std::vector<step_record> steps;
    int saves = 0;
    int restores = 0;

protected:
    void record(cosim::time_point currentT, cosim::duration deltaT)
    {
        const cosim::value_reference reference = real_in_reference;
        double input = 0.0;
        get_real_variables(
            gsl::span<const cosim::value_reference>(&reference, 1),
            gsl::span<double>(&input, 1));
        steps.push_back({currentT, deltaT, input});
    }

private:
    void require_state_saving() const
    {
        if (!canSaveState_) {
            throw cosim::error(
                cosim::make_error_code(cosim::errc::unsupported_feature),
                "This slave cannot save its state");
        }
    }

    bool canSaveState_;
};

class nonreplayable_slave final : public recording_slave
{
public:
    nonreplayable_slave() : recording_slave(true) {}
    bool supports_automatic_step_replay() const noexcept override { return false; }
};

// An event-capable slave with a list of event times.
//
// `early_return` events make `do_step_with_info()` return at the event time.
// `time_event` events are advertised as the next event time, and the slave
// fails if a step is requested across an event that has not been handled.
class event_stepping_slave final : public recording_slave, public cosim::event_controller
{
public:
    enum class event_kind
    {
        early_return,
        time_event
    };

    explicit event_stepping_slave(
        bool canSaveState,
        event_kind kind = event_kind::early_return,
        std::vector<cosim::time_point> events = {},
        std::function<double(cosim::time_point, cosim::duration, double)> realOp = nullptr)
        : recording_slave(canSaveState, std::move(realOp))
        , kind_(kind)
        , events_(std::move(events))
    {
    }

    cosim::event_controller* get_event_controller() noexcept override { return this; }
    bool supports_automatic_step_replay() const noexcept override { return replayable; }
    bool is_in_event_mode() const noexcept override { return inEventMode_; }
    void enter_event_mode() override { inEventMode_ = true; }
    cosim::event_update_result update_event_state() override
    {
        if (!inEventMode_) throw std::logic_error("Cannot update discrete states outside Event Mode");
        for (const auto event : events_) {
            if (event <= time_ && handled_.insert(event).second) {
                handledAt.push_back(time_);
                if (onEvent) onEvent();
            }
        }
        return {false, false, get_event_handling_status().next_event_time};
    }
    void enter_step_mode() override { inEventMode_ = false; }

    cosim::model_description model_description() const override
    {
        auto description = recording_slave::model_description();
        description.capabilities.might_return_early =
            declaresEarlyReturn && kind_ == event_kind::early_return;
        return description;
    }

    // Whether the slave advertises that it might return early from a step.
    bool declaresEarlyReturn = true;
    bool replayable = true;

    void setup(
        cosim::time_point startTime,
        std::optional<cosim::time_point> stopTime,
        std::optional<double> relativeTolerance) override
    {
        mock_slave::setup(startTime, stopTime, relativeTolerance);
        time_ = startTime;
    }

    cosim::step_result_info do_step_with_info(
        cosim::time_point currentT,
        cosim::duration deltaT) override
    {
        record(currentT, deltaT);
        const auto end = currentT + deltaT;
        auto stop = end;
        bool earlyReturn = false;
        bool eventHandlingNeeded = false;
        for (const auto event : events_) {
            if (handled_.count(event) != 0 || event <= currentT || event > end) continue;
            if (kind_ == event_kind::time_event) {
                if (event < end) {
                    throw std::logic_error("A step spans an unhandled time event");
                }
                continue;
            }
            stop = event;
            earlyReturn = event < end;
            eventHandlingNeeded = true;
            break;
        }
        mock_slave::do_step(currentT, stop - currentT);
        time_ = stop;
        return {cosim::step_result::complete, stop, earlyReturn, eventHandlingNeeded, false, false};
    }

    cosim::event_handling_status get_event_handling_status() const noexcept override
    {
        if (kind_ == event_kind::time_event) {
            for (const auto event : events_) {
                if (handled_.count(event) == 0) return {false, event};
            }
        }
        return {};
    }

    state_index save_state() override
    {
        const auto index = recording_slave::save_state();
        snapshots_[index] = {time_, handled_};
        return index;
    }

    void save_state(state_index overwriteState) override
    {
        recording_slave::save_state(overwriteState);
        snapshots_[overwriteState] = {time_, handled_};
    }

    void restore_state(state_index state) override
    {
        recording_slave::restore_state(state);
        const auto snapshot = snapshots_.find(state);
        if (snapshot != snapshots_.end()) {
            time_ = snapshot->second.time;
            handled_ = snapshot->second.handled;
        }
    }

    std::vector<cosim::time_point> handledAt;
    std::function<void()> onEvent;

private:
    struct snapshot
    {
        cosim::time_point time;
        std::set<cosim::time_point> handled;
    };

    event_kind kind_;
    std::vector<cosim::time_point> events_;
    cosim::time_point time_;
    std::set<cosim::time_point> handled_;
    bool inEventMode_ = false;
    std::map<state_index, snapshot> snapshots_;
};

using event_kind = event_stepping_slave::event_kind;

struct scenario
{
    std::shared_ptr<cosim::fixed_step_algorithm> algorithm =
        std::make_shared<cosim::fixed_step_algorithm>(base_step);
    cosim::execution execution{cosim::time_point{}, algorithm};

    cosim::simulator_index add(
        std::shared_ptr<cosim::slave> slave,
        const std::string& name,
        int decimationFactor = 1)
    {
        const auto index = execution.add_slave(std::move(slave), name);
        algorithm->set_stepsize_decimation_factor(index, decimationFactor);
        return index;
    }

    // Steps until the given time and returns the step sizes in milliseconds.
    std::string run_to(long long targetMs)
    {
        std::ostringstream out;
        bool first = true;
        while (to_ms(execution.current_time()) < targetMs) {
            if (!first) out << ' ';
            first = false;
            out << to_ms(execution.step());
        }
        return out.str();
    }
};

bool is_unsupported_feature(const cosim::error& e)
{
    return e.code() == cosim::make_error_code(cosim::errc::unsupported_feature);
}

bool is_bad_file(const cosim::error& e)
{
    return e.code() == cosim::make_error_code(cosim::errc::bad_file);
}

} // namespace


BOOST_AUTO_TEST_CASE(early_event_shortens_a_connected_legacy_simulator_step)
{
    scenario s;
    auto event = std::make_shared<event_stepping_slave>(
        false, event_kind::early_return, std::vector{at_ms(500)});
    auto legacy = std::make_shared<recording_slave>(false);
    const auto source = s.add(event, "event");
    const auto target = s.add(legacy, "legacy");
    s.execution.connect_variables(
        cosim::variable_id{source, cosim::variable_type::real, mock_slave::real_out_reference},
        cosim::variable_id{target, cosim::variable_type::real, mock_slave::real_in_reference});

    BOOST_CHECK_EQUAL(s.run_to(1000), "500 500");
    BOOST_CHECK_EQUAL(describe(legacy->steps), "0+500 500+500");
    BOOST_CHECK_EQUAL(legacy->saves, 0);
    BOOST_CHECK_EQUAL(legacy->restores, 0);
    BOOST_REQUIRE_EQUAL(event->handledAt.size(), 1u);
    BOOST_CHECK(event->handledAt.front() == at_ms(500));
}

BOOST_AUTO_TEST_CASE(single_simulator_events_complete_the_interrupted_grid_step)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const bool ecco : {false, true}) {
        for (const auto kind : {event_kind::early_return, event_kind::time_event}) {
            std::shared_ptr<cosim::algorithm> algorithm;
            if (ecco) {
                algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
            } else {
                algorithm = std::make_shared<cosim::fixed_step_algorithm>(base_step, 1);
            }
            cosim::execution execution{cosim::time_point{}, algorithm};
            auto source = std::make_shared<event_stepping_slave>(
                true, kind, std::vector{at_ms(400), at_ms(700)});
            execution.add_slave(source, "source");
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 400);
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 300);
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 300);
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 1000);
            BOOST_REQUIRE_EQUAL(source->handledAt.size(), 2u);
            BOOST_CHECK(source->handledAt[0] == at_ms(400));
            BOOST_CHECK(source->handledAt[1] == at_ms(700));
        }
    }
}

BOOST_AUTO_TEST_CASE(ecco_restores_an_interrupted_grid_step)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    auto source = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(400), at_ms(700)});
    cosim::slave_simulator simulator(source, "source");
    cosim::ecco_algorithm algorithm(params, 1);
    algorithm.add_simulator(0, &simulator, cosim::duration::zero());
    algorithm.setup(cosim::time_point{}, std::nullopt);
    algorithm.initialize();
    BOOST_REQUIRE_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(0)).step_size), 400);
    const auto state = algorithm.export_current_state();
    const auto simulatorState = simulator.save_state();
    BOOST_CHECK(state.get<bool>("has_interrupted_end"));
    BOOST_CHECK(state.get<std::int64_t>("interrupted_end_ticks") ==
        at_ms(1000).time_since_epoch().count());
    auto invalidState = state;
    invalidState.put<std::int64_t>("interrupted_end_ticks",
        at_ms(400).time_since_epoch().count());
    BOOST_CHECK_EXCEPTION(algorithm.import_state(invalidState), cosim::error, is_bad_file);
    BOOST_CHECK(algorithm.export_current_state() == state);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(400)).step_size), 300);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(700)).step_size), 300);
    simulator.restore_state(simulatorState);
    algorithm.import_state(state);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(400)).step_size), 300);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(700)).step_size), 300);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(1000)).step_size), 1000);
    simulator.release_state(simulatorState);
}

BOOST_AUTO_TEST_CASE(ecco_power_bond_completes_interrupted_minimum_step)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const auto kind : {event_kind::early_return, event_kind::time_event}) {
        auto algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
        cosim::execution execution{cosim::time_point{}, algorithm};
        auto first = std::make_shared<event_stepping_slave>(
            true, kind, std::vector{at_ms(600), at_ms(900)},
            [](cosim::time_point, cosim::duration, double input) { return input * 2.0; });
        auto second = std::make_shared<recording_slave>(
            true, [](cosim::time_point, cosim::duration, double input) { return input + 1.0; });
        const auto firstIndex = execution.add_slave(first, "first");
        const auto secondIndex = execution.add_slave(second, "second");
        const cosim::variable_id firstIn{
            firstIndex, cosim::variable_type::real, mock_slave::real_in_reference};
        const cosim::variable_id firstOut{
            firstIndex, cosim::variable_type::real, mock_slave::real_out_reference};
        const cosim::variable_id secondIn{
            secondIndex, cosim::variable_type::real, mock_slave::real_in_reference};
        const cosim::variable_id secondOut{
            secondIndex, cosim::variable_type::real, mock_slave::real_out_reference};
        execution.connect_variables(firstOut, secondIn);
        execution.connect_variables(secondOut, firstIn);
        algorithm->add_power_bond(firstIn, firstOut, secondIn, secondOut);
        execution.set_real_initial_value(firstIndex, mock_slave::real_in_reference, 1.0);
        execution.set_real_initial_value(secondIndex, mock_slave::real_in_reference, 2.0);
        BOOST_CHECK_EQUAL(to_ms(execution.step()), 600);
        BOOST_CHECK_EQUAL(to_ms(execution.step()), 300);
        BOOST_CHECK_EQUAL(to_ms(execution.step()), 100);
        BOOST_CHECK_EQUAL(to_ms(execution.step()), 1000);
        BOOST_CHECK_EQUAL(to_ms(execution.current_time()), 2000);
        BOOST_REQUIRE_EQUAL(first->handledAt.size(), 2u);
        BOOST_CHECK(first->handledAt[0] == at_ms(600));
        BOOST_CHECK(first->handledAt[1] == at_ms(900));
        BOOST_CHECK(!algorithm->get_powerbond_energies(0).empty());
    }
}

BOOST_AUTO_TEST_CASE(async_stop_is_not_lost_and_running_includes_shutdown)
{
    for (int i = 0; i < 50; ++i) {
        cosim::execution execution{
            cosim::time_point{}, std::make_shared<cosim::fixed_step_algorithm>(base_step, 1)};
        execution.add_slave(std::make_unique<mock_slave>(), "slave");
        auto result = execution.simulate_until_async(std::nullopt);
        execution.stop_simulation();
        const auto status = result.wait_for(std::chrono::seconds(1));
        if (status != std::future_status::ready) execution.stop_simulation();
        const bool reachedTarget = result.get();
        BOOST_CHECK(status == std::future_status::ready);
        BOOST_CHECK(!reachedTarget);
        BOOST_CHECK(!execution.is_running());
    }

    std::promise<void> entered;
    std::promise<void> release;
    auto gate = release.get_future().share();
    cosim::execution execution{
        cosim::time_point{}, std::make_shared<cosim::fixed_step_algorithm>(base_step, 1)};
    execution.add_slave(std::make_unique<detailed_step_mock_slave>(
        [&entered, gate](cosim::time_point time, cosim::duration step) {
            entered.set_value();
            gate.wait();
            return cosim::step_result_info{cosim::step_result::complete, time + step};
        }), "blocked");
    execution.initialize();
    auto enteredFuture = entered.get_future();
    auto result = execution.simulate_until_async(std::nullopt);
    const auto arrived = enteredFuture.wait_for(std::chrono::seconds(5));
    if (arrived != std::future_status::ready) {
        execution.stop_simulation();
        release.set_value();
        result.wait();
        BOOST_FAIL("The asynchronous step did not start");
        return;
    }
    execution.stop_simulation();
    const bool runningWhileStepFinishes = execution.is_running();
    bool stateRejected = false;
    try {
        execution.export_current_state();
    } catch (const std::logic_error&) {
        stateRejected = true;
    }
    release.set_value();
    const bool reachedTarget = result.get();
    BOOST_CHECK(runningWhileStepFinishes);
    BOOST_CHECK(stateRejected);
    BOOST_CHECK(!reachedTarget);
    BOOST_CHECK(!execution.is_running());
}

BOOST_AUTO_TEST_CASE(fixed_step_restores_an_interrupted_interval)
{
    auto source = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(400)});
    cosim::slave_simulator simulator(source, "source");
    cosim::fixed_step_algorithm algorithm(base_step, 1);
    algorithm.add_simulator(0, &simulator, cosim::duration::zero());
    algorithm.setup(cosim::time_point{}, std::nullopt);
    algorithm.initialize();
    BOOST_REQUIRE_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(0)).step_size), 400);
    const auto state = algorithm.export_current_state();
    const auto simulatorState = simulator.save_state();
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(400)).step_size), 600);
    simulator.restore_state(simulatorState);
    algorithm.import_state(state);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(400)).step_size), 600);
    BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(1000)).step_size), 1000);
    simulator.release_state(simulatorState);
}

BOOST_AUTO_TEST_CASE(ecco_imports_pre_interruption_snapshot)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    cosim::ecco_algorithm algorithm(params, 1);
    algorithm.setup(cosim::time_point{}, std::nullopt);
    algorithm.initialize();
    auto state = algorithm.export_current_state();
    state.put<std::uint32_t>("version", 3);
    state.erase("has_interrupted_end");
    algorithm.import_state(state);
    BOOST_CHECK(!algorithm.export_current_state().get<bool>("has_interrupted_end"));
}

BOOST_AUTO_TEST_CASE(startup_warns_when_state_support_cannot_be_replayed)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const bool ecco : {false, true}) {
        warning_capture warnings;
        std::shared_ptr<cosim::algorithm> algorithm;
        if (ecco) {
            algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
        } else {
            algorithm = std::make_shared<cosim::fixed_step_algorithm>(base_step, 1);
        }
        cosim::execution execution{cosim::time_point{}, algorithm};
        const auto source = execution.add_slave(
            std::make_shared<event_stepping_slave>(true), "event");
        const auto target = execution.add_slave(
            std::make_shared<nonreplayable_slave>(), "nonreplayable");
        execution.connect_variables(
            cosim::variable_id{source, cosim::variable_type::real, mock_slave::real_out_reference},
            cosim::variable_id{target, cosim::variable_type::real, mock_slave::real_in_reference});
        execution.initialize();
        BOOST_CHECK(warnings.messages().find("cannot safely replay a step automatically") !=
            std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(factor_one_early_events_rewind_only_capable_simulators)
{
    scenario s;
    auto earlier = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(400)});
    auto later = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(700)});
    s.add(earlier, "earlier");
    s.add(later, "later");

    BOOST_CHECK_EQUAL(to_ms(s.execution.step()), 400);
    BOOST_CHECK_EQUAL(later->restores, 1);
    BOOST_REQUIRE_EQUAL(earlier->handledAt.size(), 1u);
    BOOST_CHECK(earlier->handledAt.front() == at_ms(400));
    BOOST_CHECK(later->handledAt.empty());
}

BOOST_AUTO_TEST_CASE(decimated_ordinary_participants_rewind_only_with_usable_state)
{
    for (const bool canSaveState : {false, true}) {
        BOOST_TEST_CONTEXT("state support=" << canSaveState) {
            warning_capture warnings;
            scenario s;
            auto event = std::make_shared<event_stepping_slave>(
                false, event_kind::early_return, std::vector{at_ms(1500)});
            auto ordinary = std::make_shared<recording_slave>(canSaveState);
            const auto source = s.add(event, "event");
            const auto target = s.add(ordinary, "ordinary", 2);
            s.execution.connect_variables(
                cosim::variable_id{source, cosim::variable_type::real, mock_slave::real_out_reference},
                cosim::variable_id{target, cosim::variable_type::real, mock_slave::real_in_reference});
            BOOST_CHECK_EQUAL(to_ms(s.execution.step()), 1000);
            BOOST_CHECK_EQUAL(to_ms(s.execution.step()), 500);
            BOOST_CHECK_EQUAL(ordinary->restores, canSaveState ? 1 : 0);
            BOOST_CHECK_EQUAL(describe(ordinary->steps),
                canSaveState ? "0+2000 0+1500" : "0+2000");
            if (!canSaveState) {
                BOOST_CHECK(warnings.messages().find("could not be rewound") != std::string::npos);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(factor_one_unrewindable_event_simulator_stays_ahead)
{
    warning_capture warnings;
    scenario s;
    auto earlier = std::make_shared<event_stepping_slave>(
        false, event_kind::early_return, std::vector{at_ms(400)});
    auto later = std::make_shared<event_stepping_slave>(
        false, event_kind::early_return, std::vector{at_ms(700)});
    s.add(earlier, "earlier");
    s.add(later, "later");

    BOOST_CHECK_EQUAL(s.run_to(700), "400 300");
    BOOST_CHECK_EQUAL(later->saves, 0);
    BOOST_CHECK_EQUAL(later->restores, 0);
    BOOST_CHECK(warnings.messages().find("could not be rewound") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ecco_coordinates_multiple_early_event_participants)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    bool sawAheadWarning = false;
    for (const bool rewindable : {false, true}) {
        for (const bool reverseOrder : {false, true}) {
            warning_capture warnings;
            auto algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
            cosim::execution execution{cosim::time_point{}, algorithm};
            auto earlier = std::make_shared<event_stepping_slave>(
                rewindable, event_kind::early_return, std::vector{at_ms(400)});
            auto later = std::make_shared<event_stepping_slave>(
                rewindable, event_kind::early_return, std::vector{at_ms(700)});
            if (reverseOrder) {
                execution.add_slave(later, "later");
                execution.add_slave(earlier, "earlier");
            } else {
                execution.add_slave(earlier, "earlier");
                execution.add_slave(later, "later");
            }
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 400);
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 300);
            BOOST_REQUIRE_EQUAL(earlier->handledAt.size(), 1u);
            BOOST_REQUIRE_EQUAL(later->handledAt.size(), 1u);
            BOOST_CHECK(earlier->handledAt.front() == at_ms(400));
            BOOST_CHECK(later->handledAt.front() == at_ms(700));
            if (!rewindable) {
                BOOST_CHECK_EQUAL(earlier->saves, 0);
                BOOST_CHECK_EQUAL(later->saves, 0);
                BOOST_CHECK_EQUAL(earlier->restores, 0);
                BOOST_CHECK_EQUAL(later->restores, 0);
                sawAheadWarning = sawAheadWarning ||
                    warnings.messages().find("cannot be rewound") != std::string::npos;
            }
        }
    }
    BOOST_CHECK(sawAheadWarning);
}

BOOST_AUTO_TEST_CASE(ecco_restores_ahead_participant_coordination)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    bool testedAhead = false;
    for (const bool reverseOrder : {false, true}) {
        auto earlier = std::make_shared<event_stepping_slave>(
            true, event_kind::early_return, std::vector{at_ms(400)});
        auto later = std::make_shared<event_stepping_slave>(
            true, event_kind::early_return, std::vector{at_ms(700)});
        earlier->replayable = false;
        later->replayable = false;
        cosim::slave_simulator first(earlier, "earlier"), second(later, "later");
        cosim::ecco_algorithm algorithm(params, 1);
        const auto firstIndex = reverseOrder ? 1 : 0;
        const auto secondIndex = reverseOrder ? 0 : 1;
        if (reverseOrder) {
            algorithm.add_simulator(secondIndex, &second, cosim::duration::zero());
            algorithm.add_simulator(firstIndex, &first, cosim::duration::zero());
        } else {
            algorithm.add_simulator(firstIndex, &first, cosim::duration::zero());
            algorithm.add_simulator(secondIndex, &second, cosim::duration::zero());
        }
        algorithm.setup(cosim::time_point{}, std::nullopt);
        algorithm.initialize();
        BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(0)).step_size), 400);
        const auto coordination = algorithm.export_current_state();
        const auto reached = coordination.get<std::int64_t>(
            "coordination." + std::to_string(secondIndex) + ".reached_ticks");
        if (cosim::time_point(cosim::duration(reached)) != at_ms(700)) continue;
        testedAhead = true;
        BOOST_TEST(coordination.get<bool>(
            "coordination." + std::to_string(secondIndex) + ".event_pending"));
        const auto firstState = first.save_state();
        const auto secondState = second.save_state();
        BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(400)).step_size), 300);
        BOOST_REQUIRE_EQUAL(later->handledAt.size(), 1u);
        const auto after = algorithm.export_current_state();
        first.restore_state(firstState);
        second.restore_state(secondState);
        algorithm.import_state(coordination);
        BOOST_CHECK_EQUAL(to_ms(algorithm.do_step_with_info(at_ms(400)).step_size), 300);
        BOOST_REQUIRE_EQUAL(later->handledAt.size(), 2u);
        const auto repeated = algorithm.export_current_state();
        for (const auto& field : after) {
            BOOST_CHECK_MESSAGE(
                repeated.get_child(field.first) == field.second,
                "ECCO snapshot field differs after restore: " << field.first);
        }
        first.release_state(firstState);
        second.release_state(secondState);
    }
    BOOST_TEST(testedAhead);
}

BOOST_AUTO_TEST_CASE(ecco_rejects_power_bonds_when_a_participant_is_ahead)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    bool rejectedAhead = false;
    for (const bool reverseOrder : {false, true}) {
        auto earlier = std::make_shared<event_stepping_slave>(
            true, event_kind::early_return, std::vector{at_ms(400)});
        auto later = std::make_shared<event_stepping_slave>(
            true, event_kind::early_return, std::vector{at_ms(700)});
        earlier->replayable = false;
        later->replayable = false;
        cosim::slave_simulator first(earlier, "earlier"), second(later, "later");
        cosim::ecco_algorithm algorithm(params, 1);
        const auto firstIndex = reverseOrder ? 1 : 0;
        const auto secondIndex = reverseOrder ? 0 : 1;
        if (reverseOrder) {
            algorithm.add_simulator(secondIndex, &second, cosim::duration::zero());
            algorithm.add_simulator(firstIndex, &first, cosim::duration::zero());
        } else {
            algorithm.add_simulator(firstIndex, &first, cosim::duration::zero());
            algorithm.add_simulator(secondIndex, &second, cosim::duration::zero());
        }
        const auto input = mock_slave::real_in_reference;
        const auto output = mock_slave::real_out_reference;
        algorithm.add_power_bond(
            {firstIndex, cosim::variable_type::real, input},
            {firstIndex, cosim::variable_type::real, output},
            {secondIndex, cosim::variable_type::real, input},
            {secondIndex, cosim::variable_type::real, output});
        algorithm.setup(cosim::time_point{}, std::nullopt);
        algorithm.initialize();
        try {
            algorithm.do_step_with_info(at_ms(0));
        } catch (const cosim::error& error) {
            BOOST_CHECK(is_unsupported_feature(error));
            BOOST_CHECK(std::string(error.what()).find("power bonds") != std::string::npos);
            rejectedAhead = true;
        }
    }
    BOOST_TEST(rejectedAhead);
}

BOOST_AUTO_TEST_CASE(event_shortening_rejects_a_fixed_step_peer)
{
    struct fixed_step_peer : recording_slave
    {
        fixed_step_peer() : recording_slave(false) {}
        cosim::model_description model_description() const override
        {
            auto description = recording_slave::model_description();
            description.capabilities.can_handle_variable_step = false;
            return description;
        }
    };
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<cosim::algorithm> algorithms[]{
        std::make_shared<cosim::fixed_step_algorithm>(base_step, 1),
        std::make_shared<cosim::ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        cosim::execution execution{cosim::time_point{}, algorithm};
        auto source = std::make_shared<event_stepping_slave>(
            false, event_kind::early_return, std::vector{at_ms(400), at_ms(700)});
        auto peer = std::make_shared<fixed_step_peer>();
        execution.add_slave(source, "source");
        execution.add_slave(peer, "fixed-peer");
        BOOST_CHECK_EQUAL(to_ms(execution.step()), 400);
        BOOST_CHECK_EXCEPTION(execution.simulate_until(at_ms(2000)), cosim::error,
            [](const cosim::error& error) {
                return std::string(error.what()).find(
                    "fixed-peer: the FMU does not support variable communication step sizes") !=
                    std::string::npos;
            });
        BOOST_CHECK(!execution.is_running());
        BOOST_CHECK_EXCEPTION(execution.export_current_state(), cosim::error,
            [](const cosim::error& error) {
                return std::string(error.what()).find("Cannot save algorithm state after a failed step") !=
                    std::string::npos;
            });
        BOOST_REQUIRE_EQUAL(peer->steps.size(), 1u);
        BOOST_CHECK(peer->steps.front().size == std::chrono::milliseconds(400));
        const auto sourceSteps = source->steps.size();
        BOOST_CHECK_EXCEPTION(execution.step(), cosim::error,
            [](const cosim::error& error) {
                return std::string(error.what()).find("restore a consistent execution snapshot") !=
                    std::string::npos;
            });
        BOOST_CHECK_EQUAL(source->steps.size(), sourceSteps);
        BOOST_CHECK_EQUAL(peer->steps.size(), 1u);
    }
}

BOOST_AUTO_TEST_CASE(failed_step_requires_consistent_execution_restore)
{
    struct failing_slave : recording_slave
    {
        failing_slave() : recording_slave(true) {}

        cosim::step_result do_step(cosim::time_point current, cosim::duration step) override
        {
            const auto result = recording_slave::do_step(current, step);
            if (failOnce) {
                failOnce = false;
                throw std::runtime_error("injected post-step failure");
            }
            return result;
        }

        bool failOnce = true;
    };

    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<cosim::algorithm> algorithms[]{
        std::make_shared<cosim::fixed_step_algorithm>(base_step, 1),
        std::make_shared<cosim::ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        cosim::execution execution{cosim::time_point{}, algorithm};
        auto failing = std::make_shared<failing_slave>();
        auto peer = std::make_shared<recording_slave>(true);
        execution.add_slave(failing, "failing");
        execution.add_slave(peer, "peer");
        execution.initialize();
        const auto snapshot = execution.export_current_state();

        BOOST_CHECK_THROW(execution.simulate_until(at_ms(1000)), cosim::error);
        BOOST_CHECK(!execution.is_running());
        const auto attempts = failing->steps.size();
        BOOST_CHECK_THROW(execution.step(), cosim::error);
        BOOST_CHECK_EQUAL(failing->steps.size(), attempts);
        BOOST_CHECK_THROW(execution.export_current_state(), cosim::error);

        execution.import_state(snapshot);
        BOOST_CHECK_EQUAL(to_ms(execution.current_time()), 0);
        BOOST_CHECK_EQUAL(to_ms(execution.step()), 1000);
    }
}

BOOST_AUTO_TEST_CASE(unrewindable_ahead_participant_receives_staged_event_on_catchup)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const bool ecco : {false, true}) {
        bool observedAhead = false;
        for (const bool reverseOrder : {false, true}) {
            warning_capture warnings;
            std::shared_ptr<cosim::algorithm> algorithm;
            if (ecco) algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
            else algorithm = std::make_shared<cosim::fixed_step_algorithm>(base_step, 1);
            cosim::execution execution{cosim::time_point{}, algorithm};
            auto output = std::make_shared<double>(0.0);
            auto earlier = std::make_shared<event_stepping_slave>(
                false, event_kind::early_return, std::vector{at_ms(400)},
                [output](cosim::time_point, cosim::duration, double) { return *output; });
            earlier->onEvent = [output] { *output = 7.0; };
            auto later = std::make_shared<event_stepping_slave>(
                false, event_kind::early_return, std::vector{at_ms(700)});
            cosim::simulator_index source, target;
            if (reverseOrder) {
                target = execution.add_slave(later, "later");
                source = execution.add_slave(earlier, "earlier");
            } else {
                source = execution.add_slave(earlier, "earlier");
                target = execution.add_slave(later, "later");
            }
            execution.connect_variables(
                cosim::variable_id{source, cosim::variable_type::real, mock_slave::real_out_reference},
                cosim::variable_id{target, cosim::variable_type::real, mock_slave::real_in_reference});
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 400);
            const bool ahead = warnings.messages().find("rewound to the event") != std::string::npos;
            if (!ahead) continue;
            observedAhead = true;
            BOOST_REQUIRE_EQUAL(later->steps.size(), 1u);
            BOOST_CHECK_EQUAL(later->steps.front().realIn, 0.0);
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 300);
            BOOST_REQUIRE_EQUAL(later->steps.size(), 1u);
            execution.step();
            BOOST_REQUIRE_GE(later->steps.size(), 2u);
            BOOST_CHECK(later->steps[1].start == at_ms(700));
            BOOST_CHECK_EQUAL(later->steps[1].realIn, 7.0);
        }
        BOOST_CHECK(observedAhead);
    }
}

BOOST_AUTO_TEST_CASE(ahead_outputs_are_not_used_for_current_function_events)
{
    const cosim::ecco_algorithm_params params{
        0.99, base_step, base_step / 10, base_step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const bool ecco : {false, true}) {
        bool observedAhead = false;
        for (const bool reverseOrder : {false, true}) {
            warning_capture warnings;
            std::shared_ptr<cosim::algorithm> algorithm;
            if (ecco) algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
            else algorithm = std::make_shared<cosim::fixed_step_algorithm>(base_step, 1);
            cosim::execution execution{cosim::time_point{}, algorithm};
            auto output = std::make_shared<double>(0.0);
            auto earlier = std::make_shared<event_stepping_slave>(
                false, event_kind::early_return, std::vector{at_ms(400)},
                [output](cosim::time_point, cosim::duration, double) { return *output; });
            earlier->onEvent = [output] { *output = 5.0; };
            auto later = std::make_shared<event_stepping_slave>(
                false, event_kind::early_return, std::vector{at_ms(700)},
                [](cosim::time_point time, cosim::duration, double) {
                    return 10.0 * cosim::to_double_time_point(time);
                });
            auto target = std::make_shared<recording_slave>(false);
            cosim::simulator_index first, second;
            if (reverseOrder) {
                second = execution.add_slave(later, "later");
                first = execution.add_slave(earlier, "earlier");
            } else {
                first = execution.add_slave(earlier, "earlier");
                second = execution.add_slave(later, "later");
            }
            const auto targetIndex = execution.add_slave(target, "target");
            const auto sum = execution.add_function(
                std::make_shared<cosim::vector_sum_function<double>>(2, 1));
            const cosim::simulator_index sources[]{first, second};
            for (int i = 0; i < 2; ++i) {
                execution.connect_variables(
                    cosim::variable_id{sources[i], cosim::variable_type::real,
                        mock_slave::real_out_reference},
                    cosim::function_io_id{sum, cosim::variable_type::real,
                        cosim::vector_sum_function<double>::in_io_reference(i, 0)});
            }
            execution.connect_variables(
                cosim::function_io_id{sum, cosim::variable_type::real,
                    cosim::vector_sum_function<double>::out_io_reference(0)},
                cosim::variable_id{targetIndex, cosim::variable_type::real,
                    mock_slave::real_in_reference});
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 400);
            if (warnings.messages().find("rewound to the event") == std::string::npos) continue;
            observedAhead = true;
            BOOST_CHECK_EQUAL(to_ms(execution.step()), 300);
            BOOST_REQUIRE_EQUAL(target->steps.size(), 2u);
            BOOST_CHECK(target->steps[1].start == at_ms(400));
            BOOST_CHECK_EQUAL(target->steps[1].realIn, 5.0);
        }
        BOOST_CHECK(observedAhead);
    }
}

BOOST_AUTO_TEST_CASE(fast_event_rewinds_a_slower_event_simulator)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(1200)});
    auto slow = std::make_shared<event_stepping_slave>(true);
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    BOOST_CHECK_EQUAL(s.run_to(3000), "1000 200 800 1000");

    BOOST_CHECK_EQUAL(describe(fast->steps), "0+1000 1000+1000 1200+800 2000+1000");
    BOOST_CHECK_EQUAL(describe(slow->steps), "0+3000 0+1200 1200+1800");
    BOOST_CHECK_EQUAL(slow->restores, 1);
    BOOST_CHECK_EQUAL(fast->restores, 0);
    BOOST_REQUIRE_EQUAL(fast->handledAt.size(), 1u);
    BOOST_CHECK(fast->handledAt.front() == at_ms(1200));
    BOOST_CHECK(slow->handledAt.empty());
    BOOST_CHECK_EQUAL(warnings.messages(), "");
}

BOOST_AUTO_TEST_CASE(unrewindable_slower_simulator_keeps_its_step_and_warns)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(1200)});
    auto slow = std::make_shared<event_stepping_slave>(false);
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    BOOST_CHECK_EQUAL(s.run_to(3000), "1000 200 800 1000");

    BOOST_CHECK_EQUAL(describe(fast->steps), "0+1000 1000+1000 1200+800 2000+1000");
    BOOST_CHECK_EQUAL(describe(slow->steps), "0+3000");
    BOOST_CHECK_EQUAL(slow->saves, 0);
    BOOST_CHECK_EQUAL(slow->restores, 0);
    BOOST_REQUIRE_EQUAL(fast->handledAt.size(), 1u);
    BOOST_CHECK(fast->handledAt.front() == at_ms(1200));

    const auto messages = warnings.messages();
    BOOST_CHECK(messages.find("cannot save and restore its state") != std::string::npos);
    BOOST_CHECK(messages.find("could not be rewound") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(slow_simulator_event_stops_its_own_step_and_fast_simulator_waits)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(true);
    auto slow = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(1500)});
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    BOOST_CHECK_EQUAL(s.run_to(3000), "1000 500 500 1000");

    BOOST_CHECK_EQUAL(describe(fast->steps), "0+1000 1000+500 1500+500 2000+1000");
    BOOST_CHECK_EQUAL(describe(slow->steps), "0+3000 1500+1500");
    BOOST_CHECK_EQUAL(slow->restores, 0);
    BOOST_REQUIRE_EQUAL(slow->handledAt.size(), 1u);
    BOOST_CHECK(slow->handledAt.front() == at_ms(1500));
    BOOST_CHECK_EQUAL(warnings.messages(), "");
}

BOOST_AUTO_TEST_CASE(time_event_inside_a_decimated_group_shortens_the_group)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(true);
    auto slow = std::make_shared<event_stepping_slave>(
        true, event_kind::time_event, std::vector{at_ms(1500)});
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    BOOST_CHECK_EQUAL(s.run_to(2000), "1000 500 500");

    BOOST_CHECK_EQUAL(describe(fast->steps), "0+1000 1000+500 1500+500");
    BOOST_CHECK_EQUAL(describe(slow->steps), "0+1500 1500+1500");
    BOOST_CHECK_EQUAL(slow->restores, 0);
    BOOST_REQUIRE_EQUAL(slow->handledAt.size(), 1u);
    BOOST_CHECK(slow->handledAt.front() == at_ms(1500));
    BOOST_CHECK_EQUAL(warnings.messages(), "");
}

BOOST_AUTO_TEST_CASE(event_free_decimated_event_simulators_keep_their_step_sizes)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(true);
    auto slow = std::make_shared<event_stepping_slave>(true);
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    BOOST_CHECK_EQUAL(s.run_to(6000), "1000 1000 1000 1000 1000 1000");

    BOOST_CHECK_EQUAL(describe(slow->steps), "0+3000 3000+3000");
    BOOST_CHECK_EQUAL(slow->restores, 0);
    BOOST_CHECK_EQUAL(fast->restores, 0);
    BOOST_CHECK_EQUAL(warnings.messages(), "");
}

BOOST_AUTO_TEST_CASE(ordinary_different_factors_transfer_on_the_common_grid)
{
    scenario s;
    auto source = std::make_shared<recording_slave>(
        false, [](cosim::time_point t, cosim::duration, double) {
            return cosim::to_double_time_point(t);
        });
    auto target = std::make_shared<recording_slave>(false);
    const auto sourceIndex = s.add(source, "source", 2);
    const auto targetIndex = s.add(target, "target", 3);
    s.execution.connect_variables(
        cosim::variable_id{sourceIndex, cosim::variable_type::real, mock_slave::real_out_reference},
        cosim::variable_id{targetIndex, cosim::variable_type::real, mock_slave::real_in_reference});
    BOOST_CHECK_EQUAL(s.run_to(9000), "1000 1000 1000 1000 1000 1000 1000 1000 1000");
    BOOST_CHECK_EQUAL(describe(source->steps), "0+2000 2000+2000 4000+2000 6000+2000 8000+2000");
    BOOST_CHECK_EQUAL(describe(target->steps), "0+3000 3000+3000 6000+3000");
    BOOST_REQUIRE_EQUAL(target->steps.size(), 3u);
    BOOST_CHECK_EQUAL(target->steps[0].realIn, 0.0);
    BOOST_CHECK_EQUAL(target->steps[1].realIn, 0.0);
    BOOST_CHECK_EQUAL(target->steps[2].realIn, 6.0);
}

BOOST_AUTO_TEST_CASE(simulators_that_cannot_return_early_checkpoint_only_when_another_can_precede_them)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(true);
    auto slow = std::make_shared<event_stepping_slave>(true);
    fast->declaresEarlyReturn = false;
    slow->declaresEarlyReturn = false;
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    BOOST_CHECK_EQUAL(s.run_to(6000), "1000 1000 1000 1000 1000 1000");

    // Nothing can stop the fast simulator before its step ends, so it never
    // needs a checkpoint. The slow one can be preceded by the fast one.
    BOOST_CHECK_EQUAL(fast->saves, 0);
    BOOST_CHECK_EQUAL(slow->saves, 2);
    BOOST_CHECK_EQUAL(warnings.messages(), "");
}

BOOST_AUTO_TEST_CASE(simulator_that_might_return_early_makes_the_others_checkpoint)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(true);
    auto slow = std::make_shared<event_stepping_slave>(true);
    fast->declaresEarlyReturn = false;
    s.add(fast, "fast");
    s.add(slow, "slow", 3);

    s.run_to(3000);

    BOOST_CHECK_GT(fast->saves, 0);
    BOOST_CHECK_GT(slow->saves, 0);
}

BOOST_AUTO_TEST_CASE(undeclared_early_return_cannot_rewind_a_simulator_without_a_checkpoint)
{
    warning_capture warnings;
    scenario s;
    auto early = std::make_shared<event_stepping_slave>(
        true, event_kind::early_return, std::vector{at_ms(1200)});
    auto other = std::make_shared<event_stepping_slave>(true);
    early->declaresEarlyReturn = false;
    other->declaresEarlyReturn = false;
    s.add(early, "early", 3);
    s.add(other, "other", 3);

    s.run_to(3000);

    // Neither simulator declares that it might return early, so no checkpoints
    // were taken and the other simulator keeps its step.
    BOOST_CHECK_EQUAL(early->saves, 0);
    BOOST_CHECK_EQUAL(other->saves, 0);
    BOOST_CHECK_EQUAL(other->restores, 0);
    BOOST_REQUIRE_EQUAL(early->handledAt.size(), 1u);
    BOOST_CHECK(early->handledAt.front() == at_ms(1200));
    BOOST_CHECK(
        warnings.messages().find("could not be rewound") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(single_event_simulator_rewinds_a_capable_ordinary_participant)
{
    warning_capture warnings;
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(
        false,
        event_kind::early_return,
        std::vector{at_ms(1200)},
        [](cosim::time_point t, cosim::duration, double) {
            return cosim::to_double_time_point(t);
        });
    auto slow = std::make_shared<recording_slave>(true);
    const auto fastIndex = s.add(fast, "fast");
    const auto slowIndex = s.add(slow, "slow", 3);
    s.execution.connect_variables(
        cosim::variable_id{fastIndex, cosim::variable_type::real, mock_slave::real_out_reference},
        cosim::variable_id{slowIndex, cosim::variable_type::real, mock_slave::real_in_reference});

    s.run_to(6000);

    BOOST_CHECK_EQUAL(fast->saves, 0);
    BOOST_CHECK(slow->saves > 0);
    BOOST_CHECK_EQUAL(slow->restores, 1);
    BOOST_REQUIRE_EQUAL(fast->handledAt.size(), 1u);
    BOOST_CHECK(fast->handledAt.front() == at_ms(1200));
    BOOST_CHECK_EQUAL(describe(slow->steps), "0+3000 0+1200 1200+1800 3000+3000");
    // At a common grid point the slow simulator sees the fast output.
    BOOST_REQUIRE_EQUAL(slow->steps.size(), 4u);
    BOOST_CHECK_EQUAL(slow->steps[0].realIn, 0.0);
    BOOST_CHECK_EQUAL(slow->steps[3].realIn, 3.0);
    BOOST_CHECK(warnings.messages().find("fast is connected to slow") != std::string::npos);
    BOOST_CHECK(warnings.messages().find("does not support Event Mode") != std::string::npos);
    BOOST_CHECK(warnings.messages().find("could not be rewound") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(fast_event_to_slow_ordinary_participant_records_input_application)
{
    for (const bool canSaveState : {false, true}) {
        warning_capture warnings;
        scenario s;
        bool eventOccurred = false;
        auto fast = std::make_shared<event_stepping_slave>(
            false, event_kind::early_return, std::vector{at_ms(1200)},
            [&eventOccurred](cosim::time_point, cosim::duration, double) {
                return eventOccurred ? 7.0 : 0.0;
            });
        fast->onEvent = [&eventOccurred] { eventOccurred = true; };
        auto slow = std::make_shared<recording_slave>(canSaveState);
        const auto source = s.add(fast, "fast");
        const auto target = s.add(slow, "slow", 3);
        s.execution.connect_variables(
            cosim::variable_id{source, cosim::variable_type::real, mock_slave::real_out_reference},
            cosim::variable_id{target, cosim::variable_type::real, mock_slave::real_in_reference});

        BOOST_CHECK_EQUAL(s.run_to(6000), "1000 200 800 1000 1000 1000 1000");
        BOOST_REQUIRE_EQUAL(fast->handledAt.size(), 1u);
        BOOST_CHECK(fast->handledAt.front() == at_ms(1200));
        BOOST_CHECK_EQUAL(slow->restores, canSaveState ? 1 : 0);
        BOOST_CHECK_EQUAL(describe(slow->steps),
            canSaveState ? "0+3000 0+1200 1200+1800 3000+3000" : "0+3000 3000+3000");
        BOOST_REQUIRE_GE(slow->steps.size(), 2u);
        BOOST_CHECK_EQUAL(slow->steps.front().realIn, 0.0);
        if (canSaveState) {
            BOOST_CHECK_EQUAL(slow->steps[1].realIn, 0.0);
            BOOST_CHECK_EQUAL(slow->steps[2].realIn, 7.0);
        }
        BOOST_CHECK_EQUAL(slow->steps.back().realIn, 7.0);
        BOOST_CHECK(warnings.messages().find("does not support Event Mode") != std::string::npos);
        BOOST_CHECK_EQUAL(
            warnings.messages().find("could not be rewound") != std::string::npos,
            !canSaveState);
    }
}

BOOST_AUTO_TEST_CASE(later_event_replaces_the_pending_value_for_an_ahead_ordinary_peer)
{
    warning_capture warnings;
    scenario s;
    double output = 0.0;
    auto fast = std::make_shared<event_stepping_slave>(
        false, event_kind::early_return, std::vector{at_ms(1200), at_ms(1800)},
        [&output](cosim::time_point, cosim::duration, double) { return output; });
    fast->onEvent = [&output] { output = output == 0.0 ? 7.0 : 9.0; };
    auto slow = std::make_shared<recording_slave>(false);
    const auto source = s.add(fast, "fast");
    const auto target = s.add(slow, "slow", 3);
    s.execution.connect_variables(
        cosim::variable_id{source, cosim::variable_type::real, mock_slave::real_out_reference},
        cosim::variable_id{target, cosim::variable_type::real, mock_slave::real_in_reference});

    s.run_to(6000);
    BOOST_REQUIRE_EQUAL(fast->handledAt.size(), 2u);
    BOOST_CHECK(fast->handledAt[0] == at_ms(1200));
    BOOST_CHECK(fast->handledAt[1] == at_ms(1800));
    BOOST_CHECK_EQUAL(describe(slow->steps), "0+3000 3000+3000");
    BOOST_REQUIRE_EQUAL(slow->steps.size(), 2u);
    BOOST_CHECK_EQUAL(slow->steps[0].realIn, 0.0);
    BOOST_CHECK_EQUAL(slow->steps[1].realIn, 9.0);
    BOOST_CHECK(warnings.messages().find("could not be rewound") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(decimated_simulator_without_event_mode_cannot_advertise_time_events)
{
    scenario s;
    s.add(std::make_shared<event_stepping_slave>(true), "event");
    s.add(
        std::make_shared<detailed_step_mock_slave>(
            [](cosim::time_point t, cosim::duration d) {
                return cosim::step_result_info{
                    cosim::step_result::complete, t + d, false, false, false, false};
            },
            std::function<void()>{},
            [] {
                return cosim::event_handling_status{false, at_ms(1500)};
            }),
        "legacy",
        2);

    BOOST_CHECK_EXCEPTION(s.execution.step(), cosim::error, is_unsupported_feature);
}

BOOST_AUTO_TEST_CASE(decimated_simulator_without_event_mode_cannot_return_early)
{
    scenario s;
    s.add(std::make_shared<event_stepping_slave>(true), "event");
    s.add(
        std::make_shared<detailed_step_mock_slave>(
            [](cosim::time_point t, cosim::duration d) {
                return cosim::step_result_info{
                    cosim::step_result::complete, t + d / 2, true, false, false, false};
            }),
        "legacy",
        2);

    BOOST_CHECK_EXCEPTION(s.execution.step(), cosim::error, is_unsupported_feature);
}

BOOST_AUTO_TEST_CASE(decimation_state_is_persisted_when_simulators_are_aligned)
{
    scenario s;
    auto fast = std::make_shared<event_stepping_slave>(true);
    auto slow = std::make_shared<event_stepping_slave>(true);
    s.add(fast, "fast");
    s.add(slow, "slow", 3);
    s.execution.initialize();

    s.execution.step();
    // The slow simulator is ahead and relies on a rewind checkpoint.
    BOOST_CHECK_EXCEPTION(s.execution.export_current_state(), cosim::error, is_unsupported_feature);

    s.execution.step();
    s.execution.step();
    BOOST_REQUIRE_EQUAL(to_ms(s.execution.current_time()), 3000);
    const auto state = s.execution.export_current_state();
    const auto ticks = state.get_optional<std::int64_t>("algorithm_state.decimation.1.reached_ticks");
    BOOST_REQUIRE(ticks.has_value());
    BOOST_CHECK_EQUAL(*ticks, 3'000'000'000);

    scenario restored;
    auto restoredFast = std::make_shared<event_stepping_slave>(true);
    auto restoredSlow = std::make_shared<event_stepping_slave>(true);
    restored.add(restoredFast, "fast");
    restored.add(restoredSlow, "slow", 3);
    restored.execution.initialize();
    restored.execution.import_state(state);
    BOOST_CHECK_EQUAL(to_ms(restored.execution.current_time()), 3000);
    BOOST_CHECK_EQUAL(to_ms(restored.execution.step()), 1000);
    BOOST_CHECK_EQUAL(describe(restoredFast->steps), "3000+1000");
    BOOST_CHECK_EQUAL(describe(restoredSlow->steps), "3000+3000");
    BOOST_CHECK_EQUAL(to_ms(restored.execution.step()), 1000);
    BOOST_CHECK_EQUAL(describe(restoredFast->steps), "3000+1000 4000+1000");
    BOOST_CHECK_EQUAL(describe(restoredSlow->steps), "3000+3000");

    auto corrupt = state;
    corrupt.get_child("algorithm_state.decimation").erase("1");
    BOOST_CHECK_EXCEPTION(restored.execution.import_state(corrupt), cosim::error, is_bad_file);
}

BOOST_AUTO_TEST_CASE(legacy_algorithm_state_has_no_decimation_section)
{
    scenario s;
    s.add(std::make_shared<recording_slave>(true), "fast");
    s.add(std::make_shared<recording_slave>(true), "slow", 3);
    s.execution.initialize();
    s.run_to(3000);
    const auto state = s.execution.export_current_state();
    BOOST_CHECK(!state.get_child_optional("algorithm_state.decimation").has_value());
}
