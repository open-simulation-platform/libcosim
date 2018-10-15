#include <cse/hello_world.hpp>
#include <cstring>

constexpr int failure = 1;


int main()
{
    char buf1[20];
    const auto len1 = cse::hello_world(gsl::make_span(buf1));
    if (len1 != 12) return failure;
    if (std::memcmp(buf1, "Hello World!", len1)) return failure;

    char buf2[10];
    const auto len2 = cse::hello_world(gsl::make_span(buf2));
    if (len2 != 10) return failure;
    if (std::memcmp(buf2, "Hello World!", len2)) return failure;

    if (cse::get_ultimate_answer() != 42) return failure;

    return 0;
}
