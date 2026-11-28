#ifndef LIBCOSIM_FMI3_TEST_HELPERS_HPP
#define LIBCOSIM_FMI3_TEST_HELPERS_HPP

#include <cosim/exception.hpp>
#include <cosim/event_value.hpp>
#include <cosim/execution.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v2/fmu.hpp>
#include <cosim/fmi/v3/fmu.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/function/linear_transformation.hpp>
#include <cosim/function/vector_sum.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/slave_simulator.hpp>
#include <cosim/time.hpp>
#include <cosim/utility/filesystem.hpp>
#include <cosim/utility/zip.hpp>

#include "exact_array_function.hpp"
#include "mock_slave.hpp"
#include "warning_capture.hpp"

#include <boost/test/unit_test.hpp>


#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>


using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(fmi::fmi_version)
BOOST_TEST_DONT_PRINT_LOG_VALUE(step_result)
BOOST_TEST_DONT_PRINT_LOG_VALUE(clock_interval_variability)
BOOST_TEST_DONT_PRINT_LOG_VALUE(variable_initial)

namespace
{
inline auto error_is(errc expected)
{
    return [expected](const cosim::error& error) {
        return error.code() == make_error_code(expected);
    };
}

inline cosim::filesystem::path reference_fmu_directory()
{
    const auto* referenceDir = std::getenv("REFERENCE_FMU_DIR");
    BOOST_REQUIRE_MESSAGE(referenceDir, "REFERENCE_FMU_DIR is not set");
    return referenceDir;
}


inline cosim::filesystem::path array_fixture_directory()
{
    if (const auto* fixtureDir = std::getenv("FMI3_ARRAY_FIXTURE_DIR")) {
        return fixtureDir;
    }
    return {};
}

inline void require_fixture(const cosim::filesystem::path& path)
{
    BOOST_REQUIRE_MESSAGE(cosim::filesystem::is_regular_file(path), "Missing FMI 3 fixture: " << path);
}

inline cosim::filesystem::path fault_fixture(const char* filename)
{
    const auto* directory = std::getenv("FMI3_FAULT_FIXTURE_DIR");
    BOOST_REQUIRE_MESSAGE(directory, "FMI3_FAULT_FIXTURE_DIR is not set");
    const auto path = cosim::filesystem::path(directory) / filename;
    BOOST_REQUIRE_MESSAGE(cosim::filesystem::is_regular_file(path), "Missing fixture: " << path);
    return path;
}

inline std::shared_ptr<fmi::v3::fmu> shared_clock_fixture(
    const cosim::filesystem::path& path)
{
    // These fixtures do not inject Fatal statuses, which poison the FMU.
    static std::unordered_map<std::string, std::shared_ptr<fmi::v3::fmu>> fixtures;
    const auto key = path.string();
    const auto existing = fixtures.find(key);
    if (existing != fixtures.end()) {
        return existing->second;
    }
    const auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    fixtures.emplace(key, fmu);
    return fmu;
}


inline void rewrite_model_description(
    const cosim::filesystem::path& directory,
    const std::string& original,
    const std::string& replacement)
{
    const auto xmlPath = directory / "modelDescription.xml";
    std::ifstream input(xmlPath);
    BOOST_REQUIRE(input);
    auto xml = std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    input.close();
    xml.erase(std::remove(xml.begin(), xml.end(), '\r'), xml.end());
    const auto position = xml.find(original);
    BOOST_REQUIRE(position != std::string::npos);
    xml.replace(position, original.size(), replacement);
    std::ofstream output(xmlPath);
    BOOST_REQUIRE(output);
    output << xml;
    output.close();
    BOOST_REQUIRE(output);
}


inline std::unordered_map<std::string, value_reference> variable_references(
    const model_description& description)
{
    std::unordered_map<std::string, value_reference> references;
    for (const auto& variable : description.variables) {
        references.emplace(variable.name, variable.reference);
    }
    return references;
}


inline const variable_description* find_model_variable(
    const model_description& description,
    const std::string& name)
{
    const auto variable = std::find_if(
        description.variables.begin(),
        description.variables.end(),
        [&name](const variable_description& candidate) {
            return candidate.name == name;
        });
    return variable == description.variables.end() ? nullptr : &*variable;
}


inline const variable_type_definition* find_type_definition(
    const model_description& description,
    const std::string& name)
{
    const auto type = std::find_if(
        description.type_definitions.begin(),
        description.type_definitions.end(),
        [&name](const variable_type_definition& candidate) {
            return candidate.name == name;
        });
    return type == description.type_definitions.end() ? nullptr : &*type;
}


inline const unit_definition* find_unit_definition(
    const model_description& description,
    const std::string& name)
{
    const auto unit = std::find_if(
        description.unit_definitions.begin(),
        description.unit_definitions.end(),
        [&name](const unit_definition& candidate) {
            return candidate.name == name;
        });
    return unit == description.unit_definitions.end() ? nullptr : &*unit;
}

class event_coordination_slave : public mock_slave, public event_controller
{
public:
    static constexpr value_reference tunable_reference = 2;

    explicit event_coordination_slave(bool invert = false)
        : invert_(invert)
    {
    }

    cosim::model_description model_description() const override
    {
        auto model = mock_slave::model_description();
        for (auto& variable : model.variables) {
            if (variable.type == variable_type::real &&
                variable.reference == real_in_reference) {
                variable.variability = variable_variability::continuous;
            }
        }
        variable_description parameter{};
        parameter.name = "tunable";
        parameter.type = variable_type::real;
        parameter.reference = tunable_reference;
        parameter.causality = variable_causality::parameter;
        parameter.variability = variable_variability::tunable;
        parameter.start = 0.0;
        model.variables.push_back(std::move(parameter));
        return model;
    }

    event_controller* get_event_controller() noexcept override { return this; }
    bool is_in_event_mode() const noexcept override { return inEventMode_; }
    void enter_event_mode() override { inEventMode_ = true; ++eventEntries; }
    void enter_step_mode() override { inEventMode_ = false; }
    void start_simulation() override { running_ = true; }

    event_update_result update_event_state() override
    {
        ++updates;
        if (terminateOnUpdate) return {false, true, std::nullopt};
        if (alwaysUpdate) return {true, false, std::nullopt};
        int value = 0;
        const auto reference = integer_in_reference;
        mock_slave::get_integer_variables(
            gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
        output_ = invert_ ? 1 - value : value;
        return {};
    }

    void get_integer_variables(
        gsl::span<const value_reference> references,
        gsl::span<int> values) const override
    {
        for (std::size_t i = 0; i < references.size(); ++i) {
            if (references[i] == integer_out_reference) {
                values[i] = output_;
            } else {
                mock_slave::get_integer_variables(
                    gsl::make_span(&references[i], 1), gsl::make_span(&values[i], 1));
            }
        }
    }

    void set_integer_variables(
        gsl::span<const value_reference> references,
        gsl::span<const int> values) override
    {
        for (std::size_t i = 0; i < references.size(); ++i) {
            int current = 0;
            mock_slave::get_integer_variables(
                gsl::make_span(&references[i], 1), gsl::make_span(&current, 1));
            if (running_ && current != values[i] && !inEventMode_) {
                throw std::logic_error("Discrete input changed outside Event Mode");
            }
        }
        mock_slave::set_integer_variables(references, values);
    }

    void get_real_variables(
        gsl::span<const value_reference> references,
        gsl::span<double> values) const override
    {
        for (std::size_t i = 0; i < references.size(); ++i) {
            if (references[i] == tunable_reference) {
                values[i] = tunable_;
            } else {
                mock_slave::get_real_variables(
                    gsl::make_span(&references[i], 1), gsl::make_span(&values[i], 1));
            }
        }
    }

    void set_real_variables(
        gsl::span<const value_reference> references,
        gsl::span<const double> values) override
    {
        for (std::size_t i = 0; i < references.size(); ++i) {
            if (references[i] == tunable_reference) {
                if (running_ && tunable_ != values[i] &&
                    !(std::isnan(tunable_) && std::isnan(values[i])) && !inEventMode_) {
                    throw std::logic_error("Tunable parameter changed outside Event Mode");
                }
                tunable_ = values[i];
            } else {
                mock_slave::set_real_variables(
                    gsl::make_span(&references[i], 1), gsl::make_span(&values[i], 1));
            }
        }
    }

    step_result do_step(time_point current, duration delta) override
    {
        ++steps;
        return mock_slave::do_step(current, delta);
    }

    int eventEntries = 0;
    int updates = 0;
    int steps = 0;
    bool terminateOnUpdate = false;
    bool alwaysUpdate = false;

private:
    bool invert_ = false;
    bool running_ = false;
    bool inEventMode_ = false;
    int output_ = 0;
    double tunable_ = 0.0;
};

class shortening_event_slave : public event_coordination_slave
{
public:
    explicit shortening_event_slave(duration eventAfter)
        : eventAfter_(eventAfter)
    {
    }

    // The first discrete-state update schedules a time event shortly after
    // the current time, which shortens the step that triggered it.
    event_update_result update_event_state() override
    {
        auto result = event_coordination_slave::update_event_state();
        if (!scheduled_) {
            scheduled_ = true;
            next_ = now_ + eventAfter_;
        }
        return result;
    }

    event_handling_status get_event_handling_status() const noexcept override
    {
        if (next_ && *next_ > now_) return {false, next_};
        return {};
    }

    step_result do_step(time_point current, duration delta) override
    {
        now_ = current + delta;
        return event_coordination_slave::do_step(current, delta);
    }

private:
    duration eventAfter_;
    time_point now_ = to_time_point(0.0);
    std::optional<time_point> next_;
    bool scheduled_ = false;
};

inline std::vector<std::shared_ptr<algorithm>> event_algorithms(duration step, std::size_t limit = 100)
{
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15, limit};
    return {
        std::make_shared<fixed_step_algorithm>(fixed_step_algorithm_params{step, limit}, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
}
} // namespace

#endif
