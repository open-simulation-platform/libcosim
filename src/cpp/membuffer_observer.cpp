#include <cse/observer.hpp>

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
    std::map<step_number, double> timeSamples,
    step_number fromStep,
    gsl::span<T> values,
    gsl::span<step_number> steps,
    gsl::span<double> times)
{
    size_t samplesRead = 0;
    const auto variableIndexIt = std::find(indices.begin(), indices.end(), variableIndex);
    if (variableIndexIt != indices.end()) {
        const size_t valueIndex = variableIndexIt - indices.begin();
        auto sampleIt = samples.find(fromStep);
        for (samplesRead = 0; samplesRead < static_cast<std::size_t>(values.size()); samplesRead++) {
            if (sampleIt != samples.end()) {
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

class membuffer_observer::single_slave_observer
{
public:
    single_slave_observer(observable* observable, time_point startTime)
        : observable_(observable)
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

    void observe(step_number timeStep, time_point currentTime)
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
        timeSamples_[timeStep] = to_double_time_point(currentTime);
    }

    void get_real(gsl::span<const variable_index> variables, gsl::span<double> values)
    {
        std::lock_guard<std::mutex> lock(lock_);
        get<double>(variables, realIndexes_, realSamples_, values);
    }

    void get_int(gsl::span<const variable_index> variables, gsl::span<int> values)
    {
        std::lock_guard<std::mutex> lock(lock_);
        get<int>(variables, intIndexes_, intSamples_, values);
    }

    size_t get_real_samples(variable_index variableIndex, step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps, gsl::span<double> times)
    {
        std::lock_guard<std::mutex> lock(lock_);
        return get_samples<double>(variableIndex, realIndexes_, realSamples_, timeSamples_, fromStep, values, steps, times);
    }

    size_t get_int_samples(variable_index variableIndex, step_number fromStep, gsl::span<int> values, gsl::span<step_number> steps, gsl::span<double> times)
    {
        std::lock_guard<std::mutex> lock(lock_);
        return get_samples<int>(variableIndex, intIndexes_, intSamples_, timeSamples_, fromStep, values, steps, times);
    }

    size_t get_time_samples(step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps)
    {
        std::lock_guard<std::mutex> lock(lock_);
        size_t samplesRead = 0;
        auto sampleIt = timeSamples_.find(fromStep);
        for (samplesRead = 0; samplesRead < static_cast<std::size_t>(values.size()); samplesRead++) {
            if (sampleIt != timeSamples_.end()) {
                steps[samplesRead] = sampleIt->first;
                values[samplesRead] = sampleIt->second;
                sampleIt++;
            } else {
                break;
            }
        }
        return samplesRead;
    }

    void get_step_numbers(double tBegin, double tEnd, gsl::span<step_number> steps)
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

    void get_step_numbers(double duration, gsl::span<step_number> steps)
    {
        std::lock_guard<std::mutex> lock(lock_);
        auto lastEntry = timeSamples_.rbegin();
        step_number lastStep = lastEntry->first;

        step_number firstStep = timeSamples_.begin()->first;
        const double tBegin = lastEntry->second - duration;
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
    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::map<step_number, double> timeSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    observable* observable_;
    std::mutex lock_;
};


membuffer_observer::membuffer_observer()
{
}

void membuffer_observer::simulator_added(simulator_index index, observable* simulator, time_point startTime)
{
    slaveObservers_[index] = std::make_unique<single_slave_observer>(simulator, startTime);
}

void membuffer_observer::simulator_removed(simulator_index index)
{
    slaveObservers_.erase(index);
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
    for (const auto& slaveObserver : slaveObservers_) {
        slaveObserver.second->observe(lastStep, currentTime);
    }
}

void membuffer_observer::get_real(
    simulator_index sim,
    gsl::span<const variable_index> variables,
    gsl::span<double> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    slaveObservers_.at(sim)->get_real(variables, values);
}

void membuffer_observer::get_integer(
    simulator_index sim,
    gsl::span<const variable_index> variables,
    gsl::span<int> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    slaveObservers_.at(sim)->get_int(variables, values);
}

std::size_t membuffer_observer::get_real_samples(
    simulator_index sim,
    variable_index variableIndex,
    step_number fromStep,
    gsl::span<double> values,
    gsl::span<step_number> steps,
    gsl::span<double> times)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    CSE_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_real_samples(variableIndex, fromStep, values, steps, times);
}

std::size_t membuffer_observer::get_integer_samples(
    simulator_index sim,
    variable_index variableIndex,
    step_number fromStep,
    gsl::span<int> values,
    gsl::span<step_number> steps,
    gsl::span<double> times)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    CSE_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_int_samples(variableIndex, fromStep, values, steps, times);
}

std::size_t membuffer_observer::get_time_samples(
    simulator_index sim,
    step_number fromStep,
    gsl::span<double> values,
    gsl::span<step_number> steps)
{
    CSE_INPUT_CHECK(values.size() == steps.size());
    return slaveObservers_.at(sim)->get_time_samples(fromStep, values, steps);
}

void membuffer_observer::get_step_numbers(
    simulator_index sim,
    double duration,
    gsl::span<step_number> steps)
{
    slaveObservers_.at(sim)->get_step_numbers(duration, steps);
}

void membuffer_observer::get_step_numbers(
    simulator_index sim,
    double tBegin,
    double tEnd,
    gsl::span<step_number> steps)
{
    slaveObservers_.at(sim)->get_step_numbers(tBegin, tEnd, steps);
}

membuffer_observer::~membuffer_observer() noexcept = default;

} // namespace cse
