#define BOOST_TEST_MODULE cse::fmi::v2::fmu unittest
#include <cse/fmi/importer.hpp>
#include <cse/fmi/v2/fmu.hpp>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>


using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(fmi::fmi_version)

BOOST_AUTO_TEST_CASE(v2_fmu)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    auto importer = fmi::importer::create();
    const std::string modelName = "WaterTank_Control";
    auto fmu = importer->import(
        boost::filesystem::path(testDataDir) / "fmi2" / (modelName + ".fmu"));

    BOOST_TEST(fmu->fmi_version() == fmi::fmi_version::v2_0);
    const auto d = fmu->model_description();
    BOOST_TEST(d->name == "WaterTank.Control");
    BOOST_TEST(d->uuid == "{ad6d7bad-97d1-4fb9-ab3e-00a0d051e42c}");
    BOOST_TEST(d->description.empty());
    BOOST_TEST(d->author.empty());
    BOOST_TEST(d->version.empty());
    BOOST_TEST(std::static_pointer_cast<fmi::v2::fmu>(fmu)->fmilib_handle() != nullptr);
    BOOST_TEST(boost::filesystem::exists(
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
            BOOST_TEST(v.variability == variable_variability::continuous);
            BOOST_TEST(v.causality == variable_causality::output);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 0.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            instance->get_real_variables(gsl::make_span(&varID, 1), gsl::make_span(&varVal, 1));
            BOOST_TEST(varVal == 0.0);
        } else if (v.name == "minlevel") {
            foundMinlevel = true;
            BOOST_TEST(v.variability == variable_variability::fixed);
            BOOST_TEST(v.causality == variable_causality::parameter);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 1.0);
            const auto varID = v.reference;
            double varVal = -1.0;
            instance->get_real_variables(gsl::make_span(&varID, 1), gsl::make_span(&varVal, 1));
            BOOST_TEST(varVal == 1.0);
        }
    }
    BOOST_TEST(foundValve);
    BOOST_TEST(foundMinlevel);
}
