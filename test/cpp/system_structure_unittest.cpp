#define BOOST_TEST_MODULE system_structure unittests
#include <cse/exception.hpp>
#include <cse/system_structure.hpp>

#include <boost/test/unit_test.hpp>


class mock_model : public cse::model
{
public:
    std::shared_ptr<const cse::model_description> description()
        const noexcept override
    {
        const auto md = std::make_shared<cse::model_description>();
        md->name = "mock";
        md->uuid = "77e84b8d-c7f0-4ff3-8fa1-ce15cee1d282";
        md->variables.push_back({"realIn",
            0,
            cse::variable_type::real,
            cse::variable_causality::input,
            cse::variable_variability::continuous,
            std::nullopt});
        md->variables.push_back({"realOut",
            1,
            cse::variable_type::real,
            cse::variable_causality::output,
            cse::variable_variability::continuous,
            std::nullopt});
        md->variables.push_back({"intIn",
            0,
            cse::variable_type::integer,
            cse::variable_causality::input,
            cse::variable_variability::continuous,
            std::nullopt});
        return md;
    }

    std::shared_ptr<cse::async_slave> instantiate(std::string_view /*name*/)
        override
    {
        return nullptr;
    }
};


BOOST_AUTO_TEST_CASE(system_structure_basic_use)
{
    const auto model = std::make_shared<mock_model>();

    cse::system_structure ss;

    ss.add_simulator("simA", model);
    ss.add_simulator("simB", model);
    BOOST_CHECK_THROW(ss.add_simulator("simB", model), cse::error); // simB exists

    ss.add_scalar_connection({"simA", "realOut"}, {"simB", "realIn"});
    ss.add_scalar_connection({"simA", "realOut"}, {"simA", "realIn"}); // self connection
    BOOST_CHECK_THROW(
        ss.add_scalar_connection({"simB", "realOut"}, {"simB", "realIn"}),
        cse::error); // simB.realIn already connected
    BOOST_CHECK_THROW(
        ss.add_scalar_connection({"simA", "realOut"}, {"simB", "intIn"}),
        cse::error); // incompatible variables
}
