#include <cse/slave_value_provider.hpp>

#include <map>
#include <mutex>

#include <cse/error.hpp>

namespace cse
{

namespace
{

template<typename T>
void get(
    gsl::span<const variable_index> variables,
    const std::vector<cse::variable_index>& indices,
    const std::map<step_number, std::vector<T>>& samples,
    gsl::span<T> values)
{
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
    std::map<step_number, time_point> timeSamples,
    step_number fromStep,
    gsl::span<T> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    size_t samplesRead = 0;
    const auto variableIndexIt = std::find(indices.begin(), indices.end(), variableIndex);
    if (variableIndexIt != indices.end()) {
        const size_t valueIndex = variableIndexIt - indices.begin();
        auto sampleIt = samples.begin();

        if (fromStep >= sampleIt->first) {
            sampleIt = samples.find(fromStep);
        }

        for (samplesRead = 0; samplesRead < static_cast<std::size_t>(values.size()); samplesRead++) {
            if ((sampleIt != samples.end()) && (sampleIt->first < fromStep + values.size())) {
                steps[samplesRead] = sampleIt->first;
                values[samplesRead] = sampleIt->second[valueIndex];
                times[samplesRead] = timeSamples[sampleIt->first];
                sampleIt++;
            } else {
                break;
            }
        }
    }
    return samplesRead;
}

} // namespace

slave_value_provider::slave_value_provider(observable* observable, time_point startTime, size_t bufSize)
    : observable_(observable)
    , bufSize_(bufSize)
{

    for (const auto& vd : observable->model_description().variables) {
        observable->expose_for_getting(vd.type, vd.index);
        if (vd.type == cse::variable_type::real) {
            realIndexes_.push_back(vd.index);
        }
        if (vd.type == cse::variable_type::integer) {
            intIndexes_.push_back(vd.index);
        }
    }

    observe(0, startTime);
}

slave_value_provider::~slave_value_provider() noexcept = default;

void slave_value_provider::observe(step_number timeStep, time_point currentTime)
{
    std::lock_guard<std::mutex> lock(lock_);

    realSamples_[timeStep].reserve(realIndexes_.size());
    intSamples_[timeStep].reserve(intIndexes_.size());

    // Assuming realSamples_.size() == intSamples_.size() as is currently the case
    if (realSamples_.size() >= bufSize_)
    {
        realSamples_.erase(realSamples_.begin());
        intSamples_.erase(intSamples_.begin());
        timeSamples_.erase(timeSamples_.begin());
    }

    for (const auto idx : realIndexes_) {
        realSamples_[timeStep].push_back(observable_->get_real(idx));
    }

    for (const auto idx : intIndexes_) {
        intSamples_[timeStep].push_back(observable_->get_integer(idx));
    }

    timeSamples_[timeStep] = currentTime;

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

size_t slave_value_provider::get_real_samples(variable_index variableIndex, step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps, gsl::span<time_point> times)
{

    std::lock_guard<std::mutex> lock(lock_);
    return get_samples<double>(variableIndex, realIndexes_, realSamples_, timeSamples_, fromStep, values, steps, times);
}

size_t slave_value_provider::get_int_samples(variable_index variableIndex, step_number fromStep, gsl::span<int> values, gsl::span<step_number> steps, gsl::span<time_point> times)
{
    CSE_INPUT_CHECK(fromStep >= realSamples_.begin()->first);

    std::lock_guard<std::mutex> lock(lock_);
    return get_samples<int>(variableIndex, intIndexes_, intSamples_, timeSamples_, fromStep, values, steps, times);
}

void slave_value_provider::get_step_numbers(time_point tBegin, time_point tEnd, gsl::span<step_number> steps)
{
    std::lock_guard<std::mutex> lock(lock_);
    step_number lastStep = timeSamples_.rbegin()->first;
    auto lastEntry = std::find_if(
        timeSamples_.begin(),
        timeSamples_.end(),
        [tEnd](const auto& entry) { return entry.second >= tEnd; });
    if (lastEntry != timeSamples_.end()) {
        lastStep = lastEntry->first;
    }

    step_number firstStep = timeSamples_.begin()->first;
    auto firstEntry = std::find_if(
        timeSamples_.rbegin(),
        timeSamples_.rend(),
        [tBegin](const auto& entry) { return entry.second <= tBegin; });
    if (firstEntry != timeSamples_.rend()) {
        firstStep = firstEntry->first;
    }

    steps[0] = firstStep;
    steps[1] = lastStep;
}

void slave_value_provider::get_step_numbers(duration duration, gsl::span<step_number> steps)
{
    std::lock_guard<std::mutex> lock(lock_);
    auto lastEntry = timeSamples_.rbegin();
    step_number lastStep = lastEntry->first;

    step_number firstStep = timeSamples_.begin()->first;
    const time_point tBegin = lastEntry->second - duration;
    auto firstIt = std::find_if(
        timeSamples_.rbegin(),
        timeSamples_.rend(),
        [tBegin](const auto& entry) { return entry.second <= tBegin; });
    if (firstIt != timeSamples_.rend()) {
        firstStep = firstIt->first;
    }

    steps[0] = firstStep;
    steps[1] = lastStep;
}

} // namespace cse
