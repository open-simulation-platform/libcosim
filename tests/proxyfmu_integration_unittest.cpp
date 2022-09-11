
#include <cosim/log/simple.hpp>
#include <cosim/proxy/remote_fmu.hpp>
#include <cosim/ssp/ssp_loader.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <cstdlib>

using namespace cosim;

TEST_CASE("test_ssp")
{
    log::setup_simple_console_logging();
    log::set_global_output_level(log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(testDataDir != nullptr);
    cosim::filesystem::path sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "proxy";

    ssp_loader loader;
    const auto config = loader.load(sspFile);
    auto exec = execution(config.start_time, config.algorithm);
    const auto entityMaps = inject_system_structure(
        exec,
        config.system_structure,
        config.parameter_sets.at(""));
    CHECK(entityMaps.simulators.size() == 2);

    auto result = exec.simulate_until(to_time_point(1e-3));
    REQUIRE(result);
}

TEST_CASE("test_fmi1")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);

    auto path = proxyfmu::filesystem::path(testDataDir) / "fmi1" / "identity.fmu";
    auto fmu = proxy::remote_fmu(path);

    const auto d = fmu.description();
    CHECK(d->name == "no.viproma.demo.identity");
    CHECK(d->uuid.size() == 36U);
    CHECK(d->description ==
        "Has one input and one output of each type, and outputs are always set equal to inputs");
    CHECK(d->author == "Lars Tandle Kyllingstad");

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
            CHECK(v.type == variable_type::real);
            CHECK(v.variability == variable_variability::discrete);
            CHECK(v.causality == variable_causality::input);
            double start = std::get<double>(*v.start);
            CHECK(start == 0.0);
        } else if (v.name == "stringOut") {
            CHECK(v.type == variable_type::string);
            CHECK(v.variability == variable_variability::discrete);
            CHECK(v.causality == variable_causality::output);
            CHECK(!v.start.has_value());
        } else if (v.name == "booleanIn") {
            CHECK(v.type == variable_type::boolean);
            CHECK(v.variability == variable_variability::discrete);
            CHECK(v.causality == variable_causality::input);
            bool start = std::get<bool>(*v.start);
            CHECK(start == false);
        }
    }

    const auto tStart = time_point();
    const auto tMax = to_time_point(1.0);
    const auto dt = to_duration(0.1);

    auto instance = fmu.instantiate("testSlave");
    instance->setup(tStart, tMax, std::nullopt);
    instance->start_simulation();

    double realVal = 0.0;
    int integerVal = 0;
    bool booleanVal = false;
    std::string stringVal;

    for (auto t = tStart; t < tMax; t += dt) {
        cosim::slave::variable_values vars;
        instance->get_variables(&vars,
            gsl::make_span(&realOut, 1),
            gsl::make_span(&integerOut, 1),
            gsl::make_span(&booleanOut, 1),
            gsl::make_span(&stringOut, 1));

        CHECK(vars.real[0] == realVal);
        CHECK(vars.integer[0] == integerVal);
        CHECK(vars.boolean[0] == booleanVal);
        CHECK(vars.string[0] == stringVal);

        realVal += 1.0;
        integerVal += 1;
        booleanVal = !booleanVal;
        stringVal += 'a';

        instance->do_step(t, dt);

        instance->set_variables(
            gsl::make_span(&realIn, 1), gsl::make_span(&realVal, 1),
            gsl::make_span(&integerIn, 1), gsl::make_span(&integerVal, 1),
            gsl::make_span(&booleanIn, 1), gsl::make_span(&booleanVal, 1),
            gsl::make_span(&stringIn, 1), gsl::make_span(&stringVal, 1));
    }

    instance->end_simulation();
}

TEST_CASE("test_fmi2")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);

    auto path = proxyfmu::filesystem::path(testDataDir) / "fmi2" / "WaterTank_Control.fmu";
    auto fmu = proxy::remote_fmu(path);

    const auto d = fmu.description();
    CHECK(d->name == "WaterTank.Control");
    CHECK(d->uuid == "{ad6d7bad-97d1-4fb9-ab3e-00a0d051e42c}");
    CHECK(d->description.empty());
    CHECK(d->author.empty());
    CHECK(d->version.empty());

    auto instance = fmu.instantiate("testSlave");
    instance->setup(
        cosim::to_time_point(0.0),
        cosim::to_time_point(1.0),
        std::nullopt);

    bool foundValve = false;
    bool foundMinlevel = false;
    for (const auto& v : d->variables) {
        if (v.name == "valve") {
            foundValve = true;
            CHECK(v.variability == variable_variability::continuous);
            CHECK(v.causality == variable_causality::output);
            double start = std::get<double>(*v.start);
            CHECK(start == 0.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            cosim::slave::variable_values vars;
            instance->get_variables(&vars, gsl::make_span(&varID, 1), {}, {}, {});
            varVal = vars.real[0];
            CHECK(varVal == 0.0);
        } else if (v.name == "minlevel") {
            foundMinlevel = true;
            CHECK(v.variability == variable_variability::fixed);
            CHECK(v.causality == variable_causality::parameter);
            double start = std::get<double>(*v.start);
            CHECK(start == 1.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            cosim::slave::variable_values vars;
            instance->get_variables(&vars, gsl::make_span(&varID, 1), {}, {}, {});
            varVal = vars.real[0];
            CHECK(varVal == 1.0);
        }
    }
    CHECK(foundValve);
    CHECK(foundMinlevel);

    instance->start_simulation();
    instance->end_simulation();
}
