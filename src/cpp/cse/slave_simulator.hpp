/**
 *  \file
 *  \brief Defines `cse::slave_simulator`
 */
#ifndef CSE_SLAVE_SIMULATOR_HPP
#define CSE_SLAVE_SIMULATOR_HPP

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>

#include <memory>
#include <string_view>


namespace cse
{


class slave_simulator : public simulator
{
public:
    slave_simulator(std::shared_ptr<async_slave> slave, std::string_view name);

    ~slave_simulator() noexcept;

    slave_simulator(const slave_simulator&) = delete;
    slave_simulator& operator=(const slave_simulator&) = delete;

    slave_simulator(slave_simulator&&) noexcept;
    slave_simulator& operator=(slave_simulator&&) noexcept;

    // `observable` methods
    std::string name() const override;
    cse::model_description model_description() const override;

    void expose_for_getting(variable_type type, value_reference ref) override;
    double get_real(value_reference reference) const override;
    int get_integer(value_reference reference) const override;
    bool get_boolean(value_reference reference) const override;
    std::string_view get_string(value_reference reference) const override;

    // `simulator` methods
    void expose_for_setting(variable_type type, value_reference ref) override;
    void set_real(value_reference reference, double value) override;
    void set_integer(value_reference reference, int value) override;
    void set_boolean(value_reference reference, bool value) override;
    void set_string(value_reference reference, std::string_view value) override;

    void set_real_input_modifier(
        value_reference reference,
        std::function<double(double)> modifier) override;
    void set_integer_input_modifier(
        value_reference reference,
        std::function<int(int)> modifier) override;
    void set_boolean_input_modifier(
        value_reference reference,
        std::function<bool(bool)> modifier) override;
    void set_string_input_modifier(
        value_reference reference,
        std::function<std::string(std::string_view)> modifier) override;
    void set_real_output_modifier(
        value_reference reference,
        std::function<double(double)> modifier) override;
    void set_integer_output_modifier(
        value_reference reference,
        std::function<int(int)> modifier) override;
    void set_boolean_output_modifier(
        value_reference reference,
        std::function<bool(bool)> modifier) override;
    void set_string_output_modifier(
        value_reference reference,
        std::function<std::string(std::string_view)> modifier) override;

    std::unordered_set<value_reference>& get_modified_real_variables() const override;
    std::unordered_set<value_reference>& get_modified_integer_variables() const override;
    std::unordered_set<value_reference>& get_modified_boolean_variables() const override;
    std::unordered_set<value_reference>& get_modified_string_variables() const override;

    boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    boost::fibers::future<void> do_iteration() override;

    boost::fibers::future<void> start_simulation() override;

    boost::fibers::future<step_result> do_step(
        time_point currentT,
        duration deltaT) override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cse
#endif // header guard
