#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/time_series_observer.hpp>

#include <array>
#include <exception>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        constexpr cosim::time_point time1 = cosim::to_time_point(1.0);
        constexpr cosim::time_point time2 = cosim::to_time_point(2.0);
        constexpr cosim::time_point endTime = cosim::to_time_point(3.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cosim::time_series_observer>();
        execution.add_observer(observer);

        auto bufferedObserver = std::make_shared<cosim::time_series_observer>(3);
        execution.add_observer(bufferedObserver);

        auto simIndex = execution.add_slave(
            std::make_unique<mock_slave>([](double x) { return x + 1.234; }), "slave uno");

        const std::array<cosim::variable_type, 14> unsupportedTypes{{
            cosim::variable_type::boolean,
            cosim::variable_type::string,
            cosim::variable_type::enumeration,
            cosim::variable_type::float32,
            cosim::variable_type::int8,
            cosim::variable_type::uint8,
            cosim::variable_type::int16,
            cosim::variable_type::uint16,
            cosim::variable_type::uint32,
            cosim::variable_type::int64,
            cosim::variable_type::uint64,
            cosim::variable_type::binary,
            cosim::variable_type::clock,
            static_cast<cosim::variable_type>(-1),
        }};
        for (const auto type : unsupportedTypes) {
            const cosim::variable_id unsupported{simIndex, type, 0};
            bool startRejected = false;
            bool stopRejected = false;
            try {
                observer->start_observing(unsupported);
            } catch (const std::invalid_argument&) {
                startRejected = true;
            }
            try {
                observer->stop_observing(unsupported);
            } catch (const std::invalid_argument&) {
                stopRejected = true;
            }
            REQUIRE(startRejected);
            REQUIRE(stopRejected);
        }

        auto variableId = cosim::variable_id{simIndex, cosim::variable_type::real, 0};

        cosim::step_number stepNumbers[2];
        try {
            observer->get_step_numbers(0, cosim::to_duration(10.0), gsl::make_span(stepNumbers, 2));
        } catch (const std::exception& e) {
            std::cout << "Got expected exception: " << e.what() << std::endl;
        }
        try {
            observer->get_step_numbers(0, cosim::to_time_point(0.0), cosim::to_time_point(10.0), gsl::make_span(stepNumbers, 2));
        } catch (const std::exception& e) {
            std::cout << "Got expected exception: " << e.what() << std::endl;
        }

        // Run the simulation
        auto simResult = execution.simulate_until(time1);
        REQUIRE(simResult);

        const int numSamples = 15;
        const cosim::value_reference varIndex = 0;
        double realValues[numSamples];
        cosim::step_number steps[numSamples];
        cosim::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(simIndex, varIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 0);

        observer->start_observing(cosim::variable_id{simIndex, cosim::variable_type::integer, 0});
        int intValues[numSamples];
        try {
            observer->get_integer_samples(simIndex, varIndex, 0, gsl::make_span(intValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        } catch (const std::exception& e) {
            std::cout << "Got expected exception: " << e.what() << std::endl;
        }

        observer->start_observing(variableId);

        simResult = execution.simulate_until(time2);
        REQUIRE(simResult);

        samplesRead = observer->get_real_samples(simIndex, varIndex, 1, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));

        REQUIRE(samplesRead == 5); // Don't expect values beyond fromStep + values.size()

        double expectedReals[] = {1.234, 1.234, 1.234, 1.234, 1.234};
        int counter = 0;
        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(std::fabs(expectedReals[i] - realValues[i]) < 1.0e-9);
            counter++;
        }
        for (size_t i = 0; i < samplesRead; i++) {
            if (i > 0) {
                REQUIRE(times[i] > times[i - 1]);
            }
        }

        observer->stop_observing(variableId);

        bufferedObserver->start_observing(variableId);

        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

        samplesRead = observer->get_real_samples(simIndex, varIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 0);

        samplesRead = bufferedObserver->get_real_samples(simIndex, varIndex, 20, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 3);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
