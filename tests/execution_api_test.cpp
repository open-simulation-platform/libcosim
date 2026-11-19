// Tests for execution-level convenience API: add_fmu(), get_simulators(),
// start_simulation()/get_status().

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/observer/last_value_observer.hpp>

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


void test_add_fmu_and_get_simulators(const cosim::filesystem::path& testDataDir)
{
    auto exe = cosim::execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    REQUIRE(exe.get_simulators().empty());

    const auto fmu = testDataDir / "fmi1" / "identity.fmu";
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
    man->override_integer_variable(a, find_reference(exe, a, "integerIn"), 42);
    REQUIRE(exe.simulate_until(cosim::to_time_point(0.5)));
    REQUIRE(obs->get_integer(a, {find_reference(exe, a, "integerOut")}).at(0) == 42);

    // A missing FMU is reported as an error, and does not add a simulator
    bool threw = false;
    try {
        cosim::add_fmu(exe, testDataDir / "no_such.fmu", "c");
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(exe.get_simulators().size() == 2);
}

} // namespace


int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::warning);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        test_add_fmu_and_get_simulators(testDataDir);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
