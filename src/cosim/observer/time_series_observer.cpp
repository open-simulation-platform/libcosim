/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/time_series_observer.hpp"

#include "cosim/error.hpp"
#include "cosim/execution.hpp"
#include "cosim/model_description.hpp"
#include "cosim/observer/observer.hpp"
#include "cosim/time.hpp"

#include <boost/range/adaptor/reversed.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <gsl/span>

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>


namespace cosim
{

namespace
{
constexpr std::size_t default_buffer_size = 10000;

template<typename T>
size_t get_samples(
    value_reference valueReference,
    const std::map<value_reference, std::map<step_number, T>>& variables,
    std::map<step_number, time_point> timeSamples,
    step_number fromStep,
    gsl::span<T> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    std::size_t samplesRead = 0;
    const auto& samplesIt = variables.find(valueReference);
    if (samplesIt != variables.end()) {
        const auto& samples = samplesIt->second;
        if (samples.empty()) {
            throw std::out_of_range("No samples recorded yet!");
        }
        auto sampleIt = samples.begin();
        if (fromStep >= sampleIt->first) {
            sampleIt = samples.find(fromStep);
            if (sampleIt == samples.end()) { // fromStep was not found, use next.
                sampleIt = samples.upper_bound(fromStep);
            }
        }
        auto valueOutput = values.begin();
        auto stepOutput = steps.begin();
        auto timeOutput = times.begin();
        for (samplesRead = 0; samplesRead < values.size(); samplesRead++) {
            if ((sampleIt != samples.end()) && (sampleIt->first < fromStep + static_cast<step_number>(values.size()))) {
                *stepOutput++ = sampleIt->first;
                *valueOutput++ = sampleIt->second;
                *timeOutput++ = timeSamples.try_emplace(sampleIt->first).first->second;
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
    single_slave_observer(const single_slave_observer&) = delete;
    single_slave_observer& operator=(const single_slave_observer&) = delete;
    single_slave_observer(single_slave_observer&&) = delete;
    single_slave_observer& operator=(single_slave_observer&&) = delete;

    void observe(step_number timeStep, time_point currentTime)
    {
        std::scoped_lock const lock(lock_);
        for (auto& [type, typedSamples] : samples_) {
            for (auto& [reference, samples] : typedSamples) {
                samples[timeStep] =
                    observable_->get_variable_value(type, reference);
                adjustIfFull(samples, bufSize_);
            }
        }
        timeSamples_[timeStep] = currentTime;
        adjustIfFull(timeSamples_, bufSize_);
    }

    void reset()
    {
        timeSamples_.clear();
        for (auto& sampleGroup : samples_) {
            for (auto& entry : sampleGroup.second) {
                entry.second.clear();
            }
        }
    }

    void start_observing(variable_type type, value_reference reference)
    {
        std::scoped_lock const lock(lock_);
        if (type == variable_type::clock) {
            throw std::invalid_argument(
                "Clock variables cannot be sampled as data values");
        }
        samples_[type][reference] = {};
        observable_->expose_for_getting(type, reference);
    }

    void stop_observing(variable_type type, value_reference reference)
    {
        std::scoped_lock const lock(lock_);
        samples_.at(type).erase(reference);
    }

    size_t get_variable_samples(
        variable_type type,
        value_reference valueReference,
        step_number fromStep,
        gsl::span<variable_value> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times)
    {
        std::scoped_lock const lock(lock_);
        const auto typedSamples = samples_.find(type);
        if (typedSamples == samples_.end()) {
            return 0;
        }
        return get_samples<variable_value>(
            valueReference,
            typedSamples->second,
            timeSamples_,
            fromStep,
            values,
            steps,
            times);
    }

    size_t get_real_samples(value_reference valueReference, step_number fromStep, gsl::span<double> values, gsl::span<step_number> steps, gsl::span<time_point> times)
    {
        std::scoped_lock const lock(lock_);
        const auto typedSamples = samples_.find(variable_type::real);
        if (typedSamples == samples_.end()) {
            return 0;
        }
        std::vector<variable_value> wholeValues(values.size());
        const auto count = get_samples<variable_value>(
            valueReference,
            typedSamples->second,
            timeSamples_,
            fromStep,
            wholeValues,
            steps,
            times);
        auto output = values.begin();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& wholeValue = wholeValues.at(i);
            if (!wholeValue.is_scalar()) {
                throw std::invalid_argument(
                    "Legacy scalar access does not support arrays");
            }
            *output++ =
                std::get<real_values>(wholeValue.values()).values.at(0);
        }
        return count;
    }

    size_t get_int_samples(value_reference valueReference, step_number fromStep, gsl::span<int> values, gsl::span<step_number> steps, gsl::span<time_point> times)
    {
        std::scoped_lock const lock(lock_);
        const auto typedSamples = samples_.find(variable_type::integer);
        if (typedSamples == samples_.end()) {
            return 0;
        }
        std::vector<variable_value> wholeValues(values.size());
        const auto count = get_samples<variable_value>(
            valueReference,
            typedSamples->second,
            timeSamples_,
            fromStep,
            wholeValues,
            steps,
            times);
        auto output = values.begin();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& wholeValue = wholeValues.at(i);
            if (!wholeValue.is_scalar()) {
                throw std::invalid_argument(
                    "Legacy scalar access does not support arrays");
            }
            *output++ = std::get<integer_values>(
                wholeValue.values())
                            .values.at(0);
        }
        return count;
    }

    void get_step_numbers(time_point tBegin, time_point tEnd, gsl::span<step_number> steps) // NOLINT(bugprone-easily-swappable-parameters): The arguments describe the begin and end of the requested range.
    {
        std::scoped_lock const lock(lock_);
        COSIM_INPUT_CHECK(steps.size() >= 2);
        if (timeSamples_.empty()) {
            throw std::out_of_range("No time samples recorded yet!");
        }
        step_number lastStep = timeSamples_.rbegin()->first;
        auto const lastEntry = boost::range::find_if(
            timeSamples_,
            [tEnd](const auto& entry) { return entry.second >= tEnd; });
        if (lastEntry != timeSamples_.end()) {
            lastStep = lastEntry->first;
        }

        step_number firstStep = timeSamples_.begin()->first;
        auto const firstEntry = boost::range::find_if(
            boost::adaptors::reverse(timeSamples_),
            [tBegin](const auto& entry) { return entry.second <= tBegin; });
        if (firstEntry.base() != timeSamples_.begin()) {
            firstStep = firstEntry->first;
        }

        auto output = steps.begin();
        *output++ = firstStep;
        *output = lastStep;
    }

    void get_step_numbers(duration duration, gsl::span<step_number> steps)
    {
        std::scoped_lock const lock(lock_);
        COSIM_INPUT_CHECK(steps.size() >= 2);
        if (timeSamples_.empty()) {
            throw std::out_of_range("No time samples recorded yet!");
        }
        auto const lastEntry = timeSamples_.rbegin();
        const step_number lastStep = lastEntry->first;

        step_number firstStep = timeSamples_.begin()->first;
        const time_point tBegin = lastEntry->second - duration;
        auto const firstIt = boost::range::find_if(
            boost::adaptors::reverse(timeSamples_),
            [tBegin](const auto& entry) { return entry.second <= tBegin; });
        if (firstIt.base() != timeSamples_.begin()) {
            firstStep = firstIt->first;
        }

        auto output = steps.begin();
        *output++ = firstStep;
        *output = lastStep;
    }

    std::map<step_number, double> get_real_samples_map(value_reference idx)
    {
        std::scoped_lock const lock(lock_);
        std::map<step_number, double> result;
        for (const auto& [step, value] :
            samples_.at(variable_type::real).at(idx)) {
            if (!value.is_scalar()) {
                throw std::invalid_argument(
                    "Synchronized real series do not support arrays");
            }
            result.emplace(
                step,
                std::get<real_values>(value.values()).values.at(0));
        }
        return result;
    }

private:
    std::map<
        variable_type,
        std::map<value_reference, std::map<step_number, variable_value>>>
        samples_;
    std::map<step_number, time_point> timeSamples_;
    observable* observable_;
    size_t bufSize_;
    std::mutex lock_;
};

time_series_observer::time_series_observer()
    : bufSize_(default_buffer_size)
{
}

time_series_observer::time_series_observer(size_t bufferSize)
{
    if (bufferSize > 0) {
        bufSize_ = bufferSize;
    } else {
        std::ostringstream oss;
        oss << "Can't define an observer with buffer size " << bufferSize << ", minimum allowed buffer size is 1.";
        throw std::invalid_argument(oss.str());
    }
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

void time_series_observer::simulation_initialized(
    step_number firstStep,
    time_point startTime)
{
    for (const auto& entry : slaveObservers_) {
        entry.second->observe(firstStep, startTime);
    }
}

void time_series_observer::step_complete(
    step_number /*lastStep*/,
    duration /*lastStepSize*/,
    time_point /*currentTime*/)
{
}

void time_series_observer::simulator_step_complete(
    simulator_index index,
    step_number lastStep,
    duration /*lastStepSize*/,
    time_point currentTime)
{
    slaveObservers_.at(index)->observe(lastStep, currentTime);
}

void time_series_observer::state_restored(
    step_number currentStep, time_point currentTime)
{
    for (const auto& entry : slaveObservers_) {
        entry.second->reset();
        entry.second->observe(currentStep, currentTime);
    }
}

void time_series_observer::start_observing(variable_id variable)
{
    slaveObservers_.at(variable.simulator)->start_observing(variable.type, variable.reference);
}

void time_series_observer::stop_observing(variable_id variable)
{
    slaveObservers_.at(variable.simulator)->stop_observing(variable.type, variable.reference);
}

std::size_t time_series_observer::get_variable_samples(
    simulator_index sim,
    variable_type type,
    value_reference valueReference,
    step_number fromStep,
    gsl::span<variable_value> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    COSIM_INPUT_CHECK(values.size() == steps.size());
    COSIM_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_variable_samples(
        type, valueReference, fromStep, values, steps, times);
}

std::size_t time_series_observer::get_real_samples(
    simulator_index sim,
    value_reference valueReference,
    step_number fromStep,
    gsl::span<double> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    COSIM_INPUT_CHECK(values.size() == steps.size());
    COSIM_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_real_samples(valueReference, fromStep, values, steps, times);
}

std::size_t time_series_observer::get_integer_samples(
    simulator_index sim,
    value_reference valueReference,
    step_number fromStep,
    gsl::span<int> values,
    gsl::span<step_number> steps,
    gsl::span<time_point> times)
{
    COSIM_INPUT_CHECK(values.size() == steps.size());
    COSIM_INPUT_CHECK(times.size() == values.size());
    return slaveObservers_.at(sim)->get_int_samples(valueReference, fromStep, values, steps, times);
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

std::size_t time_series_observer::get_synchronized_real_series(
    simulator_index sim1,
    value_reference valueReference1, // NOLINT(bugprone-easily-swappable-parameters): Preserve the public paired-variable interface.
    simulator_index sim2,
    value_reference valueReference2, // NOLINT(bugprone-easily-swappable-parameters): Preserve the public paired-variable interface.
    step_number fromStep,
    gsl::span<double> values1,
    gsl::span<double> values2)
{
    COSIM_INPUT_CHECK(values1.size() == values2.size());

    const auto realSamples1 = slaveObservers_.at(sim1)->get_real_samples_map(valueReference1);
    const auto realSamples2 = slaveObservers_.at(sim2)->get_real_samples_map(valueReference2);

    if (realSamples1.empty() || realSamples2.empty()) {
        throw std::out_of_range("Samples for both variables not recorded yet!");
    }
    auto const lastStep1 = realSamples1.rbegin()->first;
    auto const lastStep2 = realSamples2.rbegin()->first;

    size_t samplesRead = 0;
    auto output1 = values1.begin();
    auto output2 = values2.begin();
    for (auto step = fromStep; step < fromStep + static_cast<step_number>(values1.size()); step++) {
        auto const sample1 = realSamples1.find(step);
        auto const sample2 = realSamples2.find(step);
        if (sample1 != realSamples1.end() && sample2 != realSamples2.end()) {
            *output1++ = sample1->second;
            *output2++ = sample2->second;
            samplesRead++;
        } else if (lastStep1 < step || lastStep2 < step) {
            break;
        }
    }
    return samplesRead;
}

time_series_observer::~time_series_observer() noexcept = default;

} // namespace cosim
