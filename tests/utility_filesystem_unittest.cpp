
#include <cosim/fs_portability.hpp>
#include <cosim/utility/filesystem.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <utility>


TEST_CASE("temp_dir")
{
    namespace fs = cosim::filesystem;
    fs::path d;
    {
        auto tmp = cosim::utility::temp_dir();
        d = tmp.path();
        REQUIRE(!d.empty());
        REQUIRE(fs::exists(d));
        CHECK(fs::is_directory(d));
        CHECK(fs::is_empty(d));

        auto tmp2 = std::move(tmp);
        CHECK(tmp.path().empty());
        CHECK(tmp2.path() == d);
        CHECK(fs::exists(d));

        auto tmp3 = cosim::utility::temp_dir();
        const auto d3 = tmp3.path();
        CHECK(fs::exists(d3));
        REQUIRE(!fs::equivalent(d, d3));

        tmp3 = std::move(tmp2);
        CHECK(tmp2.path().empty());
        CHECK(!fs::exists(d3));
        CHECK(tmp3.path() == d);
        CHECK(fs::exists(d));
    }
    CHECK(!fs::exists(d));
}
