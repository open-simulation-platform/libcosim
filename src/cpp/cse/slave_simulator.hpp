/**
 *  \file
 *  \brief Defines `cse::slave_simulator`
 */
#ifndef CSE_SLAVE_SIMULATOR_HPP
#define CSE_SLAVE_SIMULATOR_HPP

#include <memory>
#include <string_view>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>


namespace cse
{


class slave_simulator : public simulator
{
public:
    slave_simulator(std::unique_ptr<async_slave> slave, std::string_view name);

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

    boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    boost::fibers::future<void> do_iteration() override;

    boost::fibers::future<step_result> do_step(
        time_point currentT,
        time_duration deltaT) override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cse
#endif // header guard
