
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v1/fmu.hpp>
#include <cosim/utility/filesystem.hpp>
#include <cosim/utility/zip.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <cstdlib>


using namespace cosim;

namespace
{
void run_tests(std::shared_ptr<fmi::fmu> fmu)
{
    CHECK(fmu->fmi_version() == fmi::fmi_version::v1_0);
    const auto d = fmu->model_description();
    CHECK(d->name == "no.viproma.demo.identity");
    CHECK(d->uuid.size() == 36U);
    CHECK(d->description ==
        "Has one input and one output of each type, and outputs are always set equal to inputs");
    CHECK(d->author == "Lars Tandle Kyllingstad");
    CHECK(d->version == "0.3");
    CHECK(std::static_pointer_cast<fmi::v1::fmu>(fmu)->fmilib_handle() != nullptr);

    cosim::value_reference
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

    const auto tStart = cosim::time_point();
    const auto tMax = cosim::to_time_point(1.0);
    const auto dt = cosim::to_duration(0.1);
    double realVal = 0.0;
    int integerVal = 0;
    bool booleanVal = false;
    std::string stringVal;

    auto instance = fmu->instantiate_slave("testSlave");
    instance->setup(tStart, tMax, std::nullopt);
    instance->start_simulation();

    for (auto t = tStart; t < tMax; t += dt) {
        double getRealVal = -1.0;
        int getIntegerVal = -1;
        bool getBooleanVal = true;
        std::string getStringVal = "unexpected value";

        instance->get_real_variables(gsl::make_span(&realOut, 1), gsl::make_span(&getRealVal, 1));
        instance->get_integer_variables(gsl::make_span(&integerOut, 1), gsl::make_span(&getIntegerVal, 1));
        instance->get_boolean_variables(gsl::make_span(&booleanOut, 1), gsl::make_span(&getBooleanVal, 1));
        instance->get_string_variables(gsl::make_span(&stringOut, 1), gsl::make_span(&getStringVal, 1));

        CHECK(getRealVal == realVal);
        CHECK(getIntegerVal == integerVal);
        CHECK(getBooleanVal == booleanVal);
        CHECK(getStringVal == stringVal);

        realVal += 1.0;
        integerVal += 1;
        booleanVal = !booleanVal;
        stringVal += 'a';

        instance->set_real_variables(gsl::make_span(&realIn, 1), gsl::make_span(&realVal, 1));
        instance->set_integer_variables(gsl::make_span(&integerIn, 1), gsl::make_span(&integerVal, 1));
        instance->set_boolean_variables(gsl::make_span(&booleanIn, 1), gsl::make_span(&booleanVal, 1));
        instance->set_string_variables(gsl::make_span(&stringIn, 1), gsl::make_span(&stringVal, 1));

        CHECK(instance->do_step(t, dt) == step_result::complete);
    }

    instance->end_simulation();
}
} // namespace


TEST_CASE("v1_fmu")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);
    auto importer = fmi::importer::create();
    auto fmu = importer->import(
        cosim::filesystem::path(testDataDir) / "fmi1" / "identity.fmu");
    run_tests(fmu);
}


TEST_CASE("v1_fmu_unpacked")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);
    utility::temp_dir unpackDir;
    utility::zip::archive(
        cosim::filesystem::path(testDataDir) / "fmi1" / "identity.fmu")
        .extract_all(unpackDir.path());

    auto importer = fmi::importer::create();
    auto fmu = importer->import_unpacked(unpackDir.path());
    run_tests(fmu);
}
