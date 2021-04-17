#define BOOST_TEST_MODULE proxyfmu unittests

#include <cosim/log/simple.hpp>
#include <cosim/proxy/remote_fmu.hpp>
#include <cosim/ssp/ssp_loader.hpp>

#include <boost/test/unit_test.hpp>

#include <cstdlib>

using namespace cosim;

BOOST_AUTO_TEST_CASE(test_ssp)
{
    log::setup_simple_console_logging();
    log::set_global_output_level(log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    boost::filesystem::path sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "proxy";

    ssp_loader loader;
    const auto config = loader.load(sspFile);
    auto exec = execution(config.start_time, config.algorithm);
    const auto entityMaps = inject_system_structure(
        exec,
        config.system_structure,
        config.parameter_sets.at(""));
    BOOST_CHECK(entityMaps.simulators.size() == 2);

    auto result = exec.simulate_until(to_time_point(1e-3));
    BOOST_REQUIRE(result.get());
}

BOOST_AUTO_TEST_CASE(test_fmi1)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);

    auto path = proxyfmu::filesystem::path(testDataDir) / "fmi1" / "identity.fmu";
    auto fmu = proxy::remote_fmu(path);

    const auto d = fmu.description();
    BOOST_TEST(d->name == "no.viproma.demo.identity");
    BOOST_TEST(d->uuid.size() == 36U);
    BOOST_TEST(d->description ==
        "Has one input and one output of each type, and outputs are always set equal to inputs");
    BOOST_TEST(d->author == "Lars Tandle Kyllingstad");

    value_reference
        realIn = 0,
        integerIn = 0, booleanIn = 0, stringIn = 0,
        realOut = 0, integerOut = 0, booleanOut = 0, stringOut = 0;
    for (const auto& v : d->variables) {
        if (v.name == "realIn") {
            realIn = v.reference;
        } else if (v.name == "integerIn") {
            integerIn = v.reference;
        } else if (v.name == "booleanIn") {
            booleanIn = v.reference;
        } else if (v.name == "stringIn") {
            stringIn = v.reference;
        } else if (v.name == "realOut") {
            realOut = v.reference;
        } else if (v.name == "integerOut") {
            integerOut = v.reference;
        } else if (v.name == "booleanOut") {
            booleanOut = v.reference;
        } else if (v.name == "stringOut") {
            stringOut = v.reference;
        }

        if (v.name == "realIn") {
            BOOST_TEST(v.type == variable_type::real);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::input);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 0.0);
        } else if (v.name == "stringOut") {
            BOOST_TEST(v.type == variable_type::string);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::output);
            BOOST_TEST(!v.start.has_value());
        } else if (v.name == "booleanIn") {
            BOOST_TEST(v.type == variable_type::boolean);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::input);
            bool start = std::get<bool>(*v.start);
            BOOST_TEST(start == false);
        }
    }

    const auto tStart = time_point();
    const auto tMax = to_time_point(1.0);
    const auto dt = to_duration(0.1);

    auto instance = fmu.instantiate("testSlave");
    instance->setup(tStart, tMax, std::nullopt).get();
    instance->start_simulation().get();

    double realVal = 0.0;
    int integerVal = 0;
    bool booleanVal = false;
    std::string stringVal;

    for (auto t = tStart; t < tMax; t += dt) {

        auto vars = instance->get_variables(
                                gsl::make_span(&realOut, 1),
                                gsl::make_span(&integerOut, 1),
                                gsl::make_span(&booleanOut, 1),
                                gsl::make_span(&stringOut, 1))
                        .get();

        BOOST_TEST(vars.real[0] == realVal);
        BOOST_TEST(vars.integer[0] == integerVal);
        BOOST_TEST(vars.boolean[0] == booleanVal);
        BOOST_TEST(vars.string[0] == stringVal);

        realVal += 1.0;
        integerVal += 1;
        booleanVal = !booleanVal;
        stringVal += 'a';

        instance->do_step(t, dt).get();

        instance->set_variables(
                    gsl::make_span(&realIn, 1), gsl::make_span(&realVal, 1),
                    gsl::make_span(&integerIn, 1), gsl::make_span(&integerVal, 1),
                    gsl::make_span(&booleanIn, 1), gsl::make_span(&booleanVal, 1),
                    gsl::make_span(&stringIn, 1), gsl::make_span(&stringVal, 1))
            .get();
    }

    instance->end_simulation().get();
}

BOOST_AUTO_TEST_CASE(test_fmi2)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);

    auto path = proxyfmu::filesystem::path(testDataDir) / "fmi2" / "WaterTank_Control.fmu";
    auto fmu = proxy::remote_fmu(path);

    const auto d = fmu.description();
    BOOST_TEST(d->name == "WaterTank.Control");
    BOOST_TEST(d->uuid == "{ad6d7bad-97d1-4fb9-ab3e-00a0d051e42c}");
    BOOST_TEST(d->description.empty());
    BOOST_TEST(d->author.empty());
    BOOST_TEST(d->version.empty());

    auto instance = fmu.instantiate("testSlave");
    instance->setup(
                cosim::to_time_point(0.0),
                cosim::to_time_point(1.0),
                std::nullopt)
        .get();

    bool foundValve = false;
    bool foundMinlevel = false;
    for (const auto& v : d->variables) {
        if (v.name == "valve") {
            foundValve = true;
            BOOST_TEST(v.variability == variable_variability::continuous);
            BOOST_TEST(v.causality == variable_causality::output);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 0.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            varVal = instance->get_variables(gsl::make_span(&varID, 1), {}, {}, {}).get().real[0];
            BOOST_TEST(varVal == 0.0);
        } else if (v.name == "minlevel") {
            foundMinlevel = true;
            BOOST_TEST(v.variability == variable_variability::fixed);
            BOOST_TEST(v.causality == variable_causality::parameter);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 1.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            varVal = instance->get_variables(gsl::make_span(&varID, 1), {}, {}, {}).get().real[0];
            BOOST_TEST(varVal == 1.0);
        }
    }
    BOOST_TEST(foundValve);
    BOOST_TEST(foundMinlevel);
}
