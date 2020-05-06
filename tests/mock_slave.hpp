/**
 *  \file
 *  \brief  Mock slave classes for use in tests.
 */
#ifndef CSE_TEST_MOCK_SLAVE_HPP
#define CSE_TEST_MOCK_SLAVE_HPP

#include <cse/slave.hpp>

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
class mock_slave : public cse::slave
{
public:
    constexpr static cse::value_reference real_out_reference = 0;
    constexpr static cse::value_reference real_in_reference = 1;
    constexpr static cse::value_reference integer_out_reference = 0;
    constexpr static cse::value_reference integer_in_reference = 1;
    constexpr static cse::value_reference boolean_out_reference = 0;
    constexpr static cse::value_reference boolean_in_reference = 1;
    constexpr static cse::value_reference string_out_reference = 0;
    constexpr static cse::value_reference string_in_reference = 1;

    // Constructor that takes time-dependent functions
    explicit mock_slave(
        std::function<double(cse::time_point, double)> realOp = nullptr,
        std::function<int(cse::time_point, int)> intOp = nullptr,
        std::function<bool(cse::time_point, bool)> boolOp = nullptr,
        std::function<std::string(cse::time_point, std::string_view)> stringOp = nullptr,
        std::function<void(cse::time_point)> stepAction = nullptr)
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
    static std::function<R(cse::time_point, T...)> wrap_op(std::function<R(T...)> timeIndependentOp)
    {
        if (!timeIndependentOp) return nullptr;
        return [op = std::move(timeIndependentOp)](cse::time_point, T... value) { return op(value...); };
    }


    cse::model_description model_description() const override
    {
        cse::model_description md;
        md.name = "mock_slave";
        md.uuid = "09b7ee06-fc07-4ad0-86f1-cd183fbae519";
        md.variables.push_back(cse::variable_description{"realOut", real_out_reference, cse::variable_type::real, cse::variable_causality::output, cse::variable_variability::discrete, std::nullopt});
        md.variables.push_back(cse::variable_description{"realIn", real_in_reference, cse::variable_type::real, cse::variable_causality::input, cse::variable_variability::discrete, 0.0});
        md.variables.push_back(cse::variable_description{"intOut", integer_out_reference, cse::variable_type::integer, cse::variable_causality::output, cse::variable_variability::discrete, std::nullopt});
        md.variables.push_back(cse::variable_description{"intIn", integer_in_reference, cse::variable_type::integer, cse::variable_causality::input, cse::variable_variability::discrete, 0});
        md.variables.push_back(cse::variable_description{"stringOut", string_out_reference, cse::variable_type::string, cse::variable_causality::output, cse::variable_variability::discrete, std::nullopt});
        md.variables.push_back(cse::variable_description{"stringIn", string_in_reference, cse::variable_type::string, cse::variable_causality::input, cse::variable_variability::discrete, std::string()});
        md.variables.push_back(cse::variable_description{"booleanOut", boolean_out_reference, cse::variable_type::boolean, cse::variable_causality::output, cse::variable_variability::discrete, std::nullopt});
        md.variables.push_back(cse::variable_description{"booleanIn", boolean_in_reference, cse::variable_type::boolean, cse::variable_causality::input, cse::variable_variability::discrete, false});
        return md;
    }

    void setup(
        cse::time_point startTime,
        std::optional<cse::time_point> /*stopTime*/,
        std::optional<double> /*relativeTolerance*/) override
    {
        currentTime_ = startTime;
    }

    void start_simulation() override
    {
    }

    void end_simulation() override
    {
    }

    cse::step_result do_step(cse::time_point currentT, cse::duration deltaT) override
    {
        if (stepAction_) stepAction_(currentT);
        currentTime_ = currentT + deltaT;
        return cse::step_result::complete;
    }

    void get_real_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<double> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == real_out_reference) {
                values[i] = realOp_ ? realOp_(currentTime_, realIn_) : realIn_;
            } else if (variables[i] == real_in_reference) {
                values[i] = realIn_;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void get_integer_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<int> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == integer_out_reference) {
                values[i] = intOp_ ? intOp_(currentTime_, intIn_) : intIn_;
            } else if (variables[i] == integer_in_reference) {
                values[i] = intIn_;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void get_boolean_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<bool> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == boolean_out_reference) {
                values[i] = boolOp_ ? boolOp_(currentTime_, boolIn_) : boolIn_;
            } else if (variables[i] == boolean_in_reference) {
                values[i] = boolIn_;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void get_string_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<std::string> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == string_out_reference) {
                values[i] = stringOp_ ? stringOp_(currentTime_, stringIn_) : stringIn_;
            } else if (variables[i] == string_in_reference) {
                values[i] = stringIn_;
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_real_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<const double> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == real_in_reference) {
                realIn_ = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_integer_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<const int> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == integer_in_reference) {
                intIn_ = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_boolean_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<const bool> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == boolean_in_reference) {
                boolIn_ = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

    void set_string_variables(
        gsl::span<const cse::value_reference> variables,
        gsl::span<const std::string> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == string_in_reference) {
                stringIn_ = values[i];
            } else {
                throw std::out_of_range("bad reference");
            }
        }
    }

private:
    std::function<double(cse::time_point, double)> realOp_;
    std::function<int(cse::time_point, int)> intOp_;
    std::function<bool(cse::time_point, bool)> boolOp_;
    std::function<std::string(cse::time_point, std::string_view)> stringOp_;
    std::function<void(cse::time_point)> stepAction_;

    cse::time_point currentTime_;

    double realIn_ = 0.0;
    int intIn_ = 0;
    bool boolIn_ = false;
    std::string stringIn_;
};


#endif // header guard
