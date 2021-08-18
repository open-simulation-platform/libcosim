#define BOOST_TEST_MODULE proxyfmu_library unittests

#include <boost/test/unit_test.hpp>
#include <proxyfmu/client/proxy_fmu.hpp>

using namespace proxyfmu;
using namespace proxyfmu::fmi;

namespace
{

void test(fmu& fmu)
{
    const auto d = fmu.get_model_description();
    BOOST_TEST(d.modelName == "no.viproma.demo.identity");
    BOOST_TEST(d.description ==
        "Has one input and one output of each type, and outputs are always set equal to inputs");
    BOOST_TEST(d.author == "Lars Tandle Kyllingstad");

    auto slave = fmu.new_instance("instance");
    BOOST_REQUIRE(slave->setup_experiment());
    BOOST_REQUIRE(slave->enter_initialization_mode());
    BOOST_REQUIRE(slave->exit_initialization_mode());

    std::vector<value_ref> vr{0};

    std::vector<double> realVal{0.0};
    std::vector<int> integerVal{0};
    std::vector<bool> booleanVal{false};
    std::vector<std::string> stringVal{""};

    std::vector<double> realRef(1);
    std::vector<int> integerRef(1);
    std::vector<bool> booleanRef(1);
    std::vector<std::string> stringRef(1);

    double t = 0.0;
    double tEnd = 1.0;
    double dt = 0.1;

    while (t <= tEnd) {

        slave->get_real(vr, realRef);
        slave->get_integer(vr, integerRef);
        slave->get_boolean(vr, booleanRef);
        slave->get_string(vr, stringRef);

        BOOST_TEST(realVal[0] == realRef[0]);
        BOOST_TEST(integerVal[0] == integerRef[0]);
        BOOST_TEST(booleanVal[0] == booleanRef[0]);
        BOOST_TEST(stringVal[0] == stringRef[0]);

        BOOST_REQUIRE(slave->step(t, dt));

        realVal[0] += 1.0;
        integerVal[0] += 1;
        booleanVal[0] = !booleanVal[0];
        stringVal[0] += 'a';

        slave->set_real(vr, realVal);
        slave->set_integer(vr, integerVal);
        slave->set_boolean(vr, booleanVal);
        slave->set_string(vr, stringVal);

        t += dt;
    }

    BOOST_REQUIRE(slave->terminate());
    slave->freeInstance();
}

} // namespace

BOOST_AUTO_TEST_CASE(proxyfmu_fmi_test_identity)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);

    auto fmuPath = proxyfmu::filesystem::path(testDataDir) / "fmi1" / "identity.fmu";
    auto fmu = loadFmu(fmuPath);
    test(*fmu);
}

BOOST_AUTO_TEST_CASE(proxyfmu_client_test_identity)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);

    auto fmuPath = proxyfmu::filesystem::path(testDataDir) / "fmi1" / "identity.fmu";
    auto fmu = client::proxy_fmu(fmuPath);
    test(fmu);
}