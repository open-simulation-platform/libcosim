#define BOOST_TEST_MODULE cosim::utility uuid unittests
#include <cosim/utility/uuid.hpp>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(random_uuid)
{
    const auto u = cosim::utility::random_uuid();
    BOOST_TEST(u.size() == 36);
    BOOST_TEST(cosim::utility::random_uuid() != u);
}
