#include "cse/observer/file_observer.hpp"

#include "cse/error.hpp"

#include <boost/date_time/local_time/local_time.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/xml_parser.hpp>

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
    slave_value_writer(observable* observable, boost::filesystem::path logPath, size_t limit, time_point currentTime)
        : observable_(observable)
        , limit_(limit)
    {
        boost::filesystem::create_directories(logPath.parent_path());

        fsw_.open(logPath, std::ios_base::out | std::ios_base::app);

        if (fsw_.fail()) {
            throw std::runtime_error("Failed to open file stream for logging");
        }

        // Create CSV header row
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
            fsw_.write((char*)&values[0], values.size() * sizeof(T));

            for (auto it = values.begin(); it != values.end(); ++it) {
                if (it != values.begin()) ss_ << ",";
                ss_ << *it;
            }
            ss_ << ",";
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
    size_t counter_ = 0;
    size_t limit_ = 10;
};

file_observer::file_observer(boost::filesystem::path& logDir, size_t limit)
    : logDir_(logDir)
    , limit_(limit)
{
}

file_observer::file_observer(boost::filesystem::path& configPath, boost::filesystem::path& logDir)
    : configPath_(configPath)
    , logDir_(logDir)
    , logFromConfig_(true)
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

    simulators_[index] = (cse::simulator*)simulator;
    if (logFromConfig_) parse_config();

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

void file_observer::step_complete(step_number /*lastStep*/, duration /*lastStepSize*/, time_point /*currentTime*/)
{
}

void file_observer::simulator_step_complete(simulator_index index, step_number lastStep, duration /*lastStepSize*/, time_point currentTime)
{
    valueWriters_.at(index)->observe(lastStep, currentTime);
}

boost::filesystem::path file_observer::get_log_path()
{
    return logPath_;
}

boost::filesystem::path file_observer::get_config_path()
{
    return configPath_;
}

template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key)
{
    return tree.get<T>("<xmlattr>." + key);
}

struct Signal
{
    std::string name;
    std::string type;
    std::string causality;
    cse::variable_index variable_index;
};

struct Model
{
    std::string name;
    std::vector<Signal> signals;
};

struct Slave
{
    std::string name;
    size_t rate = 100;
    std::vector<Model> models;
};

Slave slave_;
std::vector<Model> models_;
std::vector<Signal> signals_;

std::pair<cse::simulator_index, cse::simulator*> find_simulator(
    const std::unordered_map<simulator_index, simulator*>& simulators,
    const std::string& model)
{
    for (const auto& [idx, simulator] : simulators) {
        if (simulator->name() == model) {
            return std::make_pair(idx, simulator);
        }
    }
    throw std::invalid_argument("Can't find model with this name");
}

cse::variable_type find_variable_type(std::string& typestr)
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

cse::variable_causality find_causality(std::string& caus)
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

void file_observer::parse_config()
{
    std::cout << "Load config from " << configPath_.string() << std::endl;

    std::string path = "slave";

    boost::property_tree::ptree tmpTree;
    boost::property_tree::read_xml(configPath_.string(), ptree_,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);

    tmpTree = ptree_.get_child(path);

    slave_.name = get_attribute<std::string>(tmpTree, "name");
    slave_.rate = get_attribute<size_t>(tmpTree, "rate");

    std::cout << slave_.name << " " << slave_.rate << std::endl;

    const auto& [index, simulator] = find_simulator(simulators_, slave_.name);

    for (const auto& [module_block_name, module] : ptree_.get_child(path + ".models")) {
        auto m = slave_.models.emplace_back();

        if (module_block_name == "model") {
            m.name = get_attribute<std::string>(module, "name");
        }

        for (const auto& [signal_block_name, signal] : module) {
            auto s = m.signals.emplace_back();

            if (signal_block_name == "signal") {
                s.name = get_attribute<std::string>(signal, "name");
                s.type = get_attribute<std::string>(signal, "type");
                s.causality = get_attribute<std::string>(signal, "causality");

                cse::variable_type type = find_variable_type(s.type);
                cse::variable_causality causality = find_causality(s.causality);
                s.variable_index = find_variable_index(simulator->model_description().variables, s.name, type, causality);

                std::cout << s.name << " parsed with index " << s.variable_index << " from module " << m.name << std::endl;
            }
        }
    }
}

file_observer::~file_observer()
{
}

} // namespace cse
