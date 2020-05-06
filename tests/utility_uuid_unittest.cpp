#define BOOST_TEST_MODULE cse::utility uuid unittests
#include <cse/utility/uuid.hpp>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(random_uuid)
{
    const auto u = cse::utility::random_uuid();
    BOOST_TEST(u.size() == 36);
    BOOST_TEST(cse::utility::random_uuid() != u);
}
