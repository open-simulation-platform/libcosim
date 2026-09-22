#define BOOST_TEST_MODULE cosim::fmi::v2::fmu unittest
#include <cosim/exception.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v2/fmu.hpp>
#include <cosim/fs_portability.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdlib>
#include <string>


using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(fmi::fmi_version)

namespace
{
cosim::filesystem::path reference_fmu_directory()
{
    const auto* referenceDir = std::getenv("REFERENCE_FMU_V2_DIR");
    if (referenceDir == nullptr) {
        throw std::runtime_error(
            "REFERENCE_FMU_V2_DIR must identify the generated FMI 2 FMUs");
    }
    return referenceDir;
}


std::shared_ptr<fmi::v2::slave_instance> instantiate_derivative_fixture(
    const std::string& instanceName)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "Fmi2Derivatives.fmu"));
    return fmu->instantiate_v2_slave(instanceName);
}
} // namespace


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


BOOST_AUTO_TEST_CASE(v2_reference_directional_derivatives)
{
    auto importer = fmi::importer::create();
    auto vanDerPol = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "VanDerPol.fmu"));
    auto instance =
        vanDerPol->instantiate_v2_slave("fmi2-reference-derivatives");

    BOOST_TEST(instance->provides_directional_derivatives());

    const value_reference unknowns[] = {2, 4};
    const value_reference initializationKnowns[] = {1, 3, 5};
    const double initializationSeed[] = {1.0, 1.0, 1.0};
    double sensitivity[] = {0.0, 0.0};

    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            unknowns,
            initializationKnowns,
            initializationSeed,
            sensitivity),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::invalid_operation) &&
                std::string(error.what()).find(
                    "GetDirectionalDerivative can only be called in lifecycle states "
                    "initialization (1), step (2), or terminated (3); current lifecycle "
                    "state: 0") !=
                std::string::npos;
        });

    instance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    instance->get_directional_derivative(
        unknowns,
        initializationKnowns,
        initializationSeed,
        sensitivity);
    BOOST_TEST(sensitivity[0] == 1.0);
    BOOST_TEST(sensitivity[1] == -4.0);

    instance->start_simulation();
    const value_reference stepKnowns[] = {1, 3};
    const double stepSeed[] = {2.0, -1.0};
    instance->get_directional_derivative(
        unknowns, stepKnowns, stepSeed, sensitivity);
    BOOST_TEST(sensitivity[0] == -1.0);
    BOOST_TEST(sensitivity[1] == 1.0);

    instance->end_simulation();
    instance->get_directional_derivative(
        unknowns, stepKnowns, stepSeed, sensitivity);
    BOOST_TEST(sensitivity[0] == -1.0);
    BOOST_TEST(sensitivity[1] == 1.0);

    auto unsupported = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "Feedthrough.fmu"));
    auto unsupportedInstance =
        unsupported->instantiate_v2_slave("fmi2-unsupported-derivatives");
    BOOST_TEST(!unsupportedInstance->provides_directional_derivatives());
    unsupportedInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    BOOST_CHECK_EXCEPTION(
        unsupportedInstance->get_directional_derivative(
            unknowns,
            initializationKnowns,
            initializationSeed,
            sensitivity),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::unsupported_feature);
        });
}


BOOST_AUTO_TEST_CASE(v2_directional_derivative_contract)
{
    auto instance = instantiate_derivative_fixture("fmi2-derivative-contract");
    instance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);

    const value_reference unknowns[] = {10, 11};
    const value_reference knowns[] = {1, 2, 3};
    const double seed[] = {2.0, -1.0, 4.0};
    double sensitivity[] = {-100.0, -100.0};
    instance->get_directional_derivative(
        unknowns, knowns, seed, sensitivity);
    BOOST_TEST(sensitivity[0] == 9.0);
    BOOST_TEST(sensitivity[1] == 14.0);

    const value_reference reorderedUnknowns[] = {11, 10};
    const value_reference reorderedKnowns[] = {3, 1, 2};
    const double reorderedSeed[] = {4.0, 2.0, -1.0};
    instance->get_directional_derivative(
        reorderedUnknowns, reorderedKnowns, reorderedSeed, sensitivity);
    BOOST_TEST(sensitivity[0] == 14.0);
    BOOST_TEST(sensitivity[1] == 9.0);

    const value_reference callCountReference = 101;
    int callCount = 0;
    instance->get_integer_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == 2);

    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            unknowns,
            knowns,
            gsl::make_span(seed, 2),
            sensitivity),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            unknowns,
            knowns,
            seed,
            gsl::make_span(sensitivity, 1)),
        std::invalid_argument);

    const value_reference nonReal = 102;
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&nonReal, 1),
            knowns,
            seed,
            gsl::make_span(sensitivity, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });
    const value_reference invalidUnknown = 1;
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&invalidUnknown, 1),
            knowns,
            seed,
            gsl::make_span(sensitivity, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });

    const gsl::span<const value_reference> noReferences;
    const gsl::span<const double> noSeed;
    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            noReferences, knowns, seed, sensitivity),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            unknowns, noReferences, noSeed, sensitivity),
        std::invalid_argument);

    instance->get_integer_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == 2);
}


BOOST_AUTO_TEST_CASE(v2_directional_derivative_statuses)
{
    const value_reference unknowns[] = {10, 11};
    const value_reference knowns[] = {1, 2, 3};
    const double seed[] = {2.0, -1.0, 4.0};
    const value_reference statusReference = 100;

    {
        auto instance = instantiate_derivative_fixture(
            "fmi2-derivative-warning");
        instance->setup(
            to_time_point(0.0), to_time_point(1.0), std::nullopt);
        const int warning = 1;
        instance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&warning, 1));
        double sensitivity[] = {-100.0, -100.0};
        instance->get_directional_derivative(
            unknowns, knowns, seed, sensitivity);
        BOOST_TEST(sensitivity[0] == 9.0);
        BOOST_TEST(sensitivity[1] == 14.0);
    }

    for (const int failureStatus : std::array<int, 4>{2, 3, 4, 5}) {
        auto instance = instantiate_derivative_fixture(
            "fmi2-derivative-failure-" + std::to_string(failureStatus));
        instance->setup(
            to_time_point(0.0), to_time_point(1.0), std::nullopt);
        instance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&failureStatus, 1));
        double sensitivity[] = {-100.0, -100.0};
        BOOST_CHECK_EXCEPTION(
            instance->get_directional_derivative(
                unknowns, knowns, seed, sensitivity),
            cosim::error,
            [](const cosim::error& error) {
                return error.code() == make_error_code(errc::model_error);
            });
        BOOST_TEST(sensitivity[0] == -100.0);
        BOOST_TEST(sensitivity[1] == -100.0);
    }
}
