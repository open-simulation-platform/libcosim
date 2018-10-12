#define BOOST_TEST_MODULE cse::utility filesystem unittests
#include <boost/test/unit_test.hpp>
#include <cse/utility/filesystem.hpp>

#include <boost/filesystem.hpp>

#include <utility>


BOOST_AUTO_TEST_CASE(temp_dir)
{
    namespace fs = boost::filesystem;
    fs::path d;
    {
        auto tmp = cse::utility::temp_dir();
        d = tmp.path();
        BOOST_TEST_REQUIRE(!d.empty());
        BOOST_TEST_REQUIRE(fs::exists(d));
        BOOST_TEST(fs::is_directory(d));
        BOOST_TEST(fs::is_empty(d));

        auto tmp2 = std::move(tmp);
        BOOST_TEST(tmp.path().empty());
        BOOST_TEST(tmp2.path() == d);
        BOOST_TEST(fs::exists(d));

        auto tmp3 = cse::utility::temp_dir();
        const auto d3 = tmp3.path();
        BOOST_TEST(fs::exists(d3));
        BOOST_TEST_REQUIRE(!fs::equivalent(d, d3));

        tmp3 = std::move(tmp2);
        BOOST_TEST(tmp2.path().empty());
        BOOST_TEST(!fs::exists(d3));
        BOOST_TEST(tmp3.path() == d);
        BOOST_TEST(fs::exists(d));
    }
    BOOST_TEST(!fs::exists(d));
}
