#define BOOST_TEST_MODULE cse::ssp_parser ssp_linear_transformation_unittest

#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_ssp_linear_transformation)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = boost::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

    auto resolver = cse::default_model_uri_resolver();
    auto algorithm = std::make_shared<cse::fixed_step_algorithm>(cse::to_duration(1e-3));
    auto [exec, simulator_map] = cse::load_ssp(*resolver, sspDir, algorithm);

    auto observer = std::make_shared<cse::last_value_observer>();
    exec.add_observer(observer);

    exec.step();

    double initialValue;
    auto slave1 = simulator_map.at("identity1");
    cse::value_reference v1Ref = cse::find_variable(slave1.description, "realOut").reference;
    observer->get_real(slave1.index, gsl::make_span(&v1Ref, 1), gsl::make_span(&initialValue, 1));
    BOOST_TEST_REQUIRE(initialValue == 2.0);

    double transformedValue;
    auto slave2 = simulator_map.at("identity2");
    cse::value_reference v2Ref = cse::find_variable(slave2.description, "realIn").reference;
    observer->get_real(slave2.index, gsl::make_span(&v2Ref, 1), gsl::make_span(&transformedValue, 1));

    double offset = 50;
    double factor = 1.3;
    double target = factor * initialValue + offset;
    BOOST_REQUIRE_CLOSE(transformedValue, target, 1e-9);
}
