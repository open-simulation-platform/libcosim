#include "slave_observer.hpp"

namespace cse
{

namespace
{

template<typename T>
void get(
    const cse_variable_index variables[],
    const std::vector<cse::variable_index>& indices,
    const std::map<long, std::vector<T>>& samples,
    size_t nv,
    T values[])
{
    if (samples.empty()) {
        throw std::out_of_range("no samples available");
    }
    const auto lastEntry = samples.rbegin();
    for (size_t i = 0; i < nv; i++) {
        const auto it = std::find(indices.begin(), indices.end(), variables[i]);
        if (it != indices.end()) {
            size_t valueIndex = it - indices.begin();
            values[i] = lastEntry->second[valueIndex];
        }
    }
}

template<typename T>
size_t get_samples(
    cse_variable_index variableIndex,
    const std::vector<cse::variable_index>& indices,
    const std::map<long, std::vector<T>>& samples,
    long fromStep,
    size_t nSamples,
    T values[],
    long steps[])
{
    size_t samplesRead = 0;
    const auto variableIndexIt = std::find(indices.begin(), indices.end(), variableIndex);
    if (variableIndexIt != indices.end()) {
        const size_t valueIndex = variableIndexIt - indices.begin();
        auto sampleIt = samples.find(fromStep);
        for (samplesRead = 0; samplesRead < nSamples; samplesRead++) {
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

} // namespace

single_slave_observer::single_slave_observer(std::shared_ptr<cse::slave> slave)
    : slave_(slave)
{
    for (const auto& vd : slave->model_description().variables) {
        if (vd.type == cse::variable_type::real) {
            realIndexes_.push_back(vd.index);
        }
        if (vd.type == cse::variable_type::integer) {
            intIndexes_.push_back(vd.index);
        }
    }

    observe(0);
}

void single_slave_observer::observe(long currentStep)
{
    realSamples_[currentStep].resize(realIndexes_.size());
    intSamples_[currentStep].resize(intIndexes_.size());

    std::lock_guard<std::mutex> lock(lock_);
    slave_->get_real_variables(
        gsl::make_span(realIndexes_),
        gsl::make_span(realSamples_[currentStep]));

    slave_->get_integer_variables(
        gsl::make_span(intIndexes_),
        gsl::make_span(intSamples_[currentStep]));
}

void single_slave_observer::get_real(
    const cse_variable_index variables[],
    size_t nv,
    double values[])
{
    std::lock_guard<std::mutex> lock(lock_);
    get<double>(variables, realIndexes_, realSamples_, nv, values);
}

void single_slave_observer::get_int(
    const cse_variable_index variables[],
    size_t nv,
    int values[])
{
    std::lock_guard<std::mutex> lock(lock_);
    get<int>(variables, intIndexes_, intSamples_, nv, values);
}

size_t single_slave_observer::get_real_samples(
    cse_variable_index variableIndex,
    long fromStep,
    size_t nSamples,
    double values[],
    long steps[])
{
    std::lock_guard<std::mutex> lock(lock_);
    return get_samples<double>(variableIndex, realIndexes_, realSamples_, fromStep, nSamples, values, steps);
}

size_t single_slave_observer::get_int_samples(
    cse_variable_index variableIndex,
    long fromStep,
    size_t nSamples,
    int values[],
    long steps[])
{
    std::lock_guard<std::mutex> lock(lock_);
    return get_samples<int>(variableIndex, intIndexes_, intSamples_, fromStep, nSamples, values, steps);
}

} // namespace cse
