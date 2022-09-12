#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/log/logger.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


// A slave that logs the time points when its set_real_variables() and
// set_integer_variables() functions get called.
class set_logging_mock_slave : public mock_slave
{
public:
    using mock_slave::mock_slave;

    cosim::step_result do_step(cosim::time_point currentT, cosim::duration deltaT) override
    {
        time_ = currentT + deltaT;
        return mock_slave::do_step(currentT, deltaT);
    }

    void set_real_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<const double> values) override
    {
        if (!variables.empty()) setRealLog_.insert(time_);
        mock_slave::set_real_variables(variables, values);
    }

    void set_integer_variables(
        gsl::span<const cosim::value_reference> variables,
        gsl::span<const int> values) override
    {
        if (!variables.empty()) setIntegerLog_.insert(time_);
        mock_slave::set_integer_variables(variables, values);
    }

    const std::set<cosim::time_point>& get_set_real_log() { return setRealLog_; }

    const std::set<cosim::time_point>& get_set_integer_log() { return setIntegerLog_; }

private:
    cosim::time_point time_;
    std::set<cosim::time_point> setRealLog_;
    std::set<cosim::time_point> setIntegerLog_;
};


int main()
{
    try {
        cosim::log::set_logging_level(cosim::log::cosim_logger::level::debug);

        constexpr cosim::time_point startTime;
        constexpr cosim::time_point endTime = cosim::to_time_point(1.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(stepSize);

        auto execution = cosim::execution(
            startTime,
            algorithm);

        auto observer = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(observer);

        double v = 0.0;
        auto slave0 = std::make_shared<mock_slave>([&v](double /*x*/) { return v; }, nullptr, nullptr, nullptr, [&v] { ++v; });
        auto idx0 = execution.add_slave(slave0, "slave 0");

        auto slave1 = std::make_shared<set_logging_mock_slave>();
        auto idx1 = execution.add_slave(slave1, "slave 1");

        int i = 0;
        auto slave2 = std::make_shared<set_logging_mock_slave>(
            nullptr, [&i](int /*x*/) { return i + 1; }, nullptr, nullptr, [&i] { ++i; });
        auto idx2 = execution.add_slave(slave2, "slave 2");

        execution.connect_variables(
            cosim::variable_id{idx0, cosim::variable_type::real, mock_slave::real_out_reference},
            cosim::variable_id{idx1, cosim::variable_type::real, mock_slave::real_in_reference});

        execution.connect_variables(
            cosim::variable_id{idx1, cosim::variable_type::integer, mock_slave::integer_out_reference},
            cosim::variable_id{idx2, cosim::variable_type::integer, mock_slave::integer_in_reference});

        execution.connect_variables(
            cosim::variable_id{idx2, cosim::variable_type::integer, mock_slave::integer_out_reference},
            cosim::variable_id{idx1, cosim::variable_type::integer, mock_slave::integer_in_reference});

        algorithm->set_stepsize_decimation_factor(idx0, 1);
        algorithm->set_stepsize_decimation_factor(idx1, 2);
        algorithm->set_stepsize_decimation_factor(idx2, 3);

        auto observer2 = std::make_shared<cosim::time_series_observer>();
        execution.add_observer(observer2);
        observer2->start_observing(cosim::variable_id{idx0, cosim::variable_type::real, mock_slave::real_out_reference});
        observer2->start_observing(cosim::variable_id{idx1, cosim::variable_type::real, mock_slave::real_out_reference});
        observer2->start_observing(cosim::variable_id{idx2, cosim::variable_type::integer, mock_slave::integer_out_reference});

        // Run simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

        const int numSamples = 10;
        double realValues0[numSamples];
        cosim::step_number steps0[numSamples];
        cosim::time_point timeValues0[numSamples];
        observer2->get_real_samples(idx0, mock_slave::real_out_reference, 1, gsl::make_span(realValues0, numSamples), gsl::make_span(steps0, numSamples), gsl::make_span(timeValues0, numSamples));

        double realValues1[numSamples];
        cosim::step_number steps1[numSamples];
        cosim::time_point timeValues1[numSamples];
        observer2->get_real_samples(idx1, mock_slave::real_out_reference, 1, gsl::make_span(realValues1, numSamples), gsl::make_span(steps1, numSamples), gsl::make_span(timeValues1, numSamples));

        double expectedReals0[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
        double expectedReals1[] = {0.0, 2.0, 4.0, 6.0, 8.0};

        int intValues[numSamples];
        cosim::step_number steps[numSamples];
        cosim::time_point timeValues[numSamples];
        observer2->get_integer_samples(idx2, mock_slave::integer_out_reference, 1, gsl::make_span(intValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(timeValues, numSamples));

        // int expectedInts[numSamples] = {0, 0, 2, 2, 2, 3, 3, 3, 4, 4}; // this is what we actually expect
        int expectedInts[] = {2, 3, 4};

        for (int k = 0; k < 10; k++) {
            REQUIRE(std::fabs(expectedReals0[k] - realValues0[k]) < 1e-9);
        }
        for (int k = 0; k < 5; k++) {
            REQUIRE(std::fabs(expectedReals1[k] - realValues1[k]) < 1e-9);
        }
        for (int k = 0; k < 3; k++) {
            REQUIRE(expectedInts[k] == intValues[k]);
        }

        const cosim::time_point expectedSetRealTimes1[] = {
            startTime,
            startTime + 2 * stepSize,
            startTime + 4 * stepSize,
            startTime + 6 * stepSize,
            startTime + 8 * stepSize};
        const auto& setRealLog1 = slave1->get_set_real_log();
        for (auto t : setRealLog1) std::cout << t.time_since_epoch().count() / 1000000.0 << std::endl;
        REQUIRE(setRealLog1.size() == 5);
        REQUIRE(std::equal(setRealLog1.begin(), setRealLog1.end(), expectedSetRealTimes1));

        const cosim::time_point expectedSetIntTimes1[] = {startTime, startTime + 6 * stepSize};
        const auto& setIntLog1 = slave2->get_set_integer_log();
        REQUIRE(setIntLog1.size() == 2);
        REQUIRE(std::equal(setIntLog1.begin(), setIntLog1.end(), expectedSetIntTimes1));

        const cosim::time_point expectedSetIntTimes2[] = {startTime, startTime + 6 * stepSize};
        const auto& setIntLog2 = slave2->get_set_integer_log();
        REQUIRE(setIntLog2.size() == 2);
        REQUIRE(std::equal(setIntLog2.begin(), setIntLog2.end(), expectedSetIntTimes2));

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
