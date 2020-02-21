#define BOOST_TEST_MODULE function unittests
#include <cse/function/linear_transformation.hpp>
#include <cse/function/vector_sum.hpp>

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string>
#include <utility>

int find_param(const cse::function_type_description& ftd, const std::string& name)
{
    for (int i = 0; i < static_cast<int>(ftd.parameters.size()); ++i) {
        if (ftd.parameters[i].name == name) return i;
    }
    throw std::logic_error("Parameter not found: " + name);
}


std::pair<int, int> find_io(
    const cse::function_description& fd,
    const std::string& groupName,
    const std::string& varName = "")
{
    for (int g = 0; g < static_cast<int>(fd.io_groups.size()); ++g) {
        const auto& group = fd.io_groups[g];
        if (group.name != groupName) continue;
        for (int v = 0; v < static_cast<int>(group.ios.size()); ++v) {
            if (group.ios[v].name == varName) return {g, v};
        }
    }
    throw std::logic_error("Variable not found: " + groupName + ":" + varName);
}


BOOST_AUTO_TEST_CASE(linear_transformation_standalone)
{
    constexpr double offset = 3.0;
    constexpr double factor = 5.0;

    auto type = cse::linear_transformation_function_type();
    const auto typeDesc = type.description();

    std::unordered_map<int, cse::function_parameter_value> params;
    params[find_param(typeDesc, "offset")] = offset;
    params[find_param(typeDesc, "factor")] = factor;

    const auto fun = type.instantiate(params);
    const auto funDesc = fun->description();
    BOOST_TEST_REQUIRE(funDesc.io_groups.size() == 2);
    BOOST_TEST_REQUIRE(funDesc.io_groups[0].ios.size() == 1);
    BOOST_TEST_REQUIRE(funDesc.io_groups[1].ios.size() == 1);

    const auto [inGID, inVID] = find_io(funDesc, "in");
    const auto [outGID, outVID] = find_io(funDesc, "out");

    fun->set_real_io({inGID, 0, inVID, 0}, 10.0);
    fun->calculate();
    BOOST_TEST(fun->get_real_io({outGID, 0, outVID, 0}) == 53.0);

    fun->set_real_io({inGID, 0, inVID, 0}, -1.0);
    fun->calculate();
    BOOST_TEST(fun->get_real_io({outGID, 0, outVID, 0}) == -2.0);
}


BOOST_AUTO_TEST_CASE(vector_sum_standalone)
{
    constexpr int inputCount = 3;
    constexpr auto numericType = cse::variable_type::integer;
    constexpr int dimension = 2;

    auto type = cse::vector_sum_function_type();
    const auto typeDesc = type.description();

    std::unordered_map<int, cse::function_parameter_value> params;
    params[find_param(typeDesc, "inputCount")] = inputCount;
    params[find_param(typeDesc, "numericType")] = numericType;
    params[find_param(typeDesc, "dimension")] = dimension;

    const auto fun = type.instantiate(params);
    const auto funDesc = fun->description();
    BOOST_TEST_REQUIRE(funDesc.io_groups.size() == 2);
    BOOST_TEST_REQUIRE(funDesc.io_groups[0].ios.size() == 1);
    BOOST_TEST_REQUIRE(funDesc.io_groups[1].ios.size() == 1);
    BOOST_TEST(std::get<int>(funDesc.io_groups[0].count) == inputCount);
    BOOST_TEST(std::get<int>(funDesc.io_groups[1].count) == 1);
    BOOST_TEST(std::get<cse::variable_type>(funDesc.io_groups[0].ios[0].type) == numericType);
    BOOST_TEST(std::get<cse::variable_type>(funDesc.io_groups[1].ios[0].type) == numericType);
    BOOST_TEST(std::get<int>(funDesc.io_groups[0].ios[0].count) == dimension);
    BOOST_TEST(std::get<int>(funDesc.io_groups[1].ios[0].count) == dimension);

    const auto [inGID, inVID] = find_io(funDesc, "in");
    const auto [outGID, outVID] = find_io(funDesc, "out");
    fun->set_integer_io({inGID, 0, inVID, 0}, 1);
    fun->set_integer_io({inGID, 0, inVID, 1}, 2);
    fun->set_integer_io({inGID, 1, inVID, 0}, 3);
    fun->set_integer_io({inGID, 1, inVID, 1}, 5);
    fun->set_integer_io({inGID, 2, inVID, 0}, 7);
    fun->set_integer_io({inGID, 2, inVID, 1}, 11);
    fun->calculate();
    BOOST_TEST(fun->get_integer_io({outGID, 0, outVID, 0}) == 11);
    BOOST_TEST(fun->get_integer_io({outGID, 0, outVID, 1}) == 18);
}
