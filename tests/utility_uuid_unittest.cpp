
#include <cosim/utility/uuid.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>


TEST_CASE("random_uuid")
{
    const auto u = cosim::utility::random_uuid();
    CHECK(u.size() == 36);
    CHECK(cosim::utility::random_uuid() != u);
}
