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

    void expose_for_getting(variable_type type, variable_index index) override;
    double get_real(variable_index index) const override;
    int get_integer(variable_index index) const override;
    bool get_boolean(variable_index index) const override;
    std::string_view get_string(variable_index index) const override;

    // `simulator` methods
    void expose_for_setting(variable_type type, variable_index index) override;
    void set_real(variable_index index, double value) override;
    void set_integer(variable_index index, int value) override;
    void set_boolean(variable_index index, bool value) override;
    void set_string(variable_index index, std::string_view value) override;

    void set_real_input_modifier(
        variable_index index,
        std::function<double(double)> modifier) override;
    void set_integer_input_modifier(
        variable_index index,
        std::function<int(int)> modifier) override;
    void set_boolean_input_modifier(
        variable_index index,
        std::function<bool(bool)> modifier) override;
    void set_string_input_modifier(
        variable_index index,
        std::function<std::string(std::string_view)> modifier) override;
    void set_real_output_modifier(
        variable_index index,
        std::function<double(double)> modifier) override;
    void set_integer_output_modifier(
        variable_index index,
        std::function<int(int)> modifier) override;
    void set_boolean_output_modifier(
        variable_index index,
        std::function<bool(bool)> modifier) override;
    void set_string_output_modifier(
        variable_index index,
        std::function<std::string(std::string_view)> modifier) override;

    std::unordered_set<variable_index>& get_modified_real_indexes() const override;
    std::unordered_set<variable_index>& get_modified_integer_indexes() const override;
    std::unordered_set<variable_index>& get_modified_boolean_indexes() const override;
    std::unordered_set<variable_index>& get_modified_string_indexes() const override;

    boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    boost::fibers::future<void> do_iteration() override;

    boost::fibers::future<step_result> do_step(
        time_point currentT,
        duration deltaT) override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cse
#endif // header guard
