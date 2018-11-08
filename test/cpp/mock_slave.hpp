/**
 *  \file
 *  \brief  Mock slave classes for use in tests.
 */
#ifndef CSE_TEST_MOCK_SLAVE_HPP
#define CSE_TEST_MOCK_SLAVE_HPP

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

#include <cse/slave.hpp>


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
    explicit mock_slave(
        std::function<double(double)> realOp = nullptr,
        std::function<int(int)> intOp = nullptr,
        std::function<bool(bool)> boolOp = nullptr,
        std::function<std::string(std::string_view)> stringOp = nullptr)
        : realOp_(std::move(realOp))
        , intOp_(std::move(intOp))
        , boolOp_(std::move(boolOp))
        , stringOp_(std::move(stringOp))
        , realIn_(0.0)
        , realOut_(1.0)
        , intIn_(0)
        , intOut_(1)
        , boolIn_(true)
        , boolOut_(false)
        , stringIn_()
        , stringOut_()
    {
    }

    cse::model_description model_description() const override
    {
        cse::model_description md;
        md.name = "mock_slave";
        md.uuid = "09b7ee06-fc07-4ad0-86f1-cd183fbae519";
        md.variables.push_back(cse::variable_description{"realOut", 0, cse::variable_type::real, cse::variable_causality::output, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"realIn", 1, cse::variable_type::real, cse::variable_causality::input, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"intOut", 0, cse::variable_type::integer, cse::variable_causality::output, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"intIn", 1, cse::variable_type::integer, cse::variable_causality::input, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"stringOut", 0, cse::variable_type::string, cse::variable_causality::output, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"stringIn", 1, cse::variable_type::string, cse::variable_causality::input, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"booleanOut", 0, cse::variable_type::boolean, cse::variable_causality::output, cse::variable_variability::discrete});
        md.variables.push_back(cse::variable_description{"booleanIn", 1, cse::variable_type::boolean, cse::variable_causality::input, cse::variable_variability::discrete});
        return md;
    }

    void setup(
        cse::time_point /*startTime*/,
        std::optional<cse::time_point> /*stopTime*/,
        std::optional<double> /*relativeTolerance*/) override
    {
    }

    void start_simulation() override
    {
    }

    void end_simulation() override
    {
    }

    cse::step_result do_step(cse::time_point /*currentT*/, cse::duration /*deltaT*/) override
    {
        realOut_ = realOp_ ? realOp_(realIn_) : realIn_;
        intOut_ = intOp_ ? intOp_(intIn_) : intIn_;
        boolOut_ = boolOp_ ? boolOp_(boolIn_) : boolIn_;
        stringOut_ = stringOp_ ? stringOp_(stringIn_) : stringIn_;
        return cse::step_result::complete;
    }

    void get_real_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<double> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = realOut_;
            } else if (variables[i] == 1) {
                values[i] = realIn_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void get_integer_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<int> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = intOut_;
            } else if (variables[i] == 1) {
                values[i] = intIn_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void get_boolean_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<bool> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = boolOut_;
            } else if (variables[i] == 1) {
                values[i] = boolIn_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void get_string_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<std::string> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = stringOut_;
            } else if (variables[i] == 1) {
                values[i] = stringIn_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_real_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const double> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 1) {
                realIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_integer_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const int> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 1) {
                intIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_boolean_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const bool> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 1) {
                boolIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_string_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const std::string> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 1) {
                stringIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

private:
    std::function<double(double)> realOp_;
    std::function<int(int)> intOp_;
    std::function<bool(bool)> boolOp_;
    std::function<std::string(std::string_view)> stringOp_;

    double realIn_, realOut_;
    int intIn_, intOut_;
    bool boolIn_, boolOut_;
    std::string stringIn_, stringOut_;
};


#endif // header guard
