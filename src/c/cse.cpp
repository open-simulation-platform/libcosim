#include <cse.h>
#include <cse/hello_world.hpp>


int cse_hello_world(char* buffer, size_t size)
{
    const auto n = cse::hello_world(gsl::make_span(buffer, size-1));
    buffer[n] = '\0';
    return cse::get_ultimate_answer();
}
