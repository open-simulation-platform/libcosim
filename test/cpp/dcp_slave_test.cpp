#include <cse/dcp/dcp_slave.hpp>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        boost::asio::io_context io_context;

        cse::dcp_server server(io_context);
        cse::dcp_client client(io_context);
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
