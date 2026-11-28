#include "fmi3_test_helpers.hpp"
#include "../src/cosim/event_value.hpp"

BOOST_AUTO_TEST_CASE(coupled_float_comparison_defaults_and_exact_override)
{
    const auto step = to_duration(1.0);
    fixed_step_algorithm fixed(step, 1);
    ecco_algorithm ecco(ecco_algorithm_params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15}, 1);
    const auto check = [](auto& algorithm) {
        const auto defaults = algorithm.event_value_tolerance();
        BOOST_TEST(defaults.absolute == 1e-10);
        BOOST_TEST(defaults.relative == 1e-10);
        BOOST_CHECK_THROW(algorithm.set_event_value_tolerance({-1.0, 0.0}), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm.set_event_value_tolerance({0.0,
            std::numeric_limits<double>::infinity()}), std::invalid_argument);
        algorithm.set_event_value_tolerance({0.0, 0.0});
        BOOST_TEST(algorithm.event_value_tolerance().absolute == 0.0);
        BOOST_TEST(algorithm.event_value_tolerance().relative == 0.0);
        algorithm.setup(time_point{}, std::nullopt);
        algorithm.initialize();
        BOOST_CHECK_THROW(algorithm.set_event_value_tolerance(defaults), cosim::error);
    };
    check(fixed);
    check(ecco);

    using detail::event_values_close;
    constexpr auto tolerance = default_event_value_tolerance;
    BOOST_TEST(event_values_close(0.0, 1e-10, {1e-10, 0.0}));
    BOOST_TEST(!event_values_close(0.0, std::nextafter(1e-10, 1.0), {1e-10, 0.0}));
    BOOST_TEST(event_values_close(1e6, 1e6 + 5e-5, tolerance));
    BOOST_TEST(!event_values_close(1e6, 1e6 + 2e-4, tolerance));
    BOOST_TEST(!event_values_close(1e6, 1e6 + 5e-5, {0.0, 0.0}));
    const variable_value floats(float32_values{{0.0f, 1.0f}}, {2});
    BOOST_TEST(event_values_close(floats, variable_value(float32_values{{5e-11f, 1.0f}}, {2}), tolerance));
    BOOST_TEST(!event_values_close(floats, variable_value(float32_values{{2e-10f, 1.0f}}, {2}), tolerance));
    BOOST_TEST(!event_values_close(variable_value(integer_values{{1}}),
        variable_value(integer_values{{2}}), {2.0, 2.0}));
    const auto inf = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    BOOST_TEST(event_values_close(inf, inf, tolerance));
    BOOST_TEST(!event_values_close(-inf, inf, tolerance));
    BOOST_TEST(!event_values_close(0.0, inf, tolerance));
    BOOST_TEST(event_values_close(nan, nan, tolerance));
}

BOOST_AUTO_TEST_CASE(coupled_float_tolerance_controls_feedback_rounds)
{
    struct float_event_slave : event_coordination_slave
    {
        explicit float_event_slave(double change) : change(change) {}
        cosim::model_description model_description() const override
        {
            auto model = event_coordination_slave::model_description();
            for (auto& variable : model.variables) {
                if (variable.type == variable_type::real &&
                    (variable.reference == real_in_reference || variable.reference == real_out_reference)) {
                    variable.variability = variable_variability::discrete;
                }
            }
            return model;
        }
        void get_real_variables(gsl::span<const value_reference> refs, gsl::span<double> values) const override
        {
            event_coordination_slave::get_real_variables(refs, values);
            for (std::size_t i = 0; i < refs.size(); ++i) {
                if (refs[i] == real_out_reference) values[i] = updates == 0 ? 0.0 : change;
            }
        }
        const double change;
    };
    const auto step = to_duration(1.0);
    for (const bool exact : {false, true}) {
        for (const double change : {5e-11, 2e-10}) {
            const auto tolerance = exact ? event_value_tolerance{0.0, 0.0} : default_event_value_tolerance;
            const std::vector<std::shared_ptr<algorithm>> algorithms{
                std::make_shared<fixed_step_algorithm>(fixed_step_algorithm_params{step, 100, tolerance}, 1),
                std::make_shared<ecco_algorithm>(ecco_algorithm_params{
                    0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15, 100, tolerance}, 1)};
            for (const auto& algorithm : algorithms) {
                auto slave = std::make_shared<float_event_slave>(change);
                slave_simulator sim(slave, "float-feedback");
                algorithm->add_simulator(0, &sim, duration::zero());
                algorithm->connect_variables(
                    variable_id{0, variable_type::real, mock_slave::real_out_reference},
                    variable_id{0, variable_type::real, mock_slave::real_in_reference});
                sim.expose_for_setting(variable_type::real, event_coordination_slave::tunable_reference);
                algorithm->setup(time_point{}, std::nullopt);
                algorithm->initialize();
                sim.set_real(event_coordination_slave::tunable_reference, 1.0);
                algorithm->do_step_with_info(time_point{});
                BOOST_TEST(slave->updates == (exact || change > 1e-10 ? 2 : 1));
                BOOST_TEST(sim.get_real(mock_slave::real_out_reference) == change);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(legacy_do_step_uses_co_simulation_for_dual_interface_fmus)
{
    const auto path = reference_fmu_directory() / "Dahlquist.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto scheduled = utility::temp_dir();
    utility::zip::archive(path).extract_all(scheduled.path());
    // The alternate declaration must not affect Co-Simulation selection.
    rewrite_model_description(scheduled.path(),
        "<ModelExchange\n    modelIdentifier=\"Dahlquist\"\n"
        "    canGetAndSetFMUState=\"true\"\n    canSerializeFMUState=\"true\"/>", "");
    rewrite_model_description(scheduled.path(), "<LogCategories>",
        "<ScheduledExecution modelIdentifier=\"Dahlquist\"/>\n\n  <LogCategories>");
    for (const auto& imported : {importer->import(path), importer->import_unpacked(scheduled.path())}) {
        const auto fmu = std::dynamic_pointer_cast<fmi::v3::fmu>(imported);
        BOOST_REQUIRE(fmu);
        fmi::v3::co_simulation_callbacks options;
        options.event_mode_used = false;
        const auto instance = fmu->instantiate_v3_slave("legacy-step", options);
        BOOST_TEST(!instance->early_return_allowed());
        instance->setup(time_point{}, std::nullopt, std::nullopt);
        instance->start_simulation();
        BOOST_CHECK(instance->do_step(time_point{}, to_duration(0.1)) == step_result::complete);
        instance->end_simulation();
    }
}

BOOST_AUTO_TEST_CASE(legacy_do_step_distinguishes_discard_and_termination)
{
    const auto unpacked = utility::temp_dir();
    utility::zip::archive(fault_fixture("StatusFault.fmu")).extract_all(unpacked.path());
    rewrite_model_description(unpacked.path(),
        "<Clock\n      name=\"inputClock\"\n      valueReference=\"1001\"\n"
        "      causality=\"input\"\n      intervalVariability=\"triggered\"/>", "");
    const auto importer = fmi::importer::create();
    const auto fmu = std::dynamic_pointer_cast<fmi::v3::fmu>(importer->import_unpacked(unpacked.path()));
    BOOST_REQUIRE(fmu);
    fmi::v3::co_simulation_callbacks options;
    options.event_mode_used = false;
    const auto instance = fmu->instantiate_v3_slave("legacy-cancel", options);
    instance->setup(time_point{}, std::nullopt, std::nullopt);
    instance->start_simulation();
    const value_reference faultRefs[] = {2, 1};
    const int discardStep[] = {16, 2};
    const int terminateStep[] = {17, 0};
    instance->set_integer_variables(faultRefs, discardStep);
    BOOST_CHECK(instance->do_step(time_point{}, to_duration(0.1)) == step_result::failed);
    instance->set_integer_variables(faultRefs, terminateStep);
    BOOST_CHECK(instance->do_step(time_point{}, to_duration(0.1)) == step_result::canceled);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(fmi3_nonconverging_initial_events_are_bounded)
{
    const auto* fixturePath = std::getenv("FMI3_NONCONVERGING_FMU");
    BOOST_REQUIRE(fixturePath != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixturePath));
    const auto instance = fmu->instantiate_v3_slave("nonconverging");
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    BOOST_CHECK_EXCEPTION(
        instance->start_simulation(),
        cosim::error,
        error_is(errc::simulation_error));
    const auto references = variable_references(*fmu->model_description());
    int counter = 0;
    instance->get_integer_variables(
        gsl::make_span(&references.at("counter"), 1), gsl::make_span(&counter, 1));
    BOOST_TEST(counter == default_max_event_iterations + 1);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(fmi3_initial_event_iteration_uses_configured_limit)
{
    const auto* fixturePath = std::getenv("FMI3_NONCONVERGING_FMU");
    BOOST_REQUIRE(fixturePath != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fixturePath));
    const auto references = variable_references(*fmu->model_description());
    const auto counterReference = references.at("counter");
    constexpr std::size_t limit = 3;

    {
        const auto instance = fmu->instantiate_v3_slave("standalone-limit");
        BOOST_CHECK_THROW(instance->set_max_event_iterations(0), std::invalid_argument);
        instance->set_max_event_iterations(limit);
        instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
        BOOST_CHECK_EXCEPTION(instance->start_simulation(), cosim::error,
            error_is(errc::simulation_error));
        int counter = 0;
        instance->get_integer_variables(
            gsl::make_span(&counterReference, 1), gsl::make_span(&counter, 1));
        BOOST_TEST(counter == limit + 1);
        BOOST_CHECK_THROW(instance->set_max_event_iterations(4), cosim::error);
        instance->end_simulation();
    }

    for (const auto& algorithm : event_algorithms(to_duration(1.0), limit)) {
        const auto instance = fmu->instantiate_v3_slave("algorithm-limit");
        slave_simulator simulator(instance, "algorithm-limit");
        algorithm->add_simulator(0, &simulator, duration::zero());
        algorithm->setup(to_time_point(0.0), to_time_point(1.0));
        BOOST_CHECK_EXCEPTION(algorithm->initialize(), cosim::error,
            error_is(errc::simulation_error));
        const auto requiresRecreation = [](const cosim::error& error) {
            return std::string(error.what()).find("failed initialization; recreate") !=
                std::string::npos;
        };
        BOOST_CHECK_EXCEPTION(algorithm->initialize(), cosim::error, requiresRecreation);
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(time_point{}), cosim::error, requiresRecreation);
        int counter = 0;
        instance->get_integer_variables(
            gsl::make_span(&counterReference, 1), gsl::make_span(&counter, 1));
        BOOST_TEST(counter == limit + 1);
        instance->end_simulation();
    }
}

BOOST_AUTO_TEST_CASE(fmi3_intermediate_callbacks_disable_only_automatic_replay)
{
    const auto path = reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.intermediate_update =
        [](fmi::v3::slave_instance&, fmi::v3::intermediate_update_info&) {};
    for (const bool withCallback : {false, true}) {
        const auto instance = withCallback
            ? fmu->instantiate_v3_slave("with-callback", callbacks)
            : fmu->instantiate_v3_slave("without-callback");
        slave_simulator simulator(instance, withCallback ? "with-callback" : "without-callback");
        simulator.setup(time_point{}, std::nullopt, std::nullopt);
        simulator.start_simulation();
        BOOST_TEST(simulator.supports_automatic_step_replay() == !withCallback);
        BOOST_TEST(simulator.apply_pending_step_inputs(to_duration(0.1)) == !withCallback);
        const auto saved = simulator.save_state();
        simulator.restore_state(saved);
        simulator.release_state(saved);
        instance->end_simulation();
    }
}


BOOST_AUTO_TEST_CASE(fmi3_advanced_event_behavior)
{
    const auto fmuPath = reference_fmu_directory() / "Stair.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    fmi::v3::co_simulation_callbacks options;
    options.early_return_allowed = true;
    auto instance = fmu->instantiate_v3_slave("fmi3-advanced-events", options);
    instance->setup(
        to_time_point(0.0), to_time_point(10.0), std::nullopt);
    instance->start_simulation();
    BOOST_REQUIRE(instance->next_event_time().has_value());
    BOOST_CHECK(*instance->next_event_time() == to_time_point(1.0));

    instance->enter_event_mode();
    BOOST_CHECK_EXCEPTION(
        instance->enter_step_mode(),
        cosim::error,
        error_is(errc::model_error));
    const auto initialUpdate = instance->update_discrete_states();
    BOOST_TEST(!initialUpdate.discrete_states_need_update);
    BOOST_REQUIRE(initialUpdate.next_event_time.has_value());
    BOOST_CHECK(*initialUpdate.next_event_time == to_time_point(1.0));
    instance->enter_step_mode();
    BOOST_CHECK_THROW(instance->update_discrete_states(), cosim::error);

    const auto firstStep =
        instance->do_step_with_info(to_time_point(0.0), to_duration(1.1));
    BOOST_TEST(firstStep.result == step_result::complete);
    BOOST_TEST(firstStep.event_handling_needed);
    BOOST_TEST(firstStep.early_return);
    BOOST_CHECK(firstStep.last_successful_time == to_time_point(1.0));
    const auto pendingEventState = instance->save_state();
    BOOST_CHECK_THROW(
        instance->do_step_with_info(to_time_point(1.0), to_duration(1.0)),
        cosim::error);
    BOOST_CHECK_THROW(instance->enter_configuration_mode(), cosim::error);
    instance->handle_events();
    BOOST_REQUIRE(instance->next_event_time().has_value());
    BOOST_CHECK(*instance->next_event_time() == to_time_point(2.0));
    instance->restore_state(pendingEventState);
    BOOST_CHECK_THROW(
        instance->do_step_with_info(to_time_point(1.0), to_duration(1.0)),
        cosim::error);
    instance->handle_events();
    BOOST_REQUIRE(instance->next_event_time().has_value());
    BOOST_CHECK(*instance->next_event_time() == to_time_point(2.0));
    instance->release_state(pendingEventState);

    for (int second = 2; second <= 9; ++second) {
        const auto result = instance->do_step_with_info(
            to_time_point(second - 1.0), to_duration(1.1));
        BOOST_TEST(result.result == step_result::complete);
        BOOST_TEST(result.event_handling_needed);
        BOOST_TEST(result.early_return);
        BOOST_CHECK(result.last_successful_time == to_time_point(second));
        instance->handle_events();
    }
    BOOST_CHECK_THROW(
        instance->do_step_with_info(to_time_point(9.0), to_duration(1.0)),
        cosim::error);
    BOOST_CHECK_THROW(instance->enter_step_mode(), cosim::error);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(nonclock_event_controller_is_available_through_simulator)
{
    const auto path = reference_fmu_directory() / "Stair.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto instance = fmu->instantiate_v3_slave("event-controller");
    const auto references = variable_references(*fmu->model_description());
    BOOST_REQUIRE(instance->get_event_controller() != nullptr);
    BOOST_TEST(instance->get_clock_controller() == nullptr);
    slave_simulator simulator(instance, "event-controller");
    auto* controller = simulator.get_event_controller();
    BOOST_REQUIRE(controller != nullptr);
    BOOST_TEST(simulator.get_clock_controller() == nullptr);
    simulator.expose_for_getting(variable_type::integer, references.at("counter"));
    simulator.setup(to_time_point(0.0), to_time_point(2.0), {});
    simulator.start_simulation();
    BOOST_TEST(!controller->is_in_event_mode());
    controller->enter_event_mode();
    BOOST_TEST(controller->is_in_event_mode());
    const auto result = controller->update_event_state();
    BOOST_TEST(!result.discrete_states_need_update);
    BOOST_TEST(!result.terminate_simulation);
    BOOST_REQUIRE(result.next_event_time);
    BOOST_CHECK(*result.next_event_time == to_time_point(1.0));
    controller->enter_step_mode();
    BOOST_TEST(!controller->is_in_event_mode());
    BOOST_TEST(simulator.get_integer(references.at("counter")) == 1);
    const auto step = simulator.do_step_with_info(to_time_point(0.0), to_duration(1.1));
    BOOST_TEST(step.event_handling_needed);
    controller->enter_event_mode();
    const auto secondUpdate = controller->update_event_state();
    BOOST_TEST(!secondUpdate.terminate_simulation);
    simulator.refresh_event_values();
    BOOST_TEST(simulator.get_integer(references.at("counter")) == 2);
    controller->enter_step_mode();

    const auto noEventPath = reference_fmu_directory() / "Dahlquist.fmu";
    require_fixture(noEventPath);
    const auto noEventFmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(noEventPath));
    const auto noEventInstance = noEventFmu->instantiate_v3_slave("no-event-controller");
    BOOST_TEST(noEventInstance->get_event_controller() == nullptr);
    slave_simulator noEventSimulator(noEventInstance, "no-event-controller");
    BOOST_TEST(noEventSimulator.get_event_controller() == nullptr);

    auto legacy = std::make_shared<mock_slave>();
    BOOST_TEST(legacy->get_event_controller() == nullptr);
    slave_simulator legacySimulator(legacy, "legacy");
    BOOST_TEST(legacySimulator.get_event_controller() == nullptr);
}

BOOST_AUTO_TEST_CASE(might_return_early_capability_is_read_from_the_fmu)
{
    const auto importer = fmi::importer::create();
    const auto stairPath = reference_fmu_directory() / "Stair.fmu";
    require_fixture(stairPath);
    const auto stair = importer->import(stairPath);
    BOOST_TEST(stair->model_description()->capabilities.might_return_early);

    const auto strictClockPath = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(strictClockPath);
    const auto strictClock = importer->import(strictClockPath);
    BOOST_TEST(!strictClock->model_description()->capabilities.might_return_early);
}

BOOST_AUTO_TEST_CASE(variable_communication_step_capability_is_imported_for_all_fmi_versions)
{
    const auto* testData = std::getenv("TEST_DATA_DIR");
    const auto* fmi2 = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(testData != nullptr);
    BOOST_REQUIRE(fmi2 != nullptr);
    const std::array<cosim::filesystem::path, 3> paths{
        cosim::filesystem::path(testData) / "fmi1" / "identity.fmu",
        cosim::filesystem::path(fmi2),
        reference_fmu_directory() / "StrictClock.fmu"};
    const std::string attribute = "canHandleVariableCommunicationStepSize=\"true\"";
    for (const auto& path : paths) {
        for (const std::string value : {"true", "false", "absent"}) {
            BOOST_TEST_CONTEXT(path.string() << " capability=" << value) {
                utility::temp_dir unpacked;
                utility::zip::archive(path).extract_all(unpacked.path());
                const auto xmlPath = unpacked.path() / "modelDescription.xml";
                std::ifstream input(xmlPath);
                BOOST_REQUIRE(input);
                auto xml = std::string(
                    std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
                input.close();
                const auto position = xml.find(attribute);
                BOOST_REQUIRE(position != std::string::npos);
                xml.replace(position, attribute.size(), value == "absent"
                    ? std::string{}
                    : "canHandleVariableCommunicationStepSize=\"" + value + "\"");
                std::ofstream output(xmlPath);
                BOOST_REQUIRE(output);
                output << xml;
                output.close();
                BOOST_REQUIRE(output);
                const auto importer = fmi::importer::create();
                const auto description = importer->import_unpacked(unpacked.path())->model_description();
                BOOST_TEST(description->capabilities.can_handle_variable_step == (value == "true"));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(event_mode_can_be_disabled_per_instance)
{
    const auto importer = fmi::importer::create();
    const auto stairPath = reference_fmu_directory() / "Stair.fmu";
    require_fixture(stairPath);
    const auto stair = std::static_pointer_cast<fmi::v3::fmu>(importer->import(stairPath));

    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.event_mode_used = false;
    const auto instance = stair->instantiate_v3_slave("no-event-mode", callbacks);
    BOOST_TEST(!instance->event_mode_used());
    BOOST_TEST(instance->get_event_controller() == nullptr);
    slave_simulator simulator(instance, "no-event-mode");
    BOOST_TEST(simulator.get_event_controller() == nullptr);

    simulator.setup(to_time_point(0.0), to_time_point(2.0), {});
    simulator.start_simulation();
    const auto step = simulator.do_step_with_info(to_time_point(0.0), to_duration(0.1));
    BOOST_TEST(step.result == step_result::complete);
    BOOST_TEST(!step.event_handling_needed);

    const auto defaultInstance = stair->instantiate_v3_slave("default-event-mode");
    BOOST_TEST(defaultInstance->event_mode_used());
    BOOST_TEST(defaultInstance->get_event_controller() != nullptr);

    const auto clockedPath = reference_fmu_directory() / "StrictClock.fmu";
    require_fixture(clockedPath);
    const auto clocked = std::static_pointer_cast<fmi::v3::fmu>(importer->import(clockedPath));
    BOOST_CHECK_EXCEPTION(
        clocked->instantiate_v3_slave("clock-no-event-mode", callbacks),
        cosim::error,
        [](const cosim::error& e) {
            return e.code() == make_error_code(errc::unsupported_feature) &&
                std::string(e.what()).find("disabled for this instance") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(event_controller_does_not_read_values_after_termination)
{
    struct terminating_slave : mock_slave, event_controller
    {
        event_controller* get_event_controller() noexcept override { return this; }
        bool is_in_event_mode() const noexcept override { return inEventMode; }
        void enter_event_mode() override { inEventMode = true; }
        event_update_result update_event_state() override
        {
            terminated = true;
            return {false, true, std::nullopt};
        }
        void enter_step_mode() override { inEventMode = false; }
        void get_integer_variables(
            gsl::span<const value_reference> variables,
            gsl::span<int> values) const override
        {
            if (terminated) throw std::logic_error("Read after termination");
            mock_slave::get_integer_variables(variables, values);
        }

        bool inEventMode = false;
        bool terminated = false;
    };

    auto slave = std::make_shared<terminating_slave>();
    slave_simulator simulator(slave, "terminating-event-controller");
    simulator.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
    simulator.setup(to_time_point(0.0), to_time_point(1.0), {});
    simulator.start_simulation();
    auto* controller = simulator.get_event_controller();
    BOOST_REQUIRE(controller != nullptr);
    controller->enter_event_mode();
    const auto result = controller->update_event_state();
    BOOST_TEST(result.terminate_simulation);
}

BOOST_AUTO_TEST_CASE(clock_controller_retains_event_updates_without_optional_accessor)
{
    struct legacy_clock_slave : mock_slave, clock_controller
    {
        clock_controller* get_clock_controller() noexcept override { return this; }
        bool is_in_event_mode() const noexcept override { return inEventMode; }
        void enter_event_mode() override { inEventMode = true; }
        bool get_output_clock(value_reference) override { return false; }
        void set_input_clock(value_reference, bool) override {}
        variable_value get_clocked_variable(variable_type, value_reference) override
        {
            throw std::logic_error("Unexpected Clock value read");
        }
        void set_clocked_variable(
            variable_type, value_reference, const variable_value&) override
        {
            throw std::logic_error("Unexpected Clock value write");
        }
        clock_update_result update_clock_state() override
        {
            ++updates;
            return {};
        }
        void enter_step_mode() override { inEventMode = false; }

        bool inEventMode = false;
        int updates = 0;
    };

    auto slave = std::make_shared<legacy_clock_slave>();
    slave_simulator simulator(slave, "legacy-clock-controller");
    simulator.setup(to_time_point(0.0), to_time_point(1.0), {});
    simulator.start_simulation();
    BOOST_TEST(simulator.get_event_controller() == nullptr);
    auto* controller = simulator.get_clock_controller();
    BOOST_REQUIRE(controller != nullptr);
    controller->enter_event_mode();
    const auto result = controller->update_event_state();
    BOOST_TEST(!result.terminate_simulation);
    BOOST_TEST(slave->updates == 1);
    controller->enter_step_mode();
}

BOOST_AUTO_TEST_CASE(staged_input_events_require_event_mode)
{
    struct strict_event_slave : mock_slave, event_controller
    {
        event_controller* get_event_controller() noexcept override { return this; }
        bool is_in_event_mode() const noexcept override { return inEventMode; }
        void enter_event_mode() override { inEventMode = true; ++entries; }
        event_update_result update_event_state() override { return {}; }
        void enter_step_mode() override { inEventMode = false; }
        void start_simulation() override { running = true; }
        cosim::model_description model_description() const override
        {
            auto description = mock_slave::model_description();
            for (auto& variable : description.variables) {
                if (variable.type == variable_type::real &&
                    variable.reference == real_in_reference) {
                    variable.variability = variable_variability::continuous;
                }
            }
            return description;
        }
        void set_integer_variables(
            gsl::span<const value_reference> references,
            gsl::span<const int> values) override
        {
            for (std::size_t i = 0; i < references.size(); ++i) {
                int current = 0;
                mock_slave::get_integer_variables(
                    gsl::make_span(&references[i], 1), gsl::make_span(&current, 1));
                if (running && current != values[i] && !inEventMode)
                    throw std::logic_error("Discrete input changed in Step Mode");
            }
            mock_slave::set_integer_variables(references, values);
        }

        bool running = false;
        bool inEventMode = false;
        int entries = 0;
    };

    auto slave = std::make_shared<strict_event_slave>();
    slave_simulator simulator(slave, "strict-input-events");
    simulator.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
    simulator.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
    simulator.setup(to_time_point(0.0), to_time_point(5.0), {});
    simulator.start_simulation();
    simulator.set_integer(mock_slave::integer_in_reference, 0);
    BOOST_TEST(!simulator.prepare_input_events(to_duration(1.0)));
    simulator.do_step_with_info(to_time_point(0.0), to_duration(1.0));

    int modifierCalls = 0;
    simulator.set_integer_input_modifier(
        mock_slave::integer_in_reference,
        [&modifierCalls](int value, duration) { ++modifierCalls; return value + 1; });
    simulator.set_integer(mock_slave::integer_in_reference, 4);
    BOOST_TEST(simulator.prepare_input_events(to_duration(1.0)));
    BOOST_CHECK_THROW(
        simulator.do_step_with_info(to_time_point(1.0), to_duration(1.0)),
        cosim::error);
    auto* controller = simulator.get_event_controller();
    controller->enter_event_mode();
    simulator.apply_event_inputs();
    BOOST_TEST(!controller->update_event_state().terminate_simulation);
    controller->enter_step_mode();
    simulator.do_step_with_info(to_time_point(1.0), to_duration(1.0));
    BOOST_TEST(modifierCalls == 1);
    BOOST_TEST(slave->entries == 1);

    simulator.set_integer_input_modifier(mock_slave::integer_in_reference, {});
    simulator.set_integer(mock_slave::integer_in_reference, 5);
    simulator.set_real(mock_slave::real_in_reference, 2.0);
    BOOST_TEST(!simulator.prepare_input_events(to_duration(1.0)));
    simulator.do_step_with_info(to_time_point(2.0), to_duration(1.0));
    simulator.signal_input_discontinuity(
        variable_type::real, mock_slave::real_in_reference);
    BOOST_TEST(simulator.prepare_input_events(to_duration(1.0)));
    controller->enter_event_mode();
    simulator.apply_event_inputs();
    BOOST_TEST(!controller->update_event_state().terminate_simulation);
    controller->enter_step_mode();
    simulator.do_step_with_info(to_time_point(3.0), to_duration(1.0));
    BOOST_TEST(slave->entries == 2);
}

BOOST_AUTO_TEST_CASE(input_events_iterate_connected_simulators_before_stepping)
{
    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step)) {
        auto a = std::make_shared<event_coordination_slave>();
        auto b = std::make_shared<event_coordination_slave>();
        auto c = std::make_shared<event_coordination_slave>();
        slave_simulator sa(a, "a"), sb(b, "b"), sc(c, "c");
        algorithm->add_simulator(0, &sa, duration::zero());
        algorithm->add_simulator(1, &sb, duration::zero());
        algorithm->add_simulator(2, &sc, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{2, variable_type::integer, mock_slave::integer_in_reference});
        sc.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
        sa.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        sa.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        sa.expose_for_setting(variable_type::real, event_coordination_slave::tunable_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();

        sa.set_integer(mock_slave::integer_in_reference, 2);
        const auto first = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!first.termination_requested);
        BOOST_TEST(a->eventEntries == 1);
        BOOST_TEST(b->eventEntries == 1);
        BOOST_TEST(c->eventEntries == 1);
        BOOST_TEST(a->steps == 1);
        BOOST_TEST(b->steps == 1);
        BOOST_TEST(c->steps == 1);
        BOOST_TEST(sc.get_integer(mock_slave::integer_out_reference) == 2);

        sa.set_integer(mock_slave::integer_in_reference, 2);
        sa.set_real(mock_slave::real_in_reference, 3.0);
        algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(a->eventEntries == 1);
        sa.set_real(event_coordination_slave::tunable_reference, 4.0);
        algorithm->do_step_with_info(to_time_point(2.0));
        BOOST_TEST(a->eventEntries == 2);
        sa.signal_input_discontinuity(
            variable_type::real, mock_slave::real_in_reference);
        algorithm->do_step_with_info(to_time_point(3.0));
        BOOST_TEST(a->eventEntries == 3);
        BOOST_TEST(b->eventEntries == 1);
        BOOST_TEST(c->eventEntries == 1);
    }
}

BOOST_AUTO_TEST_CASE(simultaneous_input_events_apply_the_latest_propagated_value)
{
    struct recording_slave : event_coordination_slave
    {
        recording_slave(char id, std::vector<std::pair<char, int>>& records)
            : id(id), records(records) {}

        event_update_result update_event_state() override
        {
            const auto reference = integer_in_reference;
            int value = 0;
            mock_slave::get_integer_variables(gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
            records.emplace_back(id, value);
            return event_coordination_slave::update_event_state();
        }

        char id;
        std::vector<std::pair<char, int>>& records;
    };

    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        std::vector<std::pair<char, int>> updates;
        auto source = std::make_shared<recording_slave>('a', updates);
        auto target = std::make_shared<recording_slave>('b', updates);
        slave_simulator a(source, "source"), b(target, "target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        b.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        a.set_integer(mock_slave::integer_in_reference, 2);
        b.set_integer(mock_slave::integer_in_reference, 1);
        algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_REQUIRE_EQUAL(updates.size(), 4U);
        BOOST_CHECK((updates[0] == std::pair<char, int>('a', 2)));
        BOOST_CHECK((updates[1] == std::pair<char, int>('b', 1)));
        BOOST_CHECK((updates[2] == std::pair<char, int>('a', 2)));
        BOOST_CHECK((updates[3] == std::pair<char, int>('b', 2)));
        BOOST_TEST(b.get_integer(mock_slave::integer_out_reference) == 2);
    }
}

BOOST_AUTO_TEST_CASE(coupled_feedthrough_stabilizes_before_commit_and_defers_restricted_sets)
{
    struct feedthrough_slave : event_coordination_slave
    {
        explicit feedthrough_slave(bool canRepeat) : canRepeat(canRepeat) {}
        cosim::model_description model_description() const override
        {
            auto model = event_coordination_slave::model_description();
            for (auto& variable : model.variables) {
                if (variable.type == variable_type::integer && variable.reference == integer_in_reference) {
                    variable.can_handle_multiple_set_per_time_instant = canRepeat;
                }
            }
            return model;
        }
        void get_integer_variables(gsl::span<const value_reference> refs, gsl::span<int> values) const override
        {
            for (std::size_t i = 0; i < refs.size(); ++i) {
                const auto ref = refs[i] == integer_out_reference ? integer_in_reference : refs[i];
                mock_slave::get_integer_variables(gsl::make_span(&ref, 1), values.subspan(i, 1));
                if (refs[i] == integer_out_reference && values[i]) values[i] = std::min(values[i] + 1, 4);
            }
        }
        void set_integer_variables(
            gsl::span<const value_reference> refs, gsl::span<const int> values) override
        {
            for (auto ref : refs) {
                if (is_in_event_mode() && ref == integer_in_reference && ++sets > 1 && !canRepeat) {
                    throw std::logic_error("Restricted input set twice in the same instant");
                }
            }
            event_coordination_slave::set_integer_variables(refs, values);
        }
        event_update_result update_event_state() override
        {
            BOOST_TEST(peer->is_in_event_mode());
            const auto ref = integer_in_reference;
            int value;
            get_integer_variables(gsl::make_span(&ref, 1), gsl::make_span(&value, 1));
            commits.push_back(value);
            sets = 0;
            return {};
        }
        const bool canRepeat;
        const feedthrough_slave* peer = nullptr;
        int sets = 0;
        std::vector<int> commits;
    };
    for (const bool canRepeat : {true, false}) {
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            auto source = std::make_shared<feedthrough_slave>(canRepeat);
            auto target = std::make_shared<feedthrough_slave>(true);
            source->peer = target.get();
            target->peer = source.get();
            slave_simulator a(source, "source"), b(target, "target");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            for (simulator_index from : {0, 1}) {
                algorithm->connect_variables(
                    variable_id{from, variable_type::integer, mock_slave::integer_out_reference},
                    variable_id{1 - from, variable_type::integer, mock_slave::integer_in_reference});
            }
            algorithm->setup(time_point{}, std::nullopt);
            algorithm->initialize();
            a.set_integer(mock_slave::integer_in_reference, 1);
            algorithm->do_step_with_info(time_point{});
            const auto expected = canRepeat ? std::vector<int>{4} : std::vector<int>{1, 3, 4};
            BOOST_TEST(source->commits == expected, boost::test_tools::per_element());
            BOOST_TEST(target->commits.size() == expected.size());
            BOOST_TEST(source->eventEntries == 1);
            BOOST_TEST(target->eventEntries == 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(post_step_event_pulse_reaches_peer_without_leaving_event_mode)
{
    struct pulse_source : event_coordination_slave
    {
        step_result_info do_step_with_info(time_point current, duration delta) override
        {
            return {do_step(current, delta), current + delta, false, true, false, false};
        }
        void get_integer_variables(gsl::span<const value_reference> refs, gsl::span<int> values) const override
        {
            event_coordination_slave::get_integer_variables(refs, values);
            for (std::size_t i = 0; i < refs.size(); ++i) {
                if (refs[i] == integer_out_reference) values[i] = updates == 1 ? 7 : 0;
            }
        }
        event_update_result update_event_state() override
        {
            ++updates;
            return {updates == 1, false, std::nullopt};
        }
    };
    struct pulse_receiver : event_coordination_slave
    {
        event_update_result update_event_state() override
        {
            BOOST_TEST(source->is_in_event_mode());
            const auto ref = integer_in_reference;
            int value;
            get_integer_variables(gsl::make_span(&ref, 1), gsl::make_span(&value, 1));
            values.push_back(value);
            return {};
        }
        const pulse_source* source = nullptr;
        std::vector<int> values;
    };
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = std::make_shared<pulse_source>();
        auto receiver = std::make_shared<pulse_receiver>();
        receiver->source = source.get();
        slave_simulator a(source, "pulse"), b(receiver, "receiver");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        algorithm->do_step_with_info(time_point{});
        const std::vector<int> expected{7, 0};
        BOOST_TEST(receiver->values == expected, boost::test_tools::per_element());
        BOOST_TEST(source->eventEntries == 1);
        BOOST_TEST(receiver->eventEntries == 1);
    }
}

BOOST_AUTO_TEST_CASE(pending_discontinuities_reject_state_operations)
{
    auto slave = std::make_shared<event_coordination_slave>();
    slave_simulator sim(slave, "pending-discontinuity");
    sim.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
    sim.setup(to_time_point(0.0), std::nullopt, std::nullopt);
    sim.start_simulation();
    const auto saved = sim.save_state();
    sim.signal_input_discontinuity(variable_type::real, mock_slave::real_in_reference);
    BOOST_CHECK_THROW(sim.save_state(), cosim::error);
    BOOST_CHECK_THROW(sim.restore_state(saved), cosim::error);
    BOOST_TEST(sim.prepare_input_events(to_duration(1.0)));
    auto* controller = sim.get_event_controller();
    controller->enter_event_mode();
    sim.apply_event_inputs();
    controller->update_event_state();
    controller->enter_step_mode();
    sim.finish_input_event_coordination();
    sim.restore_state(saved);
    const auto completed = sim.save_state();
    const auto exported = sim.export_state(completed);
    const auto imported = sim.import_state(exported);
    sim.restore_state(imported);
}

BOOST_AUTO_TEST_CASE(event_refresh_reuses_unchanged_modified_outputs)
{
    auto slave = std::make_shared<event_coordination_slave>();
    slave_simulator sim(slave, "event-output-modifier");
    sim.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
    sim.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
    sim.setup(to_time_point(0.0), std::nullopt, std::nullopt);
    sim.start_simulation();
    int calls = 0;
    sim.set_integer_output_modifier(mock_slave::integer_out_reference,
        [&calls](int value, duration) { ++calls; return value + 10; });
    sim.refresh_event_values();
    BOOST_TEST(calls == 1);
    sim.refresh_event_values();
    BOOST_TEST(calls == 1);
    sim.set_integer(mock_slave::integer_in_reference, 3);
    BOOST_TEST(sim.prepare_input_events(to_duration(1.0)));
    auto* controller = sim.get_event_controller();
    controller->enter_event_mode();
    sim.apply_event_inputs();
    controller->update_event_state();
    sim.refresh_event_values();
    BOOST_TEST(calls == 2);
    BOOST_TEST(sim.get_integer(mock_slave::integer_out_reference) == 13);
    sim.refresh_event_values();
    BOOST_TEST(calls == 2);
    controller->enter_step_mode();
}

BOOST_AUTO_TEST_CASE(input_event_nonconvergence_and_termination)
{
    const auto step = to_duration(1.0);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15, 2};
    const std::vector<std::shared_ptr<algorithm>> limitedAlgorithms{
        std::make_shared<fixed_step_algorithm>(fixed_step_algorithm_params{step, 2}, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : limitedAlgorithms) {
        warning_capture warnings;
        auto a = std::make_shared<event_coordination_slave>(true);
        auto b = std::make_shared<event_coordination_slave>();
        slave_simulator sa(a, "oscillating-a"), sb(b, "oscillating-b");
        algorithm->add_simulator(0, &sa, duration::zero());
        algorithm->add_simulator(1, &sb, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{0, variable_type::integer, mock_slave::integer_in_reference});
        sa.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sa.signal_input_discontinuity(
            variable_type::real, mock_slave::real_in_reference);
        const auto first = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!first.termination_requested);
        BOOST_TEST(a->steps == 1);
        BOOST_TEST(b->steps == 1);
        BOOST_TEST(a->eventEntries <= 2);
        BOOST_TEST(b->eventEntries <= 2);
        BOOST_TEST(warnings.messages().find("did not converge within 2 rounds") != std::string::npos);
        BOOST_TEST(warnings.messages().find("time 0") != std::string::npos);
        BOOST_TEST(warnings.messages().find("super-dense index") != std::string::npos);
        BOOST_TEST(warnings.messages().find("pending inputs at: oscillating-") != std::string::npos);
        const auto previous = a->eventEntries + b->eventEntries;
        const auto second = algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(!second.termination_requested);
        BOOST_TEST(a->steps == 2);
        BOOST_TEST(b->steps == 2);
        BOOST_TEST(a->eventEntries + b->eventEntries > previous);
        BOOST_TEST(warnings.messages().find("time 1") != std::string::npos);
    }
    for (const auto& algorithm : event_algorithms(step)) {
        auto a = std::make_shared<event_coordination_slave>();
        auto b = std::make_shared<event_coordination_slave>();
        a->terminateOnUpdate = true;
        slave_simulator sa(a, "terminating"), sb(b, "not-stepped");
        algorithm->add_simulator(0, &sa, duration::zero());
        algorithm->add_simulator(1, &sb, duration::zero());
        sa.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sa.set_integer(mock_slave::integer_in_reference, 1);
        const auto result = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(result.termination_requested);
        BOOST_CHECK(result.step_size == duration::zero());
        BOOST_TEST(a->steps == 0);
        BOOST_TEST(b->steps == 0);
    }
    for (const auto& algorithm : event_algorithms(step)) {
        auto a = std::make_shared<event_coordination_slave>();
        a->alwaysUpdate = true;
        slave_simulator sa(a, "nonconverging-update");
        algorithm->add_simulator(0, &sa, duration::zero());
        sa.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sa.set_integer(mock_slave::integer_in_reference, 1);
        BOOST_CHECK_THROW(algorithm->do_step_with_info(to_time_point(0.0)), cosim::error);
        BOOST_TEST(a->steps == 0);
        BOOST_TEST(a->updates == 100);
    }
}

BOOST_AUTO_TEST_CASE(coupled_event_default_limit_is_1000_rounds)
{
    const auto step = to_duration(1.0);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::vector<std::shared_ptr<algorithm>> algorithms{
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        warning_capture warnings;
        auto source = std::make_shared<event_coordination_slave>(true);
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "default-limit-a"), b(target, "default-limit-b");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{0, variable_type::integer, mock_slave::integer_in_reference});
        a.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        algorithm->setup(time_point{}, std::nullopt);
        algorithm->initialize();
        a.signal_input_discontinuity(
            variable_type::real, mock_slave::real_in_reference);
        BOOST_TEST(!algorithm->do_step_with_info(time_point{}).termination_requested);
        BOOST_TEST(source->steps == 1);
        BOOST_TEST(target->steps == 1);
        BOOST_TEST(warnings.messages().find("did not converge within 1000 rounds") != std::string::npos);
        const auto budget = algorithm->export_current_state().get_child("event_router");
        BOOST_TEST(budget.get<std::uint64_t>("limit") == 1000U);
        BOOST_TEST(budget.get<std::uint64_t>("iterations") == 1000U);
        BOOST_TEST(budget.get<bool>("warned"));
    }
}

BOOST_AUTO_TEST_CASE(deferred_input_events_wait_for_time_and_keep_the_latest_value)
{
    auto slave = std::make_shared<event_coordination_slave>();
    slave_simulator sim(slave, "deferred");
    const auto integer = mock_slave::integer_in_reference;
    const auto real = mock_slave::real_in_reference;
    sim.expose_for_setting(variable_type::integer, integer);
    sim.expose_for_setting(variable_type::real, real);
    sim.setup(to_time_point(0.0), std::nullopt, std::nullopt);
    sim.start_simulation();
    sim.set_integer(integer, 1);
    sim.set_real(real, 4.0);
    sim.begin_input_event_coordination(to_time_point(0.0));
    BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0)));
    sim.defer_input_events(to_time_point(0.0));
    sim.begin_input_event_coordination(to_time_point(0.0));
    BOOST_TEST(!sim.prepare_input_events(to_duration(1.0)));
    sim.do_step_with_info(to_time_point(0.0), to_duration(1.0));

    int current = -1;
    double continuous = 0.0;
    slave->get_integer_variables(gsl::make_span(&integer, 1), gsl::make_span(&current, 1));
    slave->get_real_variables(gsl::make_span(&real, 1), gsl::make_span(&continuous, 1));
    BOOST_TEST(current == 0);
    BOOST_TEST(continuous == 4.0);
    const auto saved = sim.save_state();
    const auto exported = sim.export_state(saved);
    BOOST_TEST(exported.get<int>("scheme_version") == 4);
    std::stringstream serialized;
    serialized << serialization::format::cbor << exported;
    serialization::node decoded;
    serialized >> serialization::format::cbor >> decoded;
    const auto imported = sim.import_state(decoded);
    auto priorScheme3 = exported;
    priorScheme3.erase("sample_discontinuity_inputs");
    const auto priorImported = sim.import_state(priorScheme3);

    sim.begin_input_event_coordination(to_time_point(1.0));
    sim.set_integer(integer, 3);
    BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0)));
    auto* controller = sim.get_event_controller();
    controller->enter_event_mode();
    sim.apply_event_inputs();
    BOOST_CHECK(!controller->update_event_state().terminate_simulation);
    controller->enter_step_mode();
    sim.finish_input_event_coordination();
    slave->get_integer_variables(gsl::make_span(&integer, 1), gsl::make_span(&current, 1));
    BOOST_TEST(current == 3);

    for (const auto snapshot : {saved, imported}) {
        sim.restore_state(snapshot);
        sim.begin_input_event_coordination(to_time_point(0.0));
        BOOST_TEST(!sim.prepare_input_events(to_duration(1.0)));
        sim.finish_input_event_coordination();
        sim.begin_input_event_coordination(to_time_point(1.0));
        BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0)));
        controller->enter_event_mode();
        sim.apply_event_inputs();
        BOOST_CHECK(!controller->update_event_state().terminate_simulation);
        controller->enter_step_mode();
        sim.finish_input_event_coordination();
        slave->get_integer_variables(gsl::make_span(&integer, 1), gsl::make_span(&current, 1));
        BOOST_TEST(current == 1);
    }

    auto invalid = exported;
    invalid.get_child("deferred_inputs").get_child("0").put("reference", 999U);
    BOOST_CHECK_EXCEPTION(sim.import_state(invalid), cosim::error,
        error_is(errc::bad_file));
    invalid = exported;
    invalid.put("has_deferred_time", false);
    BOOST_CHECK_EXCEPTION(sim.import_state(invalid), cosim::error,
        error_is(errc::bad_file));
    sim.release_state(saved);
    sim.release_state(imported);
    sim.release_state(priorImported);
}

BOOST_AUTO_TEST_CASE(deferred_discontinuity_survives_state_restoration)
{
    for (const bool samplePolicy : {false, true}) {
        auto slave = std::make_shared<event_coordination_slave>();
        slave_simulator sim(slave, "deferred-discontinuity");
        const auto real = mock_slave::real_in_reference;
        sim.expose_for_setting(variable_type::real, real);
        if (samplePolicy) {
            sim.set_input_sample_discontinuity_policy(variable_type::real, real, true);
        }
        sim.setup(to_time_point(0.0), std::nullopt, std::nullopt);
        sim.start_simulation();
        sim.set_real(real, 7.0);
        if (!samplePolicy) sim.signal_input_discontinuity(variable_type::real, real);
        BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0)));
        sim.defer_input_events(to_time_point(0.0));
        sim.finish_input_event_coordination();

        const auto saved = sim.save_state();
        const auto exported = sim.export_state(saved);
        const auto imported = sim.import_state(exported);
        auto* controller = sim.get_event_controller();
        for (const auto snapshot : {saved, imported}) {
            sim.restore_state(snapshot);
            sim.begin_input_event_coordination(to_time_point(0.0));
            BOOST_TEST(!sim.prepare_input_events(to_duration(1.0)));
            sim.finish_input_event_coordination();
            sim.begin_input_event_coordination(to_time_point(1.0));
            BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0)));
            controller->enter_event_mode();
            sim.apply_event_inputs();
            controller->update_event_state();
            controller->enter_step_mode();
            sim.finish_input_event_coordination();
            double current = 0.0;
            slave->get_real_variables(gsl::make_span(&real, 1), gsl::make_span(&current, 1));
            BOOST_TEST(current == 7.0);
        }

        auto invalid = exported;
        invalid.get_child("deferred_discontinuities").get_child("0").put("type",
            static_cast<int>(variable_type::integer));
        BOOST_CHECK_EXCEPTION(sim.import_state(invalid), cosim::error,
            error_is(errc::bad_file));
        sim.release_state(saved);
        sim.release_state(imported);
    }
}

BOOST_AUTO_TEST_CASE(changed_continuous_sample_discontinuity_is_opt_in)
{
    const auto step = to_duration(1.0);
    for (const bool enabled : {false, true}) {
        for (const auto& algorithm : event_algorithms(step)) {
            auto slave = std::make_shared<event_coordination_slave>();
            slave_simulator sim(slave, "sample-policy");
            algorithm->add_simulator(0, &sim, duration::zero());
            const auto real = mock_slave::real_in_reference;
            sim.expose_for_setting(variable_type::real, real);
            sim.set_input_sample_discontinuity_policy(variable_type::real, real, enabled);
            BOOST_CHECK_THROW(sim.set_input_sample_discontinuity_policy(
                variable_type::integer, mock_slave::integer_in_reference, true), cosim::error);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            BOOST_CHECK_THROW(sim.set_input_sample_discontinuity_policy(
                variable_type::real, real, false), cosim::error);
            sim.set_real(real, 1.0);
            algorithm->do_step_with_info(to_time_point(0.0));
            BOOST_TEST(slave->eventEntries == (enabled ? 1 : 0));
            sim.set_real(real, 1.0);
            algorithm->do_step_with_info(to_time_point(1.0));
            BOOST_TEST(slave->eventEntries == (enabled ? 1 : 0));
            sim.set_real(real, 2.0);
            algorithm->do_step_with_info(to_time_point(2.0));
            BOOST_TEST(slave->eventEntries == (enabled ? 2 : 0));
        }
    }
}

BOOST_AUTO_TEST_CASE(changed_continuous_sample_policy_uses_effective_modifier_value)
{
    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step)) {
        auto slave = std::make_shared<event_coordination_slave>();
        slave_simulator sim(slave, "modified-sample-policy");
        algorithm->add_simulator(0, &sim, duration::zero());
        const auto real = mock_slave::real_in_reference;
        sim.expose_for_setting(variable_type::real, real);
        sim.set_input_sample_discontinuity_policy(variable_type::real, real, true);
        int calls = 0;
        sim.set_real_input_modifier(real, [&calls](double value, duration) {
            ++calls;
            return value * 2.0;
        });
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        calls = 0;
        sim.set_real(real, 1.0);
        algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(slave->eventEntries == 1);
        sim.set_real(real, 1.0);
        algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(slave->eventEntries == 1);
        sim.set_real(real, 3.0);
        algorithm->do_step_with_info(to_time_point(2.0));
        BOOST_TEST(slave->eventEntries == 2);
        BOOST_TEST(calls == 3);
    }
}

BOOST_AUTO_TEST_CASE(sample_discontinuity_policy_is_validated_when_importing_state)
{
    const auto real = mock_slave::real_in_reference;
    auto slave = std::make_shared<event_coordination_slave>();
    slave_simulator sim(slave, "sample-policy-state");
    sim.expose_for_setting(variable_type::real, real);
    sim.set_input_sample_discontinuity_policy(variable_type::real, real, true);
    sim.setup(to_time_point(0.0), {}, {});
    sim.start_simulation();
    const auto saved = sim.save_state();
    const auto exported = sim.export_state(saved);
    const auto imported = sim.import_state(exported);
    sim.restore_state(imported);

    auto other = std::make_shared<event_coordination_slave>();
    slave_simulator incompatible(other, "different-sample-policy");
    incompatible.expose_for_setting(variable_type::real, real);
    incompatible.setup(to_time_point(0.0), {}, {});
    incompatible.start_simulation();
    BOOST_CHECK_EXCEPTION(incompatible.import_state(exported), cosim::error,
        error_is(errc::bad_file));
    auto invalid = exported;
    invalid.get_child("sample_discontinuity_inputs").get_child("0").put("reference", 999U);
    BOOST_CHECK_EXCEPTION(sim.import_state(invalid), cosim::error,
        error_is(errc::bad_file));
    sim.release_state(saved);
    sim.release_state(imported);
}

BOOST_AUTO_TEST_CASE(changed_continuous_array_sample_uses_discontinuity_policy)
{
    struct array_slave : mock_slave, event_controller
    {
        cosim::model_description model_description() const override
        {
            cosim::model_description model;
            variable_description variable{};
            variable.name = "input";
            variable.reference = 10;
            variable.type = variable_type::float32;
            variable.causality = variable_causality::input;
            variable.variability = variable_variability::continuous;
            variable.dimensions.emplace_back(fixed_dimension{2});
            variable.array_start = input;
            model.variables.push_back(std::move(variable));
            return model;
        }
        event_controller* get_event_controller() noexcept override { return this; }
        bool is_in_event_mode() const noexcept override { return inEventMode; }
        void enter_event_mode() override { inEventMode = true; ++events; }
        event_update_result update_event_state() override { return {}; }
        void enter_step_mode() override { inEventMode = false; }
        std::vector<variable_value> get_variable_values(
            gsl::span<const typed_value_reference> references) const override
        {
            std::vector<variable_value> values;
            for (const auto reference : references) {
                if (reference.type != variable_type::float32 || reference.reference != 10) {
                    throw std::invalid_argument("Invalid array reference");
                }
                values.push_back(input);
            }
            return values;
        }
        void set_variable_values(
            gsl::span<const typed_value_reference> references,
            gsl::span<const variable_value> values) override
        {
            if (references.size() != values.size()) throw std::invalid_argument("Invalid count");
            for (std::size_t i = 0; i < references.size(); ++i) {
                if (references[i].type != variable_type::float32 ||
                    references[i].reference != 10 ||
                    values[i].type() != variable_type::float32 ||
                    values[i].dimensions() != std::vector<std::uint64_t>{2}) {
                    throw std::invalid_argument("Invalid array input");
                }
                input = values[i];
            }
        }
        bool inEventMode = false;
        int events = 0;
        variable_value input{float32_values{{0.0F, 0.0F}}, {2}};
    };

    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto slave = std::make_shared<array_slave>();
        slave_simulator sim(slave, "array-sample-policy");
        algorithm->add_simulator(0, &sim, duration::zero());
        sim.expose_for_setting(variable_type::float32, 10);
        sim.set_input_sample_discontinuity_policy(variable_type::float32, 10, true);
        algorithm->setup(to_time_point(0.0), {});
        algorithm->initialize();
        sim.set_variable_value(variable_type::float32, 10,
            variable_value(float32_values{{1.0F, 2.0F}}, {2}));
        algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(slave->events == 1);
        sim.set_variable_value(variable_type::float32, 10,
            variable_value(float32_values{{1.0F, 2.0F}}, {2}));
        algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(slave->events == 1);
        sim.set_variable_value(variable_type::float32, 10,
            variable_value(float32_values{{1.0F, 3.0F}}, {2}));
        algorithm->do_step_with_info(to_time_point(2.0));
        BOOST_TEST(slave->events == 2);
    }
}

BOOST_AUTO_TEST_CASE(sample_discontinuity_policy_on_connections_and_functions)
{
    const auto step = to_duration(1.0);
    for (const bool throughFunction : {false, true}) {
        for (const auto& algorithm : event_algorithms(step)) {
            auto source = std::make_shared<mock_slave>();
            auto target = std::make_shared<event_coordination_slave>();
            slave_simulator a(source, "sample-source"), b(target, "sample-target");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            linear_transformation_function scale(0.0, 2.0);
            const variable_id output{0, variable_type::real, mock_slave::real_out_reference};
            const variable_id input{1, variable_type::real, mock_slave::real_in_reference};
            if (throughFunction) {
                algorithm->add_function(0, &scale);
                algorithm->connect_variables(output, function_io_id{0, variable_type::real,
                    linear_transformation_function::in_io_reference});
                algorithm->connect_variables(function_io_id{0, variable_type::real,
                    linear_transformation_function::out_io_reference}, input);
            } else {
                algorithm->connect_variables(output, input);
            }
            a.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
            b.set_input_sample_discontinuity_policy(
                variable_type::real, mock_slave::real_in_reference, true);
            algorithm->setup(to_time_point(0.0), {});
            algorithm->initialize();
            a.set_real(mock_slave::real_in_reference, 1.0);
            algorithm->do_step_with_info(to_time_point(0.0));
            algorithm->do_step_with_info(to_time_point(1.0));
            BOOST_TEST(target->eventEntries == 1);
            double value = 0.0;
            const auto reference = mock_slave::real_in_reference;
            target->get_real_variables(gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
            BOOST_TEST(value == (throughFunction ? 2.0 : 1.0));
            algorithm->do_step_with_info(to_time_point(2.0));
            BOOST_TEST(target->eventEntries == 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(eventful_fixed_step_state_rejects_unsaved_function_state)
{
    const auto step = to_duration(1.0);
    auto slave = std::make_shared<event_coordination_slave>();
    slave_simulator sim(slave, "eventful-function-state");
    linear_transformation_function function(0.0, 1.0);
    fixed_step_algorithm eventful(step, 1);
    eventful.add_simulator(0, &sim, duration::zero());
    eventful.add_function(0, &function);
    eventful.setup(to_time_point(0.0), {});
    eventful.initialize();
    BOOST_CHECK_EXCEPTION(eventful.export_current_state(), cosim::error,
        error_is(errc::unsupported_feature));

    auto legacySlave = std::make_shared<mock_slave>();
    slave_simulator legacySim(legacySlave, "legacy-function-state");
    linear_transformation_function legacyFunction(0.0, 1.0);
    fixed_step_algorithm legacy(step, 1);
    legacy.add_simulator(0, &legacySim, duration::zero());
    legacy.add_function(0, &legacyFunction);
    legacy.setup(to_time_point(0.0), {});
    legacy.initialize();
    const auto state = legacy.export_current_state();
    BOOST_CHECK_NO_THROW(legacy.import_state(state));
    BOOST_CHECK_EXCEPTION(eventful.import_state(state), cosim::error,
        error_is(errc::unsupported_feature));
}

BOOST_AUTO_TEST_CASE(event_iteration_limit_requires_positive_preinitialization_configuration)
{
    const auto step = to_duration(1.0);
    BOOST_CHECK_THROW(
        fixed_step_algorithm((fixed_step_algorithm_params{step, 0}), 1),
        std::invalid_argument);
    ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15, 0};
    BOOST_CHECK_THROW(ecco_algorithm(params, 1), std::invalid_argument);
    params.max_event_iterations = 4;
    fixed_step_algorithm fixed(step, 1);
    ecco_algorithm ecco(params, 1);
    BOOST_CHECK_THROW(fixed.set_max_event_iterations(0), std::invalid_argument);
    BOOST_CHECK_THROW(ecco.set_max_event_iterations(0), std::invalid_argument);
    fixed.set_max_event_iterations(1);
    ecco.set_max_event_iterations(1);
    fixed.setup(to_time_point(0.0), std::nullopt);
    ecco.setup(to_time_point(0.0), std::nullopt);
    fixed.initialize();
    ecco.initialize();
    BOOST_CHECK_THROW(fixed.set_max_event_iterations(2), cosim::error);
    BOOST_CHECK_THROW(ecco.set_max_event_iterations(2), cosim::error);
}

BOOST_AUTO_TEST_CASE(event_iteration_budget_survives_algorithm_state_restore)
{
    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step)) {
        auto instance = std::make_shared<event_coordination_slave>();
        slave_simulator sim(instance, "event-budget");
        algorithm->add_simulator(0, &sim, duration::zero());
        sim.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sim.set_integer(mock_slave::integer_in_reference, 1);
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        const auto saved = algorithm->export_current_state();
        const auto& budget = saved.get_child("event_router");
        BOOST_TEST(budget.get<bool>("has_time"));
        BOOST_TEST(budget.get<std::int64_t>("time_ticks") == 0);
        BOOST_TEST(budget.get<std::uint64_t>("iterations") == 1U);

        auto invalid = saved;
        invalid.put<std::uint64_t>("event_router.iterations",
            budget.get<std::uint64_t>("limit") + 1);
        BOOST_CHECK_EXCEPTION(algorithm->import_state(invalid), cosim::error,
            error_is(errc::bad_file));
        BOOST_CHECK(algorithm->export_current_state() == saved);

        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
        algorithm->import_state(saved);
        BOOST_CHECK(algorithm->export_current_state() == saved);
    }

    fixed_step_algorithm legacy(step, 1);
    legacy.setup(to_time_point(0.0), std::nullopt);
    legacy.initialize();
    auto oldState = legacy.export_current_state();
    oldState.erase("event_router");
    BOOST_CHECK_NO_THROW(legacy.import_state(oldState));

    fixed_step_algorithm eventful(step, 1);
    auto instance = std::make_shared<event_coordination_slave>();
    slave_simulator sim(instance, "eventful-legacy-state");
    eventful.add_simulator(0, &sim, duration::zero());
    eventful.setup(to_time_point(0.0), std::nullopt);
    eventful.initialize();
    BOOST_CHECK_EXCEPTION(eventful.import_state(oldState), cosim::error,
        error_is(errc::unsupported_feature));
}

BOOST_AUTO_TEST_CASE(coupled_cutoff_state_restores_deferred_events)
{
        struct restorable_event_slave : event_coordination_slave
        {
            explicit restorable_event_slave(bool invert = false)
                : event_coordination_slave(invert) {}

            void restore_state(state_index state) override
            {
                mock_slave::restore_state(state);
                update_event_state();
            }
        };

        const auto step = to_duration(1.0);
        for (const auto& algorithm : event_algorithms(step, 2)) {
            warning_capture warnings;
            auto source = std::make_shared<restorable_event_slave>(true);
            auto target = std::make_shared<restorable_event_slave>();
            slave_simulator a(source, "cutoff-a"), b(target, "cutoff-b");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
                variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
            algorithm->connect_variables(
                variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
                variable_id{0, variable_type::integer, mock_slave::integer_in_reference});
            a.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
            algorithm->setup(time_point{}, std::nullopt);
            algorithm->initialize();
            a.signal_input_discontinuity(
                variable_type::real, mock_slave::real_in_reference);
            BOOST_TEST(!algorithm->do_step_with_info(time_point{}).termination_requested);
            BOOST_TEST(warnings.messages().find("did not converge within 2 rounds") != std::string::npos);

            const auto aState = a.save_state();
            const auto bState = b.save_state();
            const auto aSaved = a.export_state(aState);
            const auto bSaved = b.export_state(bState);
            BOOST_CHECK(aSaved.get_child("deferred_inputs").size() +
                bSaved.get_child("deferred_inputs").size() > 0);
            const auto routerSaved = algorithm->export_current_state();
            std::stringstream stream;
            serialization::node snapshot;
            snapshot.put_child("a", aSaved);
            snapshot.put_child("b", bSaved);
            snapshot.put_child("algorithm", routerSaved);
            stream << serialization::format::cbor << snapshot;
            serialization::node decoded;
            stream >> serialization::format::cbor >> decoded;

            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
            const auto expectedAIndex = a.save_state();
            const auto expectedBIndex = b.save_state();
            const auto expectedA = a.export_state(expectedAIndex);
            const auto expectedB = b.export_state(expectedBIndex);
            const auto expectedRouter = algorithm->export_current_state();

            const auto importedA = a.import_state(decoded.get_child("a"));
            const auto importedB = b.import_state(decoded.get_child("b"));
            a.restore_state(importedA);
            b.restore_state(importedB);
            algorithm->import_state(decoded.get_child("algorithm"));
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
            const auto replayAIndex = a.save_state();
            const auto replayBIndex = b.save_state();
            BOOST_CHECK(a.export_state(replayAIndex) == expectedA);
            BOOST_CHECK(b.export_state(replayBIndex) == expectedB);
            BOOST_CHECK(algorithm->export_current_state() == expectedRouter);
            for (const auto state : {aState, expectedAIndex, importedA, replayAIndex}) {
                a.release_state(state);
            }
            for (const auto state : {bState, expectedBIndex, importedB, replayBIndex}) {
                b.release_state(state);
            }
        }
}

BOOST_AUTO_TEST_CASE(coupled_events_follow_output_dependency_order)
{
    struct ordered_event_slave : event_coordination_slave
    {
        ordered_event_slave(int id, std::vector<int>& order, int metadata)
            : id(id), order(order), metadata(metadata) {}
        cosim::model_description model_description() const override
        {
            auto description = event_coordination_slave::model_description();
            for (auto& variable : description.variables) {
                if (variable.type != variable_type::integer ||
                    variable.reference != integer_out_reference ||
                    variable.causality != variable_causality::output) continue;
                if (metadata != 0) {
                    variable.output_dependencies = std::vector<variable_dependency>{};
                    if (metadata == 1) {
                        variable.output_dependencies->push_back({
                            {variable_type::integer, integer_in_reference},
                            dependency_kind::dependent});
                    }
                }
            }
            return description;
        }
        event_update_result update_event_state() override
        {
            order.push_back(id);
            return event_coordination_slave::update_event_state();
        }
        int id;
        std::vector<int>& order;
        int metadata;
    };

    for (const int metadata : {0, 1, 2}) {
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            std::vector<int> order;
            auto first = std::make_shared<ordered_event_slave>(20, order, metadata);
            auto middle = std::make_shared<ordered_event_slave>(10, order, metadata);
            auto last = std::make_shared<ordered_event_slave>(0, order, metadata);
            slave_simulator a(first, "first"), b(middle, "middle"), c(last, "last");
            algorithm->add_simulator(20, &a, duration::zero());
            algorithm->add_simulator(10, &b, duration::zero());
            algorithm->add_simulator(0, &c, duration::zero());
            for (const auto& pair : {std::pair{20, 10}, std::pair{10, 0}}) {
                algorithm->connect_variables(
                    variable_id{pair.first, variable_type::integer, mock_slave::integer_out_reference},
                    variable_id{pair.second, variable_type::integer, mock_slave::integer_in_reference});
            }
            for (auto* sim : {&a, &b, &c}) {
                sim->expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
            }
            c.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            a.set_integer(mock_slave::integer_in_reference, 2);
            b.set_integer(mock_slave::integer_in_reference, 9);
            c.set_integer(mock_slave::integer_in_reference, 8);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
            BOOST_TEST(c.get_integer(mock_slave::integer_out_reference) == 2);
            const std::vector<int> expected = metadata != 2
                ? std::vector<int>{20, 10, 0, 20, 10, 0, 20, 10, 0}
                : std::vector<int>{10, 0, 20, 10, 0, 20, 10, 0, 20};
            BOOST_TEST(order == expected, boost::test_tools::per_element());
            BOOST_TEST(first->eventEntries == 1);
            BOOST_TEST(middle->eventEntries == 1);
            BOOST_TEST(last->eventEntries == 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(mixed_event_routing_respects_legacy_step_semantics)
{
    class legacy_step_slave : public mock_slave
    {
    public:
        void start_simulation() override { running = true; }
        void set_integer_variables(
            gsl::span<const value_reference> references,
            gsl::span<const int> values) override
        {
            mock_slave::set_integer_variables(references, values);
            if (running && !references.empty()) pendingStep = true;
        }
        void get_integer_variables(
            gsl::span<const value_reference> references,
            gsl::span<int> values) const override
        {
            if (pendingStep) {
                throw std::logic_error("Legacy output read before its next step");
            }
            mock_slave::get_integer_variables(references, values);
        }
        step_result do_step(time_point current, duration delta) override
        {
            ++steps;
            const auto result = mock_slave::do_step(current, delta);
            pendingStep = false;
            return result;
        }
        bool running = false;
        bool pendingStep = false;
        int steps = 0;
    };

    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step)) {
        auto legacy = std::make_shared<mock_slave>(
            std::function<double(time_point, duration, double)>(),
            [](time_point time, duration, int) {
                return static_cast<int>(to_double_time_point(time));
            });
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator source(legacy, "legacy-source"), sink(target, "event-sink");
        algorithm->add_simulator(0, &source, duration::zero());
        algorithm->add_simulator(1, &sink, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(target->eventEntries == 0);
        algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(target->eventEntries == 1);
        BOOST_TEST(target->steps == 2);
    }
    for (const auto& algorithm : event_algorithms(step)) {
        auto source = std::make_shared<event_coordination_slave>();
        auto legacy = std::make_shared<legacy_step_slave>();
        auto downstream = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "event-source"), b(legacy, "legacy-middle"),
            c(downstream, "downstream");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{2, variable_type::integer, mock_slave::integer_in_reference});
        a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        a.set_integer(mock_slave::integer_in_reference, 1);
        const auto first = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!first.termination_requested);
        BOOST_TEST(source->steps == 1);
        BOOST_TEST(legacy->steps == 1);
        BOOST_TEST(downstream->steps == 1);
        BOOST_TEST(downstream->eventEntries == 0);
        const auto second = algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(!second.termination_requested);
        BOOST_TEST(legacy->steps == 2);
        BOOST_TEST(downstream->eventEntries == 1);
        BOOST_TEST(downstream->steps == 2);
    }
}

BOOST_AUTO_TEST_CASE(event_changes_cross_real_fmi1_and_fmi2_only_after_stepping)
{
    const auto* testData = std::getenv("TEST_DATA_DIR");
    const auto* fmi2Path = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(testData != nullptr);
    BOOST_REQUIRE(fmi2Path != nullptr);
    const auto importer = fmi::importer::create();
    const std::array<cosim::filesystem::path, 2> paths{
        cosim::filesystem::path(testData) / "fmi1" / "identity.fmu",
        cosim::filesystem::path(fmi2Path)};
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto legacyFmu = importer->import(paths[i]);
        const value_reference input = i == 0 ? 0 : 3;
        const value_reference output = i == 0 ? 0 : 4;
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            auto source = std::make_shared<event_coordination_slave>();
            auto legacy = legacyFmu->instantiate_slave("legacy-middle");
            auto downstream = std::make_shared<event_coordination_slave>();
            slave_simulator a(source, "source"), b(legacy, "legacy"),
                c(downstream, "downstream");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->add_simulator(2, &c, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
                variable_id{1, variable_type::integer, input});
            algorithm->connect_variables(
                variable_id{1, variable_type::integer, output},
                variable_id{2, variable_type::integer, mock_slave::integer_in_reference});
            a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            a.set_integer(mock_slave::integer_in_reference, 1);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
            BOOST_TEST(downstream->eventEntries == 0);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
            BOOST_TEST(downstream->eventEntries == 1);
            BOOST_TEST(downstream->steps == 2);
        }
    }
}

BOOST_AUTO_TEST_CASE(real_fmi3_input_events_and_runtime_tunable_parameters)
{
    const auto path = reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto references = variable_references(*fmu->model_description());
    const auto step = to_duration(0.1);
    for (const auto& algorithm : event_algorithms(step)) {
        auto instance = fmu->instantiate_v3_slave("input-events");
        slave_simulator sim(instance, "input-events");
        algorithm->add_simulator(0, &sim, duration::zero());
        const auto discrete = references.at("Float64_discrete_input");
        const auto integer = references.at("Int32_input");
        const auto parameter = references.at("Float64_tunable_parameter");
        const auto continuous = references.at("Float64_continuous_input");
        sim.expose_for_setting(variable_type::real, discrete);
        sim.expose_for_setting(variable_type::integer, integer);
        sim.expose_for_setting(variable_type::real, parameter);
        sim.expose_for_setting(variable_type::real, continuous);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sim.set_real(discrete, 8.5);
        sim.set_integer(integer, 7);
        sim.set_real(parameter, 2.5);
        sim.set_real(continuous, 1.5);
        sim.signal_input_discontinuity(variable_type::real, continuous);
        const auto result = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!result.termination_requested);
        const value_reference realReferences[] = {discrete, parameter};
        double actual[2]{};
        instance->get_real_variables(realReferences, actual);
        BOOST_TEST(actual[0] == 8.5);
        BOOST_TEST(actual[1] == 2.5);
        int actualInteger = 0;
        instance->get_integer_variables(
            gsl::make_span(&integer, 1), gsl::make_span(&actualInteger, 1));
        BOOST_TEST(actualInteger == 7);
    }
}

BOOST_AUTO_TEST_CASE(fmi1_source_drives_an_fmi3_input_event)
{
    const auto* testData = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testData != nullptr);
    const auto path = reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto legacyFmu = importer->import(cosim::filesystem::path(testData) / "fmi1" / "identity.fmu");
    const auto targetFmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto refs = variable_references(*targetFmu->model_description());
    const auto step = to_duration(0.1);
    for (const auto& algorithm : event_algorithms(step)) {
        auto legacy = legacyFmu->instantiate_slave("legacy");
        auto target = targetFmu->instantiate_v3_slave("target");
        slave_simulator sourceSim(legacy, "legacy"), targetSim(target, "target");
        algorithm->add_simulator(0, &sourceSim, duration::zero());
        algorithm->add_simulator(1, &targetSim, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, 0},
            variable_id{1, variable_type::integer, refs.at("Int32_input")});
        sourceSim.expose_for_setting(variable_type::integer, 0);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sourceSim.set_integer(0, 3);
        algorithm->do_step_with_info(to_time_point(0.0));
        algorithm->do_step_with_info(to_time_point(0.1));
        int value = 0;
        const auto input = refs.at("Int32_input");
        target->get_integer_variables(gsl::make_span(&input, 1), gsl::make_span(&value, 1));
        BOOST_TEST(value == 3);
    }
}

BOOST_AUTO_TEST_CASE(scheduled_fmi3_output_events_are_resolved_at_the_endpoint)
{
    const auto* fixture = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    BOOST_REQUIRE(fixture != nullptr);
    const auto path = cosim::filesystem::path(fixture);
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto counter = variable_references(*fmu->model_description()).at("counter");
    const auto step = to_duration(1.0);
    for (const bool terminate : {false, true}) {
        for (const auto& algorithm : event_algorithms(step)) {
            auto source = fmu->instantiate_v3_slave("scheduled-source");
            auto target = std::make_shared<event_coordination_slave>();
            slave_simulator a(source, "scheduled-source"), b(target, "event-target");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::integer, counter},
                variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            target->terminateOnUpdate = terminate;
            const auto result = algorithm->do_step_with_info(to_time_point(0.0));
            BOOST_CHECK(result.step_size == step);
            BOOST_TEST(result.termination_requested == terminate);
            BOOST_TEST(target->eventEntries == 1);
            BOOST_TEST(target->steps == 1);
            if (!terminate) {
                const auto state = b.save_state();
                b.restore_state(state);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_endpoint_event_updates_fmi2_for_its_next_step)
{
    const auto* stairPath = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    const auto* fmi2Path = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(stairPath != nullptr);
    BOOST_REQUIRE(fmi2Path != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmi3 = std::static_pointer_cast<fmi::v3::fmu>(importer->import(stairPath));
    const auto fmi2 = importer->import(fmi2Path);
    const auto counter = variable_references(*fmi3->model_description()).at("counter");
    const auto fmi2Refs = variable_references(*fmi2->model_description());
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = fmi3->instantiate_v3_slave("fmi3-source");
        auto target = fmi2->instantiate_slave("fmi2-target");
        slave_simulator a(source, "fmi3-source"), b(target, "fmi2-target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, counter},
            variable_id{1, variable_type::integer, fmi2Refs.at("integer_input")});
        b.expose_for_getting(variable_type::integer, fmi2Refs.at("integer_output"));
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        algorithm->do_step_with_info(to_time_point(0.0));
        const auto endpointValue = a.get_integer(counter);
        algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(b.get_integer(fmi2Refs.at("integer_output")) == endpointValue);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_bouncing_ball_early_return_with_fmi2)
{
    const auto path = reference_fmu_directory() / "BouncingBall.fmu";
    const auto* fmi2Path = std::getenv("FMI2_MIXED_TYPES_FMU");
    require_fixture(path);
    BOOST_REQUIRE(fmi2Path != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmi3 = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto fmi2 = importer->import(fmi2Path);
    const auto height = variable_references(*fmi3->model_description()).at("h");
    const auto input = variable_references(*fmi2->model_description()).at("real_input");
    for (const bool eventMode : {false, true}) {
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            fmi::v3::co_simulation_callbacks callbacks;
            callbacks.event_mode_used = eventMode;
            callbacks.early_return_allowed = true;
            auto source = fmi3->instantiate_v3_slave("bouncing-ball", std::move(callbacks));
            auto target = fmi2->instantiate_slave("legacy-target");
            slave_simulator a(source, "bouncing-ball"), b(target, "legacy-target");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::real, height},
                variable_id{1, variable_type::real, input});
            algorithm->setup(time_point{}, std::nullopt);
            algorithm->initialize();

            const auto first = algorithm->do_step_with_info(time_point{});
            BOOST_CHECK(first.step_size > duration::zero());
            BOOST_CHECK(first.step_size < to_duration(1.0));
            BOOST_TEST(first.early_return);
            BOOST_TEST(!first.termination_requested);
            const auto second = algorithm->do_step_with_info(time_point(first.step_size));
            BOOST_CHECK(second.step_size > duration::zero());
            BOOST_TEST(!second.termination_requested);
        }
    }
}

BOOST_AUTO_TEST_CASE(fmi3_early_event_rewinds_state_capable_decimated_fmi2)
{
    const auto* fmi2Dir = std::getenv("REFERENCE_FMU_V2_DIR");
    BOOST_REQUIRE(fmi2Dir != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmi3 = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(reference_fmu_directory() / "BouncingBall.fmu"));
    const auto fmi2 = importer->import(
        cosim::filesystem::path(fmi2Dir) / "Feedthrough.fmu");
    BOOST_REQUIRE(fmi2->model_description()->capabilities.can_save_state);
    BOOST_REQUIRE(fmi2->model_description()->capabilities.can_handle_variable_step);
    const auto height = variable_references(*fmi3->model_description()).at("h");
    const auto input = variable_references(*fmi2->model_description())
                           .at("Float64_continuous_input");
    slave_simulator a(fmi3->instantiate_slave("event"), "event");
    slave_simulator b(fmi2->instantiate_slave("stateful-fmi2"), "stateful-fmi2");
    fixed_step_algorithm algorithm(to_duration(1.0), 1);
    algorithm.add_simulator(0, &a, duration::zero());
    algorithm.add_simulator(1, &b, duration::zero());
    algorithm.set_stepsize_decimation_factor(1, 2);
    algorithm.connect_variables(
        variable_id{0, variable_type::real, height},
        variable_id{1, variable_type::real, input});
    algorithm.setup(time_point{}, std::nullopt);
    algorithm.initialize();
    const auto first = algorithm.do_step_with_info(time_point{});
    BOOST_CHECK(first.step_size > duration::zero());
    BOOST_CHECK(first.step_size < to_duration(1.0));
    BOOST_TEST(first.finished.count(1) == 1u);
    const auto second = algorithm.do_step_with_info(time_point(first.step_size));
    BOOST_CHECK(second.step_size > duration::zero());
}

BOOST_AUTO_TEST_CASE(endpoint_events_do_not_evaluate_unrelated_input_modifiers)
{
    const auto* fixture = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    BOOST_REQUIRE(fixture != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fixture));
    const auto counter = variable_references(*fmu->model_description()).at("counter");
    const auto step = to_duration(1.0);
    for (const auto& algorithm : event_algorithms(step)) {
        auto source = fmu->instantiate_v3_slave("modifier-source");
        auto target = std::make_shared<event_coordination_slave>();
        auto unrelated = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "source"), b(target, "target"), c(unrelated, "unrelated");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, counter},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        c.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        int calls = 0;
        c.set_real_input_modifier(mock_slave::real_in_reference,
            [&calls](double value, duration) { ++calls; return value; });
        const auto result = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!result.termination_requested);
        BOOST_TEST(target->eventEntries == 1);
        BOOST_TEST(calls == 1);
    }
}

BOOST_AUTO_TEST_CASE(endpoint_event_rejects_reevaluation_of_a_changed_modified_input)
{
    const auto* fixture = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    BOOST_REQUIRE(fixture != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fixture));
    const auto counter = variable_references(*fmu->model_description()).at("counter");
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = fmu->instantiate_v3_slave("modified-input-source");
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "source"), b(target, "target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, counter},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        int calls = 0;
        b.set_integer_input_modifier(mock_slave::integer_in_reference,
            [&calls](int value, duration) { ++calls; return value; });
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(to_time_point(0.0)), cosim::error,
            [](const cosim::error& e) {
                return e.code() == make_error_code(errc::unsupported_feature) &&
                    std::string(e.what()).find("modified input") != std::string::npos;
            });
        BOOST_TEST(calls == 1);
        BOOST_TEST(target->steps == 1);
    }
}

BOOST_AUTO_TEST_CASE(unchanged_modified_input_is_evaluated_once_per_step)
{
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto instance = std::make_shared<event_coordination_slave>();
        slave_simulator sim(instance, "unchanged-modified-input");
        algorithm->add_simulator(0, &sim, duration::zero());
        sim.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        int calls = 0;
        sim.set_integer_input_modifier(mock_slave::integer_in_reference,
            [&calls](int value, duration) { ++calls; return value; });
        for (int i = 0; i < 2; ++i) {
            sim.set_integer(mock_slave::integer_in_reference, 0);
            sim.set_integer(mock_slave::integer_in_reference, 0);
            algorithm->do_step_with_info(to_time_point(static_cast<double>(i)));
            BOOST_TEST(calls == i + 1);
            BOOST_TEST(instance->eventEntries == 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(replay_safe_input_modifier_handles_endpoint_events)
{
    const auto* fixture = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    BOOST_REQUIRE(fixture != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fixture));
    const auto counter = variable_references(*fmu->model_description()).at("counter");
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = fmu->instantiate_v3_slave("replay-safe-source");
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "source"), b(target, "target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, counter},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        BOOST_CHECK_THROW(b.set_input_modifier_replay_safe(
            variable_type::integer, mock_slave::integer_in_reference, true), cosim::error);
        int calls = 0;
        b.set_integer_input_modifier(mock_slave::integer_in_reference,
            [&calls](int value, duration) { ++calls; return value; });
        b.set_input_modifier_replay_safe(variable_type::integer, mock_slave::integer_in_reference, true);
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        BOOST_TEST(target->steps == 1);
        BOOST_TEST(target->eventEntries == 1);
        BOOST_TEST(calls == 2);

        b.set_integer_input_modifier(mock_slave::integer_in_reference,
            [&calls](int value, duration) { ++calls; return value; });
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(to_time_point(1.0)), cosim::error,
            [](const cosim::error& e) {
                return e.code() == make_error_code(errc::unsupported_feature) &&
                    std::string(e.what()).find("modified input") != std::string::npos;
            });
    }
}

BOOST_AUTO_TEST_CASE(replay_safe_input_modifier_is_reevaluated_for_a_shortened_step)
{
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto target = std::make_shared<shortening_event_slave>(to_duration(0.5));
        slave_simulator b(target, "target");
        algorithm->add_simulator(1, &b, duration::zero());
        b.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        std::vector<duration> steps;
        b.set_integer_input_modifier(mock_slave::integer_in_reference,
            [&steps](int value, duration step) {
                steps.push_back(step);
                return step < to_duration(1.0) ? value + 100 : value;
            });
        b.set_input_modifier_replay_safe(variable_type::integer, mock_slave::integer_in_reference, true);
        b.set_integer(mock_slave::integer_in_reference, 5);
        const auto result = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!result.termination_requested);
        BOOST_CHECK(result.step_size == to_duration(0.5));
        BOOST_REQUIRE(steps.size() >= 2);
        BOOST_CHECK(steps.front() == to_duration(1.0));
        BOOST_CHECK(steps.back() == to_duration(0.5));
        int applied = 0;
        const auto reference = mock_slave::integer_in_reference;
        target->get_integer_variables(gsl::make_span(&reference, 1), gsl::make_span(&applied, 1));
        BOOST_TEST(applied >= 100);
    }
}

BOOST_AUTO_TEST_CASE(non_replay_safe_input_modifier_rejects_a_shortened_step)
{
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto target = std::make_shared<shortening_event_slave>(to_duration(0.5));
        slave_simulator b(target, "target");
        algorithm->add_simulator(1, &b, duration::zero());
        b.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        b.set_integer_input_modifier(mock_slave::integer_in_reference,
            [](int value, duration) { return value; });
        b.set_integer(mock_slave::integer_in_reference, 5);
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(to_time_point(0.0)), cosim::error,
            [](const cosim::error& e) {
                return e.code() == make_error_code(errc::unsupported_feature) &&
                    std::string(e.what()).find("replay-safe") != std::string::npos;
            });
    }
}

BOOST_AUTO_TEST_CASE(output_and_legacy_modifiers_accept_a_shortened_step)
{
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto target = std::make_shared<shortening_event_slave>(to_duration(0.5));
        auto legacy = std::make_shared<mock_slave>();
        slave_simulator b(target, "target"), c(legacy, "legacy");
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        b.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        b.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
        c.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        std::vector<duration> outputSteps;
        std::vector<duration> legacySteps;
        b.set_integer_output_modifier(mock_slave::integer_out_reference,
            [&outputSteps](int value, duration step) {
                outputSteps.push_back(step);
                return value;
            });
        c.set_integer_input_modifier(mock_slave::integer_in_reference,
            [&legacySteps](int value, duration step) {
                legacySteps.push_back(step);
                return value;
            });
        b.set_integer(mock_slave::integer_in_reference, 5);
        c.set_integer(mock_slave::integer_in_reference, 7);
        BOOST_CHECK(algorithm->do_step_with_info(to_time_point(0.0)).step_size == to_duration(0.5));
        BOOST_REQUIRE(!outputSteps.empty());
        BOOST_CHECK(outputSteps.back() == to_duration(0.5));
        BOOST_REQUIRE(!legacySteps.empty());
        BOOST_CHECK(legacySteps.back() == to_duration(0.5));
    }
}

BOOST_AUTO_TEST_CASE(replay_safe_whole_array_input_modifier)
{
    const auto path = array_fixture_directory() / "ArrayFeedthrough.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto reference = variable_references(*fmu->model_description()).at("String_array");
    slave_simulator sim(fmu->instantiate_v3_slave("replay-safe-array"), "replay-safe-array");
    sim.expose_for_setting(variable_type::string, reference);
    sim.setup(to_time_point(0.0), std::nullopt, std::nullopt);
    sim.start_simulation();
    int calls = 0;
    sim.set_variable_input_modifier(variable_type::string, reference,
        [&calls](const variable_value& value, duration) { ++calls; return value; });
    sim.set_input_modifier_replay_safe(variable_type::string, reference, true);
    auto* controller = sim.get_event_controller();
    BOOST_REQUIRE(controller != nullptr);
    for (const auto& value : {
             variable_value(string_values{{"first", "array"}}, {2}),
             variable_value(string_values{{"second", "array"}}, {2})}) {
        sim.set_variable_value(variable_type::string, reference, value);
        BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0), calls == 0));
        BOOST_CHECK_THROW(sim.set_input_modifier_replay_safe(
            variable_type::string, reference, false), cosim::error);
        controller->enter_event_mode();
        sim.apply_event_inputs();
        BOOST_CHECK(!controller->update_event_state().discrete_states_need_update);
        controller->enter_step_mode();
    }
    BOOST_TEST(calls == 2);
    sim.finish_input_event_coordination();
}

BOOST_AUTO_TEST_CASE(unchanged_nan_values_do_not_create_input_or_output_events)
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto nan32 = std::numeric_limits<float>::quiet_NaN();
    const variable_value floatArray(float32_values{{1.0F, nan32}}, {2});
    const variable_value realArray(real_values{{1.0, nan}}, {2});
    BOOST_TEST(detail::event_values_equal(
        floatArray, variable_value(float32_values{{1.0F, nan32}}, {2})));
    BOOST_TEST(!detail::event_values_equal(
        floatArray, variable_value(float32_values{{2.0F, nan32}}, {2})));
    BOOST_TEST(detail::event_values_equal(
        realArray, variable_value(real_values{{1.0, nan}}, {2})));
    BOOST_TEST(!detail::event_values_equal(
        realArray, variable_value(real_values{{1.0, nan}}, {1, 2})));
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = std::make_shared<mock_slave>(
            [nan](double) { return nan; });
        auto legacy = std::make_shared<mock_slave>();
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "nan-source"), b(legacy, "legacy-middle"), c(target, "event-target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::real, mock_slave::real_out_reference},
            variable_id{1, variable_type::real, mock_slave::real_in_reference});
        algorithm->connect_variables(
            variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{2, variable_type::integer, mock_slave::integer_in_reference});
        c.expose_for_setting(variable_type::real, event_coordination_slave::tunable_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        c.set_real(event_coordination_slave::tunable_reference, nan);
        algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(target->eventEntries == 1);
        c.set_real(event_coordination_slave::tunable_reference, nan);
        algorithm->do_step_with_info(to_time_point(1.0));
        BOOST_TEST(target->eventEntries == 1);
    }

    auto instance = std::make_shared<event_coordination_slave>();
    slave_simulator sim(instance, "nan-modifier");
    sim.expose_for_getting(variable_type::real, mock_slave::real_out_reference);
    sim.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
    sim.setup(to_time_point(0.0), std::nullopt, std::nullopt);
    sim.start_simulation();
    int calls = 0;
    sim.set_real_output_modifier(mock_slave::real_out_reference,
        [&calls](double value, duration) { ++calls; return value; });
    sim.refresh_event_values();
    sim.set_real(mock_slave::real_in_reference, nan);
    sim.signal_input_discontinuity(variable_type::real, mock_slave::real_in_reference);
    BOOST_REQUIRE(sim.prepare_input_events(to_duration(1.0)));
    auto* controller = sim.get_event_controller();
    controller->enter_event_mode();
    sim.apply_event_inputs();
    controller->update_event_state();
    sim.refresh_event_values();
    sim.refresh_event_values();
    BOOST_TEST(calls == 2);
    controller->enter_step_mode();
}

BOOST_AUTO_TEST_CASE(event_driven_function_chains_propagate)
{
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = std::make_shared<event_coordination_slave>();
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "function-source"), b(target, "function-target");
        linear_transformation_function first(0.0, 2.0), second(1.0, 1.0);
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_function(0, &first);
        algorithm->add_function(1, &second);
        algorithm->connect_variables(
            variable_id{0, variable_type::real, mock_slave::real_out_reference},
            function_io_id{0, variable_type::real,
                linear_transformation_function::in_io_reference});
        algorithm->connect_variables(
            function_io_id{0, variable_type::real,
                linear_transformation_function::out_io_reference},
            function_io_id{1, variable_type::real,
                linear_transformation_function::in_io_reference});
        algorithm->connect_variables(
            function_io_id{1, variable_type::real,
                linear_transformation_function::out_io_reference},
            variable_id{1, variable_type::real, mock_slave::real_in_reference});
        a.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        b.expose_for_getting(variable_type::real, mock_slave::real_out_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        a.set_real(mock_slave::real_in_reference, 3.0);
        a.signal_input_discontinuity(variable_type::real, mock_slave::real_in_reference);
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        BOOST_TEST(b.get_real(mock_slave::real_out_reference) == 7.0);
        BOOST_TEST(target->eventEntries == 1);
        BOOST_TEST(source->steps == 1);
        BOOST_TEST(target->steps == 1);
    }
}

BOOST_AUTO_TEST_CASE(mixed_event_capabilities_warn_for_connected_participants)
{
    for (const bool throughFunctions : {false, true}) {
        for (const bool eventIsSource : {false, true}) {
            for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
                warning_capture warnings;
                auto event = std::make_shared<event_coordination_slave>();
                auto legacy = std::make_shared<mock_slave>();
                auto unrelated = std::make_shared<mock_slave>();
                slave_simulator a(event, "event-participant"), b(legacy, "legacy-peer"),
                    c(unrelated, "unrelated");
                linear_transformation_function first(0.0, 1.0), second(0.0, 1.0);
                algorithm->add_simulator(0, &a, duration::zero());
                algorithm->add_simulator(1, &b, duration::zero());
                algorithm->add_simulator(2, &c, duration::zero());
                const variable_id output{
                    eventIsSource ? 0 : 1, variable_type::real, mock_slave::real_out_reference};
                const variable_id input{
                    eventIsSource ? 1 : 0, variable_type::real, mock_slave::real_in_reference};
                if (throughFunctions) {
                    algorithm->add_function(0, &first);
                    algorithm->add_function(1, &second);
                    algorithm->connect_variables(output, function_io_id{
                        0, variable_type::real, linear_transformation_function::in_io_reference});
                    algorithm->connect_variables(function_io_id{
                        0, variable_type::real, linear_transformation_function::out_io_reference},
                        function_io_id{1, variable_type::real,
                            linear_transformation_function::in_io_reference});
                    algorithm->connect_variables(function_io_id{
                        1, variable_type::real, linear_transformation_function::out_io_reference},
                        input);
                } else {
                    algorithm->connect_variables(output, input);
                }
                algorithm->setup(to_time_point(0.0), std::nullopt);
                algorithm->initialize();
                const auto messages = warnings.messages();
                const std::string expected = "event-participant is connected to legacy-peer";
                const auto position = messages.find(expected);
                BOOST_REQUIRE(position != std::string::npos);
                BOOST_TEST(messages.find(expected, position + expected.size()) == std::string::npos);
                BOOST_TEST(messages.find("does not support Event Mode") != std::string::npos);
                BOOST_TEST(messages.find("cannot save and restore state") != std::string::npos);
                BOOST_TEST(messages.find("unrelated") == std::string::npos);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(event_driven_multi_input_functions_and_unchanged_outputs)
{
    for (const bool unchanged : {false, true}) {
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            auto first = std::make_shared<event_coordination_slave>();
            auto second = std::make_shared<event_coordination_slave>();
            auto target = std::make_shared<event_coordination_slave>();
            slave_simulator a(first, "sum-first"), b(second, "sum-second"),
                c(target, "sum-target");
            vector_sum_function<double> sum(2, 1);
            linear_transformation_function scale(unchanged ? 5.0 : 0.0, unchanged ? 0.0 : 1.0);
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->add_simulator(2, &c, duration::zero());
            algorithm->add_function(0, &sum);
            algorithm->add_function(1, &scale);
            for (int i = 0; i < 2; ++i) {
                algorithm->connect_variables(
                    variable_id{i, variable_type::real, mock_slave::real_out_reference},
                    function_io_id{0, variable_type::real,
                        vector_sum_function<double>::in_io_reference(i, 0)});
            }
            algorithm->connect_variables(
                function_io_id{0, variable_type::real,
                    vector_sum_function<double>::out_io_reference(0)},
                function_io_id{1, variable_type::real,
                    linear_transformation_function::in_io_reference});
            algorithm->connect_variables(
                function_io_id{1, variable_type::real,
                    linear_transformation_function::out_io_reference},
                variable_id{2, variable_type::real, mock_slave::real_in_reference});
            for (auto* sim : {&a, &b}) {
                sim->expose_for_setting(variable_type::real, mock_slave::real_in_reference);
            }
            c.expose_for_getting(variable_type::real, mock_slave::real_out_reference);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            a.set_real(mock_slave::real_in_reference, 2.0);
            b.set_real(mock_slave::real_in_reference, 3.0);
            a.signal_input_discontinuity(variable_type::real, mock_slave::real_in_reference);
            b.signal_input_discontinuity(variable_type::real, mock_slave::real_in_reference);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
            BOOST_TEST(c.get_real(mock_slave::real_out_reference) == 5.0);
            BOOST_TEST(target->eventEntries == (unchanged ? 0 : 1));
        }
    }
}

BOOST_AUTO_TEST_CASE(event_driven_unsafe_functions_are_rejected)
{
    struct unsafe_function : linear_transformation_function
    {
        unsafe_function() : linear_transformation_function(0.0, 1.0) {}
    };
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto instance = std::make_shared<event_coordination_slave>();
        slave_simulator sim(instance, "function-source");
        unsafe_function function;
        algorithm->add_simulator(0, &sim, duration::zero());
        algorithm->add_function(0, &function);
        algorithm->connect_variables(
            variable_id{0, variable_type::real, mock_slave::real_out_reference},
            function_io_id{0, variable_type::real,
                linear_transformation_function::in_io_reference});
        sim.expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sim.set_real(mock_slave::real_in_reference, 3.0);
        sim.signal_input_discontinuity(variable_type::real, mock_slave::real_in_reference);
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(to_time_point(0.0)), cosim::error,
            [](const cosim::error& e) {
                return e.code() == make_error_code(errc::unsupported_feature) &&
                    std::string(e.what()).find("repeated evaluation") != std::string::npos;
            });
        BOOST_TEST(instance->steps == 0);
    }
}

BOOST_AUTO_TEST_CASE(event_driven_functions_preserve_exact_arrays)
{
    struct array_event_slave : mock_slave, event_controller
    {
        enum : value_reference { input_reference = 10, output_reference = 11 };

        cosim::model_description model_description() const override
        {
            cosim::model_description model;
            model.name = "array-event";
            for (const auto& entry : {std::pair{input_reference, variable_causality::input},
                                     std::pair{output_reference, variable_causality::output}}) {
                variable_description variable{};
                variable.name = entry.second == variable_causality::input ? "input" : "output";
                variable.type = variable_type::uint64;
                variable.reference = entry.first;
                variable.causality = entry.second;
                variable.variability = variable_variability::discrete;
                variable.dimensions.emplace_back(fixed_dimension{2});
                if (entry.second == variable_causality::input) variable.array_start = input;
                model.variables.push_back(std::move(variable));
            }
            return model;
        }
        event_controller* get_event_controller() noexcept override { return this; }
        bool is_in_event_mode() const noexcept override { return inEventMode; }
        void enter_event_mode() override { inEventMode = true; ++events; }
        event_update_result update_event_state() override { output = input; return {}; }
        void enter_step_mode() override { inEventMode = false; }
        void start_simulation() override { running = true; }
        std::vector<variable_value> get_variable_values(
            gsl::span<const typed_value_reference> references) const override
        {
            std::vector<variable_value> result;
            for (const auto reference : references) {
                if (reference.type != variable_type::uint64) {
                    throw std::invalid_argument("Invalid array type");
                }
                if (reference.reference == input_reference) result.push_back(input);
                else if (reference.reference == output_reference) result.push_back(output);
                else throw std::out_of_range("Invalid array reference");
            }
            return result;
        }
        void set_variable_values(
            gsl::span<const typed_value_reference> references,
            gsl::span<const variable_value> values) override
        {
            if (references.size() != values.size()) throw std::invalid_argument("Invalid count");
            for (std::size_t i = 0; i < references.size(); ++i) {
                if (references[i].type != variable_type::uint64 ||
                    references[i].reference != input_reference ||
                    values[i].type() != variable_type::uint64 ||
                    values[i].dimensions() != std::vector<std::uint64_t>{2}) {
                    throw std::invalid_argument("Invalid array input");
                }
                if (running && !inEventMode && input != values[i]) {
                    throw std::logic_error("Array input changed outside Event Mode");
                }
                input = values[i];
            }
        }
        bool inEventMode = false;
        bool running = false;
        int events = 0;
        variable_value input{uint64_values{{0, 0}}, {2}};
        variable_value output{uint64_values{{0, 0}}, {2}};
    };

    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto first = std::make_shared<array_event_slave>();
        auto second = std::make_shared<array_event_slave>();
        auto target = std::make_shared<array_event_slave>();
        slave_simulator a(first, "array-first"), b(second, "array-second"),
            c(target, "array-target");
        exact_array_function sum;
        for (const auto& entry : {std::pair{0, &a}, std::pair{1, &b}, std::pair{2, &c}}) {
            algorithm->add_simulator(entry.first, entry.second, duration::zero());
        }
        algorithm->add_function(0, &sum);
        for (int i = 0; i < 2; ++i) {
            algorithm->connect_variables(
                variable_id{i, variable_type::uint64, array_event_slave::output_reference},
                function_io_id{0, variable_type::uint64, exact_array_function::input_reference(i)});
        }
        algorithm->connect_variables(
            function_io_id{0, variable_type::uint64, exact_array_function::output_reference},
            variable_id{2, variable_type::uint64, array_event_slave::input_reference});
        for (auto* sim : {&a, &b}) {
            sim->expose_for_setting(variable_type::uint64, array_event_slave::input_reference);
        }
        c.expose_for_getting(variable_type::uint64, array_event_slave::output_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        a.set_variable_value(variable_type::uint64, array_event_slave::input_reference,
            variable_value(uint64_values{{9007199254740993ULL, 18446744073709551600ULL}}, {2}));
        b.set_variable_value(variable_type::uint64, array_event_slave::input_reference,
            variable_value(uint64_values{{3, 5}}, {2}));
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        BOOST_CHECK(c.get_variable_value(variable_type::uint64, array_event_slave::output_reference) ==
            variable_value(uint64_values{{9007199254740996ULL, 18446744073709551605ULL}}, {2}));
        BOOST_TEST(target->events == 1);
    }
}

BOOST_AUTO_TEST_CASE(event_function_uses_current_values_from_other_simulators)
{
    struct step_event_source : mock_slave
    {
        step_result_info do_step_with_info(time_point current, duration delta) override
        {
            mock_slave::do_step(current, delta);
            return {step_result::complete, current + delta, false, true, false, false};
        }
        void handle_events() override { output = 2.0; }
        void get_real_variables(
            gsl::span<const value_reference> references,
            gsl::span<double> values) const override
        {
            for (std::size_t i = 0; i < references.size(); ++i) {
                if (references[i] == real_out_reference) {
                    values[i] = output;
                } else {
                    mock_slave::get_real_variables(
                        gsl::make_span(&references[i], 1), gsl::make_span(&values[i], 1));
                }
            }
        }
        double output = 0.0;
    };

    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = std::make_shared<step_event_source>();
        auto other = std::make_shared<mock_slave>(
            [](time_point time, duration, double) { return to_double_time_point(time); });
        auto target = std::make_shared<event_coordination_slave>();
        slave_simulator a(source, "event-source"), b(other, "stepped-source"),
            c(target, "event-target");
        vector_sum_function<double> sum(2, 1);
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->add_simulator(2, &c, duration::zero());
        algorithm->add_function(0, &sum);
        for (int i = 0; i < 2; ++i) {
            algorithm->connect_variables(
                variable_id{i, variable_type::real, mock_slave::real_out_reference},
                function_io_id{0, variable_type::real,
                    vector_sum_function<double>::in_io_reference(i, 0)});
        }
        algorithm->connect_variables(
            function_io_id{0, variable_type::real,
                vector_sum_function<double>::out_io_reference(0)},
            variable_id{2, variable_type::real, mock_slave::real_in_reference});
        c.expose_for_getting(variable_type::real, mock_slave::real_out_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        BOOST_TEST(c.get_real(mock_slave::real_out_reference) == 3.0);
        BOOST_TEST(target->eventEntries == 1);
    }
}

BOOST_AUTO_TEST_CASE(event_feedback_through_function_uses_iteration_limit)
{
    const auto step = to_duration(1.0);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15, 3};
    const std::vector<std::shared_ptr<algorithm>> algorithms{
        std::make_shared<fixed_step_algorithm>(fixed_step_algorithm_params{step, 3}, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        warning_capture warnings;
        auto source = std::make_shared<event_coordination_slave>(true);
        slave_simulator sim(source, "function-feedback");
        vector_sum_function<int> function(1, 1);
        algorithm->add_simulator(0, &sim, duration::zero());
        algorithm->add_function(0, &function);
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            function_io_id{0, variable_type::integer,
                vector_sum_function<int>::in_io_reference(0, 0)});
        algorithm->connect_variables(
            function_io_id{0, variable_type::integer,
                vector_sum_function<int>::out_io_reference(0)},
            variable_id{0, variable_type::integer, mock_slave::integer_in_reference});
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sim.set_integer(mock_slave::integer_in_reference, 2);
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
        BOOST_TEST(source->eventEntries == 1);
        BOOST_TEST(source->updates == 3);
        BOOST_TEST(source->steps == 1);
        BOOST_TEST(warnings.messages().find("did not converge within 3 rounds") != std::string::npos);
        BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
        BOOST_TEST(source->eventEntries == 2);
        BOOST_TEST(source->updates == 6);
        BOOST_TEST(source->steps == 2);
    }
}

BOOST_AUTO_TEST_CASE(ordinary_input_events_activate_connected_clocks)
{
    struct clock_event_slave : mock_slave, clock_controller
    {
        enum : value_reference { inputClock = 10, outputClock = 11 };

        cosim::model_description model_description() const override
        {
            auto model = mock_slave::model_description();
            for (const auto& pair : {std::pair{inputClock, variable_causality::input},
                                     std::pair{outputClock, variable_causality::output}}) {
                variable_description clock{};
                clock.name = pair.second == variable_causality::input ? "inputClock" : "outputClock";
                clock.reference = pair.first;
                clock.type = variable_type::clock;
                clock.causality = pair.second;
                clock.clock = clock_description{};
                clock.clock->interval_variability = clock_interval_variability::triggered;
                model.variables.push_back(std::move(clock));
            }
            return model;
        }
        event_controller* get_event_controller() noexcept override { return this; }
        clock_controller* get_clock_controller() noexcept override { return this; }
        bool is_in_event_mode() const noexcept override { return inEventMode; }
        void enter_event_mode() override { inEventMode = true; }
        void enter_step_mode() override { inEventMode = false; outputActive = false; }
        bool get_output_clock(value_reference) override { return outputActive; }
        void set_input_clock(value_reference, bool active) override
        {
            if (!active) return;
            ++ticks;
            ++pendingTicks;
            if (deactivateOnTick) outputActive = false;
            if (emitOnTick) outputActive = true;
        }
        variable_value get_clocked_variable(variable_type, value_reference) override
        {
            throw std::logic_error("Unexpected Clock value read");
        }
        void set_clocked_variable(variable_type, value_reference, const variable_value&) override
        {
            throw std::logic_error("Unexpected Clock value write");
        }
        clock_update_result update_clock_state() override
        {
            outputActive = false;
            const auto reference = integer_in_reference;
            int current = 0;
            mock_slave::get_integer_variables(gsl::make_span(&reference, 1), gsl::make_span(&current, 1));
            if (current != lastInput) {
                outputActive = emitClock;
                lastInput = current;
            }
            if (emitAfterTick && pendingTicks != 0) outputActive = true;
            pendingTicks = 0;
            return {alwaysUpdate, false, {}};
        }
        void get_integer_variables(gsl::span<const value_reference> refs, gsl::span<int> values) const override
        {
            for (std::size_t i = 0; i < refs.size(); ++i) {
                if (refs[i] == integer_out_reference) values[i] = lastInput + ticks * 2;
                else mock_slave::get_integer_variables(gsl::make_span(&refs[i], 1), gsl::make_span(&values[i], 1));
            }
        }
        step_result do_step(time_point time, duration delta) override
        {
            ++steps;
            return mock_slave::do_step(time, delta);
        }
        bool inEventMode = false;
        bool outputActive = false;
        bool emitClock = true;
        bool emitOnTick = false;
        bool emitAfterTick = false;
        bool deactivateOnTick = false;
        bool alwaysUpdate = false;
        int lastInput = 0;
        int ticks = 0;
        int pendingTicks = 0;
        int steps = 0;
    };

    for (const auto& scenario : {
             std::pair{false, 0U}, std::pair{false, 2U}, std::pair{true, 1U},
             std::pair{true, 2U}, std::pair{true, 3U}, std::pair{true, 5U}}) {
        const auto feedback = scenario.first;
        const std::size_t limit = scenario.second;
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            warning_capture warnings;
            if (limit != 0) {
                if (const auto fixed = std::dynamic_pointer_cast<fixed_step_algorithm>(algorithm)) {
                    fixed->set_max_event_iterations(limit);
                } else {
                    std::dynamic_pointer_cast<ecco_algorithm>(algorithm)->set_max_event_iterations(limit);
                }
            }
            auto source = std::make_shared<clock_event_slave>();
            auto target = std::make_shared<clock_event_slave>();
            slave_simulator a(source, "clock-source"), b(target, "clock-target");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::clock, clock_event_slave::outputClock},
                variable_id{1, variable_type::clock, clock_event_slave::inputClock});
            if (feedback) {
                algorithm->connect_variables(
                    variable_id{1, variable_type::integer, mock_slave::integer_out_reference},
                    variable_id{0, variable_type::integer, mock_slave::integer_in_reference});
            }
            a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            a.set_integer(mock_slave::integer_in_reference, 1);
            if (feedback) {
                BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
                BOOST_TEST(source->steps == 1);
                const int ticks = static_cast<int>((limit - 1) / 2);
                BOOST_TEST(source->lastInput == (limit == 1 ? 0 : ticks <= 1 ? 1 : (ticks - 1) * 2));
                BOOST_TEST(target->ticks == ticks);
                BOOST_TEST(warnings.messages().find(
                    "did not converge within " + std::to_string(limit) + " rounds") != std::string::npos);
                BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
                BOOST_TEST(source->steps == 2);
                BOOST_TEST(target->ticks == ticks + static_cast<int>(limit / 2));
            } else {
                const auto result = algorithm->do_step_with_info(to_time_point(0.0));
                BOOST_TEST(!result.termination_requested);
                BOOST_TEST(source->steps == 1);
                BOOST_TEST(target->ticks == (limit == 0 ? 1 : 0));
                if (limit != 0) {
                    BOOST_TEST(warnings.messages().find("dropped undelivered Clock ticks") != std::string::npos);
                    BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
                    BOOST_TEST(target->ticks == 0);
            }
          }
        }
    }
    {
        const auto path = reference_fmu_directory() / "StrictClock.fmu";
        require_fixture(path);
        const auto fmu = shared_clock_fixture(path);
        for (const auto& algorithm : event_algorithms(to_duration(0.5))) {
            warning_capture warnings;
            if (const auto fixed = std::dynamic_pointer_cast<fixed_step_algorithm>(algorithm)) {
                fixed->set_max_event_iterations(5);
            } else {
                std::dynamic_pointer_cast<ecco_algorithm>(algorithm)->set_max_event_iterations(5);
            }
            auto source = std::make_shared<clock_event_slave>();
            auto target = fmu->instantiate_v3_slave("strict-feedback-target");
            slave_simulator a(source, "clock-source"), b(target, "strict-feedback-target");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::clock, clock_event_slave::outputClock},
                variable_id{1, variable_type::clock, 101});
            algorithm->connect_variables(
                variable_id{1, variable_type::integer, 202},
                variable_id{0, variable_type::integer, mock_slave::integer_in_reference});
            a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
            b.expose_for_setting(variable_type::integer, 200);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            b.set_integer(200, 4);
            a.set_integer(mock_slave::integer_in_reference, 1);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
            const value_reference updates = 203;
            int count = 0;
            target->get_integer_variables(gsl::make_span(&updates, 1), gsl::make_span(&count, 1));
            BOOST_TEST(count == 3);
            BOOST_TEST(source->lastInput == 5);
            BOOST_TEST(warnings.messages().find("did not converge") == std::string::npos);
        }
    }
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = std::make_shared<clock_event_slave>();
        auto target = std::make_shared<clock_event_slave>();
        source->deactivateOnTick = true;
        target->emitOnTick = true;
        slave_simulator a(source, "deactivating-source"), b(target, "deactivating-target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::clock, clock_event_slave::outputClock},
            variable_id{1, variable_type::clock, clock_event_slave::inputClock});
        algorithm->connect_variables(
            variable_id{1, variable_type::clock, clock_event_slave::outputClock},
            variable_id{0, variable_type::clock, clock_event_slave::inputClock});
        a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        a.set_integer(mock_slave::integer_in_reference, 1);
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(to_time_point(0.0)), cosim::error,
            error_is(errc::unsupported_feature));
        BOOST_TEST(source->steps == 0);
    }
    for (const std::size_t limit : {2U, 5U, 105U}) {
        for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
            warning_capture warnings;
            if (const auto fixed = std::dynamic_pointer_cast<fixed_step_algorithm>(algorithm)) {
                fixed->set_max_event_iterations(limit);
            } else {
                std::dynamic_pointer_cast<ecco_algorithm>(algorithm)->set_max_event_iterations(limit);
            }
            auto source = std::make_shared<clock_event_slave>();
            auto target = std::make_shared<clock_event_slave>();
            source->emitAfterTick = true;
            target->emitAfterTick = true;
            slave_simulator a(source, "repeated-clock-a"), b(target, "repeated-clock-b");
            algorithm->add_simulator(0, &a, duration::zero());
            algorithm->add_simulator(1, &b, duration::zero());
            algorithm->connect_variables(
                variable_id{0, variable_type::clock, clock_event_slave::outputClock},
                variable_id{1, variable_type::clock, clock_event_slave::inputClock});
            algorithm->connect_variables(
                variable_id{1, variable_type::clock, clock_event_slave::outputClock},
                variable_id{0, variable_type::clock, clock_event_slave::inputClock});
            a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
            algorithm->setup(to_time_point(0.0), std::nullopt);
            algorithm->initialize();
            a.set_integer(mock_slave::integer_in_reference, 1);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(0.0)).termination_requested);
            const auto delivered = limit - 2;
            BOOST_TEST(target->ticks == static_cast<int>((delivered + 1) / 2));
            BOOST_TEST(source->ticks == static_cast<int>(delivered / 2));
            BOOST_TEST(warnings.messages().find(
                "did not converge within " + std::to_string(limit) + " rounds") != std::string::npos);
            BOOST_TEST(!algorithm->do_step_with_info(to_time_point(1.0)).termination_requested);
            BOOST_TEST(target->ticks == static_cast<int>((delivered + 1) / 2));
            BOOST_TEST(source->ticks == static_cast<int>(delivered / 2));
        }
    }
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        if (const auto fixed = std::dynamic_pointer_cast<fixed_step_algorithm>(algorithm)) {
            fixed->set_max_event_iterations(2);
        } else {
            std::dynamic_pointer_cast<ecco_algorithm>(algorithm)->set_max_event_iterations(2);
        }
        auto source = std::make_shared<clock_event_slave>();
        auto target = std::make_shared<clock_event_slave>();
        slave_simulator a(source, "nonconvergent-source"), b(target, "nonconvergent-target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::clock, clock_event_slave::outputClock},
            variable_id{1, variable_type::clock, clock_event_slave::inputClock});
        a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        source->alwaysUpdate = true;
        a.set_integer(mock_slave::integer_in_reference, 1);
        BOOST_CHECK_EXCEPTION(
            algorithm->do_step_with_info(to_time_point(0.0)), cosim::error,
            error_is(errc::simulation_error));
        BOOST_TEST(source->steps == 0);
    }
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto source = std::make_shared<clock_event_slave>();
        auto target = std::make_shared<clock_event_slave>();
        source->emitClock = false;
        target->emitClock = false;
        slave_simulator a(source, "ordinary-clock-source"), b(target, "ordinary-clock-target");
        algorithm->add_simulator(0, &a, duration::zero());
        algorithm->add_simulator(1, &b, duration::zero());
        algorithm->connect_variables(
            variable_id{0, variable_type::integer, mock_slave::integer_out_reference},
            variable_id{1, variable_type::integer, mock_slave::integer_in_reference});
        a.expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        b.expose_for_getting(variable_type::integer, mock_slave::integer_out_reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        a.set_integer(mock_slave::integer_in_reference, 1);
        const auto result = algorithm->do_step_with_info(to_time_point(0.0));
        BOOST_TEST(!result.termination_requested);
        BOOST_TEST(b.get_integer(mock_slave::integer_out_reference) == 1);
        BOOST_TEST(source->steps == 1);
        BOOST_TEST(target->steps == 1);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_whole_array_input_events)
{
    const auto path = array_fixture_directory() / "ArrayFeedthrough.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto references = variable_references(*fmu->model_description());
    const std::vector<typed_value_reference> variables{
        {variable_type::string, references.at("String_array")},
        {variable_type::binary, references.at("Binary_array")},
        {variable_type::enumeration, references.at("Enumeration_array")}};
    const std::vector<variable_value> values{
        variable_value(string_values{{"event", "array"}}, {2}),
        variable_value(binary_values{{binary_value{{1, 2}}, binary_value{{3}}}}, {2}),
        variable_value(enumeration_values{{enumeration_value{2}, enumeration_value{1}}}, {2})};
    const auto step = to_duration(0.1);
    for (const auto& algorithm : event_algorithms(step)) {
        auto instance = fmu->instantiate_v3_slave("array-input-events");
        slave_simulator sim(instance, "array-input-events");
        algorithm->add_simulator(0, &sim, duration::zero());
        for (const auto variable : variables)
            sim.expose_for_setting(variable.type, variable.reference);
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        for (std::size_t i = 0; i < variables.size(); ++i)
            sim.set_variable_value(variables[i].type, variables[i].reference, values[i]);
        algorithm->do_step_with_info(to_time_point(0.0));
        const auto actual = instance->get_variable_values(gsl::make_span(variables));
        BOOST_REQUIRE_EQUAL(actual.size(), values.size());
        for (std::size_t i = 0; i < values.size(); ++i)
            BOOST_CHECK(actual[i] == values[i]);
    }
}

BOOST_AUTO_TEST_CASE(nonclocked_inputs_on_clocked_fmi3_are_processed_with_clocks)
{
    const auto path = fault_fixture("Enumeration.fmu");
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto references = variable_references(*fmu->model_description());
    const auto scalar = references.at("scalarInput");
    const auto array = references.at("arrayInput");
    const auto step = to_duration(0.1);
    for (const auto& algorithm : event_algorithms(step)) {
        auto instance = fmu->instantiate_v3_slave("clocked-ordinary-inputs");
        auto secondInstance = fmu->instantiate_v3_slave("clocked-ordinary-inputs-second");
        slave_simulator sim(instance, "clocked-ordinary-inputs");
        slave_simulator second(secondInstance, "clocked-ordinary-inputs-second");
        algorithm->add_simulator(0, &sim, duration::zero());
        algorithm->add_simulator(1, &second, duration::zero());
        sim.expose_for_setting(variable_type::enumeration, scalar);
        sim.expose_for_setting(variable_type::enumeration, array);
        second.expose_for_setting(variable_type::enumeration, scalar);
        sim.set_variable_value(variable_type::enumeration, array,
            variable_value(enumeration_values{{enumeration_value{1}, enumeration_value{2}}}, {2}));
        algorithm->setup(to_time_point(0.0), std::nullopt);
        algorithm->initialize();
        sim.set_variable_value(variable_type::enumeration, scalar,
            variable_value(enumeration_values{{enumeration_value{-1}}}));
        second.set_variable_value(variable_type::enumeration, scalar,
            variable_value(enumeration_values{{enumeration_value{4}}}));
        const auto arrayValue = variable_value(
            enumeration_values{{enumeration_value{4}, enumeration_value{-1}}}, {2});
        sim.set_variable_value(variable_type::enumeration, array, arrayValue);
        algorithm->do_step_with_info(to_time_point(0.0));
        const std::array<typed_value_reference, 2> variables{{
            {variable_type::enumeration, scalar},
            {variable_type::enumeration, array}}};
        const auto values = instance->get_variable_values(gsl::make_span(variables));
        BOOST_TEST(std::get<enumeration_values>(values[0].values()).values[0].value == -1);
        BOOST_CHECK(values[1] == arrayValue);
        const auto secondValue = secondInstance->get_variable_values(
            gsl::make_span(variables.data(), 1));
        BOOST_TEST(std::get<enumeration_values>(secondValue[0].values()).values[0].value == 4);
    }
}

BOOST_AUTO_TEST_CASE(manipulator_can_signal_a_continuous_input_jump)
{
    struct jump_manipulator : manipulator
    {
        void simulator_added(simulator_index, manipulable* sim, time_point) override
        {
            target = sim;
            target->expose_for_setting(variable_type::real, mock_slave::real_in_reference);
        }
        void simulator_removed(simulator_index, time_point) override { target = nullptr; }
        void step_commencing(time_point time) override
        {
            if (time == to_time_point(0.1)) {
                target->set_real_input_modifier(
                    mock_slave::real_in_reference, [](double, duration) { return 3.0; });
                target->signal_input_discontinuity(
                    variable_type::real, mock_slave::real_in_reference);
            }
        }
        manipulable* target = nullptr;
    };

    const auto step = to_duration(0.1);
    for (const auto& algorithm : event_algorithms(step)) {
        auto instance = std::make_shared<event_coordination_slave>();
        auto manipulator = std::make_shared<jump_manipulator>();
        execution execution(to_time_point(0.0), algorithm);
        execution.add_manipulator(manipulator);
        execution.add_slave(instance, "signal-target");
        execution.step();
        BOOST_TEST(instance->eventEntries == 0);
        execution.step();
        BOOST_TEST(instance->eventEntries == 1);
        BOOST_TEST(instance->steps == 2);
    }
}

BOOST_AUTO_TEST_CASE(termination_before_a_step_does_not_report_a_completed_step)
{
    struct input_manipulator : manipulator
    {
        void simulator_added(simulator_index, manipulable* sim, time_point) override
        {
            target = sim;
            target->expose_for_setting(variable_type::integer, mock_slave::integer_in_reference);
        }
        void simulator_removed(simulator_index, time_point) override { target = nullptr; }
        void step_commencing(time_point) override
        {
            target->set_integer_input_modifier(
                mock_slave::integer_in_reference, [](int, duration) { return 1; });
        }
        manipulable* target = nullptr;
    };
    struct counting_observer : observer
    {
        void simulator_added(simulator_index, observable*, time_point) override {}
        void simulator_removed(simulator_index, time_point) override {}
        void variables_connected(variable_id, variable_id, time_point) override {}
        void variable_disconnected(variable_id, time_point) override {}
        void simulation_initialized(step_number, time_point) override {}
        void step_complete(step_number, duration, time_point) override { ++steps; }
        void simulator_step_complete(simulator_index, step_number, duration, time_point) override {}
        void state_restored(step_number, time_point) override {}
        int steps = 0;
    };
    for (const auto& algorithm : event_algorithms(to_duration(1.0))) {
        auto instance = std::make_shared<event_coordination_slave>();
        instance->terminateOnUpdate = true;
        auto man = std::make_shared<input_manipulator>();
        auto obs = std::make_shared<counting_observer>();
        execution execution(to_time_point(0.0), algorithm);
        execution.add_manipulator(man);
        execution.add_observer(obs);
        execution.add_slave(instance, "terminating");
        BOOST_CHECK(execution.step() == duration::zero());
        BOOST_TEST(obs->steps == 0);
        BOOST_TEST(instance->steps == 0);
        BOOST_TEST(!execution.is_running());
        BOOST_CHECK_THROW(execution.step(), cosim::error);
    }
}


BOOST_AUTO_TEST_CASE(fmi3_intermediate_update_array)
{
    const auto fmuPath =
        reference_fmu_directory() / "IntermediateStateSpace.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    const auto references = variable_references(*fmu->model_description());
    const auto input = references.at("u");
    const auto output = references.at("y");

    std::atomic<int> callbackCount{0};
    std::atomic<std::size_t> observedValueCount{0};
    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.required_intermediate_variables = {input, output};
    callbacks.intermediate_update =
        [output, &callbackCount, &observedValueCount](
            fmi::v3::slave_instance& instance,
            fmi::v3::intermediate_update_info& info) {
            BOOST_TEST(info.intermediate_variable_get_allowed);
            const typed_value_reference variable{variable_type::real, output};
            const auto values =
                instance.get_variable_values(gsl::make_span(&variable, 1));
            BOOST_REQUIRE_EQUAL(values.size(), 1U);
            BOOST_REQUIRE_EQUAL(values[0].dimensions().size(), 1U);
            BOOST_TEST(values[0].dimensions()[0] == 3U);
            observedValueCount =
                std::get<real_values>(values[0].values()).values.size();
            ++callbackCount;
        };

    auto instance =
        fmu->instantiate_v3_slave("fmi3-intermediate-array", std::move(callbacks));
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    const double inputValues[] = {1.0, 2.0, 3.0};
    instance->set_real_variables(
        gsl::make_span(&input, 1),
        gsl::make_span(inputValues));
    instance->start_simulation();
    const auto result =
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.01));
    BOOST_TEST(result.result == step_result::complete);
    BOOST_TEST(callbackCount > 0);
    BOOST_TEST(observedValueCount == 3U);
    instance->end_simulation();
}


BOOST_AUTO_TEST_CASE(fmi3_intermediate_early_return_uses_communication_step_window)
{
    const auto* path = std::getenv("FMI3_STEP_PROBE_FMU");
    BOOST_REQUIRE(path);
    require_fixture(path);
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto step = to_duration(0.128);

    const auto instantiate = [&](double start, double requestedTime, bool canReturn) {
        fmi::v3::co_simulation_callbacks callbacks;
        callbacks.intermediate_update =
            [start, requestedTime, canReturn](fmi::v3::slave_instance&,
                fmi::v3::intermediate_update_info& info) {
                BOOST_CHECK(info.time == to_time_point(start + 0.096));
                BOOST_CHECK_EQUAL(info.can_return_early, canReturn);
                info.early_return_requested = true;
                info.early_return_time = to_time_point(requestedTime);
            };
        callbacks.early_return_allowed = true;
        auto instance = fmu->instantiate_v3_slave("step-probe", std::move(callbacks));
        instance->setup(to_time_point(start), std::nullopt, std::nullopt);
        instance->start_simulation();
        return instance;
    };

    {
        auto instance = instantiate(0.0, 0.064, true);
        const auto result = instance->do_step_with_info(time_point{}, step);
        BOOST_TEST(result.result == step_result::complete);
        BOOST_CHECK(result.early_return);
        BOOST_CHECK(result.last_successful_time == to_time_point(0.064));
        BOOST_CHECK(!result.event_handling_needed);
        instance->end_simulation();
    }

    for (const double invalidRequest : {-0.001, 0.129}) {
        BOOST_TEST_CONTEXT("out-of-window request: " << invalidRequest) {
            auto instance = instantiate(0.0, invalidRequest, true);
            BOOST_CHECK_EXCEPTION(instance->do_step_with_info(time_point{}, step),
                cosim::error, error_is(errc::simulation_error));
        }
    }
    auto denied = instantiate(-0.128, -0.064, false);
    BOOST_CHECK_EXCEPTION(denied->do_step_with_info(to_time_point(-0.128), step),
        cosim::error, error_is(errc::simulation_error));
}


BOOST_AUTO_TEST_CASE(fmi3_internal_event_time_can_differ_from_communication_boundary)
{
    const auto* path = std::getenv("FMI3_STEP_PROBE_FMU");
    BOOST_REQUIRE(path);
    require_fixture(path);
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    auto instance = fmu->instantiate_v3_slave("internal-event");
    BOOST_REQUIRE(instance->event_mode_used());
    instance->setup(time_point{}, std::nullopt, std::nullopt);
    instance->start_simulation();
    const auto result = instance->do_step_with_info(time_point{}, to_duration(0.128));
    BOOST_CHECK(result.result == step_result::complete);
    BOOST_CHECK(!result.early_return);
    BOOST_CHECK(result.event_handling_needed);
    BOOST_CHECK(result.last_successful_time != to_time_point(0.128));
    BOOST_CHECK_NO_THROW(instance->handle_events());
}


BOOST_AUTO_TEST_CASE(fmi3_do_step_statuses)
{
    const auto fmuPath = fault_fixture("StatusFault.fmu");

    const auto instantiate = [](const std::shared_ptr<fmi::v3::fmu>& fmu,
                                const std::string& name) {
        auto instance = fmu->instantiate_v3_slave(name);
        instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
        instance->start_simulation();
        instance->update_discrete_states();
        instance->enter_step_mode();
        return instance;
    };
    const auto setStatus = [](fmi::v3::slave_instance& instance, int status) {
        const value_reference reference = 1;
        instance.set_integer_variables(
            gsl::make_span(&reference, 1),
            gsl::make_span(&status, 1));
    };

    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));

    {
        warning_capture warnings;
        auto instance = instantiate(fmu, "fmi3-warning");
        setStatus(*instance, 1);
        const auto result =
            instance->do_step_with_info(to_time_point(0.0), to_duration(0.1));
        BOOST_TEST(result.result == step_result::complete);
        BOOST_CHECK(result.last_successful_time == to_time_point(0.1));
        BOOST_TEST(warnings.messages().find("DoStep returned FMI warning") != std::string::npos);
        instance->end_simulation();
    }

    {
        auto instance = instantiate(fmu, "fmi3-discard");
        setStatus(*instance, 2);
        const auto discarded =
            instance->do_step_with_info(to_time_point(0.0), to_duration(0.1));
        BOOST_TEST(discarded.result == step_result::failed);
        BOOST_TEST(discarded.retryable);
        BOOST_CHECK(discarded.last_successful_time == to_time_point(0.0));
        setStatus(*instance, 0);
        const auto retried =
            instance->do_step_with_info(to_time_point(0.0), to_duration(0.1));
        BOOST_TEST(retried.result == step_result::complete);
        instance->end_simulation();
    }

    {
        auto instance = instantiate(fmu, "fmi3-error");
        setStatus(*instance, 3);
        BOOST_CHECK_THROW(
            instance->do_step_with_info(
                to_time_point(0.0), to_duration(0.1)),
            cosim::error);
        instance->reset();
        instance->setup(
            to_time_point(0.0), to_time_point(1.0), std::nullopt);
        instance->start_simulation();
        instance->update_discrete_states();
        instance->enter_step_mode();
        BOOST_TEST(
            instance->do_step_with_info(to_time_point(0.0), to_duration(0.1)).result ==
            step_result::complete);
        instance->end_simulation();
    }

    {
        const auto fatalImporter = fmi::importer::create();
        const auto fatalFmu = std::static_pointer_cast<fmi::v3::fmu>(
            fatalImporter->import(fmuPath));
        auto instance = instantiate(fatalFmu, "fmi3-fatal");
        setStatus(*instance, 4);
        BOOST_CHECK_THROW(
            instance->do_step_with_info(
                to_time_point(0.0), to_duration(0.1)),
            cosim::error);
        BOOST_CHECK_THROW(instance->reset(), cosim::error);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_invalid_next_event_time_requires_recovery)
{
    const auto path = fault_fixture("StatusFault.fmu");
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    for (const int kind : {0, 1, 2}) {
        BOOST_TEST_CONTEXT("invalid time kind=" << kind) {
            const auto instance = fmu->instantiate_v3_slave("invalid-next-event");
            instance->setup(time_point{}, std::nullopt, std::nullopt);
            const std::array<value_reference, 2> references{1, 2};
            const std::array<int, 2> values{kind, 18};
            instance->set_integer_variables(gsl::make_span(references), gsl::make_span(values));
            instance->start_simulation();
            BOOST_CHECK_EXCEPTION(
                instance->update_discrete_states(), cosim::error,
                [](const cosim::error& error) {
                    return error.code() == make_error_code(errc::simulation_error) &&
                        std::string(error.what()).find("invalid next event time") != std::string::npos;
                });
            BOOST_CHECK_THROW(instance->enter_step_mode(), cosim::error);
            instance->reset();
            instance->setup(time_point{}, std::nullopt, std::nullopt);
            instance->start_simulation();
            instance->update_discrete_states();
            instance->enter_step_mode();
            instance->end_simulation();
        }
    }
}


BOOST_AUTO_TEST_CASE(fmi3_fatal_status_propagates_across_instances)
{
    const auto fmuPath = fault_fixture("StatusFault.fmu");

    const auto setStatus = [](auto& instance, int status) {
        const value_reference reference = 1;
        instance.set_integer_variables(
            gsl::make_span(&reference, 1),
            gsl::make_span(&status, 1));
    };
    const auto startSlave = [](const std::shared_ptr<fmi::v3::fmu>& fmu,
                                const std::string& name) {
        auto instance = fmu->instantiate_v3_slave(name);
        instance->setup(
            to_time_point(0.0), to_time_point(1.0), std::nullopt);
        instance->start_simulation();
        instance->update_discrete_states();
        instance->enter_step_mode();
        return instance;
    };

    auto importer = fmi::importer::create();
    auto fmu =
        std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    auto fatalSlave = startSlave(fmu, "fatal-cs-source");
    auto siblingSlave = startSlave(fmu, "fatal-cs-sibling");

    setStatus(*fatalSlave, 4);
    BOOST_CHECK_THROW(
        fatalSlave->do_step_with_info(
            to_time_point(0.0), to_duration(0.1)),
        cosim::error);
    BOOST_CHECK_THROW(
        siblingSlave->do_step_with_info(
            to_time_point(0.0), to_duration(0.1)),
        cosim::error);
    BOOST_CHECK_THROW(
        fmu->instantiate_v3_slave("fatal-cs-new"), cosim::error);
}

BOOST_AUTO_TEST_CASE(fmi3_unscheduled_event_requests_are_handled)
{
    const auto* fixture = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    BOOST_REQUIRE(fixture != nullptr);
    const auto path = cosim::filesystem::path(fixture);
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto step = to_duration(0.4);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<algorithm> algorithms[] = {
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        fmi::v3::co_simulation_callbacks options;
        options.early_return_allowed = true;
        const auto instance = fmu->instantiate_v3_slave("unscheduled-stair", options);
        execution execution(to_time_point(0.0), algorithm);
        execution.add_slave(instance, "unscheduled-stair");
        BOOST_CHECK(execution.step() == step);
        BOOST_CHECK(execution.step() == step);
        BOOST_CHECK(execution.step() == to_duration(0.2));
        BOOST_CHECK(execution.current_time() == to_time_point(1.0));
        const value_reference counterReference = 1;
        int counter = 0;
        instance->get_integer_variables(
            gsl::make_span(&counterReference, 1), gsl::make_span(&counter, 1));
        BOOST_TEST(counter == 2);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_event_requests_at_full_step_endpoint_are_handled)
{
    const auto* fixture = std::getenv("FMI3_UNSCHEDULED_STAIR_FMU");
    BOOST_REQUIRE(fixture != nullptr);
    const auto path = cosim::filesystem::path(fixture);
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto step = to_duration(1.0);
    const value_reference counterReference = 1;
    {
        const auto instance = fmu->instantiate_v3_slave("endpoint-event-probe");
        instance->setup(to_time_point(0.0), std::nullopt, std::nullopt);
        instance->start_simulation();
        const auto result = instance->do_step_with_info(to_time_point(0.0), step);
        BOOST_REQUIRE(result.event_handling_needed);
        BOOST_REQUIRE(!result.early_return);
        BOOST_CHECK(result.last_successful_time == to_time_point(1.0));
        instance->handle_events();
        int counter = 0;
        instance->get_integer_variables(
            gsl::make_span(&counterReference, 1), gsl::make_span(&counter, 1));
        BOOST_TEST(counter == 2);
    }

    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<algorithm> algorithms[] = {
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        const auto instance = fmu->instantiate_v3_slave("endpoint-event");
        execution execution(to_time_point(0.0), algorithm);
        execution.add_slave(instance, "endpoint-event");
        BOOST_CHECK(execution.step() == step);
        int counter = 0;
        instance->get_integer_variables(
            gsl::make_span(&counterReference, 1), gsl::make_span(&counter, 1));
        BOOST_TEST(counter == 2);
        BOOST_CHECK(execution.step() == step);
        instance->get_integer_variables(
            gsl::make_span(&counterReference, 1), gsl::make_span(&counter, 1));
        BOOST_TEST(counter == 3);
    }
}

BOOST_AUTO_TEST_CASE(nonclock_scheduled_events_cap_both_algorithms)
{
    const auto step = to_duration(1.0);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<algorithm> algorithms[] = {
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        int events = 0;
        auto next = std::optional<time_point>(to_time_point(0.6));
        auto slave = std::make_shared<detailed_step_mock_slave>(
            [](time_point current, duration dt) {
                return step_result_info{
                    step_result::complete, current + dt, false, false, false, false};
            },
            [&] { ++events; next.reset(); },
            [&] { return event_handling_status{false, next}; });
        execution execution(to_time_point(0.0), algorithm);
        execution.add_slave(slave, "scheduled-event");
        BOOST_CHECK(execution.step() == to_duration(0.6));
        BOOST_TEST(events == 1);
        BOOST_CHECK(execution.current_time() == to_time_point(0.6));
    }
}

BOOST_AUTO_TEST_CASE(fmi3_scheduled_event_termination_stops_execution)
{
    const auto path = reference_fmu_directory() / "DiscreteStair.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto step = to_duration(1.5);
    const ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<algorithm> algorithms[] = {
        std::make_shared<fixed_step_algorithm>(step, 1),
        std::make_shared<ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        execution execution(to_time_point(0.0), algorithm);
        execution.add_observer(std::make_shared<last_value_observer>());
        const auto instance = fmu->instantiate_v3_slave("terminating-stair");
        execution.add_slave(instance, "stair");
        BOOST_CHECK(!execution.simulate_until(to_time_point(20.0)));
        BOOST_CHECK(execution.current_time() == to_time_point(9.0));
        BOOST_CHECK(instance->get_event_handling_status().termination_requested);
        BOOST_CHECK_THROW(execution.step(), cosim::error);
        BOOST_CHECK_THROW(execution.simulate_until(to_time_point(20.0)), cosim::error);
    }
}
