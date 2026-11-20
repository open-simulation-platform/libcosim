// Tests for execution-level convenience API: add_fmu(), get_simulators(),
// start_simulation()/get_status().

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/level.hpp>
#include <cosim/log/logger.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/observer/last_value_observer.hpp>

#include "mock_slave.hpp"

#include <chrono>
#include <thread>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


namespace
{

cosim::value_reference find_reference(
    const cosim::execution& exe,
    cosim::simulator_index sim,
    const std::string& name)
{
    for (const auto& v : exe.get_model_description(sim).variables) {
        if (v.name == name) return v.reference;
    }
    throw std::runtime_error("No variable named " + name);
}


void test_add_fmu_and_get_simulators(const cosim::filesystem::path& referenceFmuDir)
{
    auto exe = cosim::execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    REQUIRE(exe.get_simulators().empty());

    // A Reference FMU, built for the platform the tests run on
    const auto fmu = referenceFmuDir / "Feedthrough.fmu";
    const auto a = cosim::add_fmu(exe, fmu, "a");
    const auto b = cosim::add_fmu(exe, fmu, "b", cosim::to_duration(0.2));
    REQUIRE(a == 0);
    REQUIRE(b == 1);

    const auto sims = exe.get_simulators();
    REQUIRE(sims.size() == 2);
    REQUIRE(sims[0].name == "a" && sims[0].index == a);
    REQUIRE(sims[1].name == "b" && sims[1].index == b);

    // The added FMUs work: an overridden input shows up on the output
    auto obs = std::make_shared<cosim::last_value_observer>();
    auto man = std::make_shared<cosim::override_manipulator>();
    exe.add_observer(obs);
    exe.add_manipulator(man);
    man->override_integer_variable(a, find_reference(exe, a, "Int32_input"), 42);
    REQUIRE(exe.simulate_until(cosim::to_time_point(0.5)));
    REQUIRE(obs->get_integer(a, {find_reference(exe, a, "Int32_output")}).at(0) == 42);

    // A missing FMU is reported as an error, and does not add a simulator
    bool threw = false;
    try {
        cosim::add_fmu(exe, referenceFmuDir / "no_such.fmu", "c");
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(exe.get_simulators().size() == 2);
}


template<typename Predicate>
bool wait_until(Predicate pred, std::chrono::milliseconds timeout = std::chrono::seconds(10))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}


cosim::execution make_mock_execution(std::function<void(cosim::time_point, cosim::duration)> stepAction = nullptr)
{
    auto exe = cosim::execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    exe.add_slave(
        std::make_shared<mock_slave>(nullptr, nullptr, nullptr, nullptr, stepAction),
        "mock");
    return exe;
}


void test_start_stop_status()
{
    auto exe = make_mock_execution();
    auto status = exe.get_status();
    REQUIRE(status.state == cosim::execution_state::stopped);
    REQUIRE(status.error_code == cosim::errc::success);
    REQUIRE(status.current_time == cosim::time_point());

    exe.start_simulation();
    REQUIRE(exe.get_status().state == cosim::execution_state::running);
    REQUIRE(wait_until([&] { return exe.get_status().current_time > cosim::to_time_point(1.0); }));

    // A second start while running is rejected
    bool rejected = false;
    try {
        exe.start_simulation();
    } catch (const cosim::error& e) {
        rejected = e.code() == cosim::errc::invalid_operation;
    }
    REQUIRE(rejected);

    exe.stop_simulation();
    REQUIRE(wait_until([&] { return exe.get_status().state == cosim::execution_state::stopped; }));
    const auto stoppedAt = exe.current_time();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(exe.current_time() == stoppedAt);

    // It can be started again, and blocking calls work afterwards
    exe.start_simulation();
    REQUIRE(wait_until([&] { return exe.current_time() > stoppedAt; }));
    exe.stop_simulation();
    REQUIRE(wait_until([&] { return !exe.is_running(); }));
    REQUIRE(exe.simulate_until(exe.current_time() + cosim::to_duration(0.5)));
}


void test_stop_immediately_after_start()
{
    auto exe = make_mock_execution();
    exe.start_simulation();
    exe.stop_simulation();
    REQUIRE(wait_until([&] { return exe.get_status().state == cosim::execution_state::stopped; }));
}


void test_error_in_background_run()
{
    auto exe = make_mock_execution([](cosim::time_point t, cosim::duration) {
        if (t >= cosim::to_time_point(0.5)) {
            throw cosim::error(make_error_code(cosim::errc::model_error), "mock failure");
        }
    });
    exe.start_simulation();
    REQUIRE(wait_until([&] { return exe.get_status().state == cosim::execution_state::error; }));
    const auto status = exe.get_status();
    REQUIRE(status.error_code != cosim::errc::success);
    REQUIRE(!status.error_message.empty());
    REQUIRE(!exe.is_running());

    // The error is reported until the next start
    REQUIRE(exe.get_status().state == cosim::execution_state::error);
}


void test_destroy_while_running()
{
    auto exe = std::make_unique<cosim::execution>(make_mock_execution());
    exe->start_simulation();
    REQUIRE(wait_until([&] { return exe->current_time() > cosim::to_time_point(0.5); }));
    exe.reset(); // must stop and join the background thread, not hang
}


void test_log_level()
{
    // A record at a given severity is only opened if it passes the filter
    const auto accepted = [](cosim::log::severity_level severity) {
        return static_cast<bool>(cosim::log::logger().open_record(boost::log::keywords::severity = severity));
    };
    cosim::log::setup_console_logging();
    cosim::log::setup_console_logging(); // second call has no effect

    cosim::log::set_output_level(cosim::log::level::error);
    REQUIRE(!accepted(cosim::log::warning));
    REQUIRE(accepted(cosim::log::error));
    cosim::log::set_output_level(cosim::log::level::trace);
    REQUIRE(accepted(cosim::log::trace));
    cosim::log::set_output_level(cosim::log::level::warning);
    REQUIRE(!accepted(cosim::log::info));
    REQUIRE(accepted(cosim::log::warning));
}

} // namespace


int main()
{
    try {
        test_log_level(); // leaves the level at "warning"

        const auto referenceFmuDir = std::getenv("REFERENCE_FMU_V2_DIR");
        REQUIRE(referenceFmuDir);
        test_add_fmu_and_get_simulators(referenceFmuDir);
        test_start_stop_status();
        test_stop_immediately_after_start();
        test_error_in_background_run();
        test_destroy_while_running();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
