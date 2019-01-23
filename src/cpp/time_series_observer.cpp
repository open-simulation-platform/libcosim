#include <cse/observer.hpp>

#include <map>
#include <mutex>
#include <sstream>

#include <cse/error.hpp>


namespace cse
{

namespace
{
template<typename T>
size_t get_samples(
    variable_index variableIndex,
    const std::map<variable_index, std::map<step_number, T>>& variables,
    std::map<step_number, time_point> timeSamples,
    step_number fromStep,
    gsl::span<T> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    size_t samplesRead = 0;
    const auto& samplesIt = variables.find(variableIndex);
    if (samplesIt != variables.end()) {
        const auto& samples = samplesIt->second;
        if (samples.empty()) {
            throw std::out_of_range("No samples recorded yet!");
        }
        auto sampleIt = samples.begin();
        if (fromStep >= sampleIt->first) {
            sampleIt = samples.find(fromStep);
        }
        for (samplesRead = 0; samplesRead < static_cast<std::size_t>(values.size()); samplesRead++) {
            if ((sampleIt != samples.end()) && (sampleIt->first < fromStep + values.size())) {
                steps[samplesRead] = sampleIt->first;
                values[samplesRead] = sampleIt->second;
                times[samplesRead] = timeSamples[sampleIt->first];
                sampleIt++;
            } else {
                break;
            }
        }
    }
    return samplesRead;
}

template<typename T>
void adjustIfFull(std::map<step_number, T>& buffer, size_t maxSize)
{
    if (maxSize <= 0) {
        return;
    }
    if (buffer.size() > maxSize) {
        buffer.erase(buffer.begin());
    }
}

} // namespace

class time_series_observer::single_slave_observer
{

public:
    single_slave_observer(observable* observable, time_point startTime, size_t bufSize)
        : observable_(observable)
        , bufSize_(bufSize)
    {
        observe(0, startTime);
    }

    ~single_slave_observer() noexcept = default;

    void observe(step_number timeStep, time_point currentTime)
    {
        std::lock_guard<std::mutex> lock(lock_);
        for (auto& [idx, samples] : realSamples_) {
            samples[timeStep] = observable_->get_real(idx);
            adjustIfFull(samples, bufSize_);
        }
        for (auto& [idx, samples] : intSamples_) {
            samples[timeStep] = observable_->get_integer(idx);
            adjustIfFull(samples, bufSize_);
        }
        timeSamples_[timeStep] = currentTime;
        adjustIfFull(timeSamples_, bufSize_);
    }

    void start_observing(variable_type type, variable_index index)
    {
        std::lock_guard<std::mutex> lock(lock_);
        switch (type) {
            case variable_type::real:
                realSamples_[index] = std::map<step_number, double>();
                observable_->expose_for_getting(type, index);
                break;
            case variable_type::integer:
                intSamples_[index] = std::map<step_number, int>();
                observable_->expose_for_getting(type, index);
                break;
            default:
                std::ostringstream oss;
                oss << "No support for observing variable with type " << type
                    << " and index " << index;
                throw std::invalid_argument(oss.str());
        }
    }

    void stop_observing(variable_type type, variable_index index)
    {
        std::lock_guard<std::mutex> lock(lock_);
        switch (type) {
            case variable_type::real:
                realSamples_.erase(index);
                break;
            case variable_type::integer:
                intSamples_.erase(index);
                break;
            default:
                std::ostringstream oss;
                oss << "Could not stop observing variable with type " << type
                    << " and index " << index;
                throw std::invalid_argument(oss.str());
        }
    }

    size_t get_real_samples(variable_index variableIndex, step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps, gsl::span<time_point> times)
    {
        std::lock_guard<std::mutex> lock(lock_);
        return get_samples<double>(variableIndex, realSamples_, timeSamples_, fromStep, values, steps, times);
    }

    size_t get_int_samples(variable_index variableIndex, step_number fromStep, gsl::span<int> values, gsl::span<step_number> steps, gsl::span<time_point> times)
    {
        std::lock_guard<std::mutex> lock(lock_);
        return get_samples<int>(variableIndex, intSamples_, timeSamples_, fromStep, values, steps, times);
    }

    void get_step_numbers(time_point tBegin, time_point tEnd, gsl::span<step_number> steps)
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

    void get_step_numbers(duration duration, gsl::span<step_number> steps)
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

private:
    std::map<variable_index, std::map<step_number, double>> realSamples_;
    std::map<variable_index, std::map<step_number, int>> intSamples_;
    std::map<step_number, time_point> timeSamples_;
    observable* observable_;
    size_t bufSize_;
    std::mutex lock_;
};

time_series_observer::time_series_observer()
    : bufSize_(0)
{
}

time_series_observer::time_series_observer(size_t bufferSize)
    : bufSize_(bufferSize)
{
}

void time_series_observer::simulator_added(simulator_index index, observable* simulator, time_point currentTime)
{
    slaveObservers_[index] = std::make_unique<single_slave_observer>(simulator, currentTime, bufSize_);
}

void time_series_observer::simulator_removed(simulator_index index, time_point /*currentTime*/)
{
    slaveObservers_.erase(index);
}

void time_series_observer::variables_connected(variable_id /*output*/, variable_id /*input*/, time_point /*currentTime*/)
{
}

void time_series_observer::variable_disconnected(variable_id /*input*/, time_point /*currentTime*/)
{
}

void time_series_observer::step_complete(
    step_number lastStep,
    duration /*lastStepSize*/,
    time_point currentTime)
{
    for (const auto& slaveObserver : slaveObservers_) {
        slaveObserver.second->observe(lastStep, currentTime);
    }
}

void time_series_observer::start_observing(variable_id id)
{
    slaveObservers_.at(id.simulator)->start_observing(id.type, id.index);
}

void time_series_observer::stop_observing(variable_id id)
{
    slaveObservers_.at(id.simulator)->stop_observing(id.type, id.index);
}

std::size_t time_series_observer::get_real_samples(
    simulator_index sim,
    variable_index variableIndex,
    step_number fromStep,
    gsl::span<double> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    CSE_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_real_samples(variableIndex, fromStep, values, steps, times);
}

std::size_t time_series_observer::get_integer_samples(
    simulator_index sim,
    variable_index variableIndex,
    step_number fromStep,
    gsl::span<int> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    CSE_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_int_samples(variableIndex, fromStep, values, steps, times);
}

void time_series_observer::get_step_numbers(
    simulator_index sim,
    duration duration,
    gsl::span<step_number> steps)
{
    slaveObservers_.at(sim)->get_step_numbers(duration, steps);
}

void time_series_observer::get_step_numbers(
    simulator_index sim,
    time_point tBegin,
    time_point tEnd,
    gsl::span<step_number> steps)
{
    slaveObservers_.at(sim)->get_step_numbers(tBegin, tEnd, steps);
}

time_series_observer::~time_series_observer() noexcept = default;

} // namespace cse
