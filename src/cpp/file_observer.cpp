//
// Created by STENBRO on 10/30/2018.
//

#include <cse/error.hpp>
#include <cse/observer.hpp>
#include <map>
#include <mutex>

namespace cse
{

class file_observer::single_slave_observer
{
public:
    single_slave_observer(observable* observable)
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

        observe(0);
    }

    void observe(step_number timeStep)
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

    std::map<step_number, std::vector<double>> get_real_samples()
    {
        return realSamples_;
    }

    std::map<step_number, std::vector<int>> get_integer_samples()
    {
        return intSamples_;
    }

private:
    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    observable* observable_;
    std::mutex lock_;
};

file_observer::file_observer(std::string logPath, bool binary)
    : binary_(binary)
{

    if (binary_) {
        fsw_ = std::ofstream(logPath, std::ios_base::out | std::ios_base::binary);
    } else {
        fsw_ = std::ofstream(logPath, std::ios_base::out | std::ios_base::app);
    }

    if (fsw_.fail()) {
        throw std::runtime_error("Failed to open file stream for logging");
    }
}

void file_observer::simulator_added(simulator_index index, observable* simulator)
{
    slaveObservers_[index] = std::make_unique<single_slave_observer>(simulator);
}

void file_observer::simulator_removed(simulator_index index)
{
    slaveObservers_.erase(index);
}

void file_observer::variables_connected(variable_id /*output*/, variable_id /*input*/)
{
}

void file_observer::variable_disconnected(variable_id /*input*/)
{
}

void file_observer::step_complete(step_number lastStep, duration /*lastStepSize*/, time_point /*currentTime*/)
{
    for (const auto& slaveObserver : slaveObservers_) {
        slaveObserver.second->observe(lastStep);
    }
}

void file_observer::write_real_samples(simulator_index sim)
{
    std::map<step_number, std::vector<double>> samples = slaveObservers_.at(sim)->get_real_samples();

    if (fsw_.is_open()) {
        if (binary_) {
            for (auto const& [stepCount, values] : samples) {
                (void)stepCount;
                fsw_.write((char*)&values[0], values.size() * sizeof(double));
            }
        } else {
            for (auto const& [stepCount, values] : samples) {
                fsw_ << stepCount << ": ";

                for (auto value : values) {
                    fsw_ << value << ",";
                }

                fsw_ << std::endl;
            }
        }
    }
}

void file_observer::write_int_samples(simulator_index sim)
{
    std::map<step_number, std::vector<int>> samples = slaveObservers_.at(sim)->get_integer_samples();

    if (fsw_.is_open()) {
        if (binary_) {
            for (auto const& [stepCount, values] : samples) {
                (void)stepCount;
                fsw_.write((char*)&values[0], values.size() * sizeof(int));
            }
        } else {
            for (auto const& [stepCount, values] : samples) {
                fsw_ << stepCount << ": ";

                for (auto value : values) {
                    fsw_ << value << ",";
                }

                fsw_ << std::endl;
            }
        }
    }
}

file_observer::~file_observer()
{
    if (fsw_.is_open()) {
        fsw_.close();
    }
}

} // namespace cse
