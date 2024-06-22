#define BOOST_TEST_MODULE cosim::slave_simulator unittest
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/slave_simulator.hpp>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(slave_simulator_save_state)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    auto importer = cosim::fmi::importer::create();
    const std::string modelName = "Dahlquist";
    auto fmu = importer->import(
        cosim::filesystem::path(testDataDir) / "fmi2" / (modelName + ".fmu"));
    const auto modelDescription = fmu->model_description();
    BOOST_TEST(modelDescription->uuid == "{221063D2-EF4A-45FE-B954-B5BFEEA9A59B}");
    BOOST_TEST_REQUIRE(modelDescription->can_save_state);

    const auto xVar = cosim::find_variable(*modelDescription, "x")->reference;

    auto t = cosim::time_point();
    const auto dt = cosim::to_duration(1.0);

    auto sim = cosim::slave_simulator(
        fmu->instantiate_slave("testSlave"),
        "testSlave");
    sim.expose_for_getting(cosim::variable_type::real, xVar);

    sim.setup(t, {}, {});
    const auto value0 = sim.get_real(xVar);
    BOOST_TEST(value0 == 1.0);
    const auto state0 = sim.save_state();

    sim.start_simulation();
    sim.do_step(t, dt);
    t += dt;
    const auto value1 = sim.get_real(xVar);
    BOOST_TEST((0.0 < value1 && value1 < value0));
    const auto state1 = sim.save_state();

    sim.do_step(t, dt);
    t += dt;
    const auto value2 = sim.get_real(xVar);
    BOOST_TEST((0.0 < value2 && value2 < value1));
    const auto state2 = sim.save_state();

    sim.do_step(t, dt);
    t += dt;
    const auto value3 = sim.get_real(xVar);
    BOOST_TEST((0.0 < value3 && value3 < value2));
    const auto state3 = state2;
    sim.save_state(state3); // Overwrite the previously-saved state

    sim.restore_state(state1);
    const auto value1Test = sim.get_real(xVar);
    BOOST_TEST(value1Test == value1);

    sim.restore_state(state3);
    const auto value3Test = sim.get_real(xVar);
    BOOST_TEST(value3Test == value3);

    sim.restore_state(state0);
    t = cosim::time_point();
    sim.start_simulation();
    sim.do_step(t, dt);
    t += dt;
    sim.do_step(t, dt);
    t += dt;
    const auto value0To2Test = sim.get_real(xVar);
    BOOST_TEST(value0To2Test == value2);

    sim.release_state(state0);
    sim.release_state(state1);
    sim.release_state(state3);
}
