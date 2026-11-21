/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/execution.hpp"

#include "cosim/algorithm/ecco_algorithm.hpp"
#include "cosim/exception.hpp"
#include "cosim/function/function.hpp"
#include "cosim/model_description.hpp"
#include "cosim/orchestration.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/slave_simulator.hpp"
#include "cosim/system_structure.hpp"
#include "cosim/time.hpp"
#include "cosim/timer.hpp"

#include <boost/range/algorithm/find_if.hpp>
#include <gsl/util>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


namespace cosim
{

class execution::impl
{
public:
    impl(time_point startTime, std::shared_ptr<algorithm> algo)
        : currentTime_(startTime)
        , stopped_(true)
        , running_(false)
        , algorithm_(std::move(algo))
    {
        algorithm_->setup(currentTime_, std::nullopt);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    simulator_index add_slave(
        std::shared_ptr<slave> slave,
        std::string_view name,
        duration stepSizeHint)
    {
        if (initialized_) {
            throw std::logic_error("Cannot add a slave after execution initialization");
        }
        const auto index = static_cast<simulator_index>(simulators_.size());
        simulators_.push_back(std::make_unique<slave_simulator>(std::move(slave), name));
        algorithm_->add_simulator(index, simulators_.back().get(), stepSizeHint);

        for (const auto& obs : observers_) {
            obs->simulator_added(index, simulators_.back().get(), currentTime_);
        }
        for (const auto& man : manipulators_) {
            man->simulator_added(index, simulators_.back().get(), currentTime_);
        }
        return index;
    }

    function_index add_function(const std::shared_ptr<function>& fun)
    {
        if (initialized_) {
            throw std::logic_error("Cannot add a function after execution initialization");
        }
        const auto index = static_cast<function_index>(functions_.size());
        functions_.push_back(fun);
        algorithm_->add_function(index, fun.get());
        return index;
    }

    void add_observer(const std::shared_ptr<observer>& obs)
    {
        observers_.push_back(obs);
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            obs->simulator_added(
                static_cast<simulator_index>(i),
                simulators_.at(i).get(),
                currentTime_);
        }
        if (initialized_) {
            obs->simulation_initialized(lastStep_, currentTime_);
        }
    }

    void add_manipulator(const std::shared_ptr<manipulator>& man)
    {
        manipulators_.push_back(man);
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            man->simulator_added(
                static_cast<simulator_index>(i),
                simulators_.at(i).get(),
                currentTime_);
        }
    }

    void connect_variables(variable_id output, variable_id input)
    {
        connect_variables_impl(connectedSimulatorInputs_, output, input);
    }

    void connect_variables(variable_id output, function_io_id input)
    {
        connect_variables_impl(connectedFunctionInputs_, output, input);
    }

    void connect_variables(function_io_id output, variable_id input)
    {
        connect_variables_impl(connectedSimulatorInputs_, output, input);
    }

    void connect_variables(function_io_id output, function_io_id input)
    {
        connect_variables_impl(connectedFunctionInputs_, output, input);
    }

    time_point current_time() const noexcept
    {
        return currentTime_;
    }

    void initialize()
    {
        if (!initialized_) {
            algorithm_->initialize();
            initialized_ = true;
            for (const auto& obs : observers_) {
                obs->simulation_initialized(lastStep_, currentTime_);
            }
        }
    }


    bool is_running() const noexcept
    {
        return running_;
    }

    bool is_initialized() const noexcept
    {
        return initialized_;
    }

    duration step()
    {
        if (terminated_) {
            throw error(make_error_code(errc::simulation_error),
                "The execution cannot continue after a simulator requested termination");
        }
        initialize(); // For backwards compatibility. Should be replaced with a precondition check at some point.
        for (const auto& man : manipulators_) {
            man->step_commencing(currentTime_);
        }
        const auto stepInfo = algorithm_->do_step_with_info(currentTime_);
        if (stepInfo.termination_requested) {
            terminated_ = true;
            stopped_ = true;
            if (stepInfo.step_size == duration::zero() && stepInfo.finished.empty()) {
                return duration::zero();
            }
        }
        currentTime_ += stepInfo.step_size;
        ++lastStep_;
        for (const auto& obs : observers_) {
            for (const auto& index : stepInfo.finished) {
                obs->simulator_step_complete(
                    index,
                    lastStep_,
                    stepInfo.step_size,
                    currentTime_);
            }
            obs->step_complete(lastStep_, stepInfo.step_size, currentTime_);
        }
        return stepInfo.step_size;
    }

    bool simulate_until(std::optional<time_point> endTime)
    {
        stopped_ = false;
        running_ = true;
        return simulate_until_started(endTime);
    }

    bool simulate_until_started(std::optional<time_point> endTime)
    {
        const auto resetRunning = gsl::finally([this] {
            stopped_ = true;
            running_ = false;
        });
        if (terminated_) {
            throw error(make_error_code(errc::simulation_error),
                "The execution cannot continue after a simulator requested termination");
        }
        if (stopped_) {
            return false;
        }
        timer_.start(currentTime_);
        while (true) {
            const auto stepSize = step();
            timer_.sleep(currentTime_);
            if (stopped_ || timed_out(endTime, currentTime_, stepSize)) {
                break;
            }
        }
        bool const isStopped = stopped_;
        return !isStopped;
    }

    std::future<bool> simulate_until_async(std::optional<time_point> endTime)
    {
        stopped_ = false;
        running_ = true;
        try {
            return std::async(std::launch::async, [this, endTime] {
                return simulate_until_started(endTime);
            });
        } catch (...) {
            stopped_ = true;
            running_ = false;
            throw;
        }
    }

    void stop_simulation()
    {
        stopped_ = true;
    }

    std::shared_ptr<real_time_config> get_real_time_config() const
    {
        return timer_.get_real_time_config();
    }

    std::shared_ptr<const real_time_metrics> get_real_time_metrics() const
    {
        return timer_.get_real_time_metrics();
    }

    model_description get_model_description(simulator_index index) const
    {
        return simulators_.at(index)->model_description();
    }

    std::shared_ptr<slave> get_slave(simulator_index index) const
    {
        return simulators_.at(index)->get_slave();
    }

    std::vector<variable_id> get_modified_variables() const
    {
        std::vector<variable_id> modifiedVariables;

        auto index = 0;
        for (const auto& sim : simulators_) {
            for (const auto& variable : sim->get_modified_variables()) {
                modifiedVariables.push_back({index, variable.type, variable.reference});
            }

            index++;
        }

        return modifiedVariables;
    }

    std::shared_ptr<cosim::algorithm> get_algorithm()
    {
        return algorithm_;
    }

    void set_real_initial_value(simulator_index sim, value_reference var, double value)
    {
        if (initialized_) {
            throw error(make_error_code(errc::unsupported_feature), "Initial values must be set before simulation is started");
        }
        simulators_.at(sim)->expose_for_setting(variable_type::real, var);
        simulators_.at(sim)->set_real(var, value);
    }

    void set_integer_initial_value(simulator_index sim, value_reference var, int value)
    {
        if (initialized_) {
            throw error(make_error_code(errc::unsupported_feature), "Initial values must be set before simulation is started");
        }
        simulators_.at(sim)->expose_for_setting(variable_type::integer, var);
        simulators_.at(sim)->set_integer(var, value);
    }

    void set_boolean_initial_value(simulator_index sim, value_reference var, bool value)
    {
        if (initialized_) {
            throw error(make_error_code(errc::unsupported_feature), "Initial values must be set before simulation is started");
        }
        simulators_.at(sim)->expose_for_setting(variable_type::boolean, var);
        simulators_.at(sim)->set_boolean(var, value);
    }

    void set_string_initial_value(simulator_index sim, value_reference var, const std::string& value)
    {
        if (initialized_) {
            throw error(make_error_code(errc::unsupported_feature), "Initial values must be set before simulation is started");
        }
        simulators_.at(sim)->expose_for_setting(variable_type::string, var);
        simulators_.at(sim)->set_string(var, value);
    }

    void set_variable_initial_value(
        variable_id variable,
        const variable_value& value)
    {
        if (initialized_) {
            throw error(make_error_code(errc::unsupported_feature), "Initial values must be set before simulation is started");
        }
        const auto model = simulators_.at(variable.simulator)->model_description();
        const auto description = boost::range::find_if(
            model.variables, [&](const auto& candidate) {
                return candidate.type == variable.type &&
                    candidate.reference == variable.reference;
            });
        if (description == model.variables.end()) {
            throw std::out_of_range("Invalid variable type or reference");
        }
        std::string reason;
        if (!is_valid_variable_value(*description, value, &reason)) {
            throw std::invalid_argument("Invalid initial variable value: " + reason);
        }
        simulators_.at(variable.simulator)
            ->expose_for_setting(variable.type, variable.reference);
        simulators_.at(variable.simulator)
            ->set_variable_value(variable.type, variable.reference, value);
    }

    serialization::node export_current_state() const
    {
        if (!initialized_ || is_running()) {
            throw std::logic_error("Execution state export requires an initialized, stopped execution");
        }
        const auto algorithmState = algorithm_->export_current_state();
        serialization::node simulatorState;
        for (const auto& sim : simulators_) {
            const auto savedStateIndex = sim->save_state();
            simulatorState.put_child(sim->name(), sim->export_state(savedStateIndex));
            sim->release_state(savedStateIndex);
        }
        serialization::node exportedState;
        exportedState.put("last_step_number", lastStep_);
        exportedState.put<std::int64_t>("current_time_ticks", currentTime_.time_since_epoch().count());
        exportedState.put_child("algorithm_state", algorithmState);
        exportedState.put_child("simulator_state", simulatorState);
        return exportedState;
    }

    void import_state(const serialization::node& exportedState)
    {
        if (!initialized_ || is_running()) {
            throw std::logic_error("Execution state import requires an initialized, stopped execution");
        }
        try {
            lastStep_ = exportedState.get<step_number>("last_step_number");
            currentTime_ = time_point(duration(
                exportedState.get<std::int64_t>("current_time_ticks")));
            const auto& simulatorState = exportedState.get_child("simulator_state");
            for (const auto& sim : simulators_) {
                const auto savedStateIndex =
                    sim->import_state(simulatorState.get_child(sim->name()));
                sim->restore_state(savedStateIndex);
                sim->release_state(savedStateIndex);
            }
            algorithm_->import_state(exportedState.get_child("algorithm_state"));
            terminated_ = false;
        } catch (const error&) {
            throw;
        } catch (...) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized execution state is invalid or corrupt");
        }

        // Update observers
        for (const auto& obs : observers_) {
            obs->state_restored(lastStep_, currentTime_);
        }
    }

private:
    template<typename OutputID, typename InputID>
    void connect_variables_impl(
        std::unordered_set<InputID>& connections,
        OutputID output,
        InputID input)
    {
        validate_variable(output, variable_causality::output);
        validate_variable(input, variable_causality::input);
        validate_connection(output, input);

        if (connections.count(input) != 0) {
            throw std::logic_error("Input variable already connected");
        }
        algorithm_->connect_variables(output, input);
        connections.emplace(input);
    }

    void validate_variable(variable_id variable, variable_causality causality)
    {
        const auto variables = simulators_.at(variable.simulator)->model_description().variables;
        const auto found = boost::range::find_if(
            variables,
            [=](const auto& var) { return var.causality == causality && var.type == variable.type && var.reference == variable.reference; });
        if (found == variables.end()) {
            std::ostringstream oss;
            oss << "Problem adding connection: Cannot find variable with reference " << variable.reference
                << ", causality " << cosim::to_text(causality)
                << " and type " << cosim::to_text(variable.type)
                << " for simulator with index " << variable.simulator
                << " and name " << simulators_.at(variable.simulator)->name();
            throw std::out_of_range(oss.str());
        }
    }

    void validate_variable(function_io_id variable, variable_causality causality)
    {
        const auto description = functions_.at(variable.function)->description();
        const auto& reference = variable.reference;
        if (reference.group < 0 ||
            reference.group >= static_cast<int>(description.io_groups.size())) {
            throw std::out_of_range("Invalid function IO group index");
        }
        const auto& group = description.io_groups.at(static_cast<std::size_t>(reference.group));
        const auto* const groupCount = std::get_if<int>(&group.count);
        if ((groupCount == nullptr) || reference.group_instance < 0 ||
            reference.group_instance >= *groupCount) {
            throw std::out_of_range("Invalid function IO group instance");
        }
        if (reference.io < 0 ||
            reference.io >= static_cast<int>(group.ios.size())) {
            throw std::out_of_range("Invalid function IO index");
        }
        const auto& inputOutput = group.ios.at(static_cast<std::size_t>(reference.io));
        const auto* const ioCount = std::get_if<int>(&inputOutput.count);
        if ((ioCount == nullptr) || reference.io_instance < 0 ||
            reference.io_instance >= *ioCount) {
            throw std::out_of_range("Invalid function IO instance");
        }
        const auto* const type = std::get_if<variable_type>(&inputOutput.type);
        if ((type == nullptr) || *type != variable.type) {
            throw std::logic_error("Error connecting function variable: Wrong type");
        }
        if (inputOutput.causality != causality) {
            throw std::logic_error("Error connecting function variable: Wrong causality");
        }
    }

    variable_description describe(
        variable_id variable,
        variable_causality causality) const
    {
        const auto variables =
            simulators_.at(variable.simulator)->model_description().variables;
        const auto found = boost::range::find_if(
            variables,
            [=](const auto& item) {
                return item.type == variable.type &&
                    item.reference == variable.reference &&
                    item.causality == causality;
            });
        if (found == variables.end()) {
            throw std::out_of_range("Invalid variable type or reference");
        }
        return *found;
    }

    function_io_description describe(
        function_io_id variable,
        variable_causality causality) const
    {
        const auto description =
            functions_.at(variable.function)->description();
        const auto result = description.io_groups.at(variable.reference.group)
                                .ios.at(variable.reference.io);
        if (result.causality != causality) {
            throw std::logic_error(
                "Function endpoint has an unexpected causality");
        }
        return result;
    }

    template<typename OutputID, typename InputID>
    void validate_connection(OutputID output, InputID input) const
    {
        std::string reason;
        if (!is_valid_connection(
                describe(output, variable_causality::output),
                describe(input, variable_causality::input),
                &reason)) {
            throw std::invalid_argument(
                "Invalid variable connection: " + reason);
        }
    }

    void validate_connection(variable_id output, variable_id input) const
    {
        const auto sourceModel =
            simulators_.at(output.simulator)->model_description();
        const auto targetModel =
            simulators_.at(input.simulator)->model_description();
        std::string reason;
        if (!is_valid_connection(
                sourceModel,
                describe(output, variable_causality::output),
                targetModel,
                describe(input, variable_causality::input),
                &reason)) {
            throw std::invalid_argument(
                "Invalid variable connection: " + reason);
        }
    }

    static bool timed_out(std::optional<time_point> endTime, time_point currentTime, duration stepSize)
    {
        constexpr double relativeTolerance = 0.01;
        if (endTime) {
            return *endTime - currentTime < stepSize * relativeTolerance;
        }
        return false;
    }

    step_number lastStep_ = 0;
    time_point currentTime_;
    bool initialized_ = false;
    std::atomic<bool> stopped_;
    std::atomic<bool> running_;
    bool terminated_ = false;

    std::shared_ptr<algorithm> algorithm_;
    std::vector<std::shared_ptr<slave_simulator>> simulators_;
    std::vector<std::shared_ptr<function>> functions_;
    std::vector<std::shared_ptr<observer>> observers_;
    std::vector<std::shared_ptr<manipulator>> manipulators_;
    std::unordered_set<variable_id> connectedSimulatorInputs_;
    std::unordered_set<function_io_id> connectedFunctionInputs_;
    real_time_timer timer_;
};


execution::execution(time_point startTime, std::shared_ptr<algorithm> algo)
    : pimpl_(std::make_unique<impl>(startTime, std::move(algo)))
{
}

execution::~execution() noexcept = default;
execution::execution(execution&& other) noexcept = default;
execution& execution::operator=(execution&& other) noexcept = default;

simulator_index execution::add_slave(
    std::shared_ptr<slave> slave,
    std::string_view name,
    duration stepSizeHint)
{
    return pimpl_->add_slave(std::move(slave), name, stepSizeHint);
}

std::shared_ptr<slave> execution::get_slave(simulator_index index) const
{
    return pimpl_->get_slave(index);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param): Preserve the public by-value interface.
function_index execution::add_function(std::shared_ptr<function> fun)
{
    return pimpl_->add_function(fun);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param): Preserve the public by-value interface.
void execution::add_observer(std::shared_ptr<observer> obs)
{
    pimpl_->add_observer(obs);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param): Preserve the public by-value interface.
void execution::add_manipulator(std::shared_ptr<manipulator> man)
{
    pimpl_->add_manipulator(man);
}

void execution::connect_variables(variable_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void execution::connect_variables(variable_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

void execution::connect_variables(function_io_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void execution::connect_variables(
    function_io_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

time_point execution::current_time() const noexcept
{
    return pimpl_->current_time();
}

void execution::initialize()
{
    pimpl_->initialize();
}

bool execution::is_initialized() const noexcept
{
    return pimpl_->is_initialized();
}

bool execution::simulate_until(std::optional<time_point> targetTime)
{
    return pimpl_->simulate_until(targetTime);
}

std::future<bool> execution::simulate_until_async(std::optional<time_point> targetTime)
{
    return pimpl_->simulate_until_async(targetTime);
}

duration execution::step()
{
    return pimpl_->step();
}

bool execution::is_running() const noexcept
{
    return pimpl_->is_running();
}

void execution::stop_simulation()
{
    pimpl_->stop_simulation();
}

// NOLINTNEXTLINE(readability-const-return-type): Preserve the public return type.
const std::shared_ptr<real_time_config> execution::get_real_time_config() const
{
    return pimpl_->get_real_time_config();
}

std::shared_ptr<const real_time_metrics> execution::get_real_time_metrics() const
{
    return pimpl_->get_real_time_metrics();
}

model_description execution::get_model_description(simulator_index index) const
{
    return pimpl_->get_model_description(index);
}

std::vector<variable_id> execution::get_modified_variables() const
{
    return pimpl_->get_modified_variables();
}

std::shared_ptr<cosim::algorithm> execution::get_algorithm() const
{
    return pimpl_->get_algorithm();
}

void execution::set_real_initial_value(simulator_index sim, value_reference var, double value)
{
    pimpl_->set_real_initial_value(sim, var, value);
}

void execution::set_integer_initial_value(simulator_index sim, value_reference var, int value)
{
    pimpl_->set_integer_initial_value(sim, var, value);
}

void execution::set_boolean_initial_value(simulator_index sim, value_reference var, bool value)
{
    pimpl_->set_boolean_initial_value(sim, var, value);
}

void execution::set_string_initial_value(simulator_index sim, value_reference var, const std::string& value)
{
    pimpl_->set_string_initial_value(sim, var, value);
}

void execution::set_variable_initial_value(
    variable_id variable,
    const variable_value& value)
{
    pimpl_->set_variable_initial_value(variable, value);
}

serialization::node execution::export_current_state() const
{
    return pimpl_->export_current_state();
}

void execution::import_state(const serialization::node& exportedState)
{
    pimpl_->import_state(exportedState);
}


namespace
{
variable_id make_variable_id(
    const system_structure& systemStructure,
    const entity_index_maps& indexMaps,
    const full_variable_name& variableName)
{
    const auto& variableDescription =
        systemStructure.get_variable_description(variableName);
    return {
        indexMaps.simulators.at(variableName.entity_name),
        variableDescription.type,
        variableDescription.reference,
    };
}

function_io_id make_function_io_id(
    const system_structure& systemStructure,
    const entity_index_maps& indexMaps,
    const full_variable_name& variableName)
{
    const auto& variableDescription =
        systemStructure.get_function_io_description(variableName);
    return {
        indexMaps.functions.at(variableName.entity_name),
        std::get<variable_type>(variableDescription.description.type),
        function_io_reference{
            variableDescription.group_index,
            variableName.variable_group_instance,
            variableDescription.io_index,
            variableName.variable_instance,
        },
    };
}

void connect_system_variables(
    execution& exe,
    const system_structure& sys,
    const entity_index_maps& indexMaps,
    const system_structure::connection& conn)
{
    if (conn.source.is_simulator_variable()) {
        if (conn.target.is_simulator_variable()) {
            exe.connect_variables(
                make_variable_id(sys, indexMaps, conn.source),
                make_variable_id(sys, indexMaps, conn.target));
        } else {
            exe.connect_variables(
                make_variable_id(sys, indexMaps, conn.source),
                make_function_io_id(sys, indexMaps, conn.target));
        }
    } else if (conn.target.is_simulator_variable()) {
        exe.connect_variables(
            make_function_io_id(sys, indexMaps, conn.source),
            make_variable_id(sys, indexMaps, conn.target));
    } else {
        exe.connect_variables(
            make_function_io_id(sys, indexMaps, conn.source),
            make_function_io_id(sys, indexMaps, conn.target));
    }
}

void validate_power_bond_endpoint(
    const system_structure& sys,
    const full_variable_name& variable)
{
    const auto& description = sys.get_variable_description(variable);
    if (description.type != variable_type::real) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "ECCO power bonds require Float64 endpoints");
    }
    if (!description.clocks.empty()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Clock-associated variables cannot be used in ECCO power bonds");
    }
}
} // namespace


entity_index_maps inject_system_structure(
    execution& exe,
    const system_structure& sys,
    const variable_value_map& initialValues)
{
    return inject_system_structure(exe, sys, initialValues, {});
}

entity_index_maps inject_system_structure(
    execution& exe,
    const system_structure& sys,
    const variable_value_map& initialValues,
    const model_instantiator& instantiate)
{
    // Add simulators and functions
    entity_index_maps indexMaps;
    for (const auto& entity : sys.entities()) {
        if (const auto model = entity_type_to<cosim::model>(entity.type)) {
            // Entity is a simulator
            auto instance = instantiate ? instantiate(model, entity.name) : model->instantiate(entity.name);
            if (!instance) {
                throw std::invalid_argument("Instantiation hook returned a null slave for '" + entity.name + "'");
            }
            const auto index = exe.add_slave(
                std::move(instance),
                entity.name,
                entity.step_size_hint);
            indexMaps.simulators.emplace(std::string(entity.name), index);
        } else {
            // Entity is a function
            const auto functionType = entity_type_to<function_type>(entity.type);
            assert(functionType);
            const auto index = exe.add_function(
                functionType->instantiate(entity.parameter_values));
            indexMaps.functions.emplace(std::string(entity.name), index);
        }
    }

    // Connect variables
    for (const auto& conn : sys.connections()) {
        connect_system_variables(exe, sys, indexMaps, conn);
    }

    // Set initial values
    for (const auto& [var, val] : initialValues) {
        if (!var.is_simulator_variable()) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Cannot set initial value of variable " +
                    to_text(var) +
                    " (only supported for simulator variables)");
        }
        const auto& varDesc = sys.get_variable_description(var);
        const auto simIdx = indexMaps.simulators.at(var.entity_name);
        const auto valRef = varDesc.reference;
        exe.set_variable_initial_value({simIdx, varDesc.type, valRef}, val);
    }

    // For ECCO algorithm, add the powerbonds
    if (const auto algorithm = std::dynamic_pointer_cast<ecco_algorithm>(exe.get_algorithm())) {
        const auto powerbonds = sys.get_power_bonds();
        if (powerbonds.empty()) {
            throw error(make_error_code(errc::invalid_system_structure), "No power bonds were found, bonds need to be configured in the system structure order to use the ECCO algorithm.");
        }

        for (const auto& [name, powerBond] : powerbonds) {
            validate_power_bond_endpoint(sys, powerBond.input_a);
            validate_power_bond_endpoint(sys, powerBond.output_a);
            validate_power_bond_endpoint(sys, powerBond.input_b);
            validate_power_bond_endpoint(sys, powerBond.output_b);

            const auto input_a = make_variable_id(sys, indexMaps, powerBond.input_a);
            const auto output_a = make_variable_id(sys, indexMaps, powerBond.output_a);
            const auto input_b = make_variable_id(sys, indexMaps, powerBond.input_b);
            const auto output_b = make_variable_id(sys, indexMaps, powerBond.output_b);

            algorithm->add_power_bond(input_a, output_a, input_b, output_b);
        }
    }

    return indexMaps;
}


} // namespace cosim
