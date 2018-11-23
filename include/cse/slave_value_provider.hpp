#ifndef CSECORE_SLAVE_VALUE_PROVIDER_HPP
#define CSECORE_SLAVE_VALUE_PROVIDER_HPP

#include <cse/observable.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>

namespace cse
{

class slave_value_provider
{

public:
    slave_value_provider(observable* obs);
    ~slave_value_provider();
    void observe(step_number timeStep);
    void get_real(gsl::span<const variable_index> variables, gsl::span<double> values);
    void get_int(gsl::span<const variable_index> variables, gsl::span<int> values);
    size_t get_real_samples(variable_index variableIndex, step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps);
    size_t get_int_samples(variable_index variableIndex, step_number fromStep, gsl::span<int> values, gsl::span<step_number> steps);

private:
    template <typename T>
    void get(
        gsl::span<const variable_index> variables,
        const std::vector<cse::variable_index>& indices,
        const std::map<step_number, std::vector<T>>& samples,
        gsl::span<T> values);

    template <typename T>
    size_t get_samples(
        variable_index variableIndex,
        const std::vector<cse::variable_index>& indices,
        const std::map<step_number, std::vector<T>>& samples,
        step_number fromStep,
        gsl::span<T> values,
        gsl::span<step_number> steps);

    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    observable* observable_;
    std::mutex lock_;
};

} // namespace cse

#endif // Header guard