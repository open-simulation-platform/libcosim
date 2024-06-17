#define BOOST_TEST_MODULE cosim::fmi::v2::fmu unittest
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v2/fmu.hpp>
#include <cosim/fs_portability.hpp>

#include <boost/test/unit_test.hpp>


using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(fmi::fmi_version)

BOOST_AUTO_TEST_CASE(v2_fmu)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    auto importer = fmi::importer::create();
    auto fmu = importer->import(
        cosim::filesystem::path(testDataDir) / "fmi2" / "vector.fmu");

    BOOST_TEST(fmu->fmi_version() == fmi::fmi_version::v2_0);
    const auto d = fmu->model_description();
    BOOST_TEST(d->name == "com.open-simulation-platform.vector");
    BOOST_TEST(d->uuid == "b928fd8c-743f-50d0-b4bb-98bc5a5f804e");
    BOOST_TEST(d->description == "Has one input and one input, both 3D vectors. Copies the input to the output each step.");
    BOOST_TEST(d->author == "SINTEF Ocean & DNV GL");
    BOOST_TEST(d->version == "0.1");
    BOOST_TEST(std::static_pointer_cast<fmi::v2::fmu>(fmu)->fmilib_handle() != nullptr);
    BOOST_TEST(cosim::filesystem::exists(
        std::static_pointer_cast<fmi::v2::fmu>(fmu)->directory() / "modelDescription.xml"));

    auto instance = fmu->instantiate_slave("testSlave");
    instance->setup(
        cosim::to_time_point(0.0),
        cosim::to_time_point(1.0),
        std::nullopt);

    bool foundInput0 = false;
    bool foundOutput1 = false;
    for (const auto& v : d->variables) {
        if (v.name == "input[0]") {
            foundInput0 = true;
            BOOST_TEST(v.type == variable_type::real);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::input);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 0.0);
            double varVal = -1.0;
            instance->get_real_variables(
                gsl::make_span(&v.reference, 1), gsl::make_span(&varVal, 1));
            BOOST_TEST(varVal == 0.0);
        } else if (v.name == "output[1]") {
            foundOutput1 = true;
            BOOST_TEST(v.type == variable_type::real);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::output);
            BOOST_TEST(!v.start.has_value());
        }
    }
    BOOST_TEST(foundInput0);
    BOOST_TEST(foundOutput1);
}
