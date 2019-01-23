#include "cse/observer/file_observer.hpp"

#include <codecvt>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include <boost/date_time/local_time/local_time.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

#include "cse/error.hpp"


namespace cse
{

class file_observer::slave_value_writer
{
public:
    slave_value_writer(observable* observable, boost::filesystem::path logPath, bool binary, size_t limit, time_point currentTime)
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

        // Create CSV header row
        if (!binary_) {
            std::vector<variable_description> intVars;
            std::vector<variable_description> realVars;
            std::vector<variable_description> strVars;
            std::vector<variable_description> boolVars;

            for (const auto& vd : observable->model_description().variables) {
                if (vd.causality != variable_causality::local) {
                    switch (vd.type) {
                        case variable_type::real:
                            realVars.push_back(vd);
                            break;
                        case variable_type::integer:
                            intVars.push_back(vd);
                            break;
                        case variable_type::string:
                            strVars.push_back(vd);
                            break;
                        case variable_type::boolean:
                            boolVars.push_back(vd);
                            break;
                    }
                }
            }

            ss_ << "Time,StepCount,";

            for (const auto& vd : realVars) {
                ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
            }
            for (const auto& vd : intVars) {
                ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
            }
            for (const auto& vd : boolVars) {
                ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
            }
            for (const auto& vd : strVars) {
                ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
            }

            ss_ << std::endl;

            if (fsw_.is_open()) {
                fsw_ << ss_.rdbuf();
            }
        }

        // Expose variables & group indexes, ignore local variables
        for (const auto& vd : observable->model_description().variables) {
            if (vd.causality != variable_causality::local) {

                observable->expose_for_getting(vd.type, vd.index);
                if (vd.type == variable_type::real) {
                    realIndexes_.push_back(vd.index);
                }
                if (vd.type == variable_type::integer) {
                    intIndexes_.push_back(vd.index);
                }
            }
        }

        observe(0, currentTime);
    }

    void observe(step_number timeStep, time_point currentTime)
    {
        realSamples_[timeStep].reserve(realIndexes_.size());
        intSamples_[timeStep].reserve(intIndexes_.size());

        for (const auto idx : realIndexes_) {
            realSamples_[timeStep].push_back(observable_->get_real(idx));
        }
        for (const auto idx : intIndexes_) {
            intSamples_[timeStep].push_back(observable_->get_integer(idx));
        }
        timeSamples_[timeStep] = to_double_time_point(currentTime);

        if (++counter_ >= limit_) {
            persist();
            counter_ = 0;
        }
    }

    ~slave_value_writer()
    {
        persist();
        if (fsw_.is_open()) {
            fsw_.close();
        }
    }


private:
    template<typename T>
    void write(const std::vector<T>& values)
    {
        if (fsw_.is_open()) {
            if (binary_) {
                fsw_.write((char*)&values[0], values.size() * sizeof(T));
            } else {
                for (auto it = values.begin(); it != values.end(); ++it) {
                    if (it != values.begin()) ss_ << ",";
                    ss_ << *it;
                }
                ss_ << ",";
            }
        }
    }

    void persist()
    {
        ss_.clear();
        ss_.str(std::string());

        for (const auto& [stepCount, values] : realSamples_) {
            ss_ << timeSamples_[stepCount] << "," << stepCount << ",";
            write<double>(values);
            write<int>(intSamples_[stepCount]);
            ss_ << std::endl;
        }

        if (fsw_.is_open()) {
            fsw_ << ss_.rdbuf();
        }

        realSamples_.clear();
        intSamples_.clear();
        timeSamples_.clear();
    }

    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    std::map<step_number, double> timeSamples_;
    observable* observable_;
    boost::filesystem::ofstream fsw_;
    std::stringstream ss_;
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

std::string format_time(boost::posix_time::ptime now)
{
    std::locale loc(std::wcout.getloc(), new boost::posix_time::wtime_facet(L"%Y%m%d_%H%M%S"));

    std::basic_stringstream<wchar_t> wss;
    wss.imbue(loc);
    wss << now;

    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

    return converter.to_bytes(wss.str());
}

void file_observer::simulator_added(simulator_index index, observable* simulator, time_point currentTime)
{
    auto time_str = format_time(boost::posix_time::second_clock::local_time());

    auto name = simulator->model_description().name.append("_").append(std::to_string(index)).append("__");
    auto extension = time_str.append(binary_ ? ".bin" : ".csv");
    auto filename = name.append(extension);

    logPath_ = logDir_ / filename;
    valueWriters_[index] = std::make_unique<slave_value_writer>(simulator, logPath_, binary_, limit_, currentTime);
}

void file_observer::simulator_removed(simulator_index index, time_point /*currentTime*/)
{
    valueWriters_.erase(index);
}

void file_observer::variables_connected(variable_id /*output*/, variable_id /*input*/, time_point /*currentTime*/)
{
}

void file_observer::variable_disconnected(variable_id /*input*/, time_point /*currentTime*/)
{
}

void file_observer::step_complete(step_number lastStep, duration /*lastStepSize*/, time_point currentTime)
{
    for (const auto& valueWriter : valueWriters_) {
        valueWriter.second->observe(lastStep, currentTime);
    }
}

boost::filesystem::path file_observer::get_log_path()
{
    return logPath_;
}

file_observer::~file_observer()
{
}

} // namespace cse
