#define BOOST_TEST_MODULE cosim::clock_router unittest
#include "mock_slave.hpp"
#include "warning_capture.hpp"

#include <cosim/algorithm/clock_router.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/error.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/slave.hpp>
#include <cosim/slave_simulator.hpp>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cosim
{

struct clock_router_test_access
{
    static std::vector<std::size_t> order(clock_router& router)
    {
        return router.linear_clock_order();
    }

    static std::size_t propagate(clock_router& router, variable_id root)
    {
        std::unordered_set<variable_id> fired, activated{root}, transferred;
        std::unordered_set<std::size_t> propagated;
        std::vector<clock_router::observed_clocked_data> observed;
        std::size_t visits = 0;
        router.require_controller(root.simulator).set_input_clock(root.reference, true);
        while (router.propagate_outputs(
            fired, activated, transferred, propagated, observed,
            [&visits](std::size_t) { ++visits; })) {
        }
        return visits;
    }
};

} // namespace cosim

namespace
{

using clock_set = std::set<cosim::value_reference>;

// A slave with only Clock variables. Output Clocks are computed from the set
// of currently active input Clocks, and every Clock activation is logged.
class scripted_clock_slave final : public mock_slave, public cosim::clock_controller
{
public:
    struct clock_spec
    {
        cosim::value_reference reference;
        cosim::variable_causality causality;
        cosim::clock_interval_variability variability;
        std::optional<double> interval;
        bool canBeDeactivated;
        std::vector<cosim::value_reference> dependencies;
    };

    explicit scripted_clock_slave(std::vector<clock_spec> clocks)
        : clocks_(std::move(clocks))
    {
    }

    cosim::model_description model_description() const override
    {
        cosim::model_description md;
        md.name = "scripted_clock_slave";
        md.uuid = "5b0c2a6e-4c3a-4f60-9a56-1d4f9a3b7a11";
        md.capabilities.can_save_state = false;
        for (const auto& clock : clocks_) {
            cosim::variable_description description{};
            description.name = "clock" + std::to_string(clock.reference);
            description.reference = clock.reference;
            description.type = cosim::variable_type::clock;
            description.causality = clock.causality;
            description.variability = cosim::variable_variability::discrete;
            description.clocks = clock.dependencies;
            cosim::clock_description details;
            details.interval_variability = clock.variability;
            details.interval_decimal = clock.interval;
            details.can_be_deactivated = clock.canBeDeactivated;
            description.clock = details;
            md.variables.push_back(std::move(description));
        }
        return md;
    }

    cosim::clock_controller* get_clock_controller() noexcept override { return this; }
    cosim::event_controller* get_event_controller() noexcept override { return this; }

    bool is_in_event_mode() const noexcept override { return eventMode_; }
    void enter_event_mode() override { eventMode_ = true; }
    void enter_step_mode() override { eventMode_ = false; }

    bool get_output_clock(cosim::value_reference reference) override
    {
        ++outputReads;
        const auto rule = outputRules.find(reference);
        return rule != outputRules.end() && rule->second(active_);
    }

    void set_input_clock(cosim::value_reference reference, bool active) override
    {
        log.push_back({reference, active});
        if (onActivation) onActivation(reference, active);
        if (active) {
            active_.insert(reference);
        } else {
            active_.erase(reference);
        }
    }

    cosim::variable_value get_clocked_variable(
        cosim::variable_type, cosim::value_reference) override
    {
        throw std::logic_error("Unexpected clocked read");
    }

    void set_clocked_variable(
        cosim::variable_type, cosim::value_reference, const cosim::variable_value&) override
    {
        throw std::logic_error("Unexpected clocked write");
    }

    cosim::clock_update_result update_clock_state() override
    {
        ++updates;
        if (!retainActive) active_.clear();
        return {};
    }

    std::map<cosim::value_reference, std::function<bool(const clock_set&)>> outputRules;
    std::function<void(cosim::value_reference, bool)> onActivation;
    std::vector<std::pair<cosim::value_reference, bool>> log;
    std::size_t outputReads = 0;
    std::size_t updates = 0;
    // Keeps Clocks active across discrete-state updates, as a model that
    // never settles would.
    bool retainActive = false;

private:
    std::vector<clock_spec> clocks_;
    clock_set active_;
    bool eventMode_ = false;
};

using spec = scripted_clock_slave::clock_spec;
constexpr auto input = cosim::variable_causality::input;
constexpr auto output = cosim::variable_causality::output;
constexpr auto periodic = cosim::clock_interval_variability::constant;
constexpr auto triggered = cosim::clock_interval_variability::triggered;

struct retraction_scenario
{
    explicit retraction_scenario(bool targetCanBeDeactivated)
        : first(std::make_shared<scripted_clock_slave>(std::vector<spec>{
              {1, input, periodic, 1.0, false, {}},
              {2, input, triggered, std::nullopt, true, {}},
              {3, output, triggered, std::nullopt, false, {1, 2}}}))
        , second(std::make_shared<scripted_clock_slave>(std::vector<spec>{
              {1, input, periodic, 1.0, false, {}},
              {2, output, triggered, std::nullopt, false, {1}}}))
        , target(std::make_shared<scripted_clock_slave>(std::vector<spec>{
              {1, input, triggered, std::nullopt, targetCanBeDeactivated, {}}}))
    {
        // The first simulator's output Clock is active only while its
        // second input Clock is inactive.
        first->outputRules[3] = [](const clock_set& active) {
            return active.count(1) != 0 && active.count(2) == 0;
        };
        second->outputRules[2] = [](const clock_set& active) { return active.count(1) != 0; };
    }

    void connect(cosim::execution& execution)
    {
        const auto firstIndex = execution.add_slave(first, "first");
        const auto secondIndex = execution.add_slave(second, "second");
        const auto targetIndex = execution.add_slave(target, "target");
        // The first connection is routed before the second one retracts the
        // output Clock it depends on.
        execution.connect_variables(
            cosim::variable_id{firstIndex, cosim::variable_type::clock, 3},
            cosim::variable_id{targetIndex, cosim::variable_type::clock, 1});
        execution.connect_variables(
            cosim::variable_id{secondIndex, cosim::variable_type::clock, 2},
            cosim::variable_id{firstIndex, cosim::variable_type::clock, 2});
    }

    std::shared_ptr<scripted_clock_slave> first;
    std::shared_ptr<scripted_clock_slave> second;
    std::shared_ptr<scripted_clock_slave> target;
};

// An ordinary slave that records the start time and size of its steps.
class stepping_slave final : public mock_slave
{
public:
    cosim::step_result do_step(cosim::time_point currentT, cosim::duration deltaT) override
    {
        steps.emplace_back(currentT, deltaT);
        return mock_slave::do_step(currentT, deltaT);
    }

    std::vector<std::pair<cosim::time_point, cosim::duration>> steps;
};

} // namespace

BOOST_AUTO_TEST_CASE(clock_chain_connection_visits_preserve_activation_order)
{
    constexpr int edges = 8;
    for (const auto& [reverse, hook] :
        {std::pair{false, false}, std::pair{true, false}, std::pair{true, true}}) {
        cosim::clock_router router;
        cosim::event_iteration_budget budget;
        router.set_event_budget(budget);
        std::vector<int> activationOrder;
        std::size_t hookCalls = 0;
        if (hook) router.set_before_input_activation([&hookCalls] { ++hookCalls; });
        std::vector<std::shared_ptr<scripted_clock_slave>> slaves;
        std::vector<std::unique_ptr<cosim::slave_simulator>> simulators;
        for (int i = 0; i <= edges; ++i) {
            std::vector<spec> clocks{
                {1, input, i == 0 ? periodic : triggered,
                    i == 0 ? std::optional<double>{1.0} : std::nullopt, false, {}}};
            if (i != edges) {
                clocks.push_back({2, output, triggered, std::nullopt, false, {1}});
            }
            auto slave = std::make_shared<scripted_clock_slave>(std::move(clocks));
            slave->onActivation = [&activationOrder, i](cosim::value_reference, bool active) {
                if (active) activationOrder.push_back(i);
            };
            slave->outputRules[2] = [](const clock_set& active) {
                return active.count(1) != 0;
            };
            simulators.push_back(std::make_unique<cosim::slave_simulator>(slave, "chain"));
            simulators.back()->setup(cosim::time_point{}, std::nullopt, std::nullopt);
            simulators.back()->start_simulation();
            simulators.back()->get_clock_controller()->enter_event_mode();
            router.add_simulator(i, simulators.back().get());
            slaves.push_back(std::move(slave));
        }
        for (int i = 0; i < edges; ++i) {
            const auto source = reverse ? edges - i - 1 : i;
            router.connect_clock(
                {source, cosim::variable_type::clock, 2},
                {source + 1, cosim::variable_type::clock, 1});
        }

        const auto visits = cosim::clock_router_test_access::propagate(
            router, {0, cosim::variable_type::clock, 1});
        BOOST_CHECK_EQUAL(visits, hook ? 72u : 16u);
        BOOST_CHECK_EQUAL(hookCalls, hook ? 8u : 0u);
        BOOST_TEST((activationOrder == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8}),
            boost::test_tools::per_element());
        for (std::size_t i = 0; i < slaves.size(); ++i) {
            const auto& slave = slaves[i];
            BOOST_REQUIRE_EQUAL(slave->log.size(), 1u);
            BOOST_CHECK(slave->log.front() == std::make_pair(cosim::value_reference{1}, true));
            BOOST_CHECK_EQUAL(slave->updates, 0u);
            BOOST_CHECK_EQUAL(slave->outputReads, i == edges ? 0u : 1u);
        }
        BOOST_REQUIRE(router.disconnect({4, cosim::variable_type::clock, 1}));
        BOOST_CHECK(cosim::clock_router_test_access::order(router).empty());
        router.connect_clock(
            {3, cosim::variable_type::clock, 2},
            {4, cosim::variable_type::clock, 1});
        BOOST_CHECK_EQUAL(cosim::clock_router_test_access::order(router).size(), edges);
        auto branch = std::make_shared<scripted_clock_slave>(std::vector<spec>{
            {1, input, triggered, std::nullopt, false, {}}});
        auto branchSim = std::make_unique<cosim::slave_simulator>(branch, "branch");
        router.add_simulator(9, branchSim.get());
        router.connect_clock(
            {0, cosim::variable_type::clock, 2},
            {9, cosim::variable_type::clock, 1});
        BOOST_CHECK(cosim::clock_router_test_access::order(router).empty());
        BOOST_REQUIRE(router.disconnect({9, cosim::variable_type::clock, 1}));
        BOOST_CHECK_EQUAL(cosim::clock_router_test_access::order(router).size(), edges);
        router.remove_simulator(4);
        BOOST_CHECK(cosim::clock_router_test_access::order(router).empty());
    }
}

BOOST_AUTO_TEST_CASE(ordered_clock_chain_preserves_event_rounds_in_both_algorithms)
{
    const auto step = std::chrono::seconds(1);
    const cosim::ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const bool ecco : {false, true}) {
        for (const bool reverse : {false, true}) {
            std::shared_ptr<cosim::algorithm> algorithm = ecco
                ? std::static_pointer_cast<cosim::algorithm>(
                    std::make_shared<cosim::ecco_algorithm>(params, 1))
                : std::static_pointer_cast<cosim::algorithm>(
                    std::make_shared<cosim::fixed_step_algorithm>(step, 1));
            cosim::execution execution(cosim::time_point{}, algorithm);
            std::vector<int> activationOrder;
            std::vector<std::shared_ptr<scripted_clock_slave>> slaves;
            std::vector<cosim::simulator_index> indices;
            for (int i = 0; i != 4; ++i) {
                std::vector<spec> clocks{
                    {1, input, i == 0 ? periodic : triggered,
                        i == 0 ? std::optional<double>{1.0} : std::nullopt, false, {}}};
                if (i != 3) clocks.push_back({2, output, triggered, std::nullopt, false, {1}});
                auto slave = std::make_shared<scripted_clock_slave>(std::move(clocks));
                slave->onActivation = [&activationOrder, i](cosim::value_reference, bool active) {
                    if (active) activationOrder.push_back(i);
                };
                slave->outputRules[2] = [](const clock_set& active) {
                    return active.count(1) != 0;
                };
                indices.push_back(execution.add_slave(slave, "clock" + std::to_string(i)));
                slaves.push_back(std::move(slave));
            }
            for (int i = 0; i != 3; ++i) {
                const int source = reverse ? 2 - i : i;
                execution.connect_variables(
                    cosim::variable_id{indices[source], cosim::variable_type::clock, 2},
                    cosim::variable_id{indices[source + 1], cosim::variable_type::clock, 1});
            }
            BOOST_REQUIRE_NO_THROW(execution.initialize());
            BOOST_TEST((activationOrder == std::vector<int>{0, 1, 2, 3}),
                boost::test_tools::per_element());
            for (const auto& slave : slaves) BOOST_CHECK_EQUAL(slave->updates, 1u);
            const auto state = algorithm->export_current_state().get_child("event_router");
            BOOST_CHECK_EQUAL(state.get<std::uint64_t>("iterations"), 1u);
            BOOST_CHECK(!state.get<bool>("warned"));
            const auto snapshot = algorithm->export_current_state();
            BOOST_REQUIRE_NO_THROW(algorithm->import_state(snapshot));
            BOOST_CHECK(execution.step() == step);
            BOOST_TEST((activationOrder == std::vector<int>{0, 1, 2, 3, 0, 1, 2, 3}),
                boost::test_tools::per_element());
            for (const auto& slave : slaves) BOOST_CHECK_EQUAL(slave->updates, 2u);
        }
    }
}


BOOST_AUTO_TEST_CASE(simultaneous_clock_connections_deliver_each_input_once)
{
    const auto nominal = std::chrono::seconds(2);
    const auto boundary = std::chrono::seconds(1);
    const cosim::ecco_algorithm_params params{
        0.99, nominal, nominal, nominal, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    for (const bool ecco : {false, true}) {
        std::shared_ptr<cosim::algorithm> algorithm;
        if (ecco) {
            algorithm = std::make_shared<cosim::ecco_algorithm>(params, 1);
        } else {
            algorithm = std::make_shared<cosim::fixed_step_algorithm>(nominal, 1);
        }
        cosim::execution execution(cosim::time_point{}, algorithm);
        auto target = std::make_shared<scripted_clock_slave>(std::vector<spec>{
            {1, input, triggered, std::nullopt, false, {}},
            {2, input, triggered, std::nullopt, false, {}}});
        const auto targetIndex = execution.add_slave(target, "target");
        for (const cosim::value_reference reference : {2, 1}) {
            auto source = std::make_shared<scripted_clock_slave>(std::vector<spec>{
                {1, input, periodic, 1.0, false, {}},
                {2, output, triggered, std::nullopt, false, {1}}});
            source->outputRules[2] = [](const clock_set& active) { return active.count(1) != 0; };
            const auto sourceIndex = execution.add_slave(source, "source" + std::to_string(reference));
            execution.connect_variables(
                cosim::variable_id{sourceIndex, cosim::variable_type::clock, 2},
                cosim::variable_id{targetIndex, cosim::variable_type::clock, reference});
        }

        const auto check_deliveries = [&] {
            BOOST_REQUIRE_EQUAL(target->log.size(), 2u);
            clock_set delivered;
            for (const auto& entry : target->log) {
                BOOST_CHECK(entry.second);
                delivered.insert(entry.first);
            }
            BOOST_CHECK((delivered == clock_set{1, 2}));
        };
        execution.initialize();
        check_deliveries();
        const auto initialUpdates = target->updates;
        target->log.clear();
        BOOST_CHECK(execution.step() == boundary);
        check_deliveries();
        BOOST_CHECK_EQUAL(target->updates, initialUpdates + 1);
    }
}

BOOST_AUTO_TEST_CASE(decimated_ordinary_simulator_runs_beside_clock_capable_simulator)
{
    auto clocked = std::make_shared<scripted_clock_slave>(std::vector<spec>{
        {1, input, periodic, 1.0, false, {}}});
    auto decimated = std::make_shared<stepping_slave>();
    auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(std::chrono::seconds(2));
    cosim::execution execution(cosim::time_point{}, algorithm);
    const auto clockedIndex = execution.add_slave(clocked, "clocked");
    const auto decimatedIndex = execution.add_slave(decimated, "decimated");
    algorithm->set_stepsize_decimation_factor(clockedIndex, 1);
    algorithm->set_stepsize_decimation_factor(decimatedIndex, 2);

    execution.initialize();
    std::vector<cosim::duration> sizes;
    while (execution.current_time() < cosim::time_point(std::chrono::seconds(4))) {
        sizes.push_back(execution.step());
    }

    BOOST_TEST(
        (sizes == std::vector<cosim::duration>(4, std::chrono::seconds(1))),
        boost::test_tools::per_element());
    BOOST_REQUIRE_EQUAL(decimated->steps.size(), 1u);
    BOOST_CHECK(decimated->steps.front().first == cosim::time_point{});
    BOOST_CHECK(decimated->steps.front().second == std::chrono::seconds(4));
    // The periodic Clock ticks at 0, 1, 2, 3 and 4 s.
    BOOST_CHECK_EQUAL(clocked->log.size(), 5u);
}

// Two simulators whose Clocks keep activating each other at the start time.
struct ping_pong_scenario
{
    explicit ping_pong_scenario(std::vector<cosim::value_reference> outputDependencies)
        : first(std::make_shared<scripted_clock_slave>(std::vector<spec>{
              {1, input, periodic, 1.0, false, {}},
              {2, input, triggered, std::nullopt, true, {}},
              {3, output, triggered, std::nullopt, false, std::move(outputDependencies)}}))
        , second(std::make_shared<scripted_clock_slave>(std::vector<spec>{
              {1, input, triggered, std::nullopt, true, {}},
              {2, output, triggered, std::nullopt, false, {1}}}))
    {
        second->outputRules[2] = [](const clock_set& active) { return active.count(1) != 0; };
    }

    void connect(cosim::execution& execution)
    {
        const auto firstIndex = execution.add_slave(first, "first");
        const auto secondIndex = execution.add_slave(second, "second");
        execution.connect_variables(
            cosim::variable_id{firstIndex, cosim::variable_type::clock, 3},
            cosim::variable_id{secondIndex, cosim::variable_type::clock, 1});
        execution.connect_variables(
            cosim::variable_id{secondIndex, cosim::variable_type::clock, 2},
            cosim::variable_id{firstIndex, cosim::variable_type::clock, 2});
    }

    std::shared_ptr<scripted_clock_slave> first;
    std::shared_ptr<scripted_clock_slave> second;
};

BOOST_AUTO_TEST_CASE(clock_ping_pong_at_initialization_is_cut_off_with_a_warning)
{
    for (const std::size_t limit : {1, 3, 8}) {
        warning_capture warnings;
        ping_pong_scenario scenario({});
        scenario.first->retainActive = true;
        scenario.second->retainActive = true;
        // An output Clock without associated inputs ticks on every update.
        scenario.first->outputRules[3] = [](const clock_set&) { return true; };
        auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(std::chrono::seconds(1));
        algorithm->set_max_event_iterations(limit);
        cosim::execution execution(cosim::time_point{}, algorithm);
        scenario.connect(execution);

        BOOST_REQUIRE_NO_THROW(execution.initialize());
        const auto message = "did not converge within " + std::to_string(limit) + " rounds";
        BOOST_CHECK(warnings.messages().find(message) != std::string::npos);
        BOOST_CHECK(warnings.messages().find("time 0 ") != std::string::npos);
        const auto state = algorithm->export_current_state().get_child("event_router");
        BOOST_CHECK_EQUAL(state.get<std::uint64_t>("iterations"), limit);
        BOOST_CHECK(state.get<bool>("warned"));
        // The cutoff drops undelivered ticks instead of iterating further.
        BOOST_CHECK(scenario.first->log.size() <= 2 * limit + 2);

        // The budget restarts at the next communication point.
        BOOST_REQUIRE_NO_THROW(execution.step());
        BOOST_CHECK(warnings.messages().find("time 1 ") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(convergent_clock_cycle_and_capability_warnings)
{
    const auto step = std::chrono::seconds(1);
    const cosim::ecco_algorithm_params params{
        0.99, step, step, step, 0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15};
    const std::shared_ptr<cosim::algorithm> algorithms[]{
        std::make_shared<cosim::fixed_step_algorithm>(step, 1),
        std::make_shared<cosim::ecco_algorithm>(params, 1)};
    for (const auto& algorithm : algorithms) {
        warning_capture warnings;
        ping_pong_scenario scenario({1, 2});
        scenario.first->outputRules[3] = [](const clock_set& active) {
            return active.count(1) != 0;
        };
        cosim::execution execution(cosim::time_point{}, algorithm);
        scenario.connect(execution);
        BOOST_REQUIRE_NO_THROW(execution.initialize());
        BOOST_CHECK(scenario.first->log.size() < 10u);
        BOOST_CHECK(scenario.second->log.size() < 10u);
        BOOST_CHECK(warnings.messages().find("first is connected to second") != std::string::npos);
        BOOST_CHECK(warnings.messages().find("cannot save and restore state") != std::string::npos);
        BOOST_CHECK(warnings.messages().find("did not converge") == std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(clock_retraction_oscillation_at_initialization_is_an_error)
{
    ping_pong_scenario scenario({1, 2});
    scenario.first->outputRules[3] = [](const clock_set& active) {
        return active.count(1) != 0 && active.count(2) == 0;
    };
    cosim::execution execution(
        cosim::time_point{},
        std::make_shared<cosim::fixed_step_algorithm>(std::chrono::seconds(1)));
    scenario.connect(execution);
    BOOST_CHECK_EXCEPTION(
        execution.initialize(),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == cosim::make_error_code(cosim::errc::simulation_error) &&
                std::string(error.what()).find("Clock deactivation did not converge") !=
                std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(clock_capable_simulator_cannot_use_step_size_decimation)
{
    auto clocked = std::make_shared<scripted_clock_slave>(std::vector<spec>{
        {1, input, periodic, 1.0, false, {}}});
    auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(std::chrono::seconds(1));
    cosim::execution execution(cosim::time_point{}, algorithm);
    const auto index = execution.add_slave(clocked, "clocked");
    algorithm->set_stepsize_decimation_factor(index, 2);
    execution.initialize();
    BOOST_CHECK_EXCEPTION(
        execution.step(),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == cosim::make_error_code(cosim::errc::unsupported_feature) &&
                std::string(error.what()).find("Clock capability") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(deactivated_output_clock_deactivates_the_clocks_it_activated)
{
    retraction_scenario scenario(true);
    cosim::execution execution(
        cosim::time_point{},
        std::make_shared<cosim::fixed_step_algorithm>(std::chrono::seconds(1)));
    scenario.connect(execution);
    execution.initialize();

    using entry = std::pair<cosim::value_reference, bool>;
    BOOST_TEST(
        (scenario.target->log == std::vector<entry>{{1, true}, {1, false}}),
        boost::test_tools::per_element());
    BOOST_TEST(
        (scenario.first->log == std::vector<entry>{{1, true}, {2, true}}),
        boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(clock_that_cannot_be_deactivated_rejects_a_retraction)
{
    retraction_scenario scenario(false);
    cosim::execution execution(
        cosim::time_point{},
        std::make_shared<cosim::fixed_step_algorithm>(std::chrono::seconds(1)));
    scenario.connect(execution);
    BOOST_CHECK_EXCEPTION(
        execution.initialize(),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == cosim::make_error_code(cosim::errc::unsupported_feature) &&
                std::string(error.what()).find("cannot be deactivated") != std::string::npos;
        });
}
