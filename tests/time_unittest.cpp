
#include <cosim/time.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>


TEST_CASE("to_double_time_point_duration_addition")
{
    constexpr cosim::duration time[] = {
        cosim::duration(0),
        std::chrono::nanoseconds(1),
        std::chrono::microseconds(1),
        std::chrono::milliseconds(1),
        std::chrono::seconds(1),
        std::chrono::minutes(1),
        std::chrono::hours(1),
        std::chrono::hours(1000),
        std::chrono::hours(100'000)};

    for (const auto t : time) {
        for (const auto dt : time) {
            const auto t1 = cosim::time_point{t};
            CHECK(cosim::to_double_time_point(t1) + cosim::to_double_duration(dt, t1) == cosim::to_double_time_point(t1 + dt));
        }
    }
}

TEST_CASE("to_time_point_duration_addition")
{
    constexpr double time[] = {0.0, 1e-9, 1e-6, 1e-3, 1.0, 1e3, 1e6, 1e9};

    for (const auto t1 : time) {
        for (const auto dt : time) {
            CHECK(cosim::to_time_point(t1) + cosim::to_duration(dt, t1) == cosim::to_time_point(t1 + dt));
        }
    }
}
