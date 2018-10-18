#include <cse/execution.hpp>

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
    impl(time_point startTime, std::shared_ptr<algorithm> algo, std::shared_ptr<real_time_timer> timer)
        : lastStep_(0)
        , currentTime_(startTime)
        , initialized_(false)
        , stopped_(true)
        , algorithm_(algo)
        , timer_(timer)
    {
        algorithm_->setup(currentTime_, std::nullopt);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    simulator_index add_slave(
        std::unique_ptr<async_slave> slave,
        std::string_view name)
    {
        const auto index = static_cast<simulator_index>(simulators_.size());
        simulators_.push_back(
            std::make_unique<slave_simulator>(std::move(slave), name));
        algorithm_->add_simulator(index, simulators_.back().get());

        for (const auto& obs : observers_) {
            obs->simulator_added(index, simulators_.back().get());
        }
        return index;
    }

    observer_index add_observer(std::shared_ptr<observer> obs)
    {
        const auto index = static_cast<observer_index>(observers_.size());
        observers_.push_back(obs);
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            obs->simulator_added(
                static_cast<simulator_index>(i),
                simulators_[i].get());
        }
        for (const auto conn : connections_) {
            obs->variables_connected(conn.second, conn.first);
        }
        return index;
    }

    void connect_variables(variable_id output, variable_id input)
    {
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

    boost::fibers::future<bool> simulate_until(time_point endTime)
    {
        constexpr double relativeTolerance = 0.01;

        return boost::fibers::async([=]() {
            if (!initialized_) {
                algorithm_->initialize();
                initialized_ = true;
            }
            stopped_ = false;
            timer_->start();
            time_duration stepSize;
            do {
                stepSize = algorithm_->do_step(currentTime_, endTime - currentTime_);
                currentTime_ += stepSize;
                ++lastStep_;
                for (const auto& obs : observers_) {
                    obs->step_complete(lastStep_, stepSize, currentTime_);
                }
                timer_->sleep();
            } while (!stopped_ && endTime - currentTime_ > stepSize * relativeTolerance);
            return !stopped_;
        });
    }

    void stop_simulation()
    {
        stopped_ = true;
    }

private:
    step_number lastStep_;
    time_point currentTime_;
    bool initialized_;
    bool stopped_;

    std::shared_ptr<algorithm> algorithm_;
    std::vector<std::unique_ptr<simulator>> simulators_;
    std::vector<std::shared_ptr<observer>> observers_;
    std::unordered_map<variable_id, variable_id> connections_; // (key, value) = (input, output)
    std::shared_ptr<real_time_timer> timer_;
};


execution::execution(time_point startTime, std::shared_ptr<algorithm> algo, std::shared_ptr<real_time_timer> timer)
    : pimpl_(std::make_unique<impl>(startTime, algo, timer))
{
}

execution::~execution() noexcept = default;
execution::execution(execution&& other) noexcept = default;
execution& execution::operator=(execution&& other) noexcept = default;

simulator_index execution::add_slave(
    std::unique_ptr<async_slave> slave,
    std::string_view name)
{
    return pimpl_->add_slave(std::move(slave), name);
}

observer_index execution::add_observer(std::shared_ptr<observer> obs)
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

boost::fibers::future<bool> execution::simulate_until(time_point endTime)
{
    return pimpl_->simulate_until(endTime);
}


} // namespace cse
