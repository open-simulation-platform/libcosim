#include <cse/error.hpp>
#include <cse/slave_value_provider.hpp>

#include <map>
#include <mutex>

namespace cse
{

template<typename T>
void get(
    gsl::span<const variable_index> variables,
    const std::vector<cse::variable_index>& indices,
    const std::map<step_number, std::vector<T>>& samples,
    gsl::span<T> values)
{
    assert(values.size() == variables.size());
    if (samples.empty()) {
        throw std::out_of_range("no samples available");
    }
    const auto lastEntry = samples.rbegin();
    for (int i = 0; i < values.size(); i++) {
        const auto it = std::find(indices.begin(), indices.end(), variables[i]);
        if (it != indices.end()) {
            size_t valueIndex = it - indices.begin();
            values[i] = lastEntry->second[valueIndex];
        }
    }
}

template<typename T>
size_t get_samples(
    variable_index variableIndex,
    const std::vector<cse::variable_index>& indices,
    const std::map<step_number, std::vector<T>>& samples,
    step_number fromStep,
    gsl::span<T> values,
    gsl::span<step_number> steps)
{
    assert(values.size() == steps.size());
    size_t samplesRead = 0;
    const auto variableIndexIt = std::find(indices.begin(), indices.end(), variableIndex);
    if (variableIndexIt != indices.end()) {
        const size_t valueIndex = variableIndexIt - indices.begin();
        auto sampleIt = samples.find(fromStep);
        for (samplesRead = 0; samplesRead < static_cast<std::size_t>(values.size()); samplesRead++) {
            if (sampleIt != samples.end()) {
                steps[samplesRead] = sampleIt->first;
                values[samplesRead] = sampleIt->second[valueIndex];
                sampleIt++;
            } else {
                break;
            }
        }
    }
    return samplesRead;
}


slave_value_provider::slave_value_provider(observable* observable)
    : observable_(observable)
{

    for (const auto& vd : observable_->model_description().variables) {
        observable_->expose_for_getting(vd.type, vd.index);
        if (vd.type == cse::variable_type::real) {
            realIndexes_.push_back(vd.index);
        }
        if (vd.type == cse::variable_type::integer) {
            intIndexes_.push_back(vd.index);
        }
    }

    observe(0);
}

slave_value_provider::~slave_value_provider() noexcept = default;

void slave_value_provider::observe(step_number timeStep)
{

    std::lock_guard<std::mutex> lock(lock_);
    realSamples_[timeStep].reserve(realIndexes_.size());
    intSamples_[timeStep].reserve(intIndexes_.size());

    for (const auto idx : realIndexes_) {
        realSamples_[timeStep].push_back(observable_->get_real(idx));
    }

    for (const auto idx : intIndexes_) {
        intSamples_[timeStep].push_back(observable_->get_integer(idx));
    }
}

void slave_value_provider::get_real(gsl::span<const variable_index> variables, gsl::span<double> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<double>(variables, realIndexes_, realSamples_, values);
}

void slave_value_provider::get_int(gsl::span<const variable_index> variables, gsl::span<int> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<int>(variables, intIndexes_, intSamples_, values);
}

size_t
slave_value_provider::get_real_samples(variable_index variableIndex, step_number fromStep, gsl::span<double> values,
    gsl::span<step_number> steps)
{
    std::lock_guard<std::mutex> lock(lock_);
    return get_samples<double>(variableIndex, realIndexes_, realSamples_, fromStep, values, steps);
}

size_t
slave_value_provider::get_int_samples(variable_index variableIndex, step_number fromStep, gsl::span<int> values,
    gsl::span<step_number> steps)
{
    std::lock_guard<std::mutex> lock(lock_);
    return get_samples<int>(variableIndex, intIndexes_, intSamples_, fromStep, values, steps);
}

} // namespace cse
