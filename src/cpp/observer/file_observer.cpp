#include "cse/observer/file_observer.hpp"

#include "cse/error.hpp"

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

    slave_value_writer(observable* observable, boost::filesystem::path& logPath, int decimationFactor, time_point currentTime,
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
            realSamples_[timeStep].reserve(realIndexes_.size());
            intSamples_[timeStep].reserve(intIndexes_.size());

            for (const auto idx : realIndexes_) {
                realSamples_[timeStep].push_back(observable_->get_real(idx));
            }
            for (const auto idx : intIndexes_) {
                intSamples_[timeStep].push_back(observable_->get_integer(idx));
            }
            for (const auto idx : boolIndexes_) {
                boolSamples_[timeStep].push_back(observable_->get_boolean(idx));
            }
            for (const auto idx : stringIndexes_) {
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
        if (fsw_.is_open()) {
            for (auto it = values.begin(); it != values.end(); ++it) {
                if (it != values.begin()) ss_ << ",";
                ss_ << *it;
            }
            ss_ << ",";
        }
    }

    /** Default constructor initialization, all variables are logged. */
    void initialize_default(time_point currentTime)
    {
        for (const auto& vd : observable_->model_description().variables) {
            if (vd.causality != variable_causality::local) {

                observable_->expose_for_getting(vd.type, vd.index);

                if (vd.type == variable_type::real) {
                    realIndexes_.push_back(vd.index);
                }
                if (vd.type == variable_type::integer) {
                    intIndexes_.push_back(vd.index);
                }
                if (vd.type == variable_type::boolean) {
                    boolIndexes_.push_back(vd.index);
                }
                if (vd.type == variable_type::string) {
                    stringIndexes_.push_back(vd.index);
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
            if (variable.causality != variable_causality::local) {
                realIndexes_.push_back(variable.index);
                observable_->expose_for_getting(variable_type::real, variable.index);
                realVars_.push_back(variable);
            }
        }

        for (const auto& variable : loggableIntVariables_) {
            if (variable.causality != variable_causality::local) {
                intIndexes_.push_back(variable.index);
                observable_->expose_for_getting(variable_type::integer, variable.index);
                intVars_.push_back(variable);
            }
        }

        for (const auto& variable : loggableBoolVariables_) {
            if (variable.causality != variable_causality::local) {
                boolIndexes_.push_back(variable.index);
                observable_->expose_for_getting(variable_type::boolean, variable.index);
                boolVars_.push_back(variable);
            }
        }

        for (const auto& variable : loggableStringVariables_) {
            if (variable.causality != variable_causality::local) {
                stringIndexes_.push_back(variable.index);
                observable_->expose_for_getting(variable_type::string, variable.index);
                stringVars_.push_back(variable);
            }
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

        ss_ << "Time,StepCount,";

        for (const auto& vd : realVars_) {
            ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
        }
        for (const auto& vd : intVars_) {
            ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
        }
        for (const auto& vd : boolVars_) {
            ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
        }
        for (const auto& vd : stringVars_) {
            ss_ << vd.name << " [" << vd.index << " " << vd.type << " " << vd.causality << "],";
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

        for (const auto& [stepCount, values] : realSamples_) {
            ss_ << timeSamples_[stepCount] << "," << stepCount << ",";

            write<double>(values);
            write<int>(intSamples_[stepCount]);
            write<bool>(boolSamples_[stepCount]);
            write<std::string_view>(stringSamples_[stepCount]);

            ss_ << std::endl;
        }

        if (fsw_.is_open()) {
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
    std::vector<variable_index> realIndexes_;
    std::vector<variable_index> intIndexes_;
    std::vector<variable_index> boolIndexes_;
    std::vector<variable_index> stringIndexes_;
    std::map<step_number, double> timeSamples_;
    std::vector<variable_description> realVars_;
    std::vector<variable_description> intVars_;
    std::vector<variable_description> boolVars_;
    std::vector<variable_description> stringVars_;
    observable* observable_;
    boost::filesystem::path logPath_;
    int decimationFactor_ = 1;
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

file_observer::file_observer(const boost::filesystem::path& configPath, const boost::filesystem::path& logDir)
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
} // namespace

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

void file_observer::simulator_added(simulator_index index, observable* obs, time_point currentTime)
{
    auto time_str = format_time(boost::posix_time::second_clock::local_time());
    auto simulator = dynamic_cast<cse::simulator*>(obs);

    auto name = simulator->name().append("_").append(std::to_string(index)).append("_");
    auto extension = time_str.append(".csv");
    auto filename = name.append(extension);

    logPath_ = logDir_ / filename;

    if (simulator) {
        simulators_[index] = simulator;
    }

    if (logFromConfig_) {
        // Read all configured model names from the XML. If simulator name is not in the list, terminate.
        std::vector<std::string> modelNames;
        for (const auto& [model_key, model] : ptree_.get_child("models")) {
            (void)model_key; // Ugly GCC 7.3 adaptation

            modelNames.push_back(get_attribute<std::string>(model, "name"));
        }
        if (std::find(modelNames.begin(), modelNames.end(), simulator->name()) != modelNames.end()) {
            parse_config(simulator->name());

            valueWriters_[index] = std::make_unique<slave_value_writer>(obs, logPath_, decimationFactor_, currentTime,
                loggableRealVariables_, loggableIntVariables_, loggableBoolVariables_, loggableStringVariables_);

            loggableRealVariables_.clear();
            loggableIntVariables_.clear();
            loggableBoolVariables_.clear();
            loggableStringVariables_.clear();
        } else
            return;

    } else {
        valueWriters_[index] = std::make_unique<slave_value_writer>(obs, logPath_, currentTime);
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

std::pair<cse::simulator_index, cse::simulator*> find_simulator(
    const std::unordered_map<simulator_index, simulator*>& simulators,
    const std::string& model)
{
    for (const auto& [idx, simulator] : simulators) {
        if (simulator->name() == model) {
            return std::make_pair(idx, simulator);
        }
    }
    throw std::invalid_argument("Can't find model: " + model);
}

cse::variable_type find_type(const std::string& typestr)
{
    if (typestr == "real") {
        return variable_type::real;
    } else if (typestr == "integer") {
        return variable_type::integer;
    } else if (typestr == "boolean") {
        return variable_type::boolean;
    } else if (typestr == "string") {
        return variable_type::string;
    }
    throw std::invalid_argument("Can't process unknown variable type");
}

cse::variable_causality find_causality(const std::string& caus)
{
    if (caus == "output") {
        return variable_causality::output;
    } else if (caus == "input") {
        return variable_causality::input;
    } else if (caus == "parameter") {
        return variable_causality::parameter;
    } else if (caus == "calculatedParameter") {
        return variable_causality::calculated_parameter;
    } else if (caus == "local") {
        return variable_causality::local;
    }
    throw std::invalid_argument("Can't process unknown variable type");
}

cse::variable_index find_variable_index(
    const std::vector<variable_description>& variables,
    const std::string& name,
    const cse::variable_type type,
    const cse::variable_causality causality)
{
    for (const auto& vd : variables) {
        if ((vd.name == name) && (vd.type == type) && (vd.causality == causality)) {
            return vd.index;
        }
    }
    throw std::invalid_argument("Can't find variable index");
}

void file_observer::parse_config(const std::string& simulatorName)
{
    for (const auto& [model_key, model] : ptree_.get_child("models")) {
        (void)model_key; // Ugly GCC 7.3 adaptation

        auto model_name = get_attribute<std::string>(model, "name");
        if (model_name != simulatorName) continue;

        decimationFactor_ = get_attribute(model, "decimationFactor", defaultDecimationFactor_);

        const auto& [sim_index, simulator] = find_simulator(simulators_, model_name);
        (void)sim_index; // Ugly GCC 7.3 adaptation

        if (model.count("signal") == 0) {
            loggableRealVariables_ = simulator->model_description().find_variables_of_type(variable_type::real);
            loggableIntVariables_ = simulator->model_description().find_variables_of_type(variable_type::integer);
            loggableBoolVariables_ = simulator->model_description().find_variables_of_type(variable_type::boolean);
            loggableStringVariables_ = simulator->model_description().find_variables_of_type(variable_type::string);

            continue;
        }

        for (const auto& [signal_block_name, signal] : model) {
            if (signal_block_name == "signal") {

                auto name = get_attribute<std::string>(signal, "name");
                auto type = get_attribute<std::string>(signal, "type");
                auto causality = get_attribute<std::string>(signal, "causality");

                auto variable = simulator->model_description().find_variable(
                    name, find_type(type), find_causality(causality));

                switch (find_type(type)) {
                    case variable_type::real:
                        loggableRealVariables_.push_back(variable);
                        break;
                    case variable_type::integer:
                        loggableIntVariables_.push_back(variable);
                        break;
                    case variable_type::boolean:
                        loggableBoolVariables_.push_back(variable);
                        break;
                    case variable_type::string:
                        loggableStringVariables_.push_back(variable);
                        break;
                }
            }
        }
    }
}

file_observer::~file_observer() = default;


} // namespace cse
