#include <cse/observer.hpp>

#include <map>
#include <mutex>

namespace cse
{

namespace
{

template<typename T>
void get(
    const variable_index variables[],
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
    variable_index variableIndex,
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

class single_slave_membuffer_observer
{
public:
    single_slave_membuffer_observer(observable* observable)
    {
        for (const auto& vd : observable->model_description().variables) {
            if (vd.type == cse::variable_type::real) {
                realIndexes_.push_back(vd.index);
            }
            if (vd.type == cse::variable_type::integer) {
                intIndexes_.push_back(vd.index);
            }
        }

        observe(0);
    }

    void observe(long timeStep)
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

    void get_real(const variable_index variables[], size_t nv, double values[])
    {
        std::lock_guard<std::mutex> lock(lock_);
        get<double>(variables, realIndexes_, realSamples_, nv, values);
    }

    void get_int(const variable_index variables[], size_t nv, int values[])
    {
        std::lock_guard<std::mutex> lock(lock_);
        get<int>(variables, intIndexes_, intSamples_, nv, values);
    }

    size_t get_real_samples(variable_index variableIndex, long fromStep, size_t nSamples, double values[], long steps[])
    {
        std::lock_guard<std::mutex> lock(lock_);
        return get_samples<double>(variableIndex, realIndexes_, realSamples_, fromStep, nSamples, values, steps);
    }

    size_t get_int_samples(variable_index variableIndex, long fromStep, size_t nSamples, int values[], long steps[])
    {
        std::lock_guard<std::mutex> lock(lock_);
        return get_samples<int>(variableIndex, intIndexes_, intSamples_, fromStep, nSamples, values, steps);
    }

private:
    std::map<long, std::vector<double>> realSamples_;
    std::map<long, std::vector<int>> intSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    observable* observable_;
    std::mutex lock_;
};


membuffer_observer::membuffer_observer()
{
}

void membuffer_observer::simulator_added(simulator_index index, observable* simulator)
{
    slaveObservers[index] = std::make_unique<single_slave_membuffer_observer>(simulator);
}

void membuffer_observer::simulator_removed(simulator_index index)
{
    slaveObservers.erase(index);
}

void membuffer_observer::variables_connected(variable_id /*output*/, variable_id /*input*/)
{
}

void membuffer_observer::variable_disconnected(variable_id /*input*/)
{
}

void membuffer_observer::step_complete(
    step_number lastStep,
    time_duration lastStepSize,
    time_point currentTime)
{
    for (const auto& slaveObserver : slaveObservers) {
        slaveObserver.second->observe(lastStep);
    }
}

membuffer_observer::~membuffer_observer() noexcept = default;

} // namespace cse