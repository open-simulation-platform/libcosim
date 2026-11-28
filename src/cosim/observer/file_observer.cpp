/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/file_observer.hpp"

#include "cosim/execution.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/observer/observer.hpp"
#include "cosim/time.hpp"

#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cosim
{

namespace
{
std::string format_time(boost::posix_time::ptime now)
{
    const std::locale loc(std::wcout.getloc(), new boost::posix_time::time_facet("%Y%m%d_%H%M%S_%f")); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

    std::ostringstream stream;
    stream.imbue(loc);
    stream << now;
    return stream.str();
}

void clear_file_contents_if_exists(const cosim::filesystem::path& filePath, std::ofstream& fsw)
{
    if (cosim::filesystem::exists(filePath)) {
        // clear file contents
        fsw.open(filePath, std::ios_base::out | std::ios_base::trunc); // NOLINT(bugprone-signed-bitwise): Standard stream flags are combined with bitwise OR.
        fsw.close();
    }
}

std::string escape_string_element(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const auto character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(character); break;
        }
    }
    result.push_back('"');
    return result;
}

std::string escape_csv_field(std::string_view value)
{
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string(value);
    }
    std::string result{"\""};
    for (const auto character : value) {
        if (character == '"') {
            result += "\"\"";
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

template<typename T>
void write_element(
    std::ostream& stream,
    const T& value,
    int precision,
    bool quoteString)
{
    if constexpr (std::is_same_v<T, std::string>) {
        stream << (quoteString ? escape_string_element(value) : value);
    } else if constexpr (std::is_same_v<T, enumeration_value>) {
        stream << value.value;
    } else if constexpr (
        std::is_same_v<T, std::int8_t> ||
        std::is_same_v<T, std::uint8_t>) {
        stream << static_cast<int>(value);
    } else if constexpr (std::is_floating_point_v<T>) {
        if (precision >= 0) {
            stream << std::fixed << std::setprecision(precision);
        }
        stream << value;
    } else {
        stream << value;
    }
}

std::string format_variable_value(const variable_value& value, int precision)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    std::visit(
        [&](const auto& values) {
            if (!value.is_scalar()) {
                stream << "[";
            }
            bool first = true;
            for (const auto& element : values.values) {
                if (!first) {
                    stream << ",";
                }
                first = false;
                write_element(
                    stream,
                    element,
                    precision,
                    !value.is_scalar());
            }
            if (!value.is_scalar()) {
                stream << "]";
            }
        },
        value.values());
    return stream.str();
}

std::string format_scalar_value(const scalar_value& value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << value;
    return stream.str();
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

    slave_value_writer(
        observable* observable,
        cosim::filesystem::path& logDir,
        size_t decimationFactor,
        const std::vector<variable_description>& variables,
        bool timeStampedFileNames = true,
        const int precision = -1)
        : observable_(observable)
        , logDir_(logDir)
        , decimationFactor_(decimationFactor)
        , timeStampedFileNames_(timeStampedFileNames)
        , precision_(precision)
    {
        initialize_config(variables);
    }

    slave_value_writer(const slave_value_writer&) = delete;
    slave_value_writer& operator=(const slave_value_writer&) = delete;
    slave_value_writer(slave_value_writer&&) = delete;
    slave_value_writer& operator=(slave_value_writer&&) = delete;

    void observe(step_number timeStep, time_point currentTime)
    {
        std::scoped_lock const lock(mutex_);
        if (recording_) {
            if (!fsw_.is_open()) {
                const auto dataFileName = create_log_file();
                create_metadata_file(dataFileName);
            }
            if (timeStep % decimationFactor_ == 0) {
                auto& samples = valueSamples_[timeStep];
                samples.reserve(variables_.size());
                for (const auto& variable : variables_) {
                    samples.push_back(observable_->get_variable_value(
                        variable.type, variable.reference));
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
        std::scoped_lock const lock(mutex_);
        if (fsw_.is_open()) {
            fsw_.close();
        }
        recording_ = false;
        valueSamples_.clear();
        timeSamples_.clear();
    }

    ~slave_value_writer() // NOLINT(bugprone-exception-escape): Preserve the existing noexcept destructor behavior.
    {
        if (fsw_.is_open()) {
            fsw_.close();
        }
    }

private:
    static constexpr int default_key_width = 14;
    int keyWidth_ = default_key_width;

    void initialize_variable(const variable_description& variable)
    {
        if (variable.type == variable_type::clock) {
            throw std::invalid_argument(
                "Clock variables cannot be logged as data values");
        }
        observable_->expose_for_getting(variable.type, variable.reference);
        variables_.push_back(variable);
    }

    /** Default constructor initialization, all variables - except those with causality local - are logged. */
    void initialize_default()
    {
        if (!timeStampedFileNames_) {
            const auto filePath = logDir_ / observable_->name().append(".csv");
            clear_file_contents_if_exists(filePath, fsw_);
        }

        for (const auto& variable : observable_->model_description().variables) {
            if (variable.causality != variable_causality::local &&
                variable.type != variable_type::clock) {
                initialize_variable(variable);
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

        for (const auto& variable : variables) {
            initialize_variable(variable);
        }
    }

    std::string create_log_file()
    {
        std::string filename;
        std::stringstream stream;
        std::string time_str;

        if (!timeStampedFileNames_) {
            filename = observable_->name().append(".csv");
        } else {
            time_str = format_time(boost::posix_time::microsec_clock::local_time());
            filename = observable_->name().append("_").append(time_str).append(".csv");
        }

        const auto filePath = logDir_ / filename;
        cosim::filesystem::create_directories(logDir_);
        fsw_.open(filePath, std::ios_base::out | std::ios_base::app); // NOLINT(bugprone-signed-bitwise): Standard stream flags are combined with bitwise OR.

        if (fsw_.fail()) {
            std::stringstream error;
            error << "Failed to open log file stream: " << filePath.string();
            throw std::runtime_error(error.str());
        }

        stream << "Time,StepCount";

        // Add variable names
        for (const auto& variable : variables_) {
            stream << "," << escape_csv_field(variable.name);
        }

        stream << stream.widen('\n');

        if (fsw_.is_open()) {
            fsw_ << stream.rdbuf();
        }

        return time_str;
    }

    void write_variable_metadata(std::stringstream& stream, std::vector<variable_description>& variables) const
    {
        for (const auto& variable : variables) {
            stream << "  - " << std::setw(keyWidth_) << "name:" << variable.name << stream.widen('\n')
                   << "    " << std::setw(keyWidth_) << "reference:" << variable.reference << stream.widen('\n')
                   << "    " << std::setw(keyWidth_) << "type:" << variable.type << stream.widen('\n')
                   << "    " << std::setw(keyWidth_) << "causality:" << variable.causality << stream.widen('\n')
                   << "    " << std::setw(keyWidth_) << "variability:" << variable.variability << stream.widen('\n');

            if (variable.start.has_value()) {
                stream << "    " << std::setw(keyWidth_) << "start value:"
                       << format_scalar_value(*variable.start) << stream.widen('\n');
            }
            if (variable.array_start.has_value()) {
                stream << "    " << std::setw(keyWidth_) << "start value:"
                       << format_variable_value(*variable.array_start, -1) << stream.widen('\n');
            }
        }
    }

    void create_metadata_file(const std::string& time_str)
    {
        std::ofstream metadata_fw;
        std::string filename;
        std::stringstream stream;

        if (!timeStampedFileNames_) {
            filename = observable_->name().append("_metadata.yaml");
        } else {
            filename = observable_->name().append("_").append(time_str).append("_metadata.yaml");
        }

        const auto filePath = logDir_ / filename;
        metadata_fw.open(filePath, std::ios_base::out | std::ios_base::app); // NOLINT(bugprone-signed-bitwise): Standard stream flags are combined with bitwise OR.

        if (metadata_fw.fail()) {
            std::stringstream error;
            error << "Failed to open log metadata file stream: " << filePath.string();
            throw std::runtime_error(error.str());
        }

        const auto description = observable_->model_description();

        stream << std::left
               << std::setw(keyWidth_) << "name:" << description.name << stream.widen('\n')
               << std::setw(keyWidth_) << "uuid:" << description.uuid << stream.widen('\n')
               << std::setw(keyWidth_) << "instantiation_token:"
               << description.instantiation_token << stream.widen('\n')
               << std::setw(keyWidth_) << "description:" << description.description << stream.widen('\n')
               << std::setw(keyWidth_) << "author:" << description.author << stream.widen('\n')
               << std::setw(keyWidth_) << "version:" << description.version << stream.widen('\n');

        stream << "variables:" << stream.widen('\n');

        write_variable_metadata(stream, variables_);

        metadata_fw << stream.rdbuf();
        metadata_fw.close();
        if (metadata_fw.fail()) {
            std::stringstream error;
            error << "Failed to write log metadata file: " << filePath.string();
            throw std::runtime_error(error.str());
        }
    }

    void persist()
    {
        std::stringstream stream;
        const auto defaultPrecision = stream.precision();

        if (fsw_.is_open()) {

            for (const auto& [stepCount, times] : timeSamples_) {
                if (precision_ > -1) {
                    stream.precision(defaultPrecision);
                    stream << times << "," << stepCount;
                    stream.precision(precision_);
                    stream << std::fixed;
                } else {
                    stream << times << "," << stepCount;
                }

                if (const auto samples = valueSamples_.find(stepCount);
                    samples != valueSamples_.end()) {
                    for (const auto& value : samples->second) {
                        stream << ","
                               << escape_csv_field(
                                      format_variable_value(value, precision_));
                    }
                }

                stream << stream.widen('\n');
            }

            fsw_ << stream.rdbuf();
        }

        valueSamples_.clear();
        timeSamples_.clear();
    }

    std::map<step_number, std::vector<variable_value>> valueSamples_;
    std::map<step_number, double> timeSamples_;
    std::vector<variable_description> variables_;
    observable* observable_;
    cosim::filesystem::path logDir_;
    size_t decimationFactor_ = 1;
    std::ofstream fsw_;
    std::atomic<bool> recording_ = true;
    std::mutex mutex_;
    bool timeStampedFileNames_ = true;
    int precision_ = -1;
};

file_observer::file_observer(const cosim::filesystem::path& logDir, std::optional<file_observer_config> config)
    : config_(std::move(config))
    , logDir_(cosim::filesystem::absolute(logDir))
{
}

file_observer::file_observer(const filesystem::path& logDir, const filesystem::path& configPath)
    : file_observer(logDir, file_observer_config::parse(configPath))
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
        const auto& observerConfig = *config_;
        if (observerConfig.should_log_simulator(simulator->name())) {
            const auto config = parse_config(simulator->name());

            valueWriters_[index] = std::make_unique<slave_value_writer>(
                simulator,
                logDir_,
                config.decimationFactor,
                config.variables,
                config.timeStampedFileNames,
                observerConfig.precision_);
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

void file_observer::simulator_step_complete(simulator_index index, step_number lastStep, duration /*lastStepSize*/, time_point currentTime) // NOLINT(bugprone-easily-swappable-parameters): Preserve the observer interface.
{
    if (auto const writer = valueWriters_.find(index); writer != valueWriters_.end() && recording_) {
        writer->second->observe(lastStep, currentTime);
    }
}

void file_observer::state_restored(step_number currentStep, time_point currentTime)
{
    if (recording_) {
        for (const auto& entry : valueWriters_) {
            entry.second->observe(currentStep, currentTime);
        }
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

namespace
{
void append_loggable_variable(
    const observable* simulator,
    const std::string& name,
    std::vector<variable_description>& variables)
{
    const auto variableDescription =
        find_variable(simulator->model_description(), name);
    if (!variableDescription) {
        throw std::runtime_error("Can't find variable descriptor with name " + name + " for model with name " + simulator->model_description().name);
    }
    if (variableDescription->type == variable_type::clock) {
        throw std::invalid_argument(
            "Clock variables cannot be logged as data values");
    }
    variables.push_back(*variableDescription);
}
} // namespace

file_observer::simulator_logging_config file_observer::parse_config(const std::string& simulatorName)
{
    const auto& observerConfig = config_.value(); // NOLINT(bugprone-unchecked-optional-access): simulator_added checks config_ before calling.
    const bool timeStampedFileNames = observerConfig.timeStampedFileNames_;
    for (const auto& [modelName, variables] : observerConfig.variablesToLog_) {
        if (modelName != simulatorName) {
            continue;
        }
        simulator_logging_config config;
        config.timeStampedFileNames = timeStampedFileNames;
        config.decimationFactor = variables.first;

        const auto& simulator = find_simulator(simulators_, modelName);
        if (variables.second.empty()) {
            for (const auto& variable : simulator->model_description().variables) {
                if (variable.type != variable_type::clock) {
                    config.variables.push_back(variable);
                }
            }
        } else {
            for (const auto& name : variables.second) {
                append_loggable_variable(simulator, name, config.variables);
                BOOST_LOG_SEV(log::logger(), log::info)
                    << "Logging variable: " << modelName << ":" << name;
            }
        }
        return config;
    }

    return {};
}

file_observer::~file_observer() = default;


file_observer_config file_observer_config::parse(const filesystem::path& configPath)
{
    boost::property_tree::ptree ptree;
    boost::property_tree::read_xml(configPath.string(), ptree,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace); // NOLINT(bugprone-signed-bitwise,misc-include-cleaner): Combine XML parser flags.

    const auto& simulators = ptree.get_child("simulators");

    file_observer_config config;
    if (const auto timestamps = get_optional_attribute<bool>(simulators, "timestampedFilenames")) {
        config.set_timestamped_filenames(*timestamps);
    }
    if (const auto precision = get_optional_attribute<int>(simulators, "floatingPointPrecision")) {
        config.fixed_precision(*precision);
    }
    for (const auto& simulator : simulators) {
        if (simulator.first == "simulator") {
            const auto modelName = get_attribute<std::string>(simulator.second, "name");
            const auto decimationFactor = get_optional_attribute<size_t>(simulator.second, "decimationFactor");
            std::vector<std::string> variableNames;
            for (const auto& variable : simulator.second) {
                if (variable.first == "variable") {
                    const auto variableName = get_attribute<std::string>(variable.second, "name");
                    variableNames.emplace_back(variableName);
                }
            }
            config.log_simulator_variables(modelName, variableNames, decimationFactor);
        }
    }
    return config;
}

} // namespace cosim
