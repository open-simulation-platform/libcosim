#define BOOST_TEST_MODULE cosim::fmi::v3 conformance unittest
#include <cosim/exception.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v3/fmu.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/time.hpp>

#include <boost/test/unit_test.hpp>

#include "warning_capture.hpp"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>


using namespace cosim;

namespace
{
constexpr value_reference faultStatusReference = 1;
constexpr value_reference faultOperationReference = 2;
constexpr value_reference valueReference = 3;

enum class fault_operation
{
    legacy_step = 0,
    enter_initialization = 1,
    exit_initialization = 2,
    enter_configuration = 3,
    exit_configuration = 4,
    update_discrete_states = 5,
    get_int32 = 6,
    set_int32 = 7,
    get_fmu_state = 8,
    set_fmu_state = 9,
    free_fmu_state = 10,
    serialized_fmu_state_size = 11,
    serialize_fmu_state = 12,
    deserialize_fmu_state = 13,
    terminate = 14,
    reset = 15,
    do_step = 16,
    endpoint_early_return = 19,
    endpoint_early_event = 20,
    after_endpoint = 21
};

enum class fault_status
{
    ok = 0,
    warning = 1,
    discard = 2,
    error = 3,
    fatal = 4
};


cosim::filesystem::path status_fault_fmu()
{
    const auto* referenceDirectory = std::getenv("FMI3_FAULT_FIXTURE_DIR");
    if (referenceDirectory == nullptr) {
        throw std::runtime_error(
            "FMI3_FAULT_FIXTURE_DIR must identify the prebuilt FMI 3 fixtures");
    }
    const auto path =
        cosim::filesystem::path(referenceDirectory) / "StatusFault.fmu";
    if (!cosim::filesystem::exists(path)) {
        throw std::runtime_error(
            "Missing FMI 3 fixture: " + path.string());
    }
    return path;
}


std::shared_ptr<fmi::v3::fmu> import_status_fault()
{
    auto importer = fmi::importer::create();
    return std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(status_fault_fmu()));
}


std::string unique_instance_name(const char* prefix)
{
    static std::atomic<unsigned int> nextId{0};
    return std::string(prefix) + "-" + std::to_string(++nextId);
}


template<typename Instance>
void set_fault(
    Instance& instance,
    fault_operation operation,
    fault_status status)
{
    const value_reference references[] = {
        faultOperationReference,
        faultStatusReference};
    const int values[] = {
        static_cast<int>(operation),
        static_cast<int>(status)};
    instance.set_integer_variables(
        gsl::make_span(references),
        gsl::make_span(values));
}


template<typename Instance>
void set_value(Instance& instance, int value)
{
    instance.set_integer_variables(
        gsl::make_span(&valueReference, 1),
        gsl::make_span(&value, 1));
}


template<typename Instance>
int get_value(Instance& instance)
{
    int value = 0;
    instance.get_integer_variables(
        gsl::make_span(&valueReference, 1),
        gsl::make_span(&value, 1));
    return value;
}


std::shared_ptr<fmi::v3::slave_instance> new_slave(
    const std::shared_ptr<fmi::v3::fmu>& fmu)
{
    fmi::v3::co_simulation_callbacks options;
    options.early_return_allowed = true;
    return fmu->instantiate_v3_slave(unique_instance_name("conformance-cs"), options);
}


void setup_slave_in_event_mode(fmi::v3::slave_instance& instance)
{
    instance.setup(
        to_time_point(0.0),
        to_time_point(10.0),
        std::nullopt);
    instance.start_simulation();
}


void setup_slave_in_step_mode(fmi::v3::slave_instance& instance)
{
    setup_slave_in_event_mode(instance);
    instance.update_discrete_states();
    instance.enter_step_mode();
}


void expect_model_error(const std::function<void()>& operation)
{
    BOOST_CHECK_EXCEPTION(
        operation(),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });
}
} // namespace

BOOST_AUTO_TEST_CASE(endpoint_early_return_warns_and_preserves_event_request)
{
    for (const bool event : {false, true}) {
        warning_capture warnings;
        auto instance = new_slave(import_status_fault());
        set_fault(*instance, event ? fault_operation::endpoint_early_event :
            fault_operation::endpoint_early_return, fault_status::ok);
        setup_slave_in_step_mode(*instance);
        for (int step = 0; step < 2; ++step) {
            const auto result = instance->do_step_with_info(
                to_time_point(step * 0.1), to_duration(0.1));
            BOOST_CHECK(!result.early_return);
            BOOST_CHECK_EQUAL(result.event_handling_needed, event);
            if (event) instance->handle_events();
        }
        const auto text = warnings.messages();
        const auto message = std::string("early return at its requested endpoint");
        const auto position = text.find(message);
        BOOST_REQUIRE(position != std::string::npos);
        BOOST_CHECK(text.find(message, position + message.size()) == std::string::npos);
        instance->end_simulation();
    }
}

BOOST_AUTO_TEST_CASE(serialized_state_rejects_an_incompatible_token_or_interface)
{
    const auto instance = new_slave(import_status_fault());
    setup_slave_in_step_mode(*instance);
    const auto saved = instance->save_state();
    const auto exported = instance->export_state(saved);
    for (const auto* key : {"fmu_instantiation_token", "interface"}) {
        auto invalid = exported;
        invalid.put(key, std::string("incompatible"));
        BOOST_CHECK_EXCEPTION(instance->import_state(invalid), cosim::error,
            [](const cosim::error& e) { return e.code() == make_error_code(errc::bad_file); });
    }
    instance->release_state(saved);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(early_return_after_endpoint_is_rejected)
{
    auto fmu = import_status_fault();
    auto instance = new_slave(fmu);
    auto sibling = new_slave(fmu);
    set_fault(*instance, fault_operation::after_endpoint, fault_status::ok);
    setup_slave_in_step_mode(*instance);
    setup_slave_in_step_mode(*sibling);
    BOOST_CHECK_THROW(instance->do_step_with_info(time_point{}, to_duration(0.1)), cosim::error);
    expect_model_error([&]() { instance->do_step_with_info(time_point{}, to_duration(0.1)); });
    instance.reset();
    BOOST_CHECK(sibling->do_step_with_info(time_point{}, to_duration(0.1)).result == step_result::complete);
    sibling->end_simulation();
}


BOOST_AUTO_TEST_CASE(fmi3_remaining_operation_statuses)
{
    auto fmu = import_status_fault();
    warning_capture warnings;

    {
        auto instance = new_slave(fmu);
        set_fault(
            *instance,
            fault_operation::enter_initialization,
            fault_status::warning);
        setup_slave_in_step_mode(*instance);
        instance->end_simulation();
    }

    {
        auto instance = new_slave(fmu);
        instance->setup(
            to_time_point(0.0),
            to_time_point(10.0),
            std::nullopt);
        set_fault(
            *instance,
            fault_operation::exit_initialization,
            fault_status::warning);
        instance->start_simulation();
        instance->update_discrete_states();
        instance->enter_step_mode();
        instance->end_simulation();
    }

    {
        auto instance = new_slave(fmu);
        set_fault(
            *instance,
            fault_operation::enter_configuration,
            fault_status::warning);
        instance->enter_configuration_mode();
        instance->exit_configuration_mode();
        set_fault(
            *instance,
            fault_operation::exit_configuration,
            fault_status::warning);
        instance->enter_configuration_mode();
        instance->exit_configuration_mode();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_event_mode(*instance);
        set_fault(
            *instance,
            fault_operation::update_discrete_states,
            fault_status::warning);
        instance->update_discrete_states();
        instance->enter_step_mode();
        instance->end_simulation();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_event_mode(*instance);
        set_fault(
            *instance,
            fault_operation::set_int32,
            fault_status::warning);
        set_value(*instance, 11);
        BOOST_TEST(get_value(*instance) == 11);

        set_fault(
            *instance,
            fault_operation::get_int32,
            fault_status::warning);
        BOOST_TEST(get_value(*instance) == 11);
        instance->end_simulation();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_step_mode(*instance);
        const auto state = instance->save_state();

        set_fault(
            *instance,
            fault_operation::set_fmu_state,
            fault_status::warning);
        instance->restore_state(state);

        set_fault(
            *instance,
            fault_operation::get_fmu_state,
            fault_status::warning);
        const auto warningState = instance->save_state();
        instance->release_state(warningState);

        set_fault(
            *instance,
            fault_operation::serialized_fmu_state_size,
            fault_status::warning);
        const auto sizeWarningState = instance->save_state();
        const auto exported = instance->export_state(sizeWarningState);
        instance->release_state(sizeWarningState);

        set_fault(
            *instance,
            fault_operation::serialize_fmu_state,
            fault_status::warning);
        const auto serializeWarningState = instance->save_state();
        instance->export_state(serializeWarningState);
        instance->release_state(serializeWarningState);

        const auto freeWarningState = instance->save_state();
        set_fault(
            *instance,
            fault_operation::free_fmu_state,
            fault_status::warning);
        instance->release_state(freeWarningState);

        set_fault(
            *instance,
            fault_operation::deserialize_fmu_state,
            fault_status::warning);
        const auto imported = instance->import_state(exported);
        instance->restore_state(imported);
        instance->release_state(imported);
        instance->release_state(state);
        instance->end_simulation();
    }

    for (const std::string operation : {
             "EnterInitializationMode", "ExitInitializationMode",
             "EnterConfigurationMode", "ExitConfigurationMode", "UpdateDiscreteStates",
             "SetInt32", "GetInt32", "SetFMUState", "GetFMUState", "FreeFMUState",
             "SerializedFMUStateSize", "SerializeFMUState", "DeserializeFMUState"}) {
        BOOST_TEST_CONTEXT(operation) {
            BOOST_CHECK(warnings.messages().find("] " + operation + " returned FMI warning") != std::string::npos);
        }
    }

    {
        auto instance = new_slave(fmu);
        set_fault(
            *instance,
            fault_operation::enter_initialization,
            fault_status::error);
        expect_model_error([&]() {
            instance->setup(
                to_time_point(0.0),
                to_time_point(10.0),
                std::nullopt);
        });
        instance->reset();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_step_mode(*instance);
        set_fault(
            *instance,
            fault_operation::terminate,
            fault_status::warning);
        instance->end_simulation();
    }

    {
        auto instance = new_slave(fmu);
        instance->setup(
            to_time_point(0.0),
            to_time_point(10.0),
            std::nullopt);
        set_fault(
            *instance,
            fault_operation::reset,
            fault_status::warning);
        instance->reset();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_step_mode(*instance);
        const auto state = instance->save_state();
        set_fault(
            *instance,
            fault_operation::get_int32,
            fault_status::discard);
        expect_model_error([&]() { get_value(*instance); });
        instance->restore_state(state);
        instance->release_state(state);
        instance->end_simulation();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_event_mode(*instance);
        set_fault(
            *instance,
            fault_operation::update_discrete_states,
            fault_status::error);
        expect_model_error(
            [&]() { instance->update_discrete_states(); });
        instance->reset();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_step_mode(*instance);
        const auto saved = instance->save_state();
        set_fault(
            *instance,
            fault_operation::get_fmu_state,
            fault_status::error);
        expect_model_error([&]() { instance->save_state(); });
        expect_model_error([&]() { instance->end_simulation(); });
        expect_model_error([&]() { set_value(*instance, 2); });
        BOOST_CHECK_NO_THROW(get_value(*instance));
        instance->restore_state(saved);
        instance->release_state(saved);
        instance->end_simulation();
    }
}


BOOST_AUTO_TEST_CASE(fmi3_fatal_lifecycle_status_poisons_fmu)
{
    auto fmu = import_status_fault();
    auto instance = new_slave(fmu);
    set_fault(
        *instance,
        fault_operation::enter_initialization,
        fault_status::fatal);
    expect_model_error([&]() {
        instance->setup(
            to_time_point(0.0),
            to_time_point(10.0),
            std::nullopt);
    });
    expect_model_error([&]() { instance->reset(); });
    expect_model_error(
        [&]() { fmu->instantiate_v3_slave("fatal-new-instance"); });
}


BOOST_AUTO_TEST_CASE(fmi3_co_simulation_state_round_trip)
{
    auto fmu = import_status_fault();
    auto instance = new_slave(fmu);
    const auto description = instance->model_description();
    BOOST_TEST(description.capabilities.can_save_state);
    BOOST_TEST(description.capabilities.can_export_state);

    setup_slave_in_event_mode(*instance);
    set_value(*instance, 10);
    const auto state = instance->save_state();

    set_value(*instance, 20);
    instance->save_state(state);
    set_value(*instance, 30);
    instance->restore_state(state);
    BOOST_TEST(get_value(*instance) == 20);

    const auto exported = instance->export_state(state);
    set_value(*instance, 40);
    const auto imported = instance->import_state(exported);
    instance->restore_state(imported);
    BOOST_TEST(get_value(*instance) == 20);

    instance->release_state(imported);
    expect_model_error([&]() { instance->restore_state(imported); });
    instance->release_state(state);
    instance->end_simulation();
    instance->reset();
}

BOOST_AUTO_TEST_CASE(fmi3_co_simulation_lifecycle_conformance)
{
    auto fmu = import_status_fault();

    {
        auto instance = new_slave(fmu);
        expect_model_error([&]() { instance->start_simulation(); });
        expect_model_error([&]() { instance->end_simulation(); });
        expect_model_error([&]() {
            instance->do_step_with_info(
                to_time_point(0.0),
                to_duration(0.1));
        });
        expect_model_error([&]() { instance->enter_event_mode(); });
        expect_model_error(
            [&]() { instance->update_discrete_states(); });
        expect_model_error([&]() { instance->enter_step_mode(); });
        expect_model_error(
            [&]() { instance->exit_configuration_mode(); });
        expect_model_error([&]() { instance->reset(); });
    }

    {
        auto instance = new_slave(fmu);
        instance->enter_configuration_mode();
        expect_model_error(
            [&]() { instance->enter_configuration_mode(); });
        expect_model_error([&]() {
            instance->setup(
                to_time_point(0.0),
                to_time_point(10.0),
                std::nullopt);
        });
        expect_model_error([&]() { instance->start_simulation(); });
        expect_model_error([&]() { instance->end_simulation(); });
        expect_model_error([&]() {
            instance->do_step_with_info(
                to_time_point(0.0),
                to_duration(0.1));
        });
        expect_model_error([&]() { instance->enter_event_mode(); });
        expect_model_error(
            [&]() { instance->update_discrete_states(); });
        expect_model_error([&]() { instance->enter_step_mode(); });
        instance->exit_configuration_mode();
    }

    {
        auto instance = new_slave(fmu);
        instance->setup(
            to_time_point(0.0),
            to_time_point(10.0),
            std::nullopt);
        expect_model_error([&]() {
            instance->setup(
                to_time_point(0.0),
                to_time_point(10.0),
                std::nullopt);
        });
        expect_model_error(
            [&]() { instance->enter_configuration_mode(); });
        expect_model_error(
            [&]() { instance->exit_configuration_mode(); });
        expect_model_error([&]() { instance->end_simulation(); });
        expect_model_error([&]() {
            instance->do_step_with_info(
                to_time_point(0.0),
                to_duration(0.1));
        });
        expect_model_error([&]() { instance->enter_event_mode(); });
        expect_model_error(
            [&]() { instance->update_discrete_states(); });
        expect_model_error([&]() { instance->enter_step_mode(); });
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_event_mode(*instance);
        expect_model_error([&]() {
            instance->setup(
                to_time_point(0.0),
                to_time_point(10.0),
                std::nullopt);
        });
        expect_model_error([&]() { instance->start_simulation(); });
        expect_model_error([&]() {
            instance->do_step_with_info(
                to_time_point(0.0),
                to_duration(0.1));
        });
        expect_model_error([&]() { instance->enter_event_mode(); });
        expect_model_error([&]() { instance->enter_step_mode(); });
        instance->update_discrete_states();
        instance->enter_step_mode();
    }

    {
        auto instance = new_slave(fmu);
        setup_slave_in_step_mode(*instance);
        expect_model_error([&]() {
            instance->setup(
                to_time_point(0.0),
                to_time_point(10.0),
                std::nullopt);
        });
        expect_model_error([&]() { instance->start_simulation(); });
        expect_model_error(
            [&]() { instance->exit_configuration_mode(); });
        expect_model_error(
            [&]() { instance->update_discrete_states(); });
        expect_model_error([&]() { instance->enter_step_mode(); });
        instance->end_simulation();

        expect_model_error([&]() {
            instance->setup(
                to_time_point(0.0),
                to_time_point(10.0),
                std::nullopt);
        });
        expect_model_error([&]() { instance->start_simulation(); });
        expect_model_error([&]() { instance->end_simulation(); });
        expect_model_error([&]() {
            instance->do_step_with_info(
                to_time_point(0.0),
                to_duration(0.1));
        });
        expect_model_error(
            [&]() { instance->enter_configuration_mode(); });
        expect_model_error(
            [&]() { instance->exit_configuration_mode(); });
        expect_model_error([&]() { instance->enter_event_mode(); });
        expect_model_error(
            [&]() { instance->update_discrete_states(); });
        expect_model_error([&]() { instance->enter_step_mode(); });
        const auto terminatedState = instance->save_state();
        instance->release_state(terminatedState);
        instance->reset();
    }
}
