#include "fmi3_test_helpers.hpp"

BOOST_AUTO_TEST_CASE(fmi3_clocked_co_simulation)
{
    const auto fmuPath =
        reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(fmuPath);

    auto fmu = shared_clock_fixture(fmuPath);
    auto instance = fmu->instantiate_v3_slave("fmi3-clocked-cs");
    const auto references = variable_references(*fmu->model_description());

    const auto& clocks = instance->clock_descriptions();
    BOOST_REQUIRE_EQUAL(clocks.size(), 4U);
    BOOST_TEST(instance->event_mode_used());

    instance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    int input = 3;
    instance->set_integer_variables(
        gsl::make_span(&references.at("input2"), 1),
        gsl::make_span(&input, 1));
    instance->start_simulation();

    input = 7;
    BOOST_CHECK_EXCEPTION(
        instance->set_integer_variables(
            gsl::make_span(&references.at("input2"), 1),
            gsl::make_span(&input, 1)),
        cosim::error,
        error_is(errc::model_error));

    const value_reference triggeredInputClock = 1002;
    bool ticked = true;
    instance->set_clocks(
        gsl::make_span(&triggeredInputClock, 1),
        gsl::make_span(&ticked, 1));
    instance->set_integer_variables(
        gsl::make_span(&references.at("input2"), 1),
        gsl::make_span(&input, 1));

    int result = 0;
    instance->get_integer_variables(
        gsl::make_span(&references.at("result2"), 1),
        gsl::make_span(&result, 1));
    BOOST_TEST(result == 7);

    const auto firstUpdate = instance->update_discrete_states();
    BOOST_TEST(!firstUpdate.discrete_states_need_update);
    instance->set_clocks(
        gsl::make_span(&triggeredInputClock, 1),
        gsl::make_span(&ticked, 1));
    instance->get_integer_variables(
        gsl::make_span(&references.at("result2"), 1),
        gsl::make_span(&result, 1));
    BOOST_TEST(result == 7);

    const value_reference outputClock = 1005;
    bool outputTicked = true;
    instance->get_clocks(
        gsl::make_span(&outputClock, 1),
        gsl::make_span(&outputTicked, 1));
    BOOST_TEST(!outputTicked);

    BOOST_CHECK_EXCEPTION(
        instance->get_clocks(
            gsl::make_span(&triggeredInputClock, 1),
            gsl::make_span(&outputTicked, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() ==
                make_error_code(errc::invalid_system_structure);
        });
    BOOST_CHECK_EXCEPTION(
        instance->set_clocks(
            gsl::make_span(&outputClock, 1),
            gsl::make_span(&ticked, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() ==
                make_error_code(errc::invalid_system_structure);
        });

    ticked = false;
    BOOST_CHECK_EXCEPTION(
        instance->set_clocks(
            gsl::make_span(&triggeredInputClock, 1),
            gsl::make_span(&ticked, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() ==
                make_error_code(errc::unsupported_feature);
        });

    const value_reference periodicInputClock = 1001;
    ticked = true;
    instance->set_clocks(
        gsl::make_span(&periodicInputClock, 1),
        gsl::make_span(&ticked, 1));
    result = 0;
    instance->get_integer_variables(
        gsl::make_span(&references.at("inClock1Ticks"), 1),
        gsl::make_span(&result, 1));
    BOOST_TEST(result == 1);

    const auto update = instance->update_discrete_states();
    BOOST_TEST(!update.discrete_states_need_update);
    BOOST_CHECK_EXCEPTION(
        instance->set_clocks(
            gsl::make_span(&periodicInputClock, 1),
            gsl::make_span(&ticked, 1)),
        cosim::error,
        error_is(errc::model_error));
    BOOST_CHECK_EXCEPTION(
        instance->get_integer_variables(
            gsl::make_span(&references.at("result2"), 1),
            gsl::make_span(&result, 1)),
        cosim::error,
        error_is(errc::model_error));
    instance->enter_step_mode();

    BOOST_CHECK_EXCEPTION(
        instance->set_clocks(
            gsl::make_span(&triggeredInputClock, 1),
            gsl::make_span(&ticked, 1)),
        cosim::error,
        error_is(errc::model_error));

    BOOST_TEST(
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.1)).result ==
        step_result::complete);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(fmi3_strict_clock_fixture_checks_actual_activation)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    const auto start = [&fmu](const char* name) {
        auto instance = fmu->instantiate_v3_slave(name);
        instance->setup(to_time_point(0.0), to_time_point(2.0), std::nullopt);
        instance->start_simulation();
        return instance;
    };
    const value_reference inputClock = 101;
    const value_reference outputClock = 102;
    const value_reference input = 200;
    const value_reference output = 201;
    const value_reference snapshot = 202;
    const bool active = true;

    {
        auto instance = start("strict-clock-valid");
        for (const int value : {8, 12}) {
            instance->set_clocks(gsl::make_span(&inputClock, 1), gsl::make_span(&active, 1));
            instance->set_integer_variables(gsl::make_span(&input, 1), gsl::make_span(&value, 1));
            bool outputActive = false;
            instance->get_clocks(gsl::make_span(&outputClock, 1), gsl::make_span(&outputActive, 1));
            BOOST_REQUIRE(outputActive);
            int result = 0;
            instance->get_integer_variables(gsl::make_span(&output, 1), gsl::make_span(&result, 1));
            BOOST_TEST(result == value + 1);
            BOOST_CHECK(!instance->update_discrete_states().discrete_states_need_update);
            instance->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&result, 1));
            BOOST_TEST(result == value + 1);
            instance->enter_step_mode();
            if (value == 8) instance->enter_event_mode();
        }
    }

    {
        auto instance = start("strict-clock-inactive-input");
        const int value = 5;
        BOOST_CHECK_THROW(instance->set_integer_variables(
            gsl::make_span(&input, 1), gsl::make_span(&value, 1)), cosim::error);
    }
    {
        auto instance = start("strict-clock-inactive-output");
        instance->set_clocks(gsl::make_span(&inputClock, 1), gsl::make_span(&active, 1));
        bool outputActive = false;
        instance->get_clocks(gsl::make_span(&outputClock, 1), gsl::make_span(&outputActive, 1));
        BOOST_REQUIRE(outputActive);
        instance->update_discrete_states();
        int result = 0;
        BOOST_CHECK_THROW(instance->get_integer_variables(
            gsl::make_span(&output, 1), gsl::make_span(&result, 1)), cosim::error);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_strict_clock_data_is_transferred_during_real_activation)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto source = fmu->instantiate_v3_slave("strict-source");
        const auto target = fmu->instantiate_v3_slave("strict-target");
        slave_simulator sourceSim(source, "strict-source"), targetSim(target, "strict-target");
        algorithm->add_simulator(0, &sourceSim, duration::zero());
        algorithm->add_simulator(1, &targetSim, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::clock, 102},
            variable_id{1, variable_type::clock, 101});
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, 201},
            variable_id{1, variable_type::integer, 200});
        int modifierCalls = 0;
        targetSim.set_integer_input_modifier(200, [&modifierCalls](int value, duration) {
            ++modifierCalls;
            return value + 10;
        });
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        const auto beforeStep = modifierCalls;
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        BOOST_TEST(modifierCalls == beforeStep + 1);
        const value_reference snapshot = 202;
        int result = 0;
        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&result, 1));
        BOOST_TEST(result == 13);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_internal_step_time_does_not_replace_communication_point)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    const auto instance = fmu->instantiate_v3_slave("internal-time");
    instance->setup(to_time_point(0.0), std::nullopt, std::nullopt);
    instance->start_simulation();
    instance->handle_events();
    const auto firstEnd = time_point(duration(333333334));
    const auto first = instance->do_step_with_info(time_point{}, firstEnd.time_since_epoch());
    BOOST_REQUIRE(first.result == step_result::complete);
    BOOST_REQUIRE(!first.early_return);
    BOOST_REQUIRE(!first.event_handling_needed);
    BOOST_CHECK(first.last_successful_time < firstEnd);
    const auto saved = instance->save_state();
    const auto state = instance->export_state(saved);
    BOOST_TEST(state.get<std::int32_t>("scheme_version") == 4);
    BOOST_TEST(state.get<std::int64_t>("current_time_ticks") == firstEnd.time_since_epoch().count());
    std::stringstream stream;
    stream << serialization::format::cbor << state;
    serialization::node decoded;
    stream >> serialization::format::cbor >> decoded;
    auto legacy = decoded;
    legacy.put<std::int32_t>("scheme_version", 1);
    legacy.erase("current_time_ticks");
    const auto legacyState = instance->import_state(legacy);
    instance->release_state(legacyState);
    auto scheme2 = decoded;
    scheme2.put<std::int32_t>("scheme_version", 2);
    scheme2.erase("resolved_input_clocks");
    const auto scheme2State = instance->import_state(scheme2);
    instance->release_state(scheme2State);
    auto invalid = decoded;
    invalid.put<std::int64_t>("current_time_ticks", firstEnd.time_since_epoch().count() + 1);
    BOOST_CHECK_EXCEPTION(instance->import_state(invalid), cosim::error,
        error_is(errc::bad_file));
    const auto secondEnd = time_point(duration(666666667));
    const auto second = instance->do_step_with_info(firstEnd, secondEnd - firstEnd);
    BOOST_REQUIRE(second.result == step_result::complete);
    BOOST_REQUIRE(!second.early_return);
    BOOST_CHECK(second.last_successful_time < secondEnd);
    const auto imported = instance->import_state(decoded);
    instance->restore_state(imported);
    const auto repeated = instance->do_step_with_info(firstEnd, secondEnd - firstEnd);
    BOOST_REQUIRE(repeated.result == step_result::complete);
    BOOST_CHECK(repeated.last_successful_time == second.last_successful_time);
    instance->release_state(imported);
    instance->release_state(saved);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(zero_time_early_event_preserves_the_next_communication_step)
{
    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step)) {
        int calls = 0;
        int events = 0;
        auto instance = std::make_shared<detailed_step_mock_slave>(
            [&calls](time_point current, duration delta) {
                ++calls;
                if (calls == 2) {
                    return step_result_info{
                        step_result::complete, current, true, true, false, false};
                }
                return step_result_info{
                    step_result::complete, current + delta, false, false, false, false};
            },
            [&events] { ++events; });
        slave_simulator sim(instance, "zero-time-early-event");
        algorithm->add_simulator(0, &sim, duration::zero());
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        BOOST_CHECK(algorithm->do_step_with_info(time_point{}).step_size == step);
        BOOST_CHECK(algorithm->do_step_with_info(time_point(step)).step_size == duration::zero());
        BOOST_TEST(events == 1);
        BOOST_CHECK(algorithm->do_step_with_info(time_point(step)).step_size == step);
        BOOST_TEST(calls == 3);
    }
}

BOOST_AUTO_TEST_CASE(termination_requires_synchronized_communication_endpoints)
{
    const auto step = to_duration(1.0);
    for (const bool terminationIsEarly : {false, true}) {
        for (const bool synchronized : {false, true}) {
            for (const auto& algorithm : event_algorithms(step)) {
                auto terminating = std::make_shared<detailed_step_mock_slave>(
                    [terminationIsEarly](time_point current, duration delta) {
                        return step_result_info{
                            step_result::complete, current + delta / 2,
                            terminationIsEarly, false, true, false};
                    });
                const bool otherIsEarly = synchronized ? terminationIsEarly : !terminationIsEarly;
                auto other = std::make_shared<detailed_step_mock_slave>(
                    [otherIsEarly](time_point current, duration delta) {
                        return step_result_info{
                            step_result::complete,
                            current + (otherIsEarly ? delta / 2 : delta),
                            otherIsEarly, false, false, false};
                    });
                slave_simulator a(terminating, "terminating"), b(other, "other");
                algorithm->add_simulator(0, &a, duration::zero());
                algorithm->add_simulator(1, &b, duration::zero());
                algorithm->setup(time_point{}, std::nullopt);
                algorithm->initialize();
                if (synchronized) {
                    const auto result = algorithm->do_step_with_info(time_point{});
                    BOOST_TEST(result.termination_requested);
                    BOOST_CHECK(result.step_size == (terminationIsEarly ? step / 2 : step));
                } else {
                    BOOST_CHECK_EXCEPTION(algorithm->do_step_with_info(time_point{}), cosim::error,
                        error_is(errc::simulation_error));
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_termination_without_early_return_preserves_internal_time)
{
    const auto path = fault_fixture("StatusFault.fmu");
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto instance = fmu->instantiate_v3_slave("termination-after-step");
    instance->setup(time_point{}, std::nullopt, std::nullopt);
    const value_reference operation = 2;
    const int terminateAfterStep = 17;
    instance->set_integer_variables(
        gsl::make_span(&operation, 1), gsl::make_span(&terminateAfterStep, 1));
    instance->start_simulation();
    instance->handle_events();
    const auto result = instance->do_step_with_info(time_point{}, to_duration(0.35));
    BOOST_REQUIRE(result.result == step_result::complete);
    BOOST_TEST(result.termination_requested);
    BOOST_TEST(!result.early_return);
    BOOST_CHECK(result.last_successful_time < to_time_point(0.35));
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(repeated_zero_progress_steps_fail_and_restore_their_budget)
{
    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step, 2)) {
        auto instance = std::make_shared<detailed_step_mock_slave>(
            [](time_point current, duration) {
                return step_result_info{
                    step_result::complete, current, true, false, false, false};
            });
        slave_simulator sim(instance, "zero-progress");
        algorithm->add_simulator(0, &sim, duration::zero());
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        BOOST_CHECK(algorithm->do_step_with_info(time_point{}).step_size == duration::zero());
        const auto saved = algorithm->export_current_state();
        BOOST_TEST(saved.get<std::uint64_t>("event_router.zero_progress_steps") == 1U);
        BOOST_CHECK(algorithm->do_step_with_info(time_point{}).step_size == duration::zero());
        BOOST_CHECK_EXCEPTION(algorithm->do_step_with_info(time_point{}), cosim::error,
            error_is(errc::simulation_error));
        BOOST_CHECK_EXCEPTION(algorithm->do_step_with_info(time_point{}), cosim::error,
            [](const cosim::error& error) {
                return std::string(error.what()).find("restore a consistent execution snapshot") !=
                    std::string::npos;
            });
        BOOST_CHECK_EXCEPTION(algorithm->export_current_state(), cosim::error,
            error_is(errc::simulation_error));
        algorithm->import_state(saved);
        BOOST_CHECK(algorithm->do_step_with_info(time_point{}).step_size == duration::zero());
        BOOST_CHECK_EXCEPTION(algorithm->do_step_with_info(time_point{}), cosim::error,
            error_is(errc::simulation_error));
        auto invalid = saved;
        invalid.put<std::uint64_t>("event_router.zero_progress_steps", 3);
        BOOST_CHECK_EXCEPTION(algorithm->import_state(invalid), cosim::error,
            error_is(errc::bad_file));
        auto legacy = saved;
        legacy.put<std::uint32_t>("event_router.version", 1);
        legacy.get_child("event_router").erase("zero_progress_steps");
        algorithm->import_state(legacy);
        BOOST_TEST(algorithm->export_current_state().get<std::uint64_t>(
            "event_router.zero_progress_steps") == 0U);
        BOOST_CHECK(algorithm->do_step_with_info(time_point{}).step_size == duration::zero());
        BOOST_CHECK(algorithm->do_step_with_info(time_point{}).step_size == duration::zero());
        BOOST_CHECK_EXCEPTION(algorithm->do_step_with_info(time_point{}), cosim::error,
            error_is(errc::simulation_error));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_fractional_constant_clock_has_no_accumulated_drift)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto unpacked = utility::temp_dir();
    utility::zip::archive(path).extract_all(unpacked.path());
    rewrite_model_description(unpacked.path(),
        "intervalVariability=\"constant\" intervalDecimal=\"1\"",
        "intervalVariability=\"constant\" intervalDecimal=\"0.3333333333333333\" "
        "supportsFraction=\"true\" intervalCounter=\"1\" resolution=\"3\" "
        "shiftCounter=\"1\" shiftDecimal=\"0.3333333333333333\"");
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import_unpacked(unpacked.path()));
    const auto& variables = fmu->model_description()->variables;
    const auto clock = std::find_if(
        variables.begin(), variables.end(), [](const auto& variable) {
            return variable.reference == 100 && variable.type == variable_type::clock;
        });
    BOOST_REQUIRE(clock != variables.end());
    BOOST_REQUIRE(clock->clock);
    BOOST_TEST(clock->clock->supports_fraction);
    BOOST_REQUIRE(clock->clock->interval_counter);
    BOOST_REQUIRE(clock->clock->resolution);
    BOOST_REQUIRE(clock->clock->shift_counter);
    BOOST_TEST(*clock->clock->interval_counter == 1U);
    BOOST_TEST(*clock->clock->resolution == 3U);
    BOOST_TEST(*clock->clock->shift_counter == 1U);
    auto algorithms = event_algorithms(to_duration(1.0));
    const ecco_algorithm_params adaptiveParams{
        0.99, to_duration(1.0), duration(1), to_duration(1.0),
        0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    auto adaptive = std::make_shared<ecco_algorithm>(adaptiveParams, 1);
    algorithms.push_back(adaptive);
    for (const auto& algorithm : algorithms) {
        auto instance = fmu->instantiate_v3_slave("fractional-clock");
        slave_simulator sim(instance, "fractional-clock");
        auto first = std::make_shared<mock_slave>();
        auto second = std::make_shared<mock_slave>();
        slave_simulator firstSim(first, "power-first"), secondSim(second, "power-second");
        algorithm->add_simulator(0, &sim, duration::zero());
        if (algorithm == adaptive) {
            algorithm->add_simulator(1, &firstSim, duration::zero());
            algorithm->add_simulator(2, &secondSim, duration::zero());
            for (auto* powerSim : {&firstSim, &secondSim}) {
                powerSim->expose_for_setting(variable_type::real, mock_slave::real_in_reference);
            }
            adaptive->add_power_bond(
                variable_id{1, variable_type::real, mock_slave::real_in_reference},
                variable_id{1, variable_type::real, mock_slave::real_out_reference},
                variable_id{2, variable_type::real, mock_slave::real_in_reference},
                variable_id{2, variable_type::real, mock_slave::real_out_reference});
            firstSim.set_real(mock_slave::real_in_reference, 1.0);
            secondSim.set_real(mock_slave::real_in_reference, 2.0);
        }
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        time_point current{};
        for (std::int64_t tick = 1; tick <= 100; ++tick) {
            const auto expected = time_point(duration((tick * 1000000000LL + 2) / 3));
            const auto result = algorithm->do_step_with_info(current);
            BOOST_TEST(!result.termination_requested);
            BOOST_REQUIRE_EQUAL(result.step_size.count(), (expected - current).count());
            current += result.step_size;
            BOOST_REQUIRE(current == expected);
            const value_reference updates = 203;
            int count = 0;
            instance->get_integer_variables(gsl::make_span(&updates, 1), gsl::make_span(&count, 1));
            BOOST_TEST(count == tick);
        }
        if (algorithm == adaptive) {
            const auto firstEnergies = adaptive->get_powerbond_energies(0);
            const auto secondEnergies = adaptive->get_powerbond_energies(1);
            BOOST_REQUIRE_EQUAL(firstEnergies.size(), 99U);
            BOOST_REQUIRE_EQUAL(secondEnergies.size(), 99U);
            BOOST_TEST(std::accumulate(firstEnergies.begin(), firstEnergies.end(), 0.0)
                == 33.0, boost::test_tools::tolerance(1e-8));
            BOOST_TEST(std::accumulate(secondEnergies.begin(), secondEnergies.end(), 0.0)
                == 4.0 * 33.0, boost::test_tools::tolerance(1e-8));
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_decimal_clock_has_no_accumulated_drift)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fixed : {false, true}) {
        for (const bool shifted : {false, true}) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\" intervalDecimal=\"1\"",
                std::string("intervalVariability=\"") + (fixed ? "fixed" : "constant") +
                    "\" intervalDecimal=\"0.3333333333333333\"" +
                    (shifted ? " shiftDecimal=\"0.2\"" : ""));
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("decimal-clock");
                execution execution(to_time_point(10.0), algorithm);
                execution.add_slave(instance, "decimal-clock");
                execution.initialize();
                serialization::node checkpoint;
                duration checkpointNextStep = duration::zero();
                for (std::int64_t tick = 0; tick < 100; ++tick) {
                    const auto counter = shifted ? tick : tick + 1;
                    const auto expected = time_point(duration(
                        10000000000LL +
                        ((shifted ? 600000000LL : 0LL) + counter * 1000000000LL + 1) / 3));
                    const auto step = execution.step();
                    BOOST_REQUIRE_EQUAL(execution.current_time().time_since_epoch().count(),
                        expected.time_since_epoch().count());
                    if (tick == 3) checkpoint = execution.export_current_state();
                    if (tick == 4) checkpointNextStep = step;
                }
                execution.import_state(checkpoint);
                BOOST_CHECK(execution.step() == checkpointNextStep);

                auto badTick = algorithm->export_current_state();
                auto& badClock = badTick.get_child("clock_router")
                    .get_child("periodic_clocks").get_child("0");
                badClock.put<std::uint64_t>("tick", badClock.get<std::uint64_t>("tick") + 1);
                const auto beforeBadTick = algorithm->export_current_state();
                BOOST_CHECK_EXCEPTION(algorithm->import_state(badTick), cosim::error,
                    error_is(errc::bad_file));
                BOOST_CHECK(algorithm->export_current_state() == beforeBadTick);

                auto legacy = algorithm->export_current_state();
                auto& router = legacy.get_child("clock_router");
                router.put<std::uint32_t>("version", 1);
                auto& clock = router.get_child("periodic_clocks").get_child("0");
                const auto legacyStart = to_time_point(10.0) +
                    to_duration(shifted ? 0.2 : 0.0, 10.0);
                const auto legacyInterval = duration(clock.get<std::int64_t>("interval_ticks"));
                const auto legacyNext = legacyStart +
                    legacyInterval * static_cast<std::int64_t>(clock.get<std::uint64_t>("tick"));
                clock.put<std::int64_t>("next_ticks", legacyNext.time_since_epoch().count());
                router.put<std::int64_t>("next_event_ticks", legacyNext.time_since_epoch().count());
                clock.erase("origin_ticks");
                clock.erase("interval_decimal");
                clock.erase("shift_decimal");
                clock.erase("tick");
                clock.erase("legacy_decimal");
                algorithm->import_state(legacy);
                const auto migrated = algorithm->export_current_state();
                BOOST_TEST(migrated.get_child("clock_router").get_child("periodic_clocks")
                    .get_child("0").get<bool>("legacy_decimal"));
                algorithm->import_state(migrated);
                const auto untilLegacyNext = legacyNext - execution.current_time();
                BOOST_CHECK(execution.step() == untilLegacyNext);
                BOOST_CHECK(execution.current_time() == legacyNext);
                BOOST_CHECK(execution.step() == legacyInterval);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_fractional_clock_shift_and_range_validation)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    struct scenario
    {
        std::string attributes;
        std::vector<std::int64_t> ticks;
        int initialUpdates;
        bool rejected;
        std::string variability = "constant";
    };
    const std::vector<scenario> scenarios{
        {"intervalDecimal=\"0.3333333333333333\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"3\"",
            {333333334, 666666667, 1000000000}, 1, false},
        {"intervalDecimal=\"0.3333333333333333\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"3\" shiftCounter=\"1\"",
            {333333334, 666666667, 1000000000}, 0, false},
        {"intervalDecimal=\"0.3333333333333333\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"3\" shiftDecimal=\"0.25\"",
            {250000000, 583333334, 916666667}, 0, false},
        {"intervalDecimal=\"0.3333333333333333\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"3\" shiftCounter=\"1\" shiftDecimal=\"0.25\"",
            {}, 0, true},
        {"intervalDecimal=\"0.000000001\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"1000000000\" shiftCounter=\"1\"",
            {1, 2, 3}, 0, false},
        {"intervalDecimal=\"0.0000000005\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"2000000000\" shiftCounter=\"1\"",
            {}, 0, true},
        {"intervalDecimal=\"0.000000001\" shiftDecimal=\"0.000000001\"",
            {1, 2, 3}, 0, false},
        {"intervalDecimal=\"0.0000000005\" shiftDecimal=\"0.000000001\"",
            {}, 0, true},
        {"intervalDecimal=\"1\" shiftDecimal=\"10000000000\"",
            {}, 0, true},
        {"intervalDecimal=\"10000000000\"", {}, 0, true},
        {"intervalDecimal=\"1\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"1\" shiftCounter=\"10000000000\"",
            {}, 0, true},
        {"intervalDecimal=\"0.25\" shiftDecimal=\"0.125\"",
            {125000000, 375000000, 625000000}, 0, false, "fixed"},
        {"intervalDecimal=\"0.3333333333333333\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"3\" shiftCounter=\"1\"",
            {333333334, 666666667, 1000000000}, 0, false, "fixed"},
        {"intervalDecimal=\"0.0000000005\" supportsFraction=\"true\" "
         "intervalCounter=\"1\" resolution=\"2000000000\" shiftCounter=\"1\"",
            {}, 0, true, "fixed"}};
    const std::string original = "intervalVariability=\"constant\" intervalDecimal=\"1\"";
    for (const auto& test : scenarios) {
        BOOST_TEST_CONTEXT(test.variability << ": " << test.attributes) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(), original,
                "intervalVariability=\"" + test.variability + "\" " + test.attributes);
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("fractional-clock-range");
                slave_simulator sim(instance, "fractional-clock-range");
                algorithm->add_simulator(0, &sim, duration::zero());
                algorithm->setup(time_point{}, std::nullopt);
                if (test.rejected) {
                    BOOST_CHECK_EXCEPTION(
                        algorithm->initialize(), cosim::error,
                        error_is(errc::unsupported_feature));
                    continue;
                }
                algorithm->initialize();
                const value_reference updates = 203;
                int count = 0;
                instance->get_integer_variables(gsl::make_span(&updates, 1), gsl::make_span(&count, 1));
                BOOST_REQUIRE_EQUAL(count, test.initialUpdates);
                time_point current{};
                for (std::size_t i = 0; i < test.ticks.size(); ++i) {
                    const time_point expected{duration(test.ticks[i])};
                    const auto result = algorithm->do_step_with_info(current);
                    BOOST_REQUIRE_EQUAL(result.step_size.count(), (expected - current).count());
                    current += result.step_size;
                    instance->get_integer_variables(gsl::make_span(&updates, 1), gsl::make_span(&count, 1));
                    BOOST_TEST(count == test.initialUpdates + static_cast<int>(i) + 1);
                }
            }
            if (test.variability == "fixed" && !test.rejected) {
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-state");
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                instance->start_simulation();
                const value_reference clock = 100;
                const bool active = true;
                instance->set_clocks(gsl::make_span(&clock, 1), gsl::make_span(&active, 1));
                const auto saved = instance->save_state();
                const auto imported = instance->import_state(instance->export_state(saved));
                instance->restore_state(imported);
                instance->release_state(imported);
                instance->release_state(saved);
                instance->update_discrete_states();
                BOOST_CHECK_EXCEPTION(
                    instance->set_clocks(gsl::make_span(&clock, 1), gsl::make_span(&active, 1)),
                    cosim::error,
                    error_is(errc::model_error));
                const auto failed = fmu->instantiate_v3_slave("fixed-default-set-failure");
                failed->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference inputValue = 200;
                const int failSet = -8;
                failed->set_integer_variables(
                    gsl::make_span(&inputValue, 1), gsl::make_span(&failSet, 1));
                BOOST_CHECK_EXCEPTION(failed->start_simulation(), cosim::error,
                    error_is(errc::model_error));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_fractional_clock_state_resumes_the_same_schedule)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto unpacked = utility::temp_dir();
    utility::zip::archive(path).extract_all(unpacked.path());
    rewrite_model_description(unpacked.path(),
        "intervalVariability=\"constant\" intervalDecimal=\"1\"",
        "intervalVariability=\"constant\" intervalDecimal=\"0.3333333333333333\" "
        "supportsFraction=\"true\" intervalCounter=\"1\" resolution=\"3\" shiftCounter=\"1\"");
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import_unpacked(unpacked.path()));
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto instance = fmu->instantiate_v3_slave("fractional-clock-state");
        const auto target = fmu->instantiate_v3_slave("fractional-clock-state-target");
        execution execution(time_point{}, algorithm);
        const auto sourceIndex = execution.add_slave(instance, "fractional-clock-state");
        const auto targetIndex = execution.add_slave(target, "fractional-clock-state-target");
        execution.connect_variables(
            variable_id{sourceIndex, variable_type::clock, 102},
            variable_id{targetIndex, variable_type::clock, 101});
        execution.connect_variables(
            variable_id{sourceIndex, variable_type::integer, 201},
            variable_id{targetIndex, variable_type::integer, 200});
        execution.initialize();
        const auto initial = execution.export_current_state();
        BOOST_REQUIRE_EQUAL(execution.step().count(), 333333334);
        const value_reference snapshot = 202;
        int targetValue = 0;
        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&targetValue, 1));
        BOOST_TEST(targetValue == 2);
        const auto checkpoint = execution.export_current_state();
        auto invalid = algorithm->export_current_state();
        auto& clock = invalid.get_child("clock_router").get_child("periodic_clocks").get_child("0");
        clock.put<std::int64_t>("next_ticks", clock.get<std::int64_t>("next_ticks") + 1);
        const auto beforeInvalidImport = algorithm->export_current_state();
        BOOST_CHECK_EXCEPTION(algorithm->import_state(invalid), cosim::error,
            error_is(errc::bad_file));
        BOOST_CHECK(algorithm->export_current_state() == beforeInvalidImport);
        auto wrongConnection = beforeInvalidImport;
        wrongConnection.get_child("clock_router").get_child("clock_connections")
            .get_child("0").put<std::uint32_t>("source_reference", 101);
        BOOST_CHECK_EXCEPTION(algorithm->import_state(wrongConnection), cosim::error,
            error_is(errc::bad_file));
        BOOST_CHECK(algorithm->export_current_state() == beforeInvalidImport);
        std::stringstream stream;
        stream << serialization::format::cbor << checkpoint;
        serialization::node decoded;
        stream >> serialization::format::cbor >> decoded;
        const auto secondStep = execution.step();
        const auto thirdStep = execution.step();
        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&targetValue, 1));
        BOOST_TEST(targetValue == 4);
        const auto expected = execution.export_current_state();
        execution.import_state(decoded);
        BOOST_REQUIRE_EQUAL(execution.current_time().time_since_epoch().count(), 333333334);
        BOOST_CHECK(execution.step() == secondStep);
        BOOST_CHECK(execution.step() == thirdStep);
        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&targetValue, 1));
        BOOST_TEST(targetValue == 4);
        BOOST_CHECK(execution.export_current_state() == expected);
        execution.import_state(initial);
        BOOST_CHECK(execution.current_time() == time_point{});
        BOOST_REQUIRE_EQUAL(execution.step().count(), 333333334);
        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&targetValue, 1));
        BOOST_TEST(targetValue == 2);
        BOOST_CHECK(execution.export_current_state() == checkpoint);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_decimal_clock_state_resumes_the_same_schedule)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fixed : {false, true}) {
        const auto unpacked = utility::temp_dir();
        utility::zip::archive(path).extract_all(unpacked.path());
        if (fixed) {
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\"",
                "intervalVariability=\"fixed\"");
        }
        const auto importer = fmi::importer::create();
        const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
            importer->import_unpacked(unpacked.path()));
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            const auto source = fmu->instantiate_v3_slave("decimal-clock-state");
            const auto target = fmu->instantiate_v3_slave("decimal-clock-state-target");
            execution execution(time_point{}, algorithm);
            const auto sourceIndex = execution.add_slave(source, "decimal-clock-state");
            const auto targetIndex = execution.add_slave(target, "decimal-clock-state-target");
            execution.connect_variables(
                variable_id{sourceIndex, variable_type::clock, 102},
                variable_id{targetIndex, variable_type::clock, 101});
            execution.connect_variables(
                variable_id{sourceIndex, variable_type::integer, 201},
                variable_id{targetIndex, variable_type::integer, 200});
            execution.initialize();
            const auto initial = execution.export_current_state();
            BOOST_CHECK(execution.step() == to_duration(1.0));
            const auto checkpoint = execution.export_current_state();
            const auto beforeInvalidImport = algorithm->export_current_state();
            auto legacy = beforeInvalidImport;
            auto& legacyRouter = legacy.get_child("clock_router");
            legacyRouter.put<std::uint32_t>("version", 1);
            auto& legacyClock = legacyRouter.get_child("periodic_clocks").get_child("0");
            legacyClock.erase("origin_ticks");
            legacyClock.erase("interval_decimal");
            legacyClock.erase("shift_decimal");
            legacyClock.erase("tick");
            legacyClock.erase("legacy_decimal");
            algorithm->import_state(legacy);
            BOOST_TEST(algorithm->export_current_state().get_child("clock_router")
                .get_child("periodic_clocks").get_child("0").get<bool>("legacy_decimal"));
            algorithm->import_state(beforeInvalidImport);
            BOOST_CHECK(algorithm->export_current_state() == beforeInvalidImport);
            auto invalid = beforeInvalidImport;
            auto& clock = invalid.get_child("clock_router").get_child("periodic_clocks").get_child("0");
            clock.put<std::int64_t>("next_ticks", clock.get<std::int64_t>("next_ticks") + 1);
            BOOST_CHECK_EXCEPTION(algorithm->import_state(invalid), cosim::error,
                error_is(errc::bad_file));
            BOOST_CHECK(algorithm->export_current_state() == beforeInvalidImport);
            BOOST_CHECK(execution.step() == to_duration(1.0));
            BOOST_CHECK(execution.step() == to_duration(1.0));
            const auto expected = execution.export_current_state();
            execution.import_state(checkpoint);
            BOOST_CHECK(execution.step() == to_duration(1.0));
            BOOST_CHECK(execution.step() == to_duration(1.0));
            BOOST_CHECK(execution.export_current_state() == expected);
            execution.import_state(initial);
            BOOST_CHECK(execution.step() == to_duration(1.0));
            BOOST_CHECK(execution.export_current_state() == checkpoint);
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_changing_clock_schedule_and_state)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fractional : {false, true}) {
        BOOST_TEST_CONTEXT("fractional=" << fractional) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\" intervalDecimal=\"1\"",
                fractional ? "intervalVariability=\"changing\" supportsFraction=\"true\""
                           : "intervalVariability=\"changing\"");
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));

            const auto direct = fmu->instantiate_v3_slave("changing-clock-direct");
            direct->setup(time_point{}, std::nullopt, std::nullopt);
            direct->start_simulation();
            constexpr value_reference changingClock = 100;
            BOOST_CHECK_EXCEPTION(direct->get_input_clock_interval(changingClock),
                cosim::error, error_is(errc::model_error));
            const bool active = true;
            direct->set_clocks(gsl::make_span(&changingClock, 1), gsl::make_span(&active, 1));
            direct->update_discrete_states();
            const auto saved = direct->save_state();
            const auto imported = direct->import_state(direct->export_state(saved));
            for (const auto checkpoint : {saved, imported}) {
                direct->restore_state(checkpoint);
                BOOST_CHECK_EXCEPTION(direct->enter_step_mode(), cosim::error, error_is(errc::model_error));
                const auto interval = direct->get_input_clock_interval(changingClock);
                BOOST_CHECK(interval.qualifier == clock_interval_qualifier::changed ||
                    interval.qualifier == clock_interval_qualifier::unchanged);
                direct->enter_step_mode();
                direct->release_state(checkpoint);
            }
            direct->end_simulation();

            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("changing-clock");
                execution execution(time_point{}, algorithm);
                const auto index = execution.add_slave(instance, "changing-clock");
                execution.set_integer_initial_value(index, 200, -20);
                execution.initialize();
                const auto initial = execution.export_current_state();
                const auto first = fractional ? duration(333333334) : to_duration(0.25);
                const auto second = fractional ? duration(666666666) : to_duration(0.5);
                const auto third = fractional ? duration(666666667) : to_duration(0.5);
                BOOST_CHECK(execution.step() == first);
                const auto checkpoint = execution.export_current_state();
                const auto beforeInvalidImport = algorithm->export_current_state();
                auto invalid = beforeInvalidImport;
                auto& clock = invalid.get_child("clock_router").get_child("changing_clocks").get_child("0");
                clock.put<std::int64_t>("next_ticks", clock.get<std::int64_t>("next_ticks") + 1);
                BOOST_CHECK_EXCEPTION(algorithm->import_state(invalid), cosim::error,
                    error_is(errc::bad_file));
                BOOST_CHECK(algorithm->export_current_state() == beforeInvalidImport);
                invalid = beforeInvalidImport;
                invalid.get_child("clock_router").get_child("changing_clocks").get_child("0")
                    .put("origin_denominator", std::string("0"));
                BOOST_CHECK_EXCEPTION(algorithm->import_state(invalid), cosim::error,
                    error_is(errc::bad_file));
                BOOST_CHECK(algorithm->export_current_state() == beforeInvalidImport);
                BOOST_CHECK(execution.step() == second);
                BOOST_CHECK(execution.step() == third);
                const auto expected = execution.export_current_state();
                execution.import_state(checkpoint);
                BOOST_CHECK(execution.step() == second);
                BOOST_CHECK(execution.step() == third);
                BOOST_CHECK(execution.export_current_state() == expected);
                execution.import_state(initial);
                BOOST_CHECK(execution.step() == first);
                BOOST_CHECK(execution.export_current_state() == checkpoint);
                if (fractional) {
                    for (std::int64_t tick = 2; tick <= 30; ++tick) {
                        const auto expectedTicks = ((2 * tick - 1) * 1000000000LL + 2) / 3;
                        execution.step();
                        BOOST_CHECK_EQUAL(execution.current_time().time_since_epoch().count(), expectedTicks);
                    }
                }
            }

            for (const int invalid : {-1, -25}) {
                const auto instance = fmu->instantiate_v3_slave("invalid-changing-initial");
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference inputRef = 200;
                instance->set_integer_variables(
                    gsl::make_span(&inputRef, 1), gsl::make_span(&invalid, 1));
                BOOST_CHECK_THROW(instance->start_simulation(), cosim::error);
            }
            for (const int invalid : {-22, -23, -24}) {
                for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                    const auto instance = fmu->instantiate_v3_slave("invalid-changing-runtime");
                    execution execution(time_point{}, algorithm);
                    const auto index = execution.add_slave(instance, "invalid-changing-runtime");
                    execution.set_integer_initial_value(index, 200, invalid);
                    execution.initialize();
                    BOOST_CHECK_THROW(execution.step(), cosim::error);
                }
            }
            for (const auto& algorithm : event_algorithms(to_duration(1.0), 3)) {
                const auto instance = fmu->instantiate_v3_slave("zero-changing-clock");
                execution execution(time_point{}, algorithm);
                const auto index = execution.add_slave(instance, "zero-changing-clock");
                execution.set_integer_initial_value(index, 200, -21);
                BOOST_CHECK_EXCEPTION(execution.initialize(), cosim::error,
                    error_is(errc::simulation_error));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_countdown_clock_revises_and_preserves_schedules)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fractional : {false, true}) {
        BOOST_TEST_CONTEXT("fractional=" << fractional) {
            const auto targetDir = utility::temp_dir();
            const auto sourceDir = utility::temp_dir();
            utility::zip::archive(path).extract_all(targetDir.path());
            utility::zip::archive(path).extract_all(sourceDir.path());
            const std::string original = "intervalVariability=\"constant\" intervalDecimal=\"1\"";
            rewrite_model_description(targetDir.path(), original, fractional
                ? "intervalVariability=\"countdown\" supportsFraction=\"true\""
                : "intervalVariability=\"countdown\"");
            rewrite_model_description(sourceDir.path(), original,
                "intervalVariability=\"constant\" intervalDecimal=\"1\" shiftDecimal=\"0.5\"");
            const auto importer = fmi::importer::create();
            const auto sourceImporter = fmi::importer::create();
            const auto targetFmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(targetDir.path()));
            const auto sourceFmu = std::static_pointer_cast<fmi::v3::fmu>(
                sourceImporter->import_unpacked(sourceDir.path()));
            for (const int mode : {-30, -31, -35, -36}) {
                for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                    BOOST_TEST_CONTEXT("mode=" << mode) {
                        const auto source = sourceFmu->instantiate_v3_slave("countdown-source");
                        const auto target = targetFmu->instantiate_v3_slave("countdown-target");
                        execution execution(time_point{}, algorithm);
                        const auto sourceIndex = execution.add_slave(source, "countdown-source");
                        const auto targetIndex = execution.add_slave(target, "countdown-target");
                        execution.set_integer_initial_value(targetIndex, 200, mode);
                        execution.connect_variables(
                            variable_id{sourceIndex, variable_type::clock, 102},
                            variable_id{targetIndex, variable_type::clock, 101});
                        execution.initialize();
                        const auto first = mode == -30 ? to_duration(0.25) : to_duration(0.5);
                        BOOST_CHECK(execution.step() == first);
                        const auto checkpoint = execution.export_current_state();
                        const auto second = mode == -30 ? to_duration(0.25)
                            : mode == -35 ? to_duration(0.25) : to_duration(0.5);
                        BOOST_CHECK(execution.step() == second);
                        const auto expected = execution.export_current_state();
                        execution.import_state(checkpoint);
                        BOOST_CHECK(execution.step() == second);
                        BOOST_CHECK(execution.export_current_state() == expected);
                    }
                }
            }
            for (const int invalid : {-32, -33, -34}) {
                for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                    const auto instance = targetFmu->instantiate_v3_slave("invalid-countdown");
                    execution execution(time_point{}, algorithm);
                    const auto index = execution.add_slave(instance, "invalid-countdown");
                    execution.set_integer_initial_value(index, 200, invalid);
                    if (invalid == -34) {
                        execution.initialize();
                        BOOST_CHECK_THROW(execution.step(), cosim::error);
                    } else {
                        BOOST_CHECK_THROW(execution.initialize(), cosim::error);
                    }
                }
                for (const auto& algorithm : event_algorithms(to_duration(1.0), 3)) {
                    const auto instance = targetFmu->instantiate_v3_slave("zero-countdown-clock");
                    execution execution(time_point{}, algorithm);
                    const auto index = execution.add_slave(instance, "zero-countdown-clock");
                    execution.set_integer_initial_value(index, 200, -21);
                    BOOST_CHECK_EXCEPTION(execution.initialize(), cosim::error,
                        error_is(errc::simulation_error));
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_tunable_clock_fmu_controlled_intervals)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fractional : {false, true}) {
        BOOST_TEST_CONTEXT("fractional=" << fractional) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\" intervalDecimal=\"1\"",
                fractional ? "intervalVariability=\"tunable\" supportsFraction=\"true\""
                           : "intervalVariability=\"tunable\"");
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("tunable-clock");
                execution execution(time_point{}, algorithm);
                const auto index = execution.add_slave(instance, "tunable-clock");
                execution.set_integer_initial_value(index, 200, -20);
                execution.initialize();
                const auto first = fractional ? duration(250000000) : duration(125000000);
                const auto second = fractional ? duration(666666667) : duration(500000000);
                const auto third = fractional ? duration(666666667) : duration(500000000);
                BOOST_CHECK(execution.step() == first);
                const auto checkpoint = execution.export_current_state();
                BOOST_CHECK(execution.step() == second);
                BOOST_CHECK(execution.step() == third);
                const auto expected = execution.export_current_state();
                execution.import_state(checkpoint);
                BOOST_CHECK(execution.step() == second);
                BOOST_CHECK(execution.step() == third);
                BOOST_CHECK(execution.export_current_state() == expected);
            }
            for (const int invalid : {-22, -23, -24}) {
                for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                    const auto instance = fmu->instantiate_v3_slave("invalid-tunable-clock");
                    execution execution(time_point{}, algorithm);
                    const auto index = execution.add_slave(instance, "invalid-tunable-clock");
                    execution.set_integer_initial_value(index, 200, invalid);
                    execution.initialize();
                    const auto checkpoint = execution.export_current_state();
                    BOOST_CHECK_THROW(execution.step(), cosim::error);
                    BOOST_CHECK_EXCEPTION(execution.step(), cosim::error,
                        [](const cosim::error& e) {
                            return std::string(e.what()).find("restore an earlier execution state") != std::string::npos;
                        });
                    execution.import_state(checkpoint);
                    BOOST_CHECK(execution.export_current_state() == checkpoint);
                    BOOST_CHECK_THROW(execution.step(), cosim::error);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_tunable_clock_host_interval_changes_and_rollback)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fractional : {false, true}) {
        BOOST_TEST_CONTEXT("fractional=" << fractional) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\" intervalDecimal=\"1\"", fractional
                ? "intervalVariability=\"tunable\" intervalDecimal=\"1\" supportsFraction=\"true\""
                : "intervalVariability=\"tunable\" intervalDecimal=\"1\"");
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("host-tunable-clock");
                if (fractional) instance->configure_tunable_clock_fraction(100, 1, 1, 4);
                else instance->configure_tunable_clock_decimal(100, 1.0, 0.25);
                execution execution(time_point{}, algorithm);
                execution.add_slave(instance, "host-tunable-clock");
                execution.initialize();
                BOOST_CHECK(execution.step() == to_duration(0.25));
                if (fractional) instance->set_tunable_clock_interval_fraction(100, 2, 3);
                else instance->set_tunable_clock_interval_decimal(100, 0.5);
                BOOST_TEST(instance->has_pending_clock_intervals());
                BOOST_CHECK_THROW(instance->set_tunable_clock_interval_decimal(999, 0.5), cosim::error);
                BOOST_CHECK_THROW(instance->set_tunable_clock_interval_decimal(100, 0.0), std::invalid_argument);
                BOOST_CHECK_THROW(instance->set_tunable_clock_interval_fraction(100, 0, 3), std::invalid_argument);
                if (fractional) {
                    BOOST_CHECK_THROW(instance->set_tunable_clock_interval_decimal(100, 0.5), cosim::error);
                } else {
                    BOOST_CHECK_THROW(instance->set_tunable_clock_interval_fraction(100, 2, 3), cosim::error);
                }
                const auto saved = instance->save_state();
                const auto serialized = instance->export_state(saved);
                auto invalid = serialized;
                auto& pending = invalid.get_child("pending_clock_intervals").get_child("clock");
                if (fractional) pending.put<std::uint64_t>("resolution", 0);
                else pending.put<double>("interval_decimal", 0.0);
                BOOST_CHECK_EXCEPTION(instance->import_state(invalid), cosim::error,
                    error_is(errc::bad_file));
                invalid = serialized;
                invalid.erase("activated_periodic_clocks");
                BOOST_CHECK_EXCEPTION(instance->import_state(invalid), cosim::error,
                    error_is(errc::bad_file));
                instance->release_state(saved);
                const auto checkpoint = execution.export_current_state();
                const auto next = fractional ? duration(666666667) : to_duration(0.5);
                BOOST_CHECK(execution.step() == next);
                BOOST_TEST(!instance->has_pending_clock_intervals());
                const auto expected = execution.export_current_state();
                execution.import_state(checkpoint);
                BOOST_TEST(instance->has_pending_clock_intervals());
                BOOST_CHECK(execution.step() == next);
                BOOST_CHECK(execution.export_current_state() == expected);
                const auto schedule = instance->resolved_input_clock_schedule(100);
                BOOST_REQUIRE(schedule);
                if (fractional) {
                    BOOST_TEST(*schedule->interval_counter == 8U);
                    BOOST_TEST(*schedule->shift_counter == 3U);
                    BOOST_TEST(*schedule->resolution == 12U);
                } else {
                    BOOST_TEST(*schedule->interval_decimal == 0.5);
                    BOOST_TEST(*schedule->shift_decimal == 0.25);
                }
            }
            for (const auto& algorithm : event_algorithms(to_duration(0.2))) {
                const auto instance = fmu->instantiate_v3_slave("past-tunable-clock");
                if (fractional) instance->configure_tunable_clock_fraction(100, 4, 1, 4);
                else instance->configure_tunable_clock_decimal(100, 1.0, 0.25);
                execution execution(time_point{}, algorithm);
                execution.add_slave(instance, "past-tunable-clock");
                execution.initialize();
                BOOST_CHECK(execution.step() == to_duration(0.2));
                BOOST_CHECK(execution.step() == to_duration(0.05));
                BOOST_CHECK(execution.step() == to_duration(0.2));
                if (fractional) instance->set_tunable_clock_interval_fraction(100, 1, 10);
                else instance->set_tunable_clock_interval_decimal(100, 0.1);
                const auto beforeRejectedUpdate = execution.export_current_state();
                BOOST_CHECK_EXCEPTION(execution.step(), cosim::error,
                    error_is(errc::unsupported_feature));
                BOOST_CHECK(execution.export_current_state() == beforeRejectedUpdate);
                BOOST_TEST(instance->has_pending_clock_intervals());
                if (fractional) instance->set_tunable_clock_interval_fraction(100, 1, 2);
                else instance->set_tunable_clock_interval_decimal(100, 0.5);
                BOOST_CHECK(execution.step() == to_duration(0.2));
                BOOST_CHECK(execution.step() == to_duration(0.1));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_fixed_clock_schedules_are_resolved_during_initialization)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const bool fractional : {false, true}) {
        BOOST_TEST_CONTEXT("fractional=" << fractional) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\" intervalDecimal=\"1\"",
                fractional ? "intervalVariability=\"fixed\" supportsFraction=\"true\""
                           : "intervalVariability=\"fixed\"");
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("resolved-fixed-clock");
                execution execution(time_point{}, algorithm);
                execution.add_slave(instance, "resolved-fixed-clock");
                execution.initialize();
                const auto schedule = instance->resolved_input_clock_schedule(100);
                BOOST_REQUIRE(schedule);
                if (fractional) {
                    BOOST_TEST(*schedule->interval_counter == 4U);
                    BOOST_TEST(*schedule->shift_counter == 3U);
                    BOOST_TEST(*schedule->resolution == 12U);
                } else {
                    BOOST_TEST(*schedule->interval_decimal == 0.25);
                    BOOST_TEST(*schedule->shift_decimal == 0.125);
                }
                const std::vector<std::int64_t> ticks = fractional
                    ? std::vector<std::int64_t>{250000000, 583333334, 916666667}
                    : std::vector<std::int64_t>{125000000, 375000000, 625000000};
                auto previous = std::int64_t{0};
                for (const auto tick : ticks) {
                    BOOST_CHECK_EQUAL(execution.step().count(), tick - previous);
                    previous = tick;
                }
                const auto checkpoint = execution.export_current_state();
                const auto next = execution.step();
                const auto expected = execution.export_current_state();
                execution.import_state(checkpoint);
                BOOST_CHECK(execution.step() == next);
                BOOST_CHECK(execution.export_current_state() == expected);
                const auto saved = instance->save_state();
                const auto serialized = instance->export_state(saved);
                auto invalidState = serialized;
                auto& resolved = invalidState.get_child("resolved_input_clocks").get_child("clock");
                if (fractional) {
                    resolved.put<std::uint64_t>("resolution", 0);
                } else {
                    resolved.put<double>("shift_decimal", -1.0);
                }
                BOOST_CHECK_EXCEPTION(instance->import_state(invalidState), cosim::error,
                    error_is(errc::bad_file));
                instance->reset();
                BOOST_CHECK(!instance->resolved_input_clock_schedule(100));
                const auto restored = instance->import_state(serialized);
                instance->restore_state(restored);
                const auto restoredSchedule = instance->resolved_input_clock_schedule(100);
                BOOST_REQUIRE(restoredSchedule);
                BOOST_TEST(*restoredSchedule->interval_decimal == *schedule->interval_decimal);
                if (fractional) {
                    BOOST_TEST(*restoredSchedule->interval_counter == *schedule->interval_counter);
                    BOOST_TEST(*restoredSchedule->resolution == *schedule->resolution);
                    BOOST_TEST(*restoredSchedule->shift_counter == *schedule->shift_counter);
                } else {
                    BOOST_TEST(*restoredSchedule->shift_decimal == *schedule->shift_decimal);
                }
                instance->release_state(restored);
                instance->release_state(saved);
            }
            for (const int invalid : {-1, -2, -3, -4}) {
                const auto instance = fmu->instantiate_v3_slave("invalid-fixed-clock");
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&invalid, 1));
                BOOST_CHECK_EXCEPTION(instance->start_simulation(), cosim::error,
                    error_is(errc::model_error));
                BOOST_CHECK_EXCEPTION(instance->start_simulation(), cosim::error,
                    error_is(errc::model_error));
                instance->reset();
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                BOOST_CHECK_NO_THROW(instance->start_simulation());
            }
            {
                warning_capture warnings;
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-warnings");
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                const int warning = -7;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&warning, 1));
                instance->start_simulation();
                const auto intervalOperation = fractional ? "GetIntervalFraction" : "GetIntervalDecimal";
                const auto shiftOperation = fractional ? "GetShiftFraction" : "GetShiftDecimal";
                BOOST_TEST(warnings.messages().find(
                    std::string(intervalOperation) + " returned FMI warning") != std::string::npos);
                BOOST_TEST(warnings.messages().find(
                    std::string(shiftOperation) + " returned FMI warning") != std::string::npos);
                BOOST_TEST(warnings.messages().find("SetInterval") == std::string::npos);
            }
            {
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-query-only");
                BOOST_CHECK_EXCEPTION(instance->configure_fixed_clock_decimal(100, 1.0),
                    cosim::error, error_is(errc::invalid_system_structure));
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                const int failIfSet = -8;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&failIfSet, 1));
                BOOST_CHECK_NO_THROW(instance->start_simulation());
            }
            if (fractional) {
                for (const int valueToSet : {-5, -6}) {
                    const auto instance = fmu->instantiate_v3_slave("fixed-clock-fraction-range");
                    instance->setup(time_point{}, std::nullopt, std::nullopt);
                    const value_reference value = 200;
                    instance->set_integer_variables(
                        gsl::make_span(&value, 1), gsl::make_span(&valueToSet, 1));
                    if (valueToSet == -5) {
                        BOOST_CHECK_EXCEPTION(instance->start_simulation(), cosim::error,
                            error_is(errc::unsupported_feature));
                    } else {
                        instance->start_simulation();
                        const auto schedule = instance->resolved_input_clock_schedule(100);
                        BOOST_REQUIRE(schedule);
                        BOOST_TEST(*schedule->interval_counter == 1U);
                        BOOST_TEST(*schedule->shift_counter == 1U);
                        BOOST_TEST(*schedule->resolution == 1U);
                    }
                }
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-large-resolution");
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                const int largeShiftResolution = -11;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&largeShiftResolution, 1));
                instance->start_simulation();
                const auto saved = instance->save_state();
                const auto imported = instance->import_state(instance->export_state(saved));
                instance->restore_state(imported);
                const auto schedule = instance->resolved_input_clock_schedule(100);
                BOOST_REQUIRE(schedule);
                BOOST_TEST(*schedule->interval_counter == UINT64_C(9007199254739995));
                BOOST_TEST(*schedule->resolution == UINT64_C(27021597764219985));
                instance->release_state(imported);
                instance->release_state(saved);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_fixed_clock_configuration_sets_interval_and_shift)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    struct scenario
    {
        std::string attributes;
        bool supportsFraction;
        bool useFraction;
    };
    const std::vector<scenario> scenarios{
        {"intervalDecimal=\"1\"", false, false},
        {"intervalDecimal=\"1\" supportsFraction=\"true\" intervalCounter=\"1\" resolution=\"1\"",
            true, false},
        {"intervalDecimal=\"1\" supportsFraction=\"true\" intervalCounter=\"1\" resolution=\"1\"",
            true, true}};
    for (const auto& test : scenarios) {
        BOOST_TEST_CONTEXT(test.attributes << ", fraction=" << test.useFraction) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            rewrite_model_description(unpacked.path(),
                "intervalVariability=\"constant\" intervalDecimal=\"1\"",
                "intervalVariability=\"fixed\" " + test.attributes);
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            const auto configure = [&test](fmi::v3::slave_instance& instance) {
                if (test.useFraction) instance.configure_fixed_clock_fraction(100, 2, 1, 4);
                else instance.configure_fixed_clock_decimal(100, 0.5, 0.25);
            };
            {
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-cleared");
                configure(*instance);
                instance->clear_fixed_clock_configuration(100);
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                instance->start_simulation();
                const auto resolved = instance->resolved_input_clock_schedule(100);
                if (test.attributes.find("intervalDecimal") == std::string::npos) {
                    BOOST_REQUIRE(resolved);
                    BOOST_TEST(*resolved->interval_decimal == 1.0 / 3.0);
                } else {
                    BOOST_CHECK(!resolved);
                }
            }
            {
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-config-check");
                BOOST_CHECK_THROW(instance->configure_fixed_clock_decimal(100, 0.0), std::invalid_argument);
                BOOST_CHECK_THROW(instance->configure_fixed_clock_decimal(
                    100, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
                BOOST_CHECK_THROW(instance->configure_fixed_clock_decimal(100, 1.0, -0.1), std::invalid_argument);
                BOOST_CHECK_THROW(instance->configure_fixed_clock_decimal(101, 1.0), cosim::error);
                if (test.supportsFraction) {
                    BOOST_CHECK_THROW(instance->configure_fixed_clock_fraction(100, 0, 0, 1),
                        std::invalid_argument);
                    BOOST_CHECK_THROW(instance->configure_fixed_clock_fraction(100, 1, 0, 0),
                        std::invalid_argument);
                } else {
                    BOOST_CHECK_EXCEPTION(instance->configure_fixed_clock_fraction(100, 1, 0, 1),
                        cosim::error, error_is(errc::unsupported_feature));
                }
                configure(*instance);
                instance->clear_fixed_clock_configuration(100);
                configure(*instance);
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                const int checkArguments = -9;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&checkArguments, 1));
                instance->start_simulation();
                BOOST_CHECK_THROW(instance->configure_fixed_clock_decimal(100, 1.0), cosim::error);
                BOOST_CHECK_THROW(instance->clear_fixed_clock_configuration(100), cosim::error);
                instance->reset();
                BOOST_CHECK(!instance->resolved_input_clock_schedule(100));
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&checkArguments, 1));
                BOOST_CHECK_NO_THROW(instance->start_simulation());
            }
            {
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-shift-failure");
                configure(*instance);
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                const int failShift = -10;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&failShift, 1));
                BOOST_CHECK_EXCEPTION(instance->start_simulation(), cosim::error,
                    error_is(errc::model_error));
            }
            {
                warning_capture warnings;
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-set-warnings");
                configure(*instance);
                instance->setup(time_point{}, std::nullopt, std::nullopt);
                const value_reference value = 200;
                const int warning = -7;
                instance->set_integer_variables(
                    gsl::make_span(&value, 1), gsl::make_span(&warning, 1));
                instance->start_simulation();
                const auto shiftOperation = test.useFraction ? "SetShiftFraction" : "SetShiftDecimal";
                const auto intervalOperation = test.useFraction ? "SetIntervalFraction" : "SetIntervalDecimal";
                BOOST_TEST(warnings.messages().find(
                    std::string(shiftOperation) + " returned FMI warning") != std::string::npos);
                BOOST_TEST(warnings.messages().find(
                    std::string(intervalOperation) + " returned FMI warning") != std::string::npos);
            }
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto instance = fmu->instantiate_v3_slave("fixed-clock-configured");
                configure(*instance);
                execution execution(time_point{}, algorithm);
                execution.add_slave(instance, "fixed-clock-configured");
                execution.initialize();
                BOOST_CHECK_EQUAL(execution.step().count(), 250000000);
                const auto checkpoint = execution.export_current_state();
                BOOST_CHECK_EQUAL(execution.step().count(), 500000000);
                const auto expected = execution.export_current_state();
                execution.import_state(checkpoint);
                BOOST_CHECK_EQUAL(execution.step().count(), 500000000);
                BOOST_CHECK(execution.export_current_state() == expected);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_cyclic_clocked_data_is_rejected_before_activation)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        slave_simulator a(fmu->instantiate_v3_slave("cycle-a"), "cycle-a");
        slave_simulator b(fmu->instantiate_v3_slave("cycle-b"), "cycle-b");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::clock, 102},
            variable_id{1, variable_type::clock, 101});
        algorithm->connect_variables(
            variable_id{1, variable_type::clock, 102},
            variable_id{0, variable_type::clock, 101});
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, 201},
            variable_id{1, variable_type::integer, 200});
        algorithm->setup(to_time_point(0.0), std::nullopt);
        BOOST_CHECK_EXCEPTION(
            algorithm->initialize(), cosim::error,
            error_is(errc::unsupported_feature));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_changed_clocked_data_after_delivery_is_rejected)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        slave_simulator a(fmu->instantiate_v3_slave("multiclock-a"), "multiclock-a");
        slave_simulator b(fmu->instantiate_v3_slave("multiclock-b"), "multiclock-b");
        slave_simulator c(fmu->instantiate_v3_slave("multiclock-c"), "multiclock-c");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        algorithm->connect_variables(
            variable_id{1, variable_type::clock, 102},
            variable_id{2, variable_type::clock, 101});
        algorithm->connect_variables(
            variable_id{0, variable_type::clock, 102},
            variable_id{1, variable_type::clock, 101});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, 201},
            variable_id{2, variable_type::integer, 200});
        b.expose_for_setting(variable_type::integer, 200);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        b.set_integer(200, 4);
        BOOST_CHECK_EXCEPTION(
            algorithm->initialize(), cosim::error,
            [](const cosim::error& e) {
                return e.code() == make_error_code(errc::unsupported_feature) &&
                    std::string(e.what()).find("changed after its delivery") != std::string::npos;
            });
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_slave_simulator_capability)
{
    const auto fmuPath =
        reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(fmuPath);

    auto fmu = shared_clock_fixture(fmuPath);
    auto instance = fmu->instantiate_v3_slave("fmi3-clocked-simulator");
    const auto references = variable_references(*fmu->model_description());
    slave_simulator simulator(instance, "clocked");

    simulator.expose_for_setting(
        variable_type::integer, references.at("input2"));
    simulator.expose_for_getting(
        variable_type::integer, references.at("result2"));
    simulator.set_integer(references.at("input2"), 3);
    simulator.setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    simulator.start_simulation();

    auto* clocks = simulator.get_clock_controller();
    BOOST_REQUIRE(clocks != nullptr);
    BOOST_TEST(clocks->is_in_event_mode());

    constexpr value_reference triggeredInputClock = 1002;
    clocks->set_input_clock(triggeredInputClock, true);
    clocks->set_clocked_variable(
        variable_type::integer,
        references.at("input2"),
        variable_value(integer_values{{7}}));
    const auto firstResult = clocks->get_clocked_variable(
        variable_type::integer, references.at("result2"));
    BOOST_TEST(
        std::get<integer_values>(firstResult.values()).values[0] == 7);
    BOOST_TEST(
        !clocks->update_clock_state().discrete_states_need_update);

    clocks->set_input_clock(triggeredInputClock, true);
    const auto secondResult = clocks->get_clocked_variable(
        variable_type::integer, references.at("result2"));
    BOOST_TEST(
        std::get<integer_values>(secondResult.values()).values[0] == 7);
    BOOST_TEST(simulator.get_integer(references.at("result2")) == 7);
    BOOST_TEST(
        !clocks->update_clock_state().discrete_states_need_update);
    clocks->enter_step_mode();

    BOOST_TEST(
        simulator.do_step_with_info(
            to_time_point(0.0), to_duration(0.1)).result ==
        step_result::complete);
}


BOOST_AUTO_TEST_CASE(fmi3_clocked_input_modifier_reset)
{
    const auto fmuPath = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(fmuPath);

    auto fmu = shared_clock_fixture(fmuPath);
    auto instance = fmu->instantiate_v3_slave("clocked-modifier-reset");
    const auto references = variable_references(*fmu->model_description());
    const auto input = references.at("input2");
    slave_simulator simulator(instance, "clocked-modifier");
    simulator.expose_for_setting(variable_type::integer, input);
    simulator.set_integer(input, 3);
    simulator.setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    simulator.start_simulation();

    auto* clocks = simulator.get_clock_controller();
    BOOST_REQUIRE(clocks != nullptr);
    simulator.set_variable_input_modifier(
        variable_type::integer, input,
        [](const variable_value& original, duration) {
            const auto value = std::get<integer_values>(original.values()).values[0];
            return variable_value(integer_values{{value + 10}});
        });
    clocks->set_input_clock(1002, true);
    clocks->set_clocked_variable(
        variable_type::integer, input, variable_value(integer_values{{7}}));
    auto current = clocks->get_clocked_variable(variable_type::integer, input);
    BOOST_TEST(std::get<integer_values>(current.values()).values[0] == 17);
    simulator.set_variable_input_modifier(variable_type::integer, input, {});
    simulator.do_iteration();
    current = clocks->get_clocked_variable(variable_type::integer, input);
    BOOST_TEST(std::get<integer_values>(current.values()).values[0] == 7);
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_input_modifiers_run_once_per_activation)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    const auto instance = fmu->instantiate_v3_slave("clocked-input-modifier");
    slave_simulator simulator(instance, "clocked");
    constexpr value_reference input = 2006;
    constexpr value_reference output = 2005;
    constexpr value_reference clock = 1002;
    simulator.expose_for_setting(variable_type::integer, input);
    simulator.expose_for_getting(variable_type::integer, input);
    simulator.expose_for_getting(variable_type::integer, output);
    simulator.setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    simulator.start_simulation();

    int calls = 0;
    simulator.set_integer_input_modifier(input, [&calls](int value, duration) {
        ++calls;
        return value + 10;
    });
    BOOST_TEST(simulator.get_modified_integer_variables().count(input) == 1);
    simulator.set_integer(input, 7);
    auto* clocks = simulator.get_clock_controller();
    BOOST_REQUIRE(clocks != nullptr);
    clocks->set_input_clock(clock, true);
    BOOST_TEST(calls == 1);
    BOOST_TEST(std::get<integer_values>(
        clocks->get_clocked_variable(variable_type::integer, output).values()).values[0] == 17);
    BOOST_CHECK(!clocks->update_clock_state().discrete_states_need_update);

    simulator.set_integer(input, 9);
    clocks->set_input_clock(clock, true);
    BOOST_TEST(calls == 2);
    BOOST_TEST(std::get<integer_values>(
        clocks->get_clocked_variable(variable_type::integer, output).values()).values[0] == 36);
    BOOST_CHECK(!clocks->update_clock_state().discrete_states_need_update);
    clocks->set_input_clock(clock, true);
    BOOST_TEST(calls == 3);
    BOOST_TEST(std::get<integer_values>(
        clocks->get_clocked_variable(variable_type::integer, output).values()).values[0] == 55);
    BOOST_CHECK(!clocks->update_clock_state().discrete_states_need_update);
    simulator.set_integer(input, 12);
    simulator.set_integer(input, 13);
    clocks->set_input_clock(clock, true);
    BOOST_TEST(calls == 4);
    BOOST_TEST(std::get<integer_values>(
        clocks->get_clocked_variable(variable_type::integer, output).values()).values[0] == 78);
    BOOST_CHECK(!clocks->update_clock_state().discrete_states_need_update);
    simulator.set_integer(input, 1);
    simulator.set_integer_input_modifier(input, [&calls](int value, duration) {
        ++calls;
        return value + 20;
    });
    clocks->set_input_clock(clock, true);
    BOOST_TEST(calls == 5);
    BOOST_TEST(std::get<integer_values>(
        clocks->get_clocked_variable(variable_type::integer, output).values()).values[0] == 99);
    simulator.set_integer_output_modifier(input, [](int value, duration) { return value; });
    simulator.set_integer_input_modifier(input, {});
    BOOST_TEST(simulator.get_modified_integer_variables().count(input) == 1);
    simulator.set_integer_output_modifier(input, {});
    BOOST_TEST(simulator.get_modified_integer_variables().count(input) == 0);
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_input_modifier_with_multiple_associated_clocks)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto unpacked = utility::temp_dir();
    utility::zip::archive(path).extract_all(unpacked.path());
    rewrite_model_description(unpacked.path(),
        "name=\"input2\" valueReference=\"2006\" causality=\"input\" start=\"0\" clocks=\"1002\"",
        "name=\"input2\" valueReference=\"2006\" causality=\"input\" start=\"0\" clocks=\"1002 1003\"");

    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import_unpacked(unpacked.path()));
    slave_simulator simulator(fmu->instantiate_v3_slave("multiple-clocks"), "multiple-clocks");
    constexpr value_reference input = 2006;
    constexpr value_reference firstClock = 1002;
    constexpr value_reference secondClock = 1003;
    simulator.expose_for_setting(variable_type::integer, input);
    simulator.setup(to_time_point(0.0), {}, {});
    simulator.start_simulation();
    unsigned calls = 0;
    simulator.set_integer_input_modifier(input, [&calls](int value, duration) {
        ++calls;
        return value + 1;
    });
    auto* clocks = simulator.get_clock_controller();
    BOOST_REQUIRE(clocks != nullptr);
    clocks->set_input_clock(firstClock, true);
    BOOST_TEST(calls == 1);
    clocks->set_clocked_variable(variable_type::integer, input, variable_value(integer_values{{3}}));
    BOOST_TEST(calls == 2);
    clocks->set_input_clock(secondClock, true);
    BOOST_TEST(calls == 3);
    clocks->set_clocked_variable(variable_type::integer, input, variable_value(integer_values{{4}}));
    BOOST_TEST(calls == 4);
    clocks->update_clock_state();
    clocks->set_input_clock(firstClock, true);
    BOOST_TEST(calls == 5);
}

BOOST_AUTO_TEST_CASE(fmi3_clock_routing_fans_out)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    const auto step = to_duration(1.5);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<algorithm> algorithms[] = {
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        const auto first = fmu->instantiate_v3_slave("first");
        const auto second = fmu->instantiate_v3_slave("second");
        const auto third = fmu->instantiate_v3_slave("third");
        execution execution(to_time_point(0.0), algorithm);
        const auto source = execution.add_slave(first, "source");
        const auto target1 = execution.add_slave(second, "target1");
        const auto target2 = execution.add_slave(third, "target2");
        for (const auto target : {target1, target2}) {
            execution.connect_variables(
                variable_id{source, variable_type::clock, 1005},
                variable_id{target, variable_type::clock, 1002});
            execution.connect_variables(
                variable_id{source, variable_type::integer, 2001},
                variable_id{target, variable_type::integer, 2006});
        }
        for (int time = 1; time <= 3; ++time) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            BOOST_CHECK(execution.current_time() == to_time_point(time));
        }
        const value_reference snapshot = 2008;
        int result = 0;
        for (const auto& target : {second, third}) {
            target->get_integer_variables(
                gsl::make_span(&snapshot, 1), gsl::make_span(&result, 1));
            BOOST_TEST(result == 3);
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_outputs_feed_ordinary_inputs)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto ordinary = std::make_shared<mock_slave>();
        execution execution(time_point{}, algorithm);
        const auto source = execution.add_slave(fmu->instantiate_v3_slave("source"), "source");
        const auto target = execution.add_slave(ordinary, "ordinary");
        execution.connect_variables(
            variable_id{source, variable_type::integer, 2001},
            variable_id{target, variable_type::integer, mock_slave::integer_in_reference});
        execution.initialize();
        std::vector<int> received;
        for (int step = 1; step <= 4; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            const value_reference input = mock_slave::integer_in_reference;
            int value = -1;
            ordinary->get_integer_variables(
                gsl::make_span(&input, 1), gsl::make_span(&value, 1));
            received.push_back(value);
        }
        // The ordinary input follows the periodic Clock samples, one step behind.
        BOOST_TEST(received == (std::vector<int>{1, 2, 3, 4}));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_data_between_independently_scheduled_clocks)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto clockSource = fmu->instantiate_v3_slave("clock-source");
        const auto dataSource = fmu->instantiate_v3_slave("data-source");
        const auto target = fmu->instantiate_v3_slave("target");
        execution execution(time_point{}, algorithm);
        const auto clockIndex = execution.add_slave(clockSource, "clock-source");
        const auto dataIndex = execution.add_slave(dataSource, "data-source");
        const auto targetIndex = execution.add_slave(target, "target");
        execution.connect_variables(
            variable_id{clockIndex, variable_type::clock, 1005},
            variable_id{targetIndex, variable_type::clock, 1002});
        // No Clock connection joins data-source and target: the target
        // receives the latest sample whenever its own Clock activates.
        execution.connect_variables(
            variable_id{dataIndex, variable_type::integer, 2001},
            variable_id{targetIndex, variable_type::integer, 2006});
        execution.initialize();
        const value_reference snapshot = 2008;
        std::vector<int> results;
        for (int step = 1; step <= 10; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            int value = -1;
            target->get_integer_variables(
                gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
            results.push_back(value);
        }
        // The source Clock fires at t=2 and t=9; the target accumulates the
        // data source's tick count sampled at those instants (3 and 10).
        BOOST_TEST(results == (std::vector<int>{0, 3, 3, 3, 3, 3, 3, 3, 13, 13}));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_event_capable_outputs_feed_clocked_inputs)
{
    const auto clockedPath = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    const auto stairPath = reference_fmu_directory() / "DiscreteStair.fmu";
    require_fixture(clockedPath);
    require_fixture(stairPath);
    const auto importer = fmi::importer::create();
    const auto clocked = std::static_pointer_cast<fmi::v3::fmu>(importer->import(clockedPath));
    const auto stair = std::static_pointer_cast<fmi::v3::fmu>(importer->import(stairPath));
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto clockSource = clocked->instantiate_v3_slave("clock-source");
        const auto target = clocked->instantiate_v3_slave("target");
        execution execution(time_point{}, algorithm);
        const auto clockIndex = execution.add_slave(clockSource, "clock-source");
        const auto stairIndex = execution.add_slave(stair->instantiate_v3_slave("stair"), "stair");
        const auto targetIndex = execution.add_slave(target, "target");
        execution.connect_variables(
            variable_id{clockIndex, variable_type::clock, 1005},
            variable_id{targetIndex, variable_type::clock, 1002});
        execution.connect_variables(
            variable_id{stairIndex, variable_type::integer, 1},
            variable_id{targetIndex, variable_type::integer, 2006});
        execution.initialize();
        const value_reference snapshot = 2008;
        std::vector<int> results;
        for (int step = 1; step <= 4; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            int value = -1;
            target->get_integer_variables(
                gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
            results.push_back(value);
        }
        // The target's Clock activates at t=2 and receives the Stair counter
        // (1 + two time events) visible at that instant.
        BOOST_TEST(results == (std::vector<int>{0, 3, 3, 3}));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_function_between_event_source_and_clocked_input)
{
    const auto clockedPath = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    const auto stairPath = reference_fmu_directory() / "DiscreteStair.fmu";
    require_fixture(clockedPath);
    require_fixture(stairPath);
    const auto importer = fmi::importer::create();
    const auto clocked = std::static_pointer_cast<fmi::v3::fmu>(importer->import(clockedPath));
    const auto stair = std::static_pointer_cast<fmi::v3::fmu>(importer->import(stairPath));
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto target = clocked->instantiate_v3_slave("target");
        execution execution(time_point{}, algorithm);
        const auto clockIndex =
            execution.add_slave(clocked->instantiate_v3_slave("clock-source"), "clock-source");
        const auto stairIndex = execution.add_slave(stair->instantiate_v3_slave("stair"), "stair");
        const auto targetIndex = execution.add_slave(target, "target");
        const auto sum = execution.add_function(std::make_shared<vector_sum_function<int>>(1, 1));
        execution.connect_variables(
            variable_id{clockIndex, variable_type::clock, 1005},
            variable_id{targetIndex, variable_type::clock, 1002});
        execution.connect_variables(
            variable_id{stairIndex, variable_type::integer, 1},
            function_io_id{sum, variable_type::integer, vector_sum_function<int>::in_io_reference(0, 0)});
        execution.connect_variables(
            function_io_id{sum, variable_type::integer, vector_sum_function<int>::out_io_reference(0)},
            variable_id{targetIndex, variable_type::integer, 2006});
        execution.initialize();
        const value_reference snapshot = 2008;
        std::vector<int> results;
        for (int step = 1; step <= 4; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            int value = -1;
            target->get_integer_variables(
                gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
            results.push_back(value);
        }
        // The function path is brought up to date when the target's Clock
        // activates at t=2, so the target samples the Stair counter visible
        // at that instant.
        BOOST_TEST(
            results == (std::vector<int>{0, 3, 3, 3}), boost::test_tools::per_element());
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_output_feeds_ordinary_input_through_function)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto ordinary = std::make_shared<mock_slave>();
        execution execution(time_point{}, algorithm);
        const auto source = execution.add_slave(fmu->instantiate_v3_slave("source"), "source");
        const auto target = execution.add_slave(ordinary, "ordinary");
        const auto sum = execution.add_function(std::make_shared<vector_sum_function<int>>(1, 1));
        execution.connect_variables(
            variable_id{source, variable_type::integer, 2001},
            function_io_id{sum, variable_type::integer, vector_sum_function<int>::in_io_reference(0, 0)});
        execution.connect_variables(
            function_io_id{sum, variable_type::integer, vector_sum_function<int>::out_io_reference(0)},
            variable_id{target, variable_type::integer, mock_slave::integer_in_reference});
        execution.initialize();
        std::vector<int> received;
        for (int step = 1; step <= 4; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            const value_reference input = mock_slave::integer_in_reference;
            int value = -1;
            ordinary->get_integer_variables(
                gsl::make_span(&input, 1), gsl::make_span(&value, 1));
            received.push_back(value);
        }
        BOOST_TEST(received == (std::vector<int>{1, 2, 3, 4}));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_data_between_independent_clocks_through_function)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto target = fmu->instantiate_v3_slave("target");
        execution execution(time_point{}, algorithm);
        const auto clockIndex =
            execution.add_slave(fmu->instantiate_v3_slave("clock-source"), "clock-source");
        const auto dataIndex =
            execution.add_slave(fmu->instantiate_v3_slave("data-source"), "data-source");
        const auto targetIndex = execution.add_slave(target, "target");
        const auto sum = execution.add_function(std::make_shared<vector_sum_function<int>>(1, 1));
        execution.connect_variables(
            variable_id{clockIndex, variable_type::clock, 1005},
            variable_id{targetIndex, variable_type::clock, 1002});
        execution.connect_variables(
            variable_id{dataIndex, variable_type::integer, 2001},
            function_io_id{sum, variable_type::integer, vector_sum_function<int>::in_io_reference(0, 0)});
        execution.connect_variables(
            function_io_id{sum, variable_type::integer, vector_sum_function<int>::out_io_reference(0)},
            variable_id{targetIndex, variable_type::integer, 2006});
        execution.initialize();
        const value_reference snapshot = 2008;
        std::vector<int> results;
        for (int step = 1; step <= 10; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            int value = -1;
            target->get_integer_variables(
                gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
            results.push_back(value);
        }
        // The function is evaluated from the Clock-associated sample before
        // the target's Clock activates, as in the direct connection.
        BOOST_TEST(
            results == (std::vector<int>{0, 3, 3, 3, 3, 3, 3, 3, 13, 13}),
            boost::test_tools::per_element());
    }
}

BOOST_AUTO_TEST_CASE(fmi3_cyclic_clock_data_network_is_rejected)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        execution execution(time_point{}, algorithm);
        const auto a = execution.add_slave(fmu->instantiate_v3_slave("a"), "a");
        const auto b = execution.add_slave(fmu->instantiate_v3_slave("b"), "b");
        execution.connect_variables(
            variable_id{a, variable_type::clock, 1005},
            variable_id{b, variable_type::clock, 1002});
        execution.connect_variables(
            variable_id{b, variable_type::clock, 1005},
            variable_id{a, variable_type::clock, 1002});
        execution.connect_variables(
            variable_id{a, variable_type::integer, 2001},
            variable_id{b, variable_type::integer, 2006});
        BOOST_CHECK_EXCEPTION(
            execution.initialize(),
            cosim::error,
            [](const cosim::error& error) {
                return error.code() == make_error_code(errc::unsupported_feature) &&
                    std::string(error.what()).find("cyclic Clock network") != std::string::npos;
            });
    }
}

BOOST_AUTO_TEST_CASE(fmi3_normal_step_outputs_feed_clocked_inputs)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    for (const std::string variability : {"constant", "fixed", "tunable", "changing", "countdown"}) {
        BOOST_TEST_CONTEXT("variability=" << variability) {
            const auto unpacked = utility::temp_dir();
            utility::zip::archive(path).extract_all(unpacked.path());
            const auto xmlPath = unpacked.path() / "modelDescription.xml";
            std::ifstream input(xmlPath);
            BOOST_REQUIRE(input);
            auto xml = std::string(
                std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            input.close();
            if (variability != "constant") {
                const std::string original =
                    "intervalVariability=\"constant\" intervalDecimal=\"1\"";
                const auto position = xml.find(original);
                BOOST_REQUIRE(position != std::string::npos);
                auto replacement = "intervalVariability=\"" + variability + "\"";
                if (variability == "fixed") replacement += " intervalDecimal=\"1\"";
                if (variability == "tunable") replacement += " intervalDecimal=\"0.25\"";
                xml.replace(position, original.size(), replacement);
                std::ofstream output(xmlPath);
                BOOST_REQUIRE(output);
                output << xml;
                output.close();
                BOOST_REQUIRE(output);
            }
            const auto importer = fmi::importer::create();
            const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
                importer->import_unpacked(unpacked.path()));
            const auto interval = variability == "constant" || variability == "fixed"
                ? to_duration(1.0)
                : to_duration(0.25);
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                const auto source = std::make_shared<mock_slave>(
                    nullptr,
                    [](time_point time, duration, int) {
                        return static_cast<int>(time.time_since_epoch().count() / 250000000) * 10;
                    });
                const std::array<std::shared_ptr<fmi::v3::slave_instance>, 2> targets{
                    fmu->instantiate_v3_slave("one-sided-target-a"),
                    fmu->instantiate_v3_slave("one-sided-target-b")};
                execution execution(time_point{}, algorithm);
                const auto sourceIndex = execution.add_slave(source, "normal-source");
                for (std::size_t i = 0; i < targets.size(); ++i) {
                    const auto targetIndex = execution.add_slave(targets[i], "one-sided-target-" + std::to_string(i));
                    execution.connect_variables(
                        variable_id{sourceIndex, variable_type::integer, mock_slave::integer_out_reference},
                        variable_id{targetIndex, variable_type::integer, 204});
                }
                execution.initialize();
                const value_reference snapshot = 205;
                for (int step = 1; step <= 3; ++step) {
                    BOOST_CHECK(execution.step() == interval);
                    for (const auto& target : targets) {
                        int value = -1;
                        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
                        BOOST_TEST(value == static_cast<int>(execution.current_time().time_since_epoch().count() /
                            250000000) * 10);
                    }
                    if (step == 2) {
                        const auto saved = execution.export_current_state();
                        BOOST_CHECK(execution.step() == interval);
                        const auto expected = execution.export_current_state();
                        execution.import_state(saved);
                        BOOST_CHECK(execution.step() == interval);
                        BOOST_CHECK(execution.export_current_state() == expected);
                        break;
                    }
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_normal_step_outputs_feed_triggered_clock_inputs)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto source = std::make_shared<mock_slave>(
            nullptr,
            [](time_point time, duration, int) {
                return static_cast<int>(time.time_since_epoch().count() / 1000000000) * 10;
            });
        const auto clockSource = fmu->instantiate_v3_slave("clock-source");
        const auto target = fmu->instantiate_v3_slave("triggered-target");
        execution execution(time_point{}, algorithm);
        const auto sourceIndex = execution.add_slave(source, "normal-source");
        const auto clockIndex = execution.add_slave(clockSource, "clock-source");
        const auto targetIndex = execution.add_slave(target, "triggered-target");
        execution.connect_variables(
            variable_id{clockIndex, variable_type::clock, 102},
            variable_id{targetIndex, variable_type::clock, 101});
        execution.connect_variables(
            variable_id{sourceIndex, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{targetIndex, variable_type::integer, 200});
        execution.initialize();
        const value_reference snapshot = 202;
        for (int step = 1; step <= 3; ++step) {
            BOOST_CHECK(execution.step() == to_duration(1.0));
            int value = -1;
            target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
            BOOST_TEST(value == step * 10 + 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_inputs_sample_only_when_their_clock_ticks)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(0.25))) {
        int sourceReads = 0;
        const auto source = std::make_shared<mock_slave>(
            nullptr,
            [&sourceReads](time_point time, duration, int) {
                ++sourceReads;
                return static_cast<int>(time.time_since_epoch().count() / 250000000) * 10;
            });
        const auto target = fmu->instantiate_v3_slave("sampling-target");
        slave_simulator sourceSim(source, "source"), targetSim(target, "target");
        algorithm->add_simulator(0, &sourceSim, duration::zero());
        algorithm->add_simulator(1, &targetSim, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, 204});
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        int modifierCalls = 0;
        targetSim.set_integer_input_modifier(204, [&modifierCalls](int value, duration) {
            ++modifierCalls;
            return value + 7;
        });
        const value_reference snapshot = 205;
        for (int step = 1; step <= 8; ++step) {
            const auto readsBefore = sourceReads;
            BOOST_CHECK(algorithm->do_step_with_info(to_time_point((step - 1) * 0.25)).step_size ==
                to_duration(0.25));
            BOOST_TEST(sourceReads == readsBefore + 1);
            int value = -1;
            target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
            const auto activations = step / 4;
            BOOST_TEST(modifierCalls == activations);
            BOOST_TEST(value == (activations == 0 ? 0 : activations * 40 + 7));
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_clocked_inputs_do_not_sample_ahead_decimated_sources)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const bool throughFunction : {false, true}) {
        BOOST_TEST_CONTEXT("through function=" << throughFunction) {
            const auto algorithm = std::make_shared<fixed_step_algorithm>(to_duration(0.5));
            const auto source = std::make_shared<mock_slave>(
                nullptr,
                [](time_point time, duration, int) {
                    return static_cast<int>(to_double_time_point(time)) * 10;
                });
            const auto target = fmu->instantiate_v3_slave("decimated-sampling-target");
            execution execution(time_point{}, algorithm);
            const auto sourceIndex = execution.add_slave(source, "decimated-source");
            const auto targetIndex = execution.add_slave(target, "clock-target");
            algorithm->set_stepsize_decimation_factor(sourceIndex, 4);
            if (throughFunction) {
                const auto sum = execution.add_function(std::make_shared<vector_sum_function<int>>(1, 1));
                execution.connect_variables(
                    variable_id{sourceIndex, variable_type::integer, mock_slave::integer_out_reference},
                    function_io_id{sum, variable_type::integer, vector_sum_function<int>::in_io_reference(0, 0)});
                execution.connect_variables(
                    function_io_id{sum, variable_type::integer, vector_sum_function<int>::out_io_reference(0)},
                    variable_id{targetIndex, variable_type::integer, 204});
            } else {
                execution.connect_variables(
                    variable_id{sourceIndex, variable_type::integer, mock_slave::integer_out_reference},
                    variable_id{targetIndex, variable_type::integer, 204});
            }
            execution.initialize();
            const value_reference snapshot = 205;
            for (int step = 1; step <= 4; ++step) {
                BOOST_CHECK(execution.step() == to_duration(0.5));
                if (step % 2 != 0) continue;
                int value = -1;
                target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
                BOOST_TEST(value == (step == 2 ? 0 : 20));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_legacy_outputs_feed_clocked_inputs_after_normal_steps)
{
    const auto* testData = std::getenv("TEST_DATA_DIR");
    const auto* fmi2Path = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(testData != nullptr);
    BOOST_REQUIRE(fmi2Path != nullptr);
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto clockFmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const std::array<cosim::filesystem::path, 2> legacyPaths{
        cosim::filesystem::path(testData) / "fmi1" / "identity.fmu",
        cosim::filesystem::path(fmi2Path)};
    for (std::size_t i = 0; i < legacyPaths.size(); ++i) {
        const auto legacyFmu = importer->import(legacyPaths[i]);
        const value_reference input = i == 0 ? 0 : 3;
        const value_reference output = i == 0 ? 0 : 4;
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            const auto target = clockFmu->instantiate_v3_slave("legacy-clock-target");
            slave_simulator source(legacyFmu->instantiate_slave("legacy-clock-source"), "legacy-source");
            slave_simulator targetSim(target, "legacy-target");
            algorithm->add_simulator(0, &source, duration::zero());
            algorithm->add_simulator(1, &targetSim, duration::zero());
            source.expose_for_setting(variable_type::integer, input);
            algorithm->connect_variables(
                variable_id{0, variable_type::integer, output},
                variable_id{1, variable_type::integer, 204});
            algorithm->setup(time_point{}, std::nullopt);
            algorithm->initialize();
            const value_reference snapshot = 205;
            for (int step = 0; step < 2; ++step) {
                source.set_integer(input, (step + 1) * 11);
                BOOST_CHECK(algorithm->do_step_with_info(to_time_point(step)).step_size == to_duration(1.0));
                int value = -1;
                target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
                BOOST_TEST(value == (step + 1) * 11);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_normal_step_whole_values_feed_clocked_inputs)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    const variable_value expected(uint64_values{{9007199254740993ULL,
        (std::numeric_limits<std::uint64_t>::max)()}}, {2});
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto source = std::make_shared<whole_value_mock_slave>(expected);
        const auto target = fmu->instantiate_v3_slave("whole-clock-target");
        execution execution(time_point{}, algorithm);
        const auto sourceIndex = execution.add_slave(source, "whole-source");
        const auto targetIndex = execution.add_slave(target, "whole-target");
        execution.connect_variables(
            variable_id{sourceIndex, variable_type::uint64, whole_value_mock_slave::output_reference},
            variable_id{targetIndex, variable_type::uint64, 206});
        execution.initialize();
        const typed_value_reference snapshot{variable_type::uint64, 207};
        const auto check = [&]() {
            const auto values = target->get_variable_values(gsl::make_span(&snapshot, 1));
            BOOST_REQUIRE_EQUAL(values.size(), 1U);
            BOOST_CHECK(detail::event_values_equal(values.front(), expected));
        };
        check();
        BOOST_CHECK(execution.step() == to_duration(1.0));
        check();
        const auto saved = execution.export_current_state();
        BOOST_CHECK(execution.step() == to_duration(1.0));
        check();
        const auto after = execution.export_current_state();
        execution.import_state(saved);
        BOOST_CHECK(execution.step() == to_duration(1.0));
        check();
        BOOST_CHECK(execution.export_current_state() == after);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_disconnected_clocked_input_is_not_transferred_during_initialization)
{
    const auto path = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        const auto source = std::make_shared<mock_slave>(
            nullptr, [](time_point, duration, int) { return 42; });
        const auto target = fmu->instantiate_v3_slave("disconnected-target");
        slave_simulator sourceSim(source, "source"), targetSim(target, "target");
        algorithm->add_simulator(0, &sourceSim, duration::zero());
        algorithm->add_simulator(1, &targetSim, duration::zero());
        const variable_id input{1, variable_type::integer, 204};
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference}, input);
        algorithm->disconnect_variable(input);
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        const value_reference snapshot = 205;
        int value = -1;
        target->get_integer_variables(gsl::make_span(&snapshot, 1), gsl::make_span(&value, 1));
        BOOST_TEST(value == 0);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_unavailable_clocked_connection_is_rejected)
{
    const auto path = reference_fmu_directory() / "ClockedCoSimulation.fmu";
    require_fixture(path);
    const auto fmu = shared_clock_fixture(path);
    const auto step = to_duration(1.0);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<algorithm> algorithms[] = {
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        execution execution(to_time_point(0.0), algorithm);
        const auto source = execution.add_slave(
            fmu->instantiate_v3_slave("source"), "source");
        const auto target = execution.add_slave(
            fmu->instantiate_v3_slave("target"), "target");
        execution.connect_variables(
            variable_id{source, variable_type::clock, 1005},
            variable_id{target, variable_type::clock, 1002});
        execution.connect_variables(
            variable_id{source, variable_type::integer, 2005},
            variable_id{target, variable_type::integer, 2006});
        const auto unavailable = [](const cosim::error& error) {
            return error.code() == make_error_code(errc::unsupported_feature) &&
                std::string(error.what()).find(
                    "Clock-associated data could not be transferred") != std::string::npos;
        };
        BOOST_CHECK_EXCEPTION(
            [&] {
                for (int i = 0; i < 5; ++i) execution.step();
            }(),
            cosim::error,
            unavailable);
    }
}
