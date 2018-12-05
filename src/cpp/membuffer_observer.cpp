#include <cse/observer.hpp>

#include <map>
#include <mutex>

#include <cse/error.hpp>
#include <cse/slave_value_provider.hpp>


namespace cse
{

membuffer_observer::membuffer_observer()
{
}

void membuffer_observer::simulator_added(simulator_index index, observable* simulator, time_point startTime)
{
    valueProviders_[index] = std::make_unique<slave_value_provider>(simulator, startTime);
}

void membuffer_observer::simulator_removed(simulator_index index)
{
    valueProviders_.erase(index);
}

void membuffer_observer::variables_connected(variable_id /*output*/, variable_id /*input*/)
{
}

void membuffer_observer::variable_disconnected(variable_id /*input*/)
{
}

void membuffer_observer::step_complete(
    step_number lastStep,
    duration /*lastStepSize*/,
    time_point currentTime)
{
    for (const auto& valueProvider : valueProviders_) {
        valueProvider.second->observe(lastStep, currentTime);
    }
}

void membuffer_observer::get_real(
    simulator_index sim,
    gsl::span<const variable_index> variables,
    gsl::span<double> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    valueProviders_.at(sim)->get_real(variables, values);
}

void membuffer_observer::get_integer(
    simulator_index sim,
    gsl::span<const variable_index> variables,
    gsl::span<int> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    valueProviders_.at(sim)->get_int(variables, values);
}

std::size_t membuffer_observer::get_real_samples(
    simulator_index sim,
    variable_index variableIndex,
    step_number fromStep,
    gsl::span<double> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    CSE_INPUT_CHECK(times.size() == values.size());
    return valueProviders_.at(sim)->get_real_samples(variableIndex, fromStep, values, steps, times);
}

std::size_t membuffer_observer::get_integer_samples(
    simulator_index sim,
    variable_index variableIndex,
    step_number fromStep,
    gsl::span<int> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    CSE_INPUT_CHECK(times.size() == values.size());
    return valueProviders_.at(sim)->get_int_samples(variableIndex, fromStep, values, steps, times);
}

void membuffer_observer::get_step_numbers(
    simulator_index sim,
    duration duration,
    gsl::span<step_number> steps)
{
    valueProviders_.at(sim)->get_step_numbers(duration, steps);
}

void membuffer_observer::get_step_numbers(
    simulator_index sim,
    time_point tBegin,
    time_point tEnd,
    gsl::span<step_number> steps)
{
    valueProviders_.at(sim)->get_step_numbers(tBegin, tEnd, steps);
}

membuffer_observer::~membuffer_observer() noexcept = default;

} // namespace cse
