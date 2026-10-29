#include <algorithm>

namespace
{
class reporting_manipulator final : public cosim::manipulator
{
public:
    void simulator_added(cosim::simulator_index, cosim::manipulable* simulator, cosim::time_point) override
    {
        simulators.push_back(simulator);
    }

    void simulator_removed(cosim::simulator_index, cosim::time_point) override {}
    void step_commencing(cosim::time_point) override {}

    std::vector<cosim::manipulable*> simulators;
};
}

// A variable remains reported until both modifier directions are removed.
// The identity includes simulator and type, because references can overlap.
BOOST_AUTO_TEST_CASE(modified_variable_reporting)
{
    cosim::execution execution(
        cosim::time_point(),
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    auto manipulator = std::make_shared<reporting_manipulator>();
    execution.add_manipulator(manipulator);
    const auto first = execution.add_slave(std::make_shared<enumeration_mock_slave>(), "first");
    const auto second = execution.add_slave(std::make_shared<enumeration_mock_slave>(), "second");
    auto* a = manipulator->simulators.at(first);
    auto* b = manipulator->simulators.at(second);
    const auto count = [&execution](cosim::simulator_index simulator, cosim::variable_type type, cosim::value_reference reference) {
        const auto modified = execution.get_modified_variables();
        return std::count_if(modified.begin(), modified.end(), [=](const auto& variable) {
            return variable.simulator == simulator && variable.type == type && variable.reference == reference;
        });
    };
    const auto identity = [](const cosim::variable_value& value, cosim::duration) { return value; };
    const auto enumeration = cosim::variable_type::enumeration;
    a->expose_for_setting(enumeration, 100);
    a->expose_for_getting(enumeration, 100);
    b->expose_for_setting(enumeration, 100);
    a->set_variable_input_modifier(enumeration, 100, identity);
    a->set_variable_output_modifier(enumeration, 100, identity);
    b->set_variable_input_modifier(enumeration, 100, identity);
    a->set_variable_input_modifier(enumeration, 100, identity);
    BOOST_TEST(count(first, enumeration, 100) == 1);
    BOOST_TEST(count(second, enumeration, 100) == 1);
    a->set_variable_input_modifier(enumeration, 100, {});
    BOOST_TEST(count(first, enumeration, 100) == 1);
    a->set_variable_output_modifier(enumeration, 100, {});
    BOOST_TEST(count(first, enumeration, 100) == 0);
    BOOST_TEST(count(second, enumeration, 100) == 1);
    b->set_variable_input_modifier(enumeration, 100, {});

    a->expose_for_setting(cosim::variable_type::real, 1);
    a->expose_for_getting(cosim::variable_type::real, 1);
    a->expose_for_getting(cosim::variable_type::integer, 1);
    a->set_real_input_modifier(1, [](double value, cosim::duration) { return value; });
    a->set_real_output_modifier(1, [](double value, cosim::duration) { return value; });
    a->set_integer_output_modifier(1, [](int value, cosim::duration) { return value; });
    BOOST_TEST(count(first, cosim::variable_type::real, 1) == 1);
    BOOST_TEST(count(first, cosim::variable_type::integer, 1) == 1);
    a->set_real_input_modifier(1, {});
    BOOST_TEST(count(first, cosim::variable_type::real, 1) == 1);
    a->set_real_output_modifier(1, {});
    BOOST_TEST(count(first, cosim::variable_type::real, 1) == 0);
    BOOST_TEST(count(first, cosim::variable_type::integer, 1) == 1);
    a->set_integer_output_modifier(1, {});
    BOOST_TEST(execution.get_modified_variables().empty());
}
