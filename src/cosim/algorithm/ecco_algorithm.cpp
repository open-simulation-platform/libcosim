/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/algorithm/ecco_algorithm.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/thread_pool.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>


namespace cosim
{

class ecco_algorithm::impl
{
public:
    explicit impl(ecco_algorithm_params params, std::optional<unsigned int> workerThreadCount)
        : params_(params)
        , stepSize_(params.step_size)
        , pool_(std::min(workerThreadCount.value_or(max_threads_), max_threads_))
    {
        COSIM_INPUT_CHECK(params_.min_step_size.count() > 0);
        COSIM_INPUT_CHECK(params_.step_size >= params_.min_step_size);
        COSIM_INPUT_CHECK(params_.step_size <= params_.max_step_size);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(simulator_index i, simulator* s, [[maybe_unused]] duration stepSizeHint)
    {
        assert(simulators_.count(i) == 0);
        simulators_[i].sim = s;
    }

    void remove_simulator(simulator_index i)
    {
        simulators_.erase(i);
        disconnect_simulator_variables(i);
    }

    void add_function(function_index i, function* f)
    {
        assert(functions_.count(i) == 0);
        functions_[i].fun = f;
    }

    void connect_variables(variable_id output, variable_id input)
    {
        auto& sourceSimInfo = simulators_.at(output.simulator);
        auto& targetSimInfo = simulators_.at(input.simulator);
        sourceSimInfo.sim->expose_for_getting(output.type, output.reference);
        targetSimInfo.sim->expose_for_setting(input.type, input.reference);
        sourceSimInfo.outgoingSimConnections.push_back({output, input});
    }

    void connect_variables(variable_id output, function_io_id input)
    {
        auto& simInfo = simulators_.at(output.simulator);
        simInfo.sim->expose_for_getting(output.type, output.reference);
        simInfo.outgoingFunConnections.push_back({output, input});
    }

    void connect_variables(function_io_id output, variable_id input)
    {
        auto& funInfo = functions_.at(output.function);
        auto& simInfo = simulators_.at(input.simulator);
        simInfo.sim->expose_for_setting(input.type, input.reference);
        funInfo.outgoingSimConnections.push_back({output, input});
    }

    void disconnect_variable(variable_id input)
    {
        for (auto& s : simulators_) {
            auto& conns = s.second.outgoingSimConnections;
            const auto it = std::find_if(
                conns.begin(),
                conns.end(),
                [input](const auto& c) { return c.target == input; });
            if (it != conns.end()) {
                conns.erase(it);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
    }

    void disconnect_variable(function_io_id input)
    {
        for (auto& s : simulators_) {
            auto& conns = s.second.outgoingFunConnections;
            const auto it = std::find_if(
                conns.begin(),
                conns.end(),
                [input](const auto& c) { return c.target == input; });
            if (it != conns.end()) {
                conns.erase(it);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
    }

    void setup(time_point startTime, std::optional<time_point> stopTime)
    {
        startTime_ = startTime;
        stopTime_ = stopTime;
    }

    void initialize()
    {
        for (auto& s : simulators_) {
            pool_.submit([&] {
                s.second.sim->setup(startTime_, stopTime_, std::nullopt);
            });
        }
        pool_.wait_for_tasks_to_finish();

        // Run N iterations of the simulators' and functions' step/calculation
        // procedures, where N is the number of simulators in the system,
        // to propagate initial values.
        for (std::size_t i = 0; i < simulators_.size() + functions_.size(); ++i) {
            for (auto& s : simulators_) {
                pool_.submit([&] {
                    s.second.sim->do_iteration();
                });
            }
            pool_.wait_for_tasks_to_finish();

            for (const auto& s : simulators_) {
                transfer_variables(s.second.outgoingSimConnections);
                transfer_variables(s.second.outgoingFunConnections);
            }
            for (const auto& f : functions_) {
                f.second.fun->calculate();
                transfer_variables(f.second.outgoingSimConnections);
            }
        }

        for (auto& s : simulators_) {
            pool_.submit([&] {
                s.second.sim->start_simulation();
            });
        }
        pool_.wait_for_tasks_to_finish();
    }

    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT)
    {
        std::mutex m;
        bool failed = false;
        std::stringstream errMessages;
        std::unordered_set<simulator_index> finished;
        // Initiate simulator time steps.

        for (auto& s : simulators_) {
            auto& info = s.second;
            pool_.submit([&] {
                try {
                    info.stepResult = info.sim->do_step(currentT, stepSize_);

                    if (info.stepResult != step_result::complete) {
                        std::lock_guard<std::mutex> lck(m);
                        errMessages
                            << info.sim->name() << ": "
                            << "Step not complete" << '\n';
                        failed = true;
                    }

                } catch (std::exception& ex) {
                    std::lock_guard<std::mutex> lck(m);
                    errMessages
                        << info.sim->name() << ": "
                        << ex.what() << '\n';
                    failed = true;
                }
            });
        }

        ++stepCounter_;

        for (auto& [idx, info] : simulators_) {
            finished.insert(idx);
        }

        pool_.wait_for_tasks_to_finish();

        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }

        const auto stepSizeTaken = stepSize_;
        if (stepCounter_ >= 2) {
            stepSize_ = adjust_step_size(currentT, stepSize_, params_);
        }

        // Transfer the outputs from simulators that have finished their
        // individual time steps within this co-simulation time step.
        for (auto simIndex : finished) {
            auto& simInfo = simulators_.at(simIndex);
            transfer_variables(simInfo.outgoingSimConnections);
            transfer_variables(simInfo.outgoingFunConnections);
        }

        // Calculate functions and transfer their outputs to simulators.
        for (const auto& f : functions_) {
            const auto& info = f.second;
            info.fun->calculate();
            transfer_variables(info.outgoingSimConnections);
        }


        return {stepSizeTaken, std::move(finished)};
    }

    serialization::node export_current_state() const
    {
        throw error(make_error_code(errc::unsupported_feature), "State saving not yet supported by the ECCO algorithm!");
    }

    void import_state([[maybe_unused]] const serialization::node& exportedState)
    {
        throw error(make_error_code(errc::unsupported_feature), "State saving not yet supported by the ECCO algorithm!");
    }

    void get_energies()
    {
        for (std::size_t i = 0; i < energies_.size(); ++i) {
            std::cout << "Avg energy for sim idx " << i << ": " << get_mean(energies_.at(i)) << std::endl;
        }
    }

    duration adjust_step_size(time_point currentTime, const duration& stepSize, const ecco_algorithm_params& params)
    {
        std::vector<double> power_residuals{};
        std::vector<double> power{};

        const auto dt = to_double_duration(stepSize, currentTime);

        for (std::size_t i = 0; i < inputVariables_.size(); i += 2) {
            auto input_a = inputVariables_.at(i);
            auto input_b = inputVariables_.at(i + 1);
            auto output_a = outputVariables_.at(i);
            auto output_b = outputVariables_.at(i + 1);

            double input_a_value = simulators_.at(input_a.simulator).sim->get_real(input_a.reference);
            double input_b_value = simulators_.at(input_b.simulator).sim->get_real(input_b.reference);
            double output_a_value = simulators_.at(output_a.simulator).sim->get_real(output_a.reference);
            double output_b_value = simulators_.at(output_b.simulator).sim->get_real(output_b.reference);

            double power_a = input_a_value * output_a_value;
            energies_.at(i).push_back(power_a * dt);

            double power_b = input_b_value * output_b_value;
            energies_.at(i + 1).push_back(power_b * dt);

            double power_residual = std::abs(power_a - power_b);

            power_residuals.push_back(power_residual);
        }

        if (power_residuals.empty()) {
            return stepSize;
        }

        double max_power_residual = *std::max_element(power_residuals.begin(), power_residuals.end());
        const auto energy_level = max_power_residual * dt;
        double mean_square{};
        for (auto power_residual : power_residuals) {
            const auto energy_residual = power_residual * dt;
            mean_square += std::pow(energy_residual / (params.abs_tolerance + params.rel_tolerance * energy_level), 2);
        }
        const auto num_bonds = power_residuals.size(); // TODO: Still valid for multidimensial bonds?
        const auto error_estimate = std::sqrt(mean_square / (double)num_bonds);

        if (prev_error_estimate_ == 0 || error_estimate == 0) {
            prev_error_estimate_ = error_estimate;
            return stepSize;
        }

        // Compute a new step size
        const auto new_step_size_gain_value = params.safety_factor * std::pow(error_estimate, -params.i_gain - params.p_gain) * std::pow(prev_error_estimate_, params.p_gain);
        auto new_step_size_gain = std::clamp(new_step_size_gain_value, params.min_change_rate, params.max_change_rate);

        prev_error_estimate_ = error_estimate;
        const auto new_step_size = to_duration(new_step_size_gain * to_double_duration(stepSize, currentTime));
        const auto actual_new_step_size = std::clamp(new_step_size, params.min_step_size, params.max_step_size);
        return actual_new_step_size;
    }

    void add_power_bond(cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b)
    {
        energies_.emplace_back();
        energies_.emplace_back();
        inputVariables_.push_back(input_a);
        outputVariables_.push_back(output_a);
        inputVariables_.push_back(input_b);
        outputVariables_.push_back(output_b);
    }

    std::vector<double> get_powerbond_energies(cosim::simulator_index simulator_index)
    {
        return energies_.at(simulator_index);
    }

private:
    std::vector<cosim::variable_id> inputVariables_{};
    std::vector<cosim::variable_id> outputVariables_{};
    std::vector<std::vector<double>> energies_{};

    double get_mean(const std::vector<double>& elems)
    {
        if (elems.empty()) return 0.0;
        double sum{};
        for (auto elem : elems) {
            sum += elem;
        }
        return sum / (double)elems.size();
    }

    struct connection_ss
    {
        variable_id source;
        variable_id target;
    };

    struct connection_sf
    {
        variable_id source;
        function_io_id target;
    };

    struct connection_fs
    {
        function_io_id source;
        variable_id target;
    };

    struct simulator_info
    {
        simulator* sim;
        step_result stepResult;
        std::vector<connection_ss> outgoingSimConnections;
        std::vector<connection_sf> outgoingFunConnections;
    };

    struct function_info
    {
        function* fun;
        std::vector<connection_fs> outgoingSimConnections;
    };

    void disconnect_simulator_variables(simulator_index i)
    {
        for (auto& s : simulators_) {
            auto& connections = s.second.outgoingSimConnections;
            const auto newEnd = std::remove_if(
                connections.begin(),
                connections.end(),
                [i](const auto& c) { return c.target.simulator == i; });
            connections.erase(newEnd, connections.end());
        }
        for (auto& f : functions_) {
            auto& connections = f.second.outgoingSimConnections;
            const auto newEnd = std::remove_if(
                connections.begin(),
                connections.end(),
                [i](const auto& c) { return c.target.simulator == i; });
            connections.erase(newEnd, connections.end());
        }
    }

    void transfer_variables(const std::vector<connection_ss>& connections)
    {
        for (const auto& c : connections) {
            transfer_variable(c);
        }
    }

    void transfer_variables(const std::vector<connection_sf>& connections)
    {
        for (const auto& c : connections) {
            transfer_variable(c);
        }
    }

    void transfer_variables(const std::vector<connection_fs>& connections)
    {
        for (const auto& c : connections) {
            transfer_variable(c);
        }
    }

    void transfer_variable(const connection_ss& c)
    {
        assert(c.source.type == c.target.type);
        switch (c.target.type) {
            case variable_type::real:
                simulators_.at(c.target.simulator).sim->set_real(c.target.reference, simulators_.at(c.source.simulator).sim->get_real(c.source.reference));
                break;
            case variable_type::integer:
                simulators_.at(c.target.simulator).sim->set_integer(c.target.reference, simulators_.at(c.source.simulator).sim->get_integer(c.source.reference));
                break;
            case variable_type::boolean:
                simulators_.at(c.target.simulator).sim->set_boolean(c.target.reference, simulators_.at(c.source.simulator).sim->get_boolean(c.source.reference));
                break;
            case variable_type::string:
                simulators_.at(c.target.simulator).sim->set_string(c.target.reference, simulators_.at(c.source.simulator).sim->get_string(c.source.reference));
                break;
            case variable_type::enumeration:
                COSIM_PANIC_M("Can't handle variable of type 'enumeration' yet");
        }
    }

    void transfer_variable(const connection_sf& c)
    {
        assert(c.source.type == c.target.type);
        switch (c.target.type) {
            case variable_type::real:
                functions_.at(c.target.function).fun->set_real(c.target.reference, simulators_.at(c.source.simulator).sim->get_real(c.source.reference));
                break;
            case variable_type::integer:
                functions_.at(c.target.function).fun->set_integer(c.target.reference, simulators_.at(c.source.simulator).sim->get_integer(c.source.reference));
                break;
            case variable_type::boolean:
                functions_.at(c.target.function).fun->set_boolean(c.target.reference, simulators_.at(c.source.simulator).sim->get_boolean(c.source.reference));
                break;
            case variable_type::string:
                functions_.at(c.target.function).fun->set_string(c.target.reference, simulators_.at(c.source.simulator).sim->get_string(c.source.reference));
                break;
            case variable_type::enumeration:
                COSIM_PANIC_M("Can't handle variable of type 'enumeration' yet");
        }
    }

    void transfer_variable(const connection_fs& c)
    {
        assert(c.source.type == c.target.type);
        switch (c.target.type) {
            case variable_type::real:
                simulators_.at(c.target.simulator).sim->set_real(c.target.reference, functions_.at(c.source.function).fun->get_real(c.source.reference));
                break;
            case variable_type::integer:
                simulators_.at(c.target.simulator).sim->set_integer(c.target.reference, functions_.at(c.source.function).fun->get_integer(c.source.reference));
                break;
            case variable_type::boolean:
                simulators_.at(c.target.simulator).sim->set_boolean(c.target.reference, functions_.at(c.source.function).fun->get_boolean(c.source.reference));
                break;
            case variable_type::string:
                simulators_.at(c.target.simulator).sim->set_string(c.target.reference, functions_.at(c.source.function).fun->get_string(c.source.reference));
                break;
            case variable_type::enumeration:
                COSIM_PANIC_M("Can't handle variable of type 'enumeration' yet");
        }
    }

    const ecco_algorithm_params params_;
    duration stepSize_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator_info> simulators_;
    std::unordered_map<function_index, function_info> functions_;
    int64_t stepCounter_ = 0;
    unsigned int max_threads_ = std::thread::hardware_concurrency() - 1;
    utility::thread_pool pool_;
    double prev_error_estimate_{1.0};
};


ecco_algorithm::ecco_algorithm(ecco_algorithm_params params, std::optional<unsigned int> workerThreadCount)
    : pimpl_(std::make_unique<impl>(params, workerThreadCount))
{
}


ecco_algorithm::~ecco_algorithm() noexcept
{
}


ecco_algorithm::ecco_algorithm(ecco_algorithm&& other) noexcept
    : pimpl_(std::move(other.pimpl_))
{
}


ecco_algorithm& ecco_algorithm::operator=(
    ecco_algorithm&& other) noexcept
{
    pimpl_ = std::move(other.pimpl_);
    return *this;
}


void ecco_algorithm::add_simulator(
    simulator_index i,
    simulator* s,
    duration stepSizeHint)
{
    pimpl_->add_simulator(i, s, stepSizeHint);
}


void ecco_algorithm::remove_simulator(simulator_index i)
{
    pimpl_->remove_simulator(i);
}


void ecco_algorithm::add_function(function_index i, function* f)
{
    pimpl_->add_function(i, f);
}

void ecco_algorithm::connect_variables(variable_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::connect_variables(variable_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::connect_variables(function_io_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::disconnect_variable(variable_id input)
{
    pimpl_->disconnect_variable(input);
}

void ecco_algorithm::disconnect_variable(function_io_id input)
{
    pimpl_->disconnect_variable(input);
}


void ecco_algorithm::setup(
    time_point startTime,
    std::optional<time_point> stopTime)
{
    pimpl_->setup(startTime, stopTime);
}


void ecco_algorithm::initialize()
{
    pimpl_->initialize();
}


std::pair<duration, std::unordered_set<simulator_index>> ecco_algorithm::do_step(
    time_point currentT)
{
    return pimpl_->do_step(currentT);
}

serialization::node ecco_algorithm::export_current_state() const
{
    return pimpl_->export_current_state();
}

void ecco_algorithm::import_state(const serialization::node& exportedState)
{
    pimpl_->import_state(exportedState);
}

void ecco_algorithm::add_power_bond(cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b)
{
    pimpl_->add_power_bond(input_a, output_a, input_b, output_b);
}

std::vector<double> ecco_algorithm::get_powerbond_energies(cosim::simulator_index simulator_index)
{
    return pimpl_->get_powerbond_energies(simulator_index);
}

} // namespace cosim
