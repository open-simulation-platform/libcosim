#include <cstdlib>
#include <exception>

#include <cse/ssp_parser.hpp>
#include <cse/log.hpp>

#include <boost/filesystem.hpp>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::set_global_output_level(cse::log::level::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo";
        std::cout << "Path: " << xmlPath.string() << std::endl;
        auto execution = cse::load_ssp(xmlPath, cse::to_time_point(0.0));

        auto result = execution.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
