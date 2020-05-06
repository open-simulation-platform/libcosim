#ifndef LIBCOSIM_MANIPULATOR_OVERRIDE_MANIPULATOR_HPP
#define LIBCOSIM_MANIPULATOR_OVERRIDE_MANIPULATOR_HPP

#include <cse/manipulator/manipulator.hpp>
#include <cse/scenario.hpp>

#include <mutex>
#include <unordered_map>
#include <variant>

namespace cosim
{

/**
 *  A manipulator implementation handling overrides of variable values.
 */
class override_manipulator : public manipulator
{
public:
    void simulator_added(simulator_index, manipulable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void step_commencing(time_point currentTime) override;

    /// Override the value of a variable with type `real`
    void override_real_variable(simulator_index, value_reference, double value);
    /// Override the value of a variable with type `integer`
    void override_integer_variable(simulator_index, value_reference, int value);
    /// Override the value of a variable with type `boolean`
    void override_boolean_variable(simulator_index, value_reference, bool value);
    /// Override the value of a variable with type `string`
    void override_string_variable(simulator_index, value_reference, const std::string& value);
    /// Reset override of a variable
    void reset_variable(simulator_index, variable_type, value_reference);

    ~override_manipulator() noexcept override;

private:
    void add_action(
        simulator_index index,
        value_reference variable,
        variable_type type,
        const std::variant<
            scenario::real_modifier,
            scenario::integer_modifier,
            scenario::boolean_modifier,
            scenario::string_modifier>& m);

    std::unordered_map<simulator_index, manipulable*> simulators_;
    std::vector<scenario::variable_action> actions_;
    std::mutex lock_;
};

} // namespace cse

#endif
