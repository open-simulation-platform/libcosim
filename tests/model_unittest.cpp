#define BOOST_TEST_MODULE model.hpp unittests
#include <cse/model.hpp>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(to_double_time_point_duration_addition)
{
    constexpr cse::duration time[] = {
        cse::duration(0),
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
            const auto t1 = cse::time_point{t};
            BOOST_TEST(cse::to_double_time_point(t1) + cse::to_double_duration(dt, t1) == cse::to_double_time_point(t1 + dt));
        }
    }
}

BOOST_TEST_DONT_PRINT_LOG_VALUE(cse::duration)
BOOST_TEST_DONT_PRINT_LOG_VALUE(cse::time_point)

BOOST_AUTO_TEST_CASE(to_time_point_duration_addition)
{
    constexpr double time[] = {0.0, 1e-9, 1e-6, 1e-3, 1.0, 1e3, 1e6, 1e9};

    for (const auto t1 : time) {
        for (const auto dt : time) {
            BOOST_TEST(cse::to_time_point(t1) + cse::to_duration(dt, t1) == cse::to_time_point(t1 + dt));
        }
    }
}
