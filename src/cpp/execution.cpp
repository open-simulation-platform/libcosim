#include <cse/execution.hpp>

#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/functional/hash.hpp>

#include "cse/slave_simulator.hpp"
#include <cse/algorithm.hpp>
#include <cse/timer.hpp>


// Specialisation of std::hash for variable_id
namespace std
{
template<>
class hash<cse::variable_id>
{
public:
    std::size_t operator()(const cse::variable_id& v) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, v.simulator);
        boost::hash_combine(seed, v.type);
        boost::hash_combine(seed, v.index);
        return seed;
    }
};
} // namespace std


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
        return index;
    }

    std::shared_ptr<simulator> get_simulator(simulator_index index)
    {
        return std::shared_ptr<simulator>(simulators_.at(index));
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
        for (const auto conn : connections_) {
            obs->variables_connected(conn.second, conn.first, currentTime_);
        }
    }

    void connect_variables(variable_id output, variable_id input)
    {
        validate_variable(output, variable_causality::output);
        validate_variable(input, variable_causality::input);

        const auto existing = connections_.find(input);
        const auto hasExisting = existing != connections_.end();

        algorithm_->connect_variables(output, input, hasExisting);
        if (hasExisting) {
            existing->second = output;
        } else {
            connections_.emplace(input, output);
        }
    }

    time_point current_time() const noexcept
    {
        return currentTime_;
    }

    bool is_running() const noexcept
    {
        return !stopped_;
    }

    duration step(std::optional<duration> maxDeltaT)
    {
        if (!initialized_) {
            algorithm_->initialize();
            initialized_ = true;
        }
        const auto stepSize = algorithm_->do_step(currentTime_, maxDeltaT);
        currentTime_ += stepSize;
        ++lastStep_;
        for (const auto& obs : observers_) {
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
                auto maxDeltaT = max_delta_t(endTime, currentTime_);
                stepSize = step(maxDeltaT);
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

    double get_real_time_factor()
    {
        return timer_.get_real_time_factor();
    }

private:
    void validate_variable(variable_id variable, variable_causality causality)
    {
        const auto variables = simulators_.at(variable.simulator)->model_description().variables;
        const auto it = std::find_if(
            variables.begin(),
            variables.end(),
            [=](const auto& var) { return var.causality == causality && var.type == variable.type && var.index == variable.index; });
        if (it == variables.end()) {
            std::ostringstream oss;
            oss << "Cannot find variable with index " << variable.index
                << ", causality " << cse::to_text(causality)
                << " and type " << cse::to_text(variable.type)
                << " for simulator with index " << variable.simulator
                << " and name " << simulators_.at(variable.simulator)->name();
            throw std::out_of_range(oss.str());
        }
    }

    static std::optional<duration> max_delta_t(std::optional<time_point> endTime, time_point currentTime)
    {
        if (endTime) {
            return *endTime - currentTime;
        }
        return {};
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
    std::unordered_map<variable_id, variable_id> connections_; // (key, value) = (input, output)
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

std::shared_ptr<simulator> execution::get_simulator(simulator_index index)
{
    return pimpl_->get_simulator(index);
}

void execution::add_observer(std::shared_ptr<observer> obs)
{
    return pimpl_->add_observer(obs);
}

void execution::connect_variables(variable_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

time_point execution::current_time() const noexcept
{
    return pimpl_->current_time();
}

boost::fibers::future<bool> execution::simulate_until(std::optional<time_point> endTime)
{
    return pimpl_->simulate_until(endTime);
}

duration execution::step(std::optional<duration> maxDeltaT)
{
    return pimpl_->step(maxDeltaT);
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

double execution::get_real_time_factor()
{
    return pimpl_->get_real_time_factor();
}


} // namespace cse
