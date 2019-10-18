#include "cse/execution.hpp"

#include "cse/algorithm.hpp"
#include "cse/exception.hpp"
#include "cse/slave_simulator.hpp"
#include "cse/timer.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>


namespace cse
{

class execution::impl
{
public:
    impl(time_point startTime, std::shared_ptr<algorithm> algo)
        : lastStep_(0)
        , currentTime_(startTime)
        , initialized_(false)
        , stopped_(true)
        , algorithm_(algo)
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
        std::shared_ptr<async_slave> slave,
        std::string_view name)
    {
        const auto index = static_cast<simulator_index>(simulators_.size());
        simulators_.push_back(std::make_unique<slave_simulator>(slave, name));
        algorithm_->add_simulator(index, simulators_.back().get());

        for (const auto& obs : observers_) {
            obs->simulator_added(index, simulators_.back().get(), currentTime_);
        }
        for (const auto& man : manipulators_) {
            man->simulator_added(index, simulators_.back().get(), currentTime_);
        }
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

    void add_connection(std::shared_ptr<connection> conn)
    {
        for (const auto& destination : conn->get_destinations()) {
            if (find_connection(destination)) {
                std::ostringstream oss;
                oss << "A connection to this destination variable already exists: "
                    << destination;
                throw error(make_error_code(errc::unsupported_feature), oss.str());
            }
            validate_variable(destination, variable_causality::input);
        }
        for (const auto& source : conn->get_sources()) {
            validate_variable(source, variable_causality::output);
        }
        algorithm_->add_connection(conn);
        connections_.push_back(conn);
    }

    void remove_connection(variable_id destination)
    {
        const auto& toRemove = find_connection(destination);
        if (!toRemove) {
            std::ostringstream oss;
            oss << "Can't find connection connected to destination: " << destination;
            throw std::out_of_range(oss.str());
        }
        algorithm_->remove_connection(toRemove);
        connections_.erase(
            std::remove(
                connections_.begin(),
                connections_.end(),
                toRemove),
            connections_.end());
    }

    const std::vector<std::shared_ptr<connection>>& get_connections()
    {
        return connections_;
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

    boost::fibers::future<bool> simulate_until(std::optional<time_point> endTime)
    {
        return boost::fibers::async([=]() {
            stopped_ = false;
            timer_.start(currentTime_);
            duration stepSize;
            do {
                std::cout << "Stepping" << std::endl;
                stepSize = step();
                timer_.sleep(currentTime_);
            } while (!stopped_ && !timed_out(endTime, currentTime_, stepSize));
            return !stopped_;
        });
    }

    void stop_simulation()
    {
        stopped_ = true;
    }

    void enable_real_time_simulation()
    {
        timer_.enable_real_time_simulation();
    }

    void disable_real_time_simulation()
    {
        timer_.disable_real_time_simulation();
    }

    bool is_real_time_simulation()
    {
        return timer_.is_real_time_simulation();
    }

    double get_measured_real_time_factor()
    {
        return timer_.get_measured_real_time_factor();
    }

    void set_real_time_factor_target(double realTimeFactor)
    {
        timer_.set_real_time_factor_target(realTimeFactor);
    }

    double get_real_time_factor_target()
    {
        return timer_.get_real_time_factor_target();
    }

    std::vector<variable_id> get_modified_variables()
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
                << ", causality " << cse::to_text(causality)
                << " and type " << cse::to_text(variable.type)
                << " for simulator with index " << variable.simulator
                << " and name " << simulators_.at(variable.simulator)->name();
            throw std::out_of_range(oss.str());
        }
    }

    std::shared_ptr<connection> find_connection(variable_id destination)
    {
        for (const auto& c : connections_) {
            for (const auto& id : c->get_destinations()) {
                if (id == destination) {
                    return c;
                }
            }
        }
        return nullptr;
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
    bool stopped_;

    std::shared_ptr<algorithm> algorithm_;
    std::vector<std::shared_ptr<simulator>> simulators_;
    std::vector<std::shared_ptr<observer>> observers_;
    std::vector<std::shared_ptr<manipulator>> manipulators_;
    std::vector<std::shared_ptr<connection>> connections_;
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
    std::shared_ptr<async_slave> slave,
    std::string_view name)
{
    return pimpl_->add_slave(std::move(slave), name);
}

void execution::add_observer(std::shared_ptr<observer> obs)
{
    return pimpl_->add_observer(obs);
}

void execution::add_manipulator(std::shared_ptr<manipulator> man)
{
    return pimpl_->add_manipulator(man);
}

void execution::add_connection(std::shared_ptr<connection> conn)
{
    pimpl_->add_connection(conn);
}

void execution::remove_connection(variable_id destination)
{
    pimpl_->remove_connection(destination);
}

const std::vector<std::shared_ptr<connection>>& execution::get_connections()
{
    return pimpl_->get_connections();
}

time_point execution::current_time() const noexcept
{
    return pimpl_->current_time();
}

boost::fibers::future<bool> execution::simulate_until(std::optional<time_point> endTime)
{
    return pimpl_->simulate_until(endTime);
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

void execution::enable_real_time_simulation()
{
    pimpl_->enable_real_time_simulation();
}

void execution::disable_real_time_simulation()
{
    pimpl_->disable_real_time_simulation();
}

bool execution::is_real_time_simulation()
{
    return pimpl_->is_real_time_simulation();
}

double execution::get_measured_real_time_factor()
{
    return pimpl_->get_measured_real_time_factor();
}

void execution::set_real_time_factor_target(double realTimeFactor)
{
    pimpl_->set_real_time_factor_target(realTimeFactor);
}

double execution::get_real_time_factor_target()
{
    return pimpl_->get_real_time_factor_target();
}

std::vector<variable_id> execution::get_modified_variables()
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

} // namespace cse
