/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/execution.hpp"

#include "cosim/algorithm.hpp"
#include "cosim/exception.hpp"
#include "cosim/slave_simulator.hpp"
#include "cosim/utility/utility.hpp"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>


namespace cosim
{

class execution::impl
{
public:
    impl(time_point startTime, std::shared_ptr<algorithm> algo)
        : lastStep_(0)
        , currentTime_(startTime)
        , initialized_(false)
        , stopped_(true)
        , algorithm_(std::move(algo))
        , timer_()
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
        const auto index = static_cast<simulator_index>(simulators_.size());
        simulators_.push_back(std::make_unique<slave_simulator>(slave, name));
        algorithm_->add_simulator(index, simulators_.back().get(), stepSizeHint);

        for (const auto& obs : observers_) {
            obs->simulator_added(index, simulators_.back().get(), currentTime_);
        }
        for (const auto& man : manipulators_) {
            man->simulator_added(index, simulators_.back().get(), currentTime_);
        }
        return index;
    }

    function_index add_function(std::shared_ptr<function> fun)
    {
        const auto index = static_cast<function_index>(functions_.size());
        functions_.push_back(fun);
        algorithm_->add_function(index, fun.get());
        return index;
    }

    void add_observer(std::shared_ptr<observer> obs)
    {
        observers_.push_back(obs);
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            obs->simulator_added(
                static_cast<simulator_index>(i),
                simulators_[i].get(),
                currentTime_);
        }
        if (initialized_) {
            obs->simulation_initialized(lastStep_, currentTime_);
        }
    }

    void add_manipulator(std::shared_ptr<manipulator> man)
    {
        manipulators_.push_back(man);
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            man->simulator_added(
                static_cast<simulator_index>(i),
                simulators_[i].get(),
                currentTime_);
        }
    }

    void connect_variables(variable_id output, variable_id input)
    {
        connect_variables_impl(ssConnections_, output, input);
    }

    void connect_variables(variable_id output, function_io_id input)
    {
        connect_variables_impl(sfConnections_, output, input);
    }

    void connect_variables(function_io_id output, variable_id input)
    {
        connect_variables_impl(fsConnections_, output, input);
    }

    time_point current_time() const noexcept
    {
        return currentTime_;
    }

    bool is_running() const noexcept
    {
        return !stopped_;
    }

    duration step()
    {
        if (!initialized_) {
            algorithm_->initialize();
            initialized_ = true;
            for (const auto& obs : observers_) {
                obs->simulation_initialized(lastStep_, currentTime_);
            }
        }
        for (const auto& man : manipulators_) {
            man->step_commencing(currentTime_);
        }
        const auto [stepSize, finished] = algorithm_->do_step(currentTime_);
        currentTime_ += stepSize;
        ++lastStep_;
        for (const auto& obs : observers_) {
            for (const auto& index : finished) {
                obs->simulator_step_complete(index, lastStep_, stepSize, currentTime_);
            }
            obs->step_complete(lastStep_, stepSize, currentTime_);
        }
        return stepSize;
    }

    bool simulate_until(std::optional<time_point> endTime)
    {
        stopped_ = false;
        timer_.start(currentTime_);
        duration stepSize;
        do {
            stepSize = step();
            timer_.sleep(currentTime_);
        } while (!stopped_ && !timed_out(endTime, currentTime_, stepSize));
        bool isStopped = stopped_;
        stopped_ = true;
        return !isStopped;
    }

    std::future<bool> simulate_until_async(std::optional<time_point> endTime)
    {
        return std::async(std::launch::async, [this, endTime] {
            return simulate_until(endTime);
        });
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

    std::vector<variable_id> get_modified_variables() const
    {
        std::vector<variable_id> modifiedVariables;

        auto index = 0;
        for (const auto& sim : simulators_) {

            const auto& realRefs = sim->get_modified_real_variables();
            const auto& integerRefs = sim->get_modified_integer_variables();
            const auto& booleanRefs = sim->get_modified_boolean_variables();
            const auto& stringRefs = sim->get_modified_string_variables();

            for (const auto& ref : realRefs) {
                variable_id var = {index, variable_type::real, ref};
                modifiedVariables.push_back(var);
            }

            for (const auto& ref : integerRefs) {
                variable_id var = {index, variable_type::integer, ref};
                modifiedVariables.push_back(var);
            }

            for (const auto& ref : booleanRefs) {
                variable_id var = {index, variable_type::boolean, ref};
                modifiedVariables.push_back(var);
            }

            for (const auto& ref : stringRefs) {
                variable_id var = {index, variable_type::string, ref};
                modifiedVariables.push_back(var);
            }

            index++;
        }

        return modifiedVariables;
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

private:
    template<typename OutputID, typename InputID>
    void connect_variables_impl(
        std::unordered_map<InputID, OutputID>& connections,
        OutputID output,
        InputID input)
    {
        validate_variable(output, variable_causality::output);
        validate_variable(input, variable_causality::input);

        if (connections.count(input)) {
            throw std::logic_error("Input variable already connected");
        }
        algorithm_->connect_variables(output, input);
        connections.emplace(input, output);
    }

    void validate_variable(variable_id variable, variable_causality causality)
    {
        const auto variables = simulators_.at(variable.simulator)->model_description().variables;
        const auto it = std::find_if(
            variables.begin(),
            variables.end(),
            [=](const auto& var) { return var.causality == causality && var.type == variable.type && var.reference == variable.reference; });
        if (it == variables.end()) {
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
        const auto& group = description.io_groups.at(variable.reference.group);
        const auto& io = group.ios.at(variable.reference.io);
        if (io.causality != causality) {
            throw std::logic_error("Error connecting function variable: Wrong causality");
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

    step_number lastStep_;
    time_point currentTime_;
    bool initialized_;
    std::atomic<bool> stopped_;

    std::shared_ptr<algorithm> algorithm_;
    std::vector<std::shared_ptr<simulator>> simulators_;
    std::vector<std::shared_ptr<function>> functions_;
    std::vector<std::shared_ptr<observer>> observers_;
    std::vector<std::shared_ptr<manipulator>> manipulators_;
    std::unordered_map<variable_id, variable_id> ssConnections_;
    std::unordered_map<function_io_id, variable_id> sfConnections_;
    std::unordered_map<variable_id, function_io_id> fsConnections_;
    real_time_timer timer_;
};


execution::execution(time_point startTime, std::shared_ptr<algorithm> algo)
    : pimpl_(std::make_unique<impl>(startTime, algo))
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

function_index execution::add_function(std::shared_ptr<function> fun)
{
    return pimpl_->add_function(fun);
}

void execution::add_observer(std::shared_ptr<observer> obs)
{
    return pimpl_->add_observer(obs);
}

void execution::add_manipulator(std::shared_ptr<manipulator> man)
{
    return pimpl_->add_manipulator(man);
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

time_point execution::current_time() const noexcept
{
    return pimpl_->current_time();
}

bool execution::simulate_until(std::optional<time_point> endTime)
{
    return pimpl_->simulate_until(endTime);
}

std::future<bool> execution::simulate_until_async(std::optional<time_point> endTime)
{
    return pimpl_->simulate_until_async(endTime);
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
        variableDescription.reference};
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
            variableName.variable_instance}};
}
} // namespace


entity_index_maps inject_system_structure(
    execution& exe,
    const system_structure& sys,
    const variable_value_map& initialValues)
{
    // Sort entities in the configuration file sequence order
    const auto& range = sys.entities();
    std::vector<system_structure::entity> sorted_entities(range.begin(), range.end());
    std::sort(sorted_entities.begin(), sorted_entities.end(), [](system_structure::entity& x, system_structure::entity& y) {
        return x.index <= y.index;
    });

    // Add simulators and functions
    entity_index_maps indexMaps;
    for (const auto& entity : sorted_entities) {
        if (const auto model = entity_type_to<cosim::model>(entity.type)) {
            // Entity is a simulator
            const auto index = exe.add_slave(
                model->instantiate(entity.name),
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
        if (conn.source.is_simulator_variable()) {
            if (conn.target.is_simulator_variable()) {
                // Source is simulator, target is simulator
                exe.connect_variables(
                    make_variable_id(sys, indexMaps, conn.source),
                    make_variable_id(sys, indexMaps, conn.target));
            } else {
                // Source is simulator, target is function
                exe.connect_variables(
                    make_variable_id(sys, indexMaps, conn.source),
                    make_function_io_id(sys, indexMaps, conn.target));
            }
        } else {
            // Source is function, target must be simulator
            exe.connect_variables(
                make_function_io_id(sys, indexMaps, conn.source),
                make_variable_id(sys, indexMaps, conn.target));
        }
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
        if (varDesc.causality != variable_causality::parameter &&
            varDesc.causality != variable_causality::input) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Cannot set initial value of variable " +
                    to_text(var) +
                    " (only supported for parameters and inputs)");
        }
        const auto simIdx = indexMaps.simulators.at(var.entity_name);
        const auto valRef = varDesc.reference;
        std::visit(
            visitor(
                [&](double v) { exe.set_real_initial_value(simIdx, valRef, v); },
                [&](int v) { exe.set_integer_initial_value(simIdx, valRef, v); },
                [&](bool v) { exe.set_boolean_initial_value(simIdx, valRef, v); },
                [&](const std::string& v) { exe.set_string_initial_value(simIdx, valRef, v); }),
            val);
    }

    return indexMaps;
}


} // namespace cosim
