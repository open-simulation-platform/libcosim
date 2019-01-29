#ifndef CSE_OBSERVER_SLAVE_VALUE_PROVIDER_HPP
#define CSE_OBSERVER_SLAVE_VALUE_PROVIDER_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>

#include <gsl/span>

#include <map>
#include <mutex>
#include <vector>

namespace cse
{

class slave_value_provider
{

public:
    slave_value_provider(observable* obs, time_point, size_t);
    ~slave_value_provider() noexcept;
    void observe(step_number timeStep, time_point);
    void get_real(gsl::span<const variable_index> variables, gsl::span<double> values);
    void get_int(gsl::span<const variable_index> variables, gsl::span<int> values);
    size_t get_real_samples(variable_index variableIndex, step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps, gsl::span<time_point> times);
    size_t get_int_samples(variable_index variableIndex, step_number fromStep, gsl::span<int> values, gsl::span<step_number> steps, gsl::span<time_point> times);
    void get_step_numbers(time_point tBegin, time_point tEnd, gsl::span<step_number> steps);
    void get_step_numbers(duration duration, gsl::span<step_number> steps);

private:
    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::map<step_number, time_point> timeSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    observable* observable_;
    std::mutex lock_;
    size_t bufSize_;
};

} // namespace cse

#endif // Header guard
