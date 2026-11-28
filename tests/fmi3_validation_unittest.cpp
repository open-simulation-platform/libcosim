#define BOOST_TEST_MODULE FMI 3 validation traces

#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v3/fmu.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/slave_simulator.hpp>
#include <cosim/time.hpp>

#include <boost/test/unit_test.hpp>

#include "warning_capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(step_result)

namespace
{
filesystem::path fixture(const char* directory, const char* name)
{
    const auto* path = std::getenv(directory);
    if (!path) {
        throw std::runtime_error(std::string(directory) + " is not set");
    }
    const auto result = filesystem::path(path) / name;
    if (!filesystem::exists(result)) {
        throw std::runtime_error("Missing FMU: " + result.string());
    }
    return result;
}

std::unordered_map<std::string, value_reference> references(
    const model_description& description)
{
    std::unordered_map<std::string, value_reference> result;
    for (const auto& variable : description.variables) {
        result.emplace(variable.name, variable.reference);
    }
    return result;
}

double get_real(slave& instance, value_reference reference)
{
    double value = 0.0;
    instance.get_real_variables(
        gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
    return value;
}

int get_integer(slave& instance, value_reference reference)
{
    int value = 0;
    instance.get_integer_variables(
        gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
    return value;
}

void trace(
    const std::string& scenario, const std::string& action,
    double start, double requested, double reached,
    double first, double second, double expectedFirst, double expectedSecond,
    int early, int event)
{
    std::cout << std::setprecision(17)
              << "FMI_TRACE|" << scenario << '|' << action << '|'
              << start << '|' << requested << '|' << reached << '|'
              << first << '|' << second << '|'
              << expectedFirst << '|' << expectedSecond << '|'
              << early << '|' << event << '\n';
}

struct ball_oracle
{
    double height = 1.0;
    double velocity = 0.0;
    int steps = 0;
    int impacts = 0;

    void advance(int target, bool deferLastImpact)
    {
        for (; steps < target; ++steps) {
            height += 0.001 * velocity;
            velocity -= 0.001 * 9.81;
            if (height <= 0.0 && velocity < 0.0) {
                ++impacts;
                if (!(deferLastImpact && steps + 1 == target)) {
                    bounce();
                }
            }
        }
    }

    void bounce()
    {
        height = std::numeric_limits<double>::min();
        velocity = -0.7 * velocity;
        if (velocity < 0.1) velocity = 0.0;
    }
};

std::shared_ptr<algorithm> make_algorithm(const std::string& name, duration step)
{
    if (name == "fixed") {
        return std::make_shared<fixed_step_algorithm>(step, 1);
    }
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    return std::make_shared<ecco_algorithm>(params, 1);
}
} // namespace

BOOST_AUTO_TEST_CASE(standalone_bouncing_ball_trajectory)
{
    const auto importer = fmi::importer::create();
    const auto model = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "BouncingBall.fmu")));
    const auto vars = references(*model->model_description());

    for (const bool eventMode : {false, true}) {
        for (const int stepMilliseconds : {50, 200}) {
            BOOST_TEST_CONTEXT("Event Mode=" << eventMode
                << ", requested ms=" << stepMilliseconds) {
                const auto scenario = std::string("ball-") +
                    (eventMode ? "event-" : "internal-") +
                    std::to_string(stepMilliseconds);
                fmi::v3::co_simulation_callbacks callbacks;
                callbacks.event_mode_used = eventMode;
                callbacks.early_return_allowed = true;
                auto instance = model->instantiate_v3_slave(scenario, std::move(callbacks));
                instance->setup(time_point{}, to_time_point(2.0), std::nullopt);
                instance->start_simulation();
                if (eventMode) instance->handle_events();
                ball_oracle expected;
                auto now = time_point{};
                const auto end = to_time_point(2.0);
                const auto nominal = std::chrono::milliseconds(stepMilliseconds);
                while (now < end) {
                    const auto request = std::min(duration(nominal), end - now);
                    const auto result = instance->do_step_with_info(now, request);
                    BOOST_REQUIRE(result.result == step_result::complete);
                    BOOST_REQUIRE(!result.termination_requested);
                    const auto reached = result.early_return
                        ? result.last_successful_time : now + request;
                    BOOST_REQUIRE(reached > now);
                    BOOST_REQUIRE(reached <= now + request);
                    const auto solverSteps = static_cast<int>(
                        std::llround(to_double_time_point(reached) / 0.001));
                    const int previousImpacts = expected.impacts;
                    expected.advance(solverSteps, eventMode && result.event_handling_needed);
                    const double height = get_real(*instance, vars.at("h"));
                    const double velocity = get_real(*instance, vars.at("v"));
                    trace(scenario, "step", to_double_time_point(now),
                        to_double_time_point(now + request), to_double_time_point(reached),
                        height, velocity, expected.height, expected.velocity,
                        result.early_return, result.event_handling_needed);
                    BOOST_CHECK_SMALL(height - expected.height, 1e-8);
                    BOOST_CHECK_SMALL(velocity - expected.velocity, 1e-8);
                    if (eventMode && expected.impacts > previousImpacts) {
                        BOOST_CHECK(result.event_handling_needed);
                    }
                    if (result.event_handling_needed) {
                        BOOST_REQUIRE(eventMode);
                        instance->handle_events();
                        if (expected.impacts > previousImpacts) expected.bounce();
                        const double postHeight = get_real(*instance, vars.at("h"));
                        const double postVelocity = get_real(*instance, vars.at("v"));
                        trace(scenario, "event", to_double_time_point(reached),
                            to_double_time_point(reached), to_double_time_point(reached),
                            postHeight, postVelocity, expected.height, expected.velocity, 0, 1);
                        BOOST_CHECK_SMALL(postHeight - expected.height, 1e-8);
                        BOOST_CHECK_SMALL(postVelocity - expected.velocity, 1e-8);
                    }
                    now = reached;
                }
                BOOST_TEST(expected.impacts >= 3);
                instance->end_simulation();
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(bouncing_ball_state_replay_across_impact)
{
    const auto importer = fmi::importer::create();
    const auto model = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "BouncingBall.fmu")));
    BOOST_REQUIRE(model->model_description()->capabilities.can_save_state);
    const auto vars = references(*model->model_description());
    fmi::v3::co_simulation_callbacks options;
    options.early_return_allowed = true;
    auto instance = model->instantiate_v3_slave("ball-state-replay", options);
    instance->setup(time_point{}, to_time_point(1.0), std::nullopt);
    instance->start_simulation();
    instance->handle_events();
    const auto before = instance->do_step_with_info(time_point{}, to_duration(0.4));
    BOOST_REQUIRE(!before.early_return);
    const auto state = instance->save_state();
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt) instance->restore_state(state);
        const auto result = instance->do_step_with_info(
            to_time_point(0.4), to_duration(0.2));
        BOOST_REQUIRE(result.early_return);
        BOOST_REQUIRE(result.event_handling_needed);
        const double height = get_real(*instance, vars.at("h"));
        const double velocity = get_real(*instance, vars.at("v"));
        trace("ball-replay", attempt ? "restore-step" : "step", 0.4, 0.6,
            to_double_time_point(result.last_successful_time),
            height, velocity, -0.00432818, -4.44393, 1, 1);
        BOOST_CHECK_SMALL(to_double_time_point(result.last_successful_time) - 0.453, 1e-8);
        BOOST_CHECK_SMALL(height - (-0.00432818), 1e-8);
        BOOST_CHECK_SMALL(velocity - (-4.44393), 1e-8);
        instance->handle_events();
    }
    instance->release_state(state);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(bouncing_ball_interrupted_steps_complete_the_nominal_interval)
{
    const auto importer = fmi::importer::create();
    const auto model = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "BouncingBall.fmu")));
    for (const std::string& name : {std::string("fixed"), std::string("ecco")}) {
        auto source = model->instantiate_slave(name + "-grid-ball");
        slave_simulator simulator(source, name + "-grid-ball");
        auto algorithm = make_algorithm(name, to_duration(0.1));
        algorithm->add_simulator(0, &simulator, duration::zero());
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        auto now = time_point{};
        bool sawImpact = false;
        bool sawGridEnd = false;
        for (int i = 0; i < 12 && now < to_time_point(0.6); ++i) {
            const auto result = algorithm->do_step_with_info(now);
            BOOST_REQUIRE(result.step_size > duration::zero());
            now += result.step_size;
            if (now == to_time_point(0.453)) sawImpact = true;
            if (now == to_time_point(0.5)) sawGridEnd = true;
            BOOST_CHECK(now <= to_time_point(0.6));
        }
        BOOST_CHECK(sawImpact);
        BOOST_CHECK(sawGridEnd);
        BOOST_CHECK(now == to_time_point(0.6));
    }
}

BOOST_AUTO_TEST_CASE(tolerates_reference_fmu_early_return_at_requested_endpoint)
{
    warning_capture warnings;
    const auto importer = fmi::importer::create();
    const auto model = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "BouncingBall.fmu")));
    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.event_mode_used = false;
    callbacks.early_return_allowed = true;
    const auto instance = model->instantiate_v3_slave(
        "bad-endpoint", std::move(callbacks));
    instance->setup(time_point{}, to_time_point(1.0), std::nullopt);
    instance->start_simulation();
    const auto result = instance->do_step_with_info(time_point{}, to_duration(0.453));
    BOOST_REQUIRE(result.result == step_result::complete);
    BOOST_CHECK(!result.early_return);
    BOOST_CHECK(!result.event_handling_needed);
    BOOST_CHECK(result.last_successful_time == to_time_point(0.453));
    BOOST_CHECK(warnings.messages().find("early return at its requested endpoint") !=
        std::string::npos);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(stair_standalone_time_event_trace)
{
    const auto importer = fmi::importer::create();
    const auto model = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "Stair.fmu")));
    const auto vars = references(*model->model_description());
    fmi::v3::co_simulation_callbacks options;
    options.early_return_allowed = true;
    const auto instance = model->instantiate_v3_slave("stair-standalone", options);
    instance->setup(time_point{}, to_time_point(3.0), std::nullopt);
    instance->start_simulation();
    instance->handle_events();
    for (int event = 1; event <= 2; ++event) {
        const auto start = to_time_point(static_cast<double>(event - 1));
        const auto result = instance->do_step_with_info(start, to_duration(1.1));
        BOOST_REQUIRE(result.early_return);
        BOOST_REQUIRE(result.event_handling_needed);
        BOOST_CHECK_SMALL(to_double_time_point(result.last_successful_time) - event, 1e-8);
        const auto before = get_integer(*instance, vars.at("counter"));
        instance->handle_events();
        const auto after = get_integer(*instance, vars.at("counter"));
        trace("stair-standalone", "event",
            static_cast<double>(event - 1), static_cast<double>(event) + 0.1,
            to_double_time_point(result.last_successful_time),
            before, after, event, event + 1, 1, 1);
        BOOST_CHECK_EQUAL(before, event);
        BOOST_CHECK_EQUAL(after, event + 1);
    }
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(mixed_fmi3_fmi2_fmi2_event_chain)
{
    const auto importer = fmi::importer::create();
    const auto ball = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "BouncingBall.fmu")));
    const auto legacy = importer->import(fixture("REFERENCE_FMU_V2_DIR", "Feedthrough.fmu"));
    const auto ballRefs = references(*ball->model_description());
    const auto legacyRefs = references(*legacy->model_description());
    const auto input = legacyRefs.at("Float64_continuous_input");
    const auto output = legacyRefs.at("Float64_continuous_output");

    for (const std::string& name : {std::string("fixed"), std::string("ecco")}) {
        for (const bool eventMode : {false, true}) {
            const auto scenario = name + (eventMode ? "-mixed-event" : "-mixed-internal");
            fmi::v3::co_simulation_callbacks callbacks;
            callbacks.event_mode_used = eventMode;
            callbacks.early_return_allowed = true;
            auto source = ball->instantiate_v3_slave(scenario, std::move(callbacks));
            auto middle = legacy->instantiate_slave(scenario + "-middle");
            auto target = legacy->instantiate_slave(scenario + "-target");
            slave_simulator a(source, scenario + "-ball");
            slave_simulator b(middle, scenario + "-middle");
            slave_simulator c(target, scenario + "-target");
            auto algorithm = make_algorithm(name, to_duration(0.1));
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->add_simulator(2, &c, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::real, ballRefs.at("h")},
                variable_id{1, variable_type::real, input});
            algorithm->connect_variables(
                variable_id{1, variable_type::real, output},
                variable_id{2, variable_type::real, input});
            b.expose_for_getting(variable_type::real, output);
            c.expose_for_getting(variable_type::real, output);
            algorithm->setup(time_point{}, std::nullopt);
            algorithm->initialize();
            auto now = time_point{};
            int earlyReturns = 0;
            double previousHeight = 1.0;
            double previousMiddle = 1.0;
            while (now < to_time_point(1.0)) {
                const auto result = algorithm->do_step_with_info(now);
                BOOST_REQUIRE(result.step_size > duration::zero());
                BOOST_REQUIRE(!result.termination_requested);
                const auto reached = now + result.step_size;
                const double height = get_real(*source, ballRefs.at("h"));
                const double middleValue = b.get_real(output);
                const double targetValue = c.get_real(output);
                trace(scenario, "macro", to_double_time_point(now),
                    to_double_time_point(now + to_duration(0.1)),
                    to_double_time_point(reached), height, middleValue,
                    targetValue, 0, result.early_return, -1);
                BOOST_TEST(std::isfinite(height));
                BOOST_TEST(std::isfinite(middleValue));
                BOOST_TEST(std::isfinite(targetValue));
                BOOST_CHECK_SMALL(middleValue - previousHeight, 1e-8);
                BOOST_CHECK_SMALL(targetValue - previousMiddle, 1e-8);
                previousHeight = height;
                previousMiddle = middleValue;
                if (result.early_return) ++earlyReturns;
                now = reached;
            }
            BOOST_TEST(earlyReturns >= 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi2_bouncing_ball_to_fmi3_feedthrough)
{
    const auto importer = fmi::importer::create();
    const auto ball = importer->import(
        fixture("REFERENCE_FMU_V2_DIR", "BouncingBall.fmu"));
    const auto feedthrough = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "Feedthrough.fmu")));
    const auto ballRefs = references(*ball->model_description());
    const auto feedRefs = references(*feedthrough->model_description());
    for (const std::string& name : {std::string("fixed"), std::string("ecco")}) {
        auto source = ball->instantiate_slave(name + "-fmi2-ball");
        auto target = feedthrough->instantiate_v3_slave(name + "-fmi3-feedthrough");
        slave_simulator a(source, name + "-fmi2-ball");
        slave_simulator b(target, name + "-fmi3-feedthrough");
        const auto output = feedRefs.at("Float64_continuous_output");
        auto algorithm = make_algorithm(name, to_duration(0.1));
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::real, ballRefs.at("h")},
            variable_id{1, variable_type::real, feedRefs.at("Float64_continuous_input")});
        b.expose_for_getting(variable_type::real, output);
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        double previousHeight = 1.0;
        auto now = time_point{};
        for (int i = 0; i < 10; ++i) {
            const auto result = algorithm->do_step_with_info(now);
            BOOST_REQUIRE(result.step_size > duration::zero());
            BOOST_REQUIRE(!result.termination_requested);
            const auto reached = now + result.step_size;
            const auto height = get_real(*source, ballRefs.at("h"));
            const auto observed = b.get_real(output);
            trace(name + "-fmi2-to-fmi3", "macro",
                to_double_time_point(now), to_double_time_point(now + to_duration(0.1)),
                to_double_time_point(reached), height, observed, previousHeight, 0,
                result.early_return, -1);
            BOOST_CHECK_SMALL(observed - previousHeight, 1e-8);
            previousHeight = height;
            now = reached;
        }
    }
}

BOOST_AUTO_TEST_CASE(stair_time_event_at_reported_early_endpoint_is_handled)
{
    for (const std::string& name : {std::string("fixed"), std::string("ecco")}) {
        warning_capture warnings;
        const auto importer = fmi::importer::create();
        const auto stair = std::static_pointer_cast<fmi::v3::fmu>(
            importer->import(fixture("REFERENCE_FMU_DIR", "Stair.fmu")));
        const auto legacy = importer->import(
            fixture("REFERENCE_FMU_V2_DIR", "Feedthrough.fmu"));
        const auto stairRefs = references(*stair->model_description());
        const auto legacyRefs = references(*legacy->model_description());
        fmi::v3::co_simulation_callbacks options;
        options.early_return_allowed = true;
        auto source = stair->instantiate_v3_slave(name + "-stair", options);
        auto receiver = legacy->instantiate_slave(name + "-stair-fmi2");
        slave_simulator a(source, name + "-stair");
        slave_simulator b(receiver, name + "-stair-fmi2");
        auto algorithm = make_algorithm(name, to_duration(0.6));
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, stairRefs.at("counter")},
            variable_id{1, variable_type::integer, legacyRefs.at("Int32_input")});
        b.expose_for_getting(variable_type::integer, legacyRefs.at("Int32_output"));
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        const auto first = algorithm->do_step_with_info(time_point{});
        BOOST_REQUIRE(first.step_size == to_duration(0.6));
        BOOST_CHECK_EQUAL(get_integer(*source, stairRefs.at("counter")), 1);
        trace(name + "-stair-fmi2", "macro", 0, 0.6, 0.6,
            1, b.get_integer(legacyRefs.at("Int32_output")), 0, 0,
            first.early_return, -1);
        const auto second = algorithm->do_step_with_info(to_time_point(0.6));
        BOOST_CHECK(second.step_size == to_duration(0.4));
        BOOST_CHECK_EQUAL(get_integer(*source, stairRefs.at("counter")), 2);
        BOOST_CHECK(warnings.messages().find("early return at its requested endpoint") !=
            std::string::npos);
        const auto third = algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_CHECK(third.step_size == to_duration(0.2));
    }
}

BOOST_AUTO_TEST_CASE(multirate_fmi2_state_capability_at_early_event)
{
    const auto importer = fmi::importer::create();
    const auto ball = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "BouncingBall.fmu")));
    const auto ballRefs = references(*ball->model_description());
    for (const bool stateful : {false, true}) {
        warning_capture warnings;
        const auto* path = std::getenv("FMI2_MIXED_TYPES_FMU");
        BOOST_REQUIRE(path != nullptr);
        const auto legacy = importer->import(stateful
            ? fixture("REFERENCE_FMU_V2_DIR", "Feedthrough.fmu")
            : filesystem::path(path));
        BOOST_TEST(legacy->model_description()->capabilities.can_save_state == stateful);
        const auto vars = references(*legacy->model_description());
        const auto input = vars.at(stateful ? "Float64_continuous_input" : "real_input");
        const auto output = vars.at(stateful ? "Float64_continuous_output" : "real_output");
        const auto scenario = stateful ? "multirate-stateful" : "multirate-no-state";
        auto source = ball->instantiate_slave(scenario);
        auto target = legacy->instantiate_slave(std::string(scenario) + "-fmi2");
        slave_simulator a(source, std::string(scenario) + "-ball");
        slave_simulator b(target, std::string(scenario) + "-fmi2");
        fixed_step_algorithm algorithm(to_duration(0.1), 1);
        algorithm.add_simulator(0, &a, duration::zero());
        algorithm.add_simulator(1, &b, duration::zero());
        algorithm.set_stepsize_decimation_factor(1, 3);
        algorithm.connect_variables(
            variable_id{0, variable_type::real, ballRefs.at("h")},
            variable_id{1, variable_type::real, input});
        b.expose_for_getting(variable_type::real, output);
        algorithm.setup(time_point{}, std::nullopt);
        algorithm.initialize();
        auto now = time_point{};
        bool sawImpact = false;
        while (now < to_time_point(0.9)) {
            const auto result = algorithm.do_step_with_info(now);
            BOOST_REQUIRE(result.step_size > duration::zero());
            BOOST_REQUIRE(!result.termination_requested);
            const auto reached = now + result.step_size;
            const auto height = get_real(*source, ballRefs.at("h"));
            const auto receiverInput = get_real(*target, input);
            const auto receiverOutput = get_real(*target, output);
            trace(scenario, "macro", to_double_time_point(now),
                to_double_time_point(now + to_duration(0.1)),
                to_double_time_point(reached), height, receiverInput,
                receiverOutput, result.finished.count(1) ? 1.0 : 0.0,
                result.early_return, -1);
            if (result.early_return &&
                std::abs(to_double_time_point(reached) - 0.453) < 1e-8) {
                sawImpact = true;
                BOOST_CHECK_EQUAL(result.finished.count(1), stateful ? 1u : 0u);
            }
            now = reached;
        }
        BOOST_CHECK(sawImpact);
        BOOST_CHECK(warnings.messages().find("does not support Event Mode") !=
            std::string::npos);
        BOOST_CHECK_EQUAL(
            warnings.messages().find("could not be rewound") != std::string::npos,
            !stateful);
        trace(scenario, "warnings", 0, 0, 0,
            warnings.messages().find("does not support Event Mode") != std::string::npos,
            warnings.messages().find("could not be rewound") != std::string::npos,
            stateful, 0, -1, -1);
    }
}

BOOST_AUTO_TEST_CASE(fmi2_values_are_sampled_at_fmi3_clock_ticks)
{
    const auto importer = fmi::importer::create();
    const auto clock = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "StrictClock.fmu")));
    const auto* path = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(path != nullptr);
    const auto legacy = importer->import(filesystem::path(path));
    const auto vars = references(*legacy->model_description());
    for (const std::string& name : {std::string("fixed"), std::string("ecco")}) {
        auto source = legacy->instantiate_slave(name + "-fmi2-clock-input");
        auto target = clock->instantiate_v3_slave(name + "-fmi3-clock-input");
        slave_simulator a(source, name + "-fmi2-clock-input");
        slave_simulator b(target, name + "-fmi3-clock-input");
        auto algorithm = make_algorithm(name, to_duration(1.0));
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        a.expose_for_setting(variable_type::integer, vars.at("integer_input"));
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, vars.at("integer_output")},
            variable_id{1, variable_type::integer, 204});
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        for (int step = 0; step < 3; ++step) {
            const int input = (step + 1) * 11;
            a.set_integer(vars.at("integer_input"), input);
            const auto now = to_time_point(static_cast<double>(step));
            const auto result = algorithm->do_step_with_info(now);
            BOOST_REQUIRE(result.step_size == to_duration(1.0));
            const auto snapshot = get_integer(*target, 205);
            trace(name + "-fmi2-clock-input", "tick",
                static_cast<double>(step), static_cast<double>(step + 1),
                static_cast<double>(step + 1), input, snapshot, input, 0,
                result.early_return, 1);
            BOOST_CHECK_EQUAL(snapshot, input);
        }
    }
}

BOOST_AUTO_TEST_CASE(clocked_fmi3_chain_with_fmi2_value_target)
{
    const auto importer = fmi::importer::create();
    const auto clock = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixture("REFERENCE_FMU_DIR", "StrictClock.fmu")));
    const auto legacy = importer->import(fixture("REFERENCE_FMU_V2_DIR", "Feedthrough.fmu"));
    const auto legacyRefs = references(*legacy->model_description());

    for (const std::string& name : {std::string("fixed"), std::string("ecco")}) {
        auto source = clock->instantiate_v3_slave(name + "-clock-source");
        auto target = clock->instantiate_v3_slave(name + "-clock-target");
        auto receiver = legacy->instantiate_slave(name + "-clock-fmi2");
        slave_simulator a(source, name + "-clock-source");
        slave_simulator b(target, name + "-clock-target");
        slave_simulator c(receiver, name + "-clock-fmi2");
        auto algorithm = make_algorithm(name, to_duration(0.5));
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::clock, 102}, variable_id{1, variable_type::clock, 101});
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, 201}, variable_id{1, variable_type::integer, 200});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, 202},
            variable_id{2, variable_type::integer, legacyRefs.at("Int32_input")});
        c.expose_for_getting(variable_type::integer, legacyRefs.at("Int32_output"));
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        auto now = time_point{};
        for (int i = 0; i < 6; ++i) {
            const auto result = algorithm->do_step_with_info(now);
            BOOST_REQUIRE(result.step_size > duration::zero());
            BOOST_REQUIRE(!result.termination_requested);
            const auto reached = now + result.step_size;
            const auto sourceSnapshot = get_integer(*source, 202);
            const auto targetSnapshot = get_integer(*target, 202);
            const auto output = c.get_integer(legacyRefs.at("Int32_output"));
            trace(name + "-clock-fmi2", "macro",
                to_double_time_point(now), to_double_time_point(now + to_duration(0.5)),
                to_double_time_point(reached), sourceSnapshot, targetSnapshot,
                output, 0, result.early_return, -1);
            BOOST_CHECK_EQUAL(targetSnapshot, sourceSnapshot + 1);
            now = reached;
        }
    }
}
