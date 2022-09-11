
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v2/fmu.hpp>
#include <cosim/fs_portability.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>


using namespace cosim;

TEST_CASE("v2_fmu")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);
    auto importer = fmi::importer::create();
    const std::string modelName = "WaterTank_Control";
    auto fmu = importer->import(
        cosim::filesystem::path(testDataDir) / "fmi2" / (modelName + ".fmu"));

    CHECK(fmu->fmi_version() == fmi::fmi_version::v2_0);
    const auto d = fmu->model_description();
    CHECK(d->name == "WaterTank.Control");
    CHECK(d->uuid == "{ad6d7bad-97d1-4fb9-ab3e-00a0d051e42c}");
    CHECK(d->description.empty());
    CHECK(d->author.empty());
    CHECK(d->version.empty());
    CHECK(std::static_pointer_cast<fmi::v2::fmu>(fmu)->fmilib_handle() != nullptr);
    CHECK(cosim::filesystem::exists(
        std::static_pointer_cast<fmi::v2::fmu>(fmu)->directory() / "modelDescription.xml"));

    auto instance = fmu->instantiate_slave("testSlave");
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
            instance->get_real_variables(gsl::make_span(&varID, 1), gsl::make_span(&varVal, 1));
            CHECK(varVal == 0.0);
        } else if (v.name == "minlevel") {
            foundMinlevel = true;
            CHECK(v.variability == variable_variability::fixed);
            CHECK(v.causality == variable_causality::parameter);
            double start = std::get<double>(*v.start);
            CHECK(start == 1.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            instance->get_real_variables(gsl::make_span(&varID, 1), gsl::make_span(&varVal, 1));
            CHECK(varVal == 1.0);
        }
    }
    CHECK(foundValve);
    CHECK(foundMinlevel);
}
