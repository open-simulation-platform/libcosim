
#include <cse/ssp_parser.hpp>
#include <boost/filesystem.hpp>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {

    boost::filesystem::path xmlPath = boost::filesystem::system_complete("../../../../ssp/SimpleSystemStructure.ssd");
    std::cout << "Path: " << xmlPath.string() << std::endl;

    const auto parser = cse::ssp_parser(xmlPath);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
    }

    return 0;
}