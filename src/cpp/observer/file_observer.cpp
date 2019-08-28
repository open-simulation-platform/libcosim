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
#include <mutex>
#include <sstream>
#include <vector>

namespace cse
{

class file_observer::slave_value_writer
{
public:
    slave_value_writer(observable* observable, boost::filesystem::path& logPath, time_point currentTime)
        : observable_(observable)
        , logPath_(logPath)
    {
        initialize_default(currentTime);
    }

    slave_value_writer(observable* observable, boost::filesystem::path& logPath, size_t decimationFactor, time_point currentTime,
        std::vector<variable_description>& realVars, std::vector<variable_description>& intVars,
        std::vector<variable_description>& boolVars, std::vector<variable_description>& strVars)
        : observable_(observable)
        , logPath_(logPath)
        , decimationFactor_(decimationFactor)
        , loggableRealVariables_(realVars)
        , loggableIntVariables_(intVars)
        , loggableBoolVariables_(boolVars)
        , loggableStringVariables_(strVars)
    {
        initialize_config(currentTime);
    }

    void observe(step_number timeStep, time_point currentTime)
    {
        if (++counter_ % decimationFactor_ == 0) {

            if (!realReferences_.empty()) realSamples_[timeStep].reserve(realReferences_.size());
            if (!intReferences_.empty()) intSamples_[timeStep].reserve(intReferences_.size());
            if (!boolReferences_.empty()) boolSamples_[timeStep].reserve(boolReferences_.size());
            if (!stringReferences_.empty()) stringSamples_[timeStep].reserve(stringReferences_.size());

            for (const auto idx : realReferences_) {
                realSamples_[timeStep].push_back(observable_->get_real(idx));
            }
            for (const auto idx : intReferences_) {
                intSamples_[timeStep].push_back(observable_->get_integer(idx));
            }
            for (const auto idx : boolReferences_) {
                boolSamples_[timeStep].push_back(observable_->get_boolean(idx));
            }
            for (const auto idx : stringReferences_) {
                stringSamples_[timeStep].push_back(observable_->get_string(idx));
            }
            timeSamples_[timeStep] = to_double_time_point(currentTime);

            if (counter_ >= decimationFactor_) {
                persist();
                counter_ = 0;
            }
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
        for (auto it = values.begin(); it != values.end(); ++it) {
            ss_ << "," << *it;
        }
    }

    /** Default constructor initialization, all variables are logged. */
    void initialize_default(time_point currentTime)
    {
        for (const auto& vd : observable_->model_description().variables) {
            if (vd.causality != variable_causality::local) {

                observable_->expose_for_getting(vd.type, vd.reference);

                if (vd.type == variable_type::real) {
                    realReferences_.push_back(vd.reference);
                }
                if (vd.type == variable_type::integer) {
                    intReferences_.push_back(vd.reference);
                }
                if (vd.type == variable_type::boolean) {
                    boolReferences_.push_back(vd.reference);
                }
                if (vd.type == variable_type::string) {
                    stringReferences_.push_back(vd.reference);
                }

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
        }

        write_header();
        observe(0, currentTime);
    }

    /** External config initialization, only configured variables are logged. */
    void initialize_config(time_point currentTime)
    {
        for (const auto& variable : loggableRealVariables_) {
            realReferences_.push_back(variable.reference);
            observable_->expose_for_getting(variable_type::real, variable.reference);
            realVars_.push_back(variable);
        }

        for (const auto& variable : loggableIntVariables_) {
            intReferences_.push_back(variable.reference);
            observable_->expose_for_getting(variable_type::integer, variable.reference);
            intVars_.push_back(variable);
        }

        for (const auto& variable : loggableBoolVariables_) {
            boolReferences_.push_back(variable.reference);
            observable_->expose_for_getting(variable_type::boolean, variable.reference);
            boolVars_.push_back(variable);
        }

        for (const auto& variable : loggableStringVariables_) {
            stringReferences_.push_back(variable.reference);
            observable_->expose_for_getting(variable_type::string, variable.reference);
            stringVars_.push_back(variable);
        }

        write_header();
        observe(0, currentTime);
    }

    void write_header()
    {
        boost::filesystem::create_directories(logPath_.parent_path());
        fsw_.open(logPath_, std::ios_base::out | std::ios_base::app);

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
    std::vector<value_reference> realReferences_;
    std::vector<value_reference> intReferences_;
    std::vector<value_reference> boolReferences_;
    std::vector<value_reference> stringReferences_;
    std::map<step_number, double> timeSamples_;
    std::vector<variable_description> realVars_;
    std::vector<variable_description> intVars_;
    std::vector<variable_description> boolVars_;
    std::vector<variable_description> stringVars_;
    observable* observable_;
    boost::filesystem::path logPath_;
    size_t decimationFactor_ = 1;
    std::vector<variable_description> loggableRealVariables_;
    std::vector<variable_description> loggableIntVariables_;
    std::vector<variable_description> loggableBoolVariables_;
    std::vector<variable_description> loggableStringVariables_;
    boost::filesystem::ofstream fsw_;
    std::stringstream ss_;
    size_t counter_ = 0;
};

file_observer::file_observer(const boost::filesystem::path& logDir)
    : logDir_(logDir)
{
}

file_observer::file_observer(const boost::filesystem::path& logDir, const boost::filesystem::path& configPath)
    : configPath_(configPath)
    , logDir_(logDir)
    , logFromConfig_(true)
{
    boost::property_tree::read_xml(configPath_.string(), ptree_,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);
}

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

void file_observer::simulator_added(simulator_index index, observable* simulator, time_point currentTime)
{
    auto time_str = format_time(boost::posix_time::second_clock::local_time());
    auto name = simulator->name().append("_").append(std::to_string(index)).append("_");
    auto extension = time_str.append(".csv");
    auto filename = name.append(extension);

    logPath_ = logDir_ / filename;
    simulators_[index] = simulator;

    if (logFromConfig_) {
        // Read all configured model names from the XML. If simulator name is not in the list, terminate.
        std::vector<std::string> modelNames;
        for (const auto& [model_key, model] : ptree_.get_child("simulators")) {
            (void)model_key; // Ugly GCC 7.3 adaptation

            modelNames.push_back(get_attribute<std::string>(model, "name"));
        }
        if (std::find(modelNames.begin(), modelNames.end(), simulator->name()) != modelNames.end()) {
            parse_config(simulator->name());

            valueWriters_[index] = std::make_unique<slave_value_writer>(simulator, logPath_, decimationFactor_, currentTime,
                loggableRealVariables_, loggableIntVariables_, loggableBoolVariables_, loggableStringVariables_);

            loggableRealVariables_.clear();
            loggableIntVariables_.clear();
            loggableBoolVariables_.clear();
            loggableStringVariables_.clear();
        } else
            return;

    } else {
        valueWriters_[index] = std::make_unique<slave_value_writer>(simulator, logPath_, currentTime);
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

void file_observer::step_complete(step_number /*lastStep*/, duration /*lastStepSize*/, time_point /*currentTime*/)
{
}

void file_observer::simulator_step_complete(simulator_index index, step_number lastStep, duration /*lastStepSize*/, time_point currentTime)
{
    if (valueWriters_.find(index) != valueWriters_.end()) {
        valueWriters_.at(index)->observe(lastStep, currentTime);
    }
}

boost::filesystem::path file_observer::get_log_path()
{
    return logPath_;
}

std::pair<cse::simulator_index, cse::observable*> find_simulator(
    const std::unordered_map<simulator_index, observable*>& simulators,
    const std::string& simulatorName)
{
    for (const auto& [idx, simulator] : simulators) {
        if (simulator->name() == simulatorName) {
            return std::make_pair(idx, simulator);
        }
    }
    throw std::invalid_argument("Can't find simulator with name: " + simulatorName);
}

void file_observer::parse_config(const std::string& simulatorName)
{
    for (const auto& [simulatorElementName, simulatorElement] : ptree_.get_child("simulators")) {
        (void)simulatorElementName; // Ugly GCC 7.3 adaptation

        auto modelName = get_attribute<std::string>(simulatorElement, "name");
        if (modelName != simulatorName) continue;

        decimationFactor_ = get_attribute(simulatorElement, "decimationFactor", defaultDecimationFactor_);

        const auto& [simulatorIndex, simulator] = find_simulator(simulators_, modelName);
        (void)simulatorIndex; // Ugly GCC 7.3 adaptation

        if (simulatorElement.count("variable") == 0) {
            loggableRealVariables_ = find_variables_of_type(simulator->model_description(), variable_type::real);
            loggableIntVariables_ = find_variables_of_type(simulator->model_description(), variable_type::integer);
            loggableBoolVariables_ = find_variables_of_type(simulator->model_description(), variable_type::boolean);
            loggableStringVariables_ = find_variables_of_type(simulator->model_description(), variable_type::string);

            continue;
        }

        for (const auto& [variableElementName, variableElement] : simulatorElement) {
            if (variableElementName == "variable") {
                const auto name = get_attribute<std::string>(variableElement, "name");
                const auto variableDescription =
                    find_variable(simulator->model_description(), name);

                BOOST_LOG_SEV(log::logger(), log::info) << "Logging variable: " << modelName << ":" << name;

                switch (variableDescription.type) {
                    case variable_type::real:
                        loggableRealVariables_.push_back(variableDescription);
                        break;
                    case variable_type::integer:
                        loggableIntVariables_.push_back(variableDescription);
                        break;
                    case variable_type::boolean:
                        loggableBoolVariables_.push_back(variableDescription);
                        break;
                    case variable_type::string:
                        loggableStringVariables_.push_back(variableDescription);
                        break;
                    case variable_type::enumeration:
                        CSE_PANIC();
                }
            }
        }
    }
}

file_observer::~file_observer() = default;


} // namespace cse
