#include "cse/observer/file_observer.hpp"

#include "cse/error.hpp"
#include "cse/log/logger.hpp"

#include <boost/date_time/local_time/local_time.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <codecvt>
#include <locale>
#include <map>
#include <sstream>
#include <vector>

namespace cse
{

namespace
{
std::string format_time(boost::posix_time::ptime now)
{
    std::locale loc(std::wcout.getloc(), new boost::posix_time::wtime_facet(L"%Y%m%d_%H%M%S"));

    std::basic_stringstream<wchar_t> wss;
    wss.imbue(loc);
    wss << now;

    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.to_bytes(wss.str());
}
} // namespace


class file_observer::slave_value_writer
{
public:
    slave_value_writer(observable* observable, boost::filesystem::path& logDir)
        : observable_(observable)
        , logDir_(logDir)
    {
        initialize_default();
    }

    slave_value_writer(observable* observable, boost::filesystem::path& logDir, size_t decimationFactor,
        const std::vector<variable_description>& variables)
        : observable_(observable)
        , logDir_(logDir)
        , decimationFactor_(decimationFactor)
    {
        initialize_config(variables);
    }

    void observe(step_number timeStep, time_point currentTime)
    {
        if (!recording_) {
            write_header();
            recording_ = true;
        }
        if (++counter_ % decimationFactor_ == 0) {

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

            if (counter_ >= decimationFactor_) {
                persist();
                counter_ = 0;
            }
        }
    }

    void stop_recording()
    {
        persist();
        counter_ = 0;
        if (fsw_.is_open()) {
            fsw_.close();
        }
        recording_ = false;
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
        for (auto it = values.begin(); it != values.end(); ++it) {
            ss_ << "," << *it;
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
                CSE_PANIC();
        }
    }

    /** Default constructor initialization, all variables are logged. */
    void initialize_default()
    {
        for (const auto& vd : observable_->model_description().variables) {
            if (vd.causality != variable_causality::local) {
                initialize_variable(vd);
            }
        }
    }

    /** External config initialization, only configured variables are logged. */
    void initialize_config(const std::vector<variable_description>& variables)
    {
        for (const auto& vd : variables) {
            initialize_variable(vd);
        }
    }

    void write_header()
    {
        auto time_str = format_time(boost::posix_time::second_clock::local_time());
        auto filename = observable_->name().append("_").append(time_str).append(".csv");

        auto filepath = logDir_ / filename;
        boost::filesystem::create_directories(logDir_);
        fsw_.open(filepath, std::ios_base::out | std::ios_base::app);

        if (fsw_.fail()) {
            throw std::runtime_error("Failed to open file stream for logging");
        }

        ss_ << "Time,StepCount";

        for (const auto& vd : realVars_) {
            ss_ << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }
        for (const auto& vd : intVars_) {
            ss_ << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }
        for (const auto& vd : boolVars_) {
            ss_ << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }
        for (const auto& vd : stringVars_) {
            ss_ << "," << vd.name << " [" << vd.reference << " " << vd.type << " " << vd.causality << "]";
        }

        ss_ << std::endl;

        if (fsw_.is_open()) {
            fsw_ << ss_.rdbuf();
        }
    }

    void persist()
    {
        ss_.clear();
        ss_.str(std::string());

        if (fsw_.is_open()) {

            for (const auto& [stepCount, times] : timeSamples_) {
                ss_ << times << "," << stepCount;

                if (realSamples_.count(stepCount)) write<double>(realSamples_[stepCount]);
                if (intSamples_.count(stepCount)) write<int>(intSamples_[stepCount]);
                if (boolSamples_.count(stepCount)) write<bool>(boolSamples_[stepCount]);
                if (stringSamples_.count(stepCount)) write<std::string_view>(stringSamples_[stepCount]);

                ss_ << std::endl;
            }

            fsw_ << ss_.rdbuf();
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
    boost::filesystem::path logDir_;
    size_t decimationFactor_ = 1;
    boost::filesystem::ofstream fsw_;
    std::stringstream ss_;
    size_t counter_ = 0;
    bool recording_ = false;
};

file_observer::file_observer(const boost::filesystem::path& logDir)
    : logDir_(boost::filesystem::absolute(logDir))
{
}

file_observer::file_observer(const boost::filesystem::path& logDir, const boost::filesystem::path& configPath)
    : configPath_(configPath)
    , logDir_(boost::filesystem::absolute(logDir))
    , logFromConfig_(true)
{
    boost::property_tree::read_xml(configPath_.string(), ptree_,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);
}

namespace
{

template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key)
{
    return tree.get<T>("<xmlattr>." + key);
}

template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key, T& defaultValue)
{
    return tree.get<T>("<xmlattr>." + key, defaultValue);
}
} // namespace

void file_observer::simulator_added(
    simulator_index index,
    observable* simulator,
    time_point /*currentTime*/)
{
    simulators_[index] = simulator;

    if (logFromConfig_) {
        // Read all configured model names from the XML. If simulator name is not in the list, do nothing.
        std::vector<std::string> modelNames;
        for (const auto& simulatorChild : ptree_.get_child("simulators")) {
            modelNames.push_back(get_attribute<std::string>(simulatorChild.second, "name"));
        }
        if (std::find(modelNames.begin(), modelNames.end(), simulator->name()) != modelNames.end()) {
            auto config = parse_config(simulator->name());

            valueWriters_[index] = std::make_unique<slave_value_writer>(
                simulator,
                logDir_,
                config.decimationFactor,
                config.variables);
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
    for (const auto& entry : valueWriters_) {
        entry.second->observe(firstStep, startTime);
    }
}

void file_observer::step_complete(step_number /*lastStep*/, duration /*lastStepSize*/, time_point /*currentTime*/)
{
}

void file_observer::simulator_step_complete(simulator_index index, step_number lastStep, duration /*lastStepSize*/, time_point currentTime)
{
    if (recording_) {
        valueWriters_.at(index)->observe(lastStep, currentTime);
    }
}

boost::filesystem::path file_observer::get_log_path()
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
        recording_ = true;
    }
}

void file_observer::stop_recording()
{
    if (!recording_) {
        throw std::runtime_error("File observer has already stopeed recording");
    } else {
        for (const auto& entry : valueWriters_) {
            entry.second->stop_recording();
        }
        recording_ = false;
    }
}

cse::observable* find_simulator(
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
    for (const auto& childElement : ptree_.get_child("simulators")) {
        auto simulatorElement = childElement.second;

        auto modelName = get_attribute<std::string>(simulatorElement, "name");
        if (modelName == simulatorName) {
            simulator_logging_config config;

            config.decimationFactor = get_attribute<size_t>(simulatorElement, "decimationFactor", defaultDecimationFactor_);

            const auto& simulator = find_simulator(simulators_, modelName);

            if (simulatorElement.count("variable") == 0) {

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
                for (const auto& [variableElementName, variableElement] : simulatorElement) {
                    if (variableElementName == "variable") {
                        const auto name = get_attribute<std::string>(variableElement, "name");
                        const auto variableDescription =
                            find_variable(simulator->model_description(), name);

                        switch (variableDescription.type) {
                            case variable_type::real:
                            case variable_type::integer:
                            case variable_type::boolean:
                            case variable_type::string:
                                config.variables.push_back(variableDescription);
                                BOOST_LOG_SEV(log::logger(), log::info) << "Logging variable: " << modelName << ":" << name;
                                break;
                            default:
                                CSE_PANIC();
                        }
                    }
                }
            }
            return config;
        }
    }
    return simulator_logging_config();
}

file_observer::~file_observer() = default;


} // namespace cse
