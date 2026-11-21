/**
 *  \file
 *  \brief  Mock slave classes for use in tests.
 */
#ifndef COSIM_TEST_MOCK_SLAVE_HPP
#define COSIM_TEST_MOCK_SLAVE_HPP

#include <cosim/slave.hpp>

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>


/**
 *  A simple slave implementation for testing purposes.
 *
 *  The slave has one input and one output of each of the four data types.
 *  When `do_step()` is called, it performs a user-defined operation on
 *  each of the inputs and places the results in the outputs. The default
 *  is just the identity operator.
 */
class mock_slave : public cosim::slave
{
public:
    constexpr static cosim::value_reference real_out_reference = 0;
    constexpr static cosim::value_reference real_in_reference = 1;
    constexpr static cosim::value_reference integer_out_reference = 0;
    constexpr static cosim::value_reference integer_in_reference = 1;
    constexpr static cosim::value_reference boolean_out_reference = 0;
    constexpr static cosim::value_reference boolean_in_reference = 1;
    constexpr static cosim::value_reference string_out_reference = 0;
    constexpr static cosim::value_reference string_in_reference = 1;

    // Constructor that takes time-dependent functions
    explicit mock_slave(
        std::function<double(cosim::time_point, cosim::duration, double)> realOp = nullptr,
        std::function<int(cosim::time_point, cosim::duration, int)> intOp = nullptr,
        std::function<bool(cosim::time_point, cosim::duration, bool)> boolOp = nullptr,
        std::function<std::string(cosim::time_point, cosim::duration, std::string_view)> stringOp = nullptr,
        std::function<void(cosim::time_point, cosim::duration)> stepAction = nullptr)
        : realOp_(std::move(realOp))
        , intOp_(std::move(intOp))
        , boolOp_(std::move(boolOp))
        , stringOp_(std::move(stringOp))
        , stepAction_(std::move(stepAction))
    {
    }

    // Constructor that takes time-independent functions (and wraps them in
    // time-dependent ones).
    explicit mock_slave(
        std::function<double(double)> realOp,
        std::function<int(int)> intOp = nullptr,
        std::function<bool(bool)> boolOp = nullptr,
        std::function<std::string(std::string_view)> stringOp = nullptr,
        std::function<void()> stepAction = nullptr)
        : realOp_(wrap_op(realOp))
        , intOp_(wrap_op(intOp))
        , boolOp_(wrap_op(boolOp))
        , stringOp_(wrap_op(stringOp))
        , stepAction_(wrap_op(stepAction))
    {
    }

    template<typename R, typename... T>
    static std::function<R(cosim::time_point, cosim::duration, T...)> wrap_op(std::function<R(T...)> timeIndependentOp)
    {
        if (!timeIndependentOp) return nullptr;
        return [op = std::move(timeIndependentOp)](cosim::time_point, cosim::duration, T... value) { return op(value...); };
    }

    cosim::model_description model_description() const override
    {
        cosim::model_description md;
        md.name = "mock_slave";
        md.uuid = "09b7ee06-fc07-4ad0-86f1-cd183fbae519";
        const auto add_variable =
            [&md](const char* name,
                cosim::value_reference reference,
                cosim::variable_type type,
                cosim::variable_causality causality,
                cosim::variable_variability variability,
                auto start) {
                cosim::variable_description description{};
                description.name = name;
                description.reference = reference;
                description.type = type;
                description.causality = causality;
                description.variability = variability;
                description.start = std::move(start);
                md.variables.push_back(std::move(description));
            };
        add_variable("realOut", real_out_reference, cosim::variable_type::real, cosim::variable_causality::output, cosim::variable_variability::discrete, std::nullopt);
        add_variable("realIn", real_in_reference, cosim::variable_type::real, cosim::variable_causality::input, cosim::variable_variability::discrete, 0.0);
        add_variable("intOut", integer_out_reference, cosim::variable_type::integer, cosim::variable_causality::output, cosim::variable_variability::discrete, std::nullopt);
        add_variable("intIn", integer_in_reference, cosim::variable_type::integer, cosim::variable_causality::input, cosim::variable_variability::discrete, 0);
        add_variable("stringOut", string_out_reference, cosim::variable_type::string, cosim::variable_causality::output, cosim::variable_variability::discrete, std::nullopt);
        add_variable("stringIn", string_in_reference, cosim::variable_type::string, cosim::variable_causality::input, cosim::variable_variability::discrete, std::string());
        add_variable("booleanOut", boolean_out_reference, cosim::variable_type::boolean, cosim::variable_causality::output, cosim::variable_variability::discrete, std::nullopt);
        add_variable("booleanIn", boolean_in_reference, cosim::variable_type::boolean, cosim::variable_causality::input, cosim::variable_variability::discrete, false);
        return md;
    }

    void setup(
        cosim::time_point startTime,
        std::optional<cosim::time_point> /*stopTime*/,
        std::optional<double> /*relativeTolerance*/) override
    {
        state_.currentTime = startTime;
    }

    void start_simulation() override
    {
    }

    void end_simulation() override
    {
    }

    cosim::step_result do_step(cosim::time_point currentT, cosim::duration deltaT) override
    {
        state_.currentStepSize = deltaT;
        if (stepAction_) stepAction_(currentT, deltaT);
        state_.currentTime = currentT + deltaT;
        return cosim::step_result::complete;
    }

    void get_real_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<double> values) const override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == real_out_reference) {
                values[i] = realOp_ ? realOp_(state_.currentTime, state_.currentStepSize, state_.realIn) : state_.realIn;
            } else if (variables[i] == real_in_reference) {
                values[i] = state_.realIn;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void get_integer_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<int> values) const override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == integer_out_reference) {
                values[i] = intOp_ ? intOp_(state_.currentTime, state_.currentStepSize, state_.intIn) : state_.intIn;
            } else if (variables[i] == integer_in_reference) {
                values[i] = state_.intIn;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void get_boolean_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<bool> values) const override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == boolean_out_reference) {
                values[i] = boolOp_ ? boolOp_(state_.currentTime, state_.currentStepSize, state_.boolIn) : state_.boolIn;
            } else if (variables[i] == boolean_in_reference) {
                values[i] = state_.boolIn;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void get_string_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<std::string> values) const override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == string_out_reference) {
                values[i] = stringOp_ ? stringOp_(state_.currentTime, state_.currentStepSize, state_.stringIn) : state_.stringIn;
            } else if (variables[i] == string_in_reference) {
                values[i] = state_.stringIn;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_real_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<const double> values) override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == real_in_reference) {
                state_.realIn = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_integer_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<const int> values) override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == integer_in_reference) {
                state_.intIn = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_boolean_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<const bool> values) override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == boolean_in_reference) {
                state_.boolIn = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_string_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<const std::string> values) override
    {
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i] == string_in_reference) {
                state_.stringIn = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    state_index save_state() override
    {
        savedStates_.push_back(state_);
        return static_cast<state_index>(savedStates_.size() - 1);
    }

    void save_state(state_index overwriteState) override
    {
        savedStates_.at(overwriteState) = state_;
    }

    void restore_state(state_index state) override
    {
        state_ = savedStates_.at(state);
    }

    void release_state(state_index /*state*/) override
    {
        // Let's not worry about this.
    }

    cosim::serialization::node export_state(state_index state) const override
    {
        const auto& ss = savedStates_.at(state);
        cosim::serialization::node es;
        es.put("currentTime", cosim::to_double_time_point(ss.currentTime));
        es.put("currentStepSize", cosim::to_double_duration(ss.currentStepSize, ss.currentTime));
        es.put("realIn", ss.realIn);
        es.put("intIn", ss.intIn);
        es.put("boolIn", ss.boolIn);
        es.put("stringIn", ss.stringIn);
        return es;
    }

    state_index import_state(const cosim::serialization::node& exportedState) override
    {
        state ss;
        ss.currentStepSize = cosim::to_duration(exportedState.get<decltype(ss.realIn)>("currentStepSize"));
        ss.currentTime = cosim::to_time_point(exportedState.get<decltype(ss.realIn)>("currentTime"));
        ss.realIn = exportedState.get<decltype(ss.realIn)>("realIn");
        ss.intIn = exportedState.get<decltype(ss.intIn)>("intIn");
        ss.boolIn = exportedState.get<decltype(ss.boolIn)>("boolIn");
        ss.stringIn = exportedState.get<decltype(ss.stringIn)>("stringIn");
        savedStates_.push_back(ss);
        return static_cast<state_index>(savedStates_.size() - 1);
    }

private:
    std::function<double(cosim::time_point, cosim::duration, double)> realOp_;
    std::function<int(cosim::time_point, cosim::duration, int)> intOp_;
    std::function<bool(cosim::time_point, cosim::duration, bool)> boolOp_;
    std::function<std::string(cosim::time_point, cosim::duration, std::string_view)> stringOp_;
    std::function<void(cosim::time_point, cosim::duration)> stepAction_;

    struct state
    {
        cosim::time_point currentTime;
        cosim::duration currentStepSize{1000};

        double realIn = 0.0;
        int intIn = 0;
        bool boolIn = false;
        std::string stringIn;
    };
    state state_;
    std::vector<state> savedStates_;
};


class whole_value_mock_slave final : public mock_slave
{
public:
    static constexpr cosim::value_reference output_reference = 10;
    static constexpr cosim::value_reference input_reference = 11;

    explicit whole_value_mock_slave(cosim::variable_value output)
        : output_(std::move(output))
        , input_(
              cosim::uint64_values{
                  std::vector<std::uint64_t>(output_.size(), 0)},
              output_.dimensions())
    {
        if (output_.type() != cosim::variable_type::uint64) {
            throw std::invalid_argument(
                "whole_value_mock_slave requires UInt64 values");
        }
    }

    cosim::model_description model_description() const override
    {
        cosim::model_description result;
        result.name = "whole_value_mock_slave";
        result.uuid = "baee524a-c541-4e07-9798-70ba879cd28a";
        result.variables.push_back(variable(
            "output",
            output_reference,
            cosim::variable_causality::output,
            std::nullopt));
        result.variables.push_back(variable(
            "input",
            input_reference,
            cosim::variable_causality::input,
            input_));
        return result;
    }

    std::vector<cosim::variable_value> get_variable_values(
        gsl::span<const cosim::typed_value_reference> variables) const override
    {
        std::vector<cosim::variable_value> result;
        result.reserve(variables.size());
        for (const auto variable : variables) {
            validate_type(variable);
            if (variable.reference == output_reference) {
                result.push_back(output_);
            } else if (variable.reference == input_reference) {
                result.push_back(input_);
            } else {
                throw std::out_of_range("Invalid whole-value reference");
            }
        }
        return result;
    }

    void set_variable_values(
        gsl::span<const cosim::typed_value_reference> variables,
        gsl::span<const cosim::variable_value> values) override
    {
        if (variables.size() != values.size()) {
            throw std::invalid_argument(
                "Variable and value counts must match");
        }
        for (std::size_t i = 0; i < variables.size(); ++i) {
            validate_type(variables[i]);
            if (variables[i].reference != input_reference) {
                throw std::out_of_range("Invalid whole-value input reference");
            }
            if (values[i].type() != cosim::variable_type::uint64 ||
                values[i].dimensions() != input_.dimensions()) {
                throw std::invalid_argument(
                    "Whole-value input type or shape mismatch");
            }
            input_ = values[i];
        }
    }

    const cosim::variable_value& input_value() const noexcept
    {
        return input_;
    }

private:
    cosim::variable_description variable(
        std::string name,
        cosim::value_reference reference,
        cosim::variable_causality causality,
        std::optional<cosim::variable_value> start) const
    {
        cosim::variable_description result;
        result.name = std::move(name);
        result.reference = reference;
        result.type = cosim::variable_type::uint64;
        result.causality = causality;
        result.variability = cosim::variable_variability::discrete;
        for (const auto dimension : output_.dimensions()) {
            result.dimensions.emplace_back(cosim::fixed_dimension{dimension});
        }
        result.array_start = std::move(start);
        return result;
    }

    static void validate_type(const cosim::typed_value_reference& variable)
    {
        if (variable.type != cosim::variable_type::uint64) {
            throw std::invalid_argument("Invalid whole-value type");
        }
    }

    cosim::variable_value output_;
    cosim::variable_value input_;
};


class detailed_step_mock_slave : public mock_slave
{
public:
    explicit detailed_step_mock_slave(
        std::function<cosim::step_result_info(
            cosim::time_point,
            cosim::duration)>
            stepOperation,
        std::function<void()> eventOperation = {},
        std::function<cosim::event_handling_status()> eventStatus = {})
        : stepOperation_(std::move(stepOperation))
        , eventOperation_(std::move(eventOperation))
        , eventStatus_(std::move(eventStatus))
    {
    }

    cosim::step_result_info do_step_with_info(
        cosim::time_point currentT,
        cosim::duration deltaT) override
    {
        const auto result = stepOperation_(currentT, deltaT);
        if (result.result == cosim::step_result::complete) {
            mock_slave::do_step(
                currentT, result.last_successful_time - currentT);
        }
        return result;
    }

    void handle_events() override
    {
        if (eventOperation_) eventOperation_();
    }

    cosim::event_handling_status get_event_handling_status() const noexcept override
    {
        return eventStatus_ ? eventStatus_() : cosim::event_handling_status{};
    }

private:
    std::function<cosim::step_result_info(
        cosim::time_point,
        cosim::duration)>
        stepOperation_;
    std::function<void()> eventOperation_;
    std::function<cosim::event_handling_status()> eventStatus_;
};


#endif // header guard
