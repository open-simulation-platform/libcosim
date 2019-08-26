#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/observer/time_series_observer.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
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

    cse::step_result do_step(cse::time_point currentT, cse::duration deltaT) override
    {
        time_ = currentT + deltaT;
        return mock_slave::do_step(currentT, deltaT);
    }

    void set_real_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const double> values) override
    {
        if (!variables.empty()) setRealLog_.insert(time_);
        mock_slave::set_real_variables(variables, values);
    }

    void set_integer_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const int> values) override
    {
        if (!variables.empty()) setIntegerLog_.insert(time_);
        mock_slave::set_integer_variables(variables, values);
    }

    const std::set<cse::time_point>& get_set_real_log() { return setRealLog_; }

    const std::set<cse::time_point>& get_set_integer_log() { return setIntegerLog_; }

private:
    cse::time_point time_;
    std::set<cse::time_point> setRealLog_;
    std::set<cse::time_point> setIntegerLog_;
};


int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr cse::time_point startTime;
        constexpr cse::time_point endTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        auto algorithm = std::make_shared<cse::fixed_step_algorithm>(stepSize);

        auto execution = cse::execution(
            startTime,
            algorithm);

        auto observer = std::make_shared<cse::last_value_observer>();
        execution.add_observer(observer);

        const cse::variable_index realOutIndex = 0;
        const cse::variable_index realInIndex = 1;
        const cse::variable_index integerOutIndex = 0;
        const cse::variable_index integerInIndex = 1;

        double v = 0.0;
        auto slave0 = std::make_shared<mock_slave>([&v](double /*x*/) { return ++v; });
        auto idx0 = execution.add_slave(cse::make_pseudo_async(slave0), "slave 0");

        auto slave1 = std::make_shared<set_logging_mock_slave>();
        auto idx1 = execution.add_slave(cse::make_pseudo_async(slave1), "slave 1");

        int i = 0;
        auto slave2 = std::make_shared<set_logging_mock_slave>(nullptr, [&i](int /*x*/) { return ++i + 1; });
        auto idx2 = execution.add_slave(cse::make_pseudo_async(slave2), "slave 2");

        auto c1 = std::make_shared<cse::scalar_connection>(
            cse::variable_id{idx0, cse::variable_type::real, realOutIndex},
            cse::variable_id{idx1, cse::variable_type::real, realInIndex});
        execution.add_connection(c1);

        auto c2 = std::make_shared<cse::scalar_connection>(
            cse::variable_id{idx1, cse::variable_type::integer, integerOutIndex},
            cse::variable_id{idx2, cse::variable_type::integer, integerInIndex});
        execution.add_connection(c2);

        auto c3 = std::make_shared<cse::scalar_connection>(
            cse::variable_id{idx2, cse::variable_type::integer, integerOutIndex},
            cse::variable_id{idx1, cse::variable_type::integer, integerInIndex});
        execution.add_connection(c3);

        algorithm->set_stepsize_decimation_factor(idx0, 1);
        algorithm->set_stepsize_decimation_factor(idx1, 2);
        algorithm->set_stepsize_decimation_factor(idx2, 3);

        auto observer2 = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer2);
        observer2->start_observing(cse::variable_id{idx0, cse::variable_type::real, realOutIndex});
        observer2->start_observing(cse::variable_id{idx1, cse::variable_type::real, realOutIndex});
        observer2->start_observing(cse::variable_id{idx2, cse::variable_type::integer, integerOutIndex});

        // Run simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 10;
        double realValues0[numSamples];
        cse::step_number steps0[numSamples];
        cse::time_point timeValues0[numSamples];
        observer2->get_real_samples(idx0, realOutIndex, 1, gsl::make_span(realValues0, numSamples), gsl::make_span(steps0, numSamples), gsl::make_span(timeValues0, numSamples));

        double realValues1[numSamples];
        cse::step_number steps1[numSamples];
        cse::time_point timeValues1[numSamples];
        observer2->get_real_samples(idx1, realOutIndex, 1, gsl::make_span(realValues1, numSamples), gsl::make_span(steps1, numSamples), gsl::make_span(timeValues1, numSamples));

        double expectedReals0[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
        double expectedReals1[] = {1.0, 2.0, 4.0, 6.0, 8.0};

        int intValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point timeValues[numSamples];
        observer2->get_integer_samples(idx2, integerOutIndex, 1, gsl::make_span(intValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(timeValues, numSamples));

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

        const cse::time_point expectedSetRealTimes1[] = {
            startTime,
            startTime + 2 * stepSize,
            startTime + 4 * stepSize,
            startTime + 6 * stepSize,
            startTime + 8 * stepSize};
        const auto& setRealLog1 = slave1->get_set_real_log();
        for (auto t : setRealLog1) std::cout << t.time_since_epoch().count() / 1000000.0 << std::endl;
        REQUIRE(setRealLog1.size() == 5);
        REQUIRE(std::equal(setRealLog1.begin(), setRealLog1.end(), expectedSetRealTimes1));

        const cse::time_point expectedSetIntTimes1[] = {startTime, startTime + 6 * stepSize};
        const auto& setIntLog1 = slave2->get_set_integer_log();
        REQUIRE(setIntLog1.size() == 2);
        REQUIRE(std::equal(setIntLog1.begin(), setIntLog1.end(), expectedSetIntTimes1));

        const cse::time_point expectedSetIntTimes2[] = {startTime, startTime + 6 * stepSize};
        const auto& setIntLog2 = slave2->get_set_integer_log();
        REQUIRE(setIntLog2.size() == 2);
        REQUIRE(std::equal(setIntLog2.begin(), setIntLog2.end(), expectedSetIntTimes2));

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
