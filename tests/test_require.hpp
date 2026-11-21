#ifndef LIBCOSIM_TEST_REQUIRE_HPP
#define LIBCOSIM_TEST_REQUIRE_HPP

#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

#endif
