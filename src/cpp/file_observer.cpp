#include <map>
#include <mutex>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>

#include <cse/error.hpp>
#include <cse/observer.hpp>


namespace cse
{

namespace
{

} // namespace

class file_observer::single_slave_observer
{
public:
    single_slave_observer(observable* observable, boost::filesystem::path logPath, bool binary, size_t limit)
        : observable_(observable)
        , binary_(binary)
        , limit_(limit)
    {
        boost::filesystem::create_directories(logPath.parent_path());

        if (binary_) {
            fsw_.open(logPath, std::ios_base::out | std::ios_base::binary);
        } else {
            fsw_.open(logPath, std::ios_base::out | std::ios_base::app);
        }
        if (fsw_.fail()) {
            throw std::runtime_error("Failed to open file stream for logging");
        }

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
        realSamples_[timeStep].reserve(realIndexes_.size());
        intSamples_[timeStep].reserve(intIndexes_.size());

        for (const auto idx : realIndexes_) {
            realSamples_[timeStep].push_back(observable_->get_real(idx));
        }
        for (const auto idx : intIndexes_) {
            intSamples_[timeStep].push_back(observable_->get_integer(idx));
        }
        if (++counter_ >= limit_) {
            persist();
            counter_ = 0;
        }
    }

    ~single_slave_observer()
    {
        persist();
        if (fsw_.is_open()) {
            fsw_.close();
        }
    }


private:
    template<typename T>
    void write(step_number stepCount, const std::vector<T>& values)
    {
        if (fsw_.is_open()) {
            if (binary_) {
                fsw_.write((char*)&values[0], values.size() * sizeof(T));
            } else {
                fsw_ << stepCount << ": ";
                for (auto value : values) {
                    fsw_ << value << ",";
                }
                fsw_ << std::endl;
            }
        }
    }

    void persist()
    {
        for (auto const& [stepCount, values] : realSamples_) {
            write<double>(stepCount, values);
        }
        for (auto const& [stepCount, values] : intSamples_) {
            write<int>(stepCount, values);
        }
        realSamples_.clear();
        intSamples_.clear();
    }

    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    observable* observable_;
    boost::filesystem::ofstream fsw_;
    bool binary_;
    size_t counter_ = 0;
    size_t limit_ = 10;
};

file_observer::file_observer(boost::filesystem::path logDir, bool binary, size_t limit)
    : logDir_(logDir)
    , binary_(binary)
    , limit_(limit)
{
}

void file_observer::simulator_added(simulator_index index, observable* simulator)
{
    auto filename = std::to_string(index).append(binary_ ? ".bin" : ".csv");
    auto slaveLogPath = logDir_ / filename;
    slaveObservers_[index] = std::make_unique<single_slave_observer>(simulator, slaveLogPath, binary_, limit_);
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

file_observer::~file_observer()
{
}

} // namespace cse
