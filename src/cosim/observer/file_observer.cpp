/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/file_observer.hpp"

#include "cosim/error.hpp"
#include "cosim/log/logger.hpp"

#include <boost/date_time/local_time/local_time.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <codecvt>
#include <fstream>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

namespace cosim
{

namespace
{
std::string format_time(boost::posix_time::ptime now)
{
    std::locale loc(std::wcout.getloc(), new boost::posix_time::wtime_facet(L"%Y%m%d_%H%M%S_%f"));

    std::basic_stringstream<wchar_t> wss;
    wss.imbue(loc);
    wss << now;

    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.to_bytes(wss.str());
}

void clear_file_contents_if_exists(const cosim::filesystem::path& filePath, std::ofstream& fsw)
{
    if (cosim::filesystem::exists(filePath)) {
        // clear file contents
        fsw.open(filePath, std::ios_base::out | std::ios_base::trunc);
        fsw.close();
    }
}
} // namespace


class file_observer::slave_value_writer
{
public:
    slave_value_writer(observable* observable, cosim::filesystem::path& logDir, bool timeStampedFileNames = true)
        : observable_(observable)
        , logDir_(logDir)
        , timeStampedFileNames_(timeStampedFileNames)
    {
        initialize_default();
    }

    slave_value_writer(observable* observable, cosim::filesystem::path& logDir, size_t decimationFactor,
        const std::vector<variable_description>& variables, bool timeStampedFileNames = true)
        : observable_(observable)
        , logDir_(logDir)
        , decimationFactor_(decimationFactor)
        , timeStampedFileNames_(timeStampedFileNames)
    {
        initialize_config(variables);
    }

    void observe(step_number timeStep, time_point currentTime)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recording_) {
            if (!fsw_.is_open()) {
                create_log_file();
            }
            if (timeStep % decimationFactor_ == 0) {

                if (!realVars_.empty()) realSamples_[timeStep].reserve(realVars_.size());
                if (!intVars_.empty()) intSamples_[timeStep].reserve(intVars_.size());
                if (!boolVars_.empty()) boolSamples_[timeStep].reserve(boolVars_.size());
                if (!stringVars_.empty()) stringSamples_[timeStep].reserve(stringVars_.size());

                for (const auto& vd : realVars_) {
                    realSamples_[timeStep].push_back(observable_->get_real(vd.reference));
                }
                for (const auto& vd : intVars_) {
                    intSamples_[timeStep].push_back(observable_->get_integer(vd.reference));
                }
                for (const auto& vd : boolVars_) {
                    boolSamples_[timeStep].push_back(observable_->get_boolean(vd.reference));
                }
                for (const auto& vd : stringVars_) {
                    stringSamples_[timeStep].push_back(observable_->get_string(vd.reference));
                }
                timeSamples_[timeStep] = to_double_time_point(currentTime);

                persist();
            }
        }
    }

    void start_recording()
    {
        recording_ = true;
    }

    void stop_recording()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fsw_.is_open()) {
            fsw_.close();
        }
        recording_ = false;
        realSamples_.clear();
        intSamples_.clear();
        boolSamples_.clear();
        stringSamples_.clear();
        timeSamples_.clear();
    }

    ~slave_value_writer()
    {
        if (fsw_.is_open()) {
            fsw_.close();
        }
    }

private:
    template<typename T>
    void write(const std::vector<T>& values, std::stringstream& ss)
    {
        for (auto it = values.begin(); it != values.end(); ++it) {
            ss << "," << *it;
        }
    }

    void initialize_variable(const variable_description& vd)
    {
        observable_->expose_for_getting(vd.type, vd.reference);

        switch (vd.type) {
            case variable_type::real:
                realVars_.push_back(vd);
                break;
            case variable_type::integer:
                intVars_.push_back(vd);
                break;
            case variable_type::string:
                stringVars_.push_back(vd);
                break;
            case variable_type::boolean:
                boolVars_.push_back(vd);
                break;
            case variable_type::enumeration:
                COSIM_PANIC();
        }
    }

    /** Default constructor initialization, all variables are logged. */
    void initialize_default()
    {
        if (!timeStampedFileNames_) {
            const auto filePath = logDir_ / observable_->name().append(".csv");
            clear_file_contents_if_exists(filePath, fsw_);
        }

        for (const auto& vd : observable_->model_description().variables) {
            if (vd.causality != variable_causality::local) {
                initialize_variable(vd);
            }
        }
    }

    /** External config initialization, only configured variables are logged. */
    void initialize_config(const std::vector<variable_description>& variables)
    {
        if (!timeStampedFileNames_) {
            const auto filePath = logDir_ / observable_->name().append(".csv");
            clear_file_contents_if_exists(filePath, fsw_);
        }

        for (const auto& vd : variables) {
            initialize_variable(vd);
        }
    }

    void create_log_file()
    {
        std::string filename;
        std::stringstream ss;
        if (!timeStampedFileNames_) {
            filename = observable_->name().append(".csv");
        } else {
            auto time_str = format_time(boost::posix_time::microsec_clock::local_time());
            filename = observable_->name().append("_").append(time_str).append(".csv");
        }

        const auto filePath = logDir_ / filename;
        cosim::filesystem::create_directories(logDir_);
        fsw_.open(filePath, std::ios_base::out | std::ios_base::app);

        if (fsw_.fail()) {
            throw std::runtime_error("Failed to open file stream for logging");
        }

        ss << "Time,StepCount";

        for (const auto& vd : realVars_) {
            ss << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }
        for (const auto& vd : intVars_) {
            ss << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }
        for (const auto& vd : boolVars_) {
            ss << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }
        for (const auto& vd : stringVars_) {
            ss << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }

        ss << std::endl;

        if (fsw_.is_open()) {
            fsw_ << ss.rdbuf();
        }
    }

    void persist()
    {
        std::stringstream ss;
        if (fsw_.is_open()) {

            for (const auto& [stepCount, times] : timeSamples_) {
                ss << times << "," << stepCount;

                if (realSamples_.count(stepCount)) write<double>(realSamples_[stepCount], ss);
                if (intSamples_.count(stepCount)) write<int>(intSamples_[stepCount], ss);
                if (boolSamples_.count(stepCount)) write<bool>(boolSamples_[stepCount], ss);
                if (stringSamples_.count(stepCount)) write<std::string_view>(stringSamples_[stepCount], ss);

                ss << std::endl;
            }

            fsw_ << ss.rdbuf();
        }

        realSamples_.clear();
        intSamples_.clear();
        boolSamples_.clear();
        stringSamples_.clear();
        timeSamples_.clear();
    }

    std::map<step_number, std::vector<double>> realSamples_;
    std::map<step_number, std::vector<int>> intSamples_;
    std::map<step_number, std::vector<bool>> boolSamples_;
    std::map<step_number, std::vector<std::string_view>> stringSamples_;
    std::map<step_number, double> timeSamples_;
    std::vector<variable_description> realVars_;
    std::vector<variable_description> intVars_;
    std::vector<variable_description> boolVars_;
    std::vector<variable_description> stringVars_;
    observable* observable_;
    cosim::filesystem::path logDir_;
    size_t decimationFactor_ = 1;
    std::ofstream fsw_;
    std::atomic<bool> recording_ = true;
    std::mutex mutex_;
    bool timeStampedFileNames_ = true;
};

file_observer::file_observer(const cosim::filesystem::path& logDir, const std::optional<file_observer_config>& config)
    : config_(config)
    , logDir_(cosim::filesystem::absolute(logDir))
{
}

namespace
{

template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key)
{
    return tree.get<T>("<xmlattr>." + key);
}

template<typename T>
std::optional<T> get_optional_attribute(const boost::property_tree::ptree& tree, const std::string& key)
{
    const auto result = tree.get_optional<T>("<xmlattr>." + key);
    return result ? *result : std::optional<T>();
}

} // namespace

void file_observer::simulator_added(
    simulator_index index,
    observable* simulator,
    time_point /*currentTime*/)
{
    simulators_[index] = simulator;

    if (config_) {
        //        // Read all configured model names from the XML. If simulator name is not in the list, do nothing.
        //        std::vector<std::string> modelNames;
        //        for (const auto& simulatorChild : ptree_.get_child("simulators")) {
        //            if (simulatorChild.first == "simulator") {
        //                modelNames.push_back(get_attribute<std::string>(simulatorChild.second, "name"));
        //            }
        //        }
        if (config_->should_log_simulator(simulator->name())) {
            auto config = parse_config(simulator->name());

            valueWriters_[index] = std::make_unique<slave_value_writer>(
                simulator,
                logDir_,
                config.decimationFactor,
                config.variables,
                config.timeStampedFileNames);
        } else {
            return;
        }
    } else {
        valueWriters_[index] = std::make_unique<slave_value_writer>(simulator, logDir_);
    }
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

void file_observer::simulation_initialized(step_number firstStep, time_point startTime)
{
    if (recording_) {
        for (const auto& entry : valueWriters_) {
            entry.second->observe(firstStep, startTime);
        }
    }
}

void file_observer::step_complete(step_number /*lastStep*/, duration /*lastStepSize*/, time_point /*currentTime*/)
{
}

void file_observer::simulator_step_complete(simulator_index index, step_number lastStep, duration /*lastStepSize*/, time_point currentTime)
{
    if (auto writer = valueWriters_.find(index); writer != valueWriters_.end() && recording_) {
        writer->second->observe(lastStep, currentTime);
    }
}

cosim::filesystem::path file_observer::get_log_path()
{
    return logDir_;
}

bool file_observer::is_recording()
{
    return recording_;
}

void file_observer::start_recording()
{
    if (recording_) {
        throw std::runtime_error("File observer is already recording");
    } else {
        for (const auto& entry : valueWriters_) {
            entry.second->start_recording();
        }
        recording_ = true;
    }
}

void file_observer::stop_recording()
{
    if (!recording_) {
        throw std::runtime_error("File observer has already stopped recording");
    } else {
        for (const auto& entry : valueWriters_) {
            entry.second->stop_recording();
        }
        recording_ = false;
    }
}

cosim::observable* find_simulator(
    const std::unordered_map<simulator_index, observable*>& simulators,
    const std::string& simulatorName)
{
    for (const auto& entry : simulators) {
        if (entry.second->name() == simulatorName) {
            return entry.second;
        }
    }
    throw std::invalid_argument("Can't find simulator with name: " + simulatorName);
}

file_observer::simulator_logging_config file_observer::parse_config(const std::string& simulatorName)
{
    const bool timeStampedFileNames = config_->timeStampedFileNames_;
    for (const auto& [modelName, variables] : config_->variablesToLog_) {

        if (modelName == simulatorName) {
            simulator_logging_config config;
            config.timeStampedFileNames = timeStampedFileNames;
            config.decimationFactor = variables.first;

            const auto& simulator = find_simulator(simulators_, modelName);
            if (variables.second.empty()) {

                for (const auto& vd : simulator->model_description().variables) {
                    switch (vd.type) {
                        case variable_type::real:
                        case variable_type::integer:
                        case variable_type::boolean:
                        case variable_type::string:
                            config.variables.push_back(vd);
                            break;
                        default:
                            break;
                    }
                }
            } else {
                for (const auto& name : variables.second) {

                    const auto variableDescription =
                        find_variable(simulator->model_description(), name);

                    if (!variableDescription) {
                        throw std::runtime_error("Can't find variable descriptor with name " + name + " for model with name " + simulator->model_description().name);
                    }

                    switch (variableDescription->type) {
                        case variable_type::real:
                        case variable_type::integer:
                        case variable_type::boolean:
                        case variable_type::string:
                            config.variables.push_back(*variableDescription);
                            BOOST_LOG_SEV(log::logger(), log::info) << "Logging variable: " << modelName << ":" << name;
                            break;
                        default:
                            COSIM_PANIC_M("Variable type not supported.");
                    }
                }
            }
            return config;
        }
    }

    return simulator_logging_config();
}

file_observer::~file_observer() = default;


file_observer_config file_observer_config::parse(const filesystem::path& configPath)
{
    boost::property_tree::ptree ptree;
    boost::property_tree::read_xml(configPath.string(), ptree,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);

    file_observer_config config;
    std::vector<std::string> modelNames;
    for (const auto& simulator : ptree.get_child("simulators")) {
        if (simulator.first == "simulator") {
            const auto modelName = get_attribute<std::string>(simulator.second, "name");
            const auto decimationFactor = get_optional_attribute<size_t>(simulator.second, "decimationFactor");
            for (const auto& variable : simulator.second) {
                if (variable.first == "variable") {
                    const auto variableName = get_attribute<std::string>(variable.second, "name");
                    config.log_simulator_variable(modelName, variableName, decimationFactor);
                }
            }
            if (config.variablesToLog_[modelName].second.empty()) {
                config.log_all_simulator_variables(modelName, decimationFactor);
            }
        }
    }

    return config;
}

} // namespace cosim
