#ifdef _WIN32
#    define NOMINMAX
#endif
#include <cse/algorithm.hpp>

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "cse/error.hpp"
#include <cse/exception.hpp>


namespace
{
std::string exception_ptr_msg(std::exception_ptr ep)
{
    assert(ep);
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception& e) {
        return std::string(e.what());
    } catch (...) {
        return "Unknown error";
    }
}
} // namespace


namespace cse
{

class fixed_step_algorithm::impl
{
public:
    explicit impl(duration stepSize)
        : stepSize_(stepSize)
    {
        CSE_INPUT_CHECK(stepSize.count() > 0);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(simulator_index i, simulator* s)
    {
        assert(simulators_.count(i) == 0);
        simulators_[i] = s;
    }

    void remove_simulator(simulator_index i)
    {
        simulators_.erase(i);
        disconnect_simulator_variables(i);
    }

    void connect_variables(
        variable_id output,
        variable_id input,
        bool inputAlreadyConnected)
    {
        if (inputAlreadyConnected) disconnect_variable(input);
        simulators_[output.simulator]->expose_for_getting(output.type, output.index);
        simulators_[input.simulator]->expose_for_setting(input.type, input.index);
        connections_.push_back({output, input});
    }

    void disconnect_variable(variable_id input)
    {
        const auto it = std::find_if(
            connections_.begin(),
            connections_.end(),
            [input](const auto& c) { return c.input == input; });
        connections_.erase(it);
    }

    void setup(time_point startTime, std::optional<time_point> stopTime)
    {
        startTime_ = startTime;
        stopTime_ = stopTime;
    }

    void initialize()
    {
        setup_simulators();
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            iterate_simulators();
            transfer_variables();
        }
    }

    duration do_step(time_point currentT, std::optional<duration> maxDeltaT)
    {
        const auto currentStepSize = maxDeltaT ? std::min(stepSize_, *maxDeltaT)
                                               : stepSize_;
        step_simulators(currentT, currentStepSize);
        transfer_variables();
        return currentStepSize;
    }

private:
    struct connection
    {
        variable_id output;
        variable_id input;
    };

    void disconnect_simulator_variables(simulator_index i)
    {
        const auto newEnd = std::remove_if(
            connections_.begin(),
            connections_.end(),
            [i](const auto& c) {
                return c.output.simulator == i || c.input.simulator == i;
            });
        connections_.erase(newEnd, connections_.end());
    }

    template<typename F>
    void for_all_simulators(F f)
    {
        std::unordered_map<simulator_index, boost::fibers::future<void>> results;
        for (const auto& s : simulators_) {
            results.emplace(s.first, f(s.second));
        }

        bool failed = false;
        std::stringstream errMessages;
        for (auto& r : results) {
            try {
                r.second.get();
            } catch (const std::exception& e) {
                errMessages
                    << simulators_.at(r.first)->name() << ": "
                    << e.what() << '\n';
                failed = true;
            }
        }
        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }
    }

    void setup_simulators()
    {
        for_all_simulators([=](auto s) {
            return s->setup(startTime_, stopTime_, std::nullopt);
        });
    }

    void iterate_simulators()
    {
        for_all_simulators([=](auto s) {
            return s->do_iteration();
        });
    }

    void step_simulators(time_point currentT, duration deltaT)
    {
        std::unordered_map<simulator_index, boost::fibers::future<step_result>> results;
        for (const auto& s : simulators_) {
            results.emplace(s.first, s.second->do_step(currentT, deltaT));
        }

        bool failed = false;
        std::stringstream errMessages;
        for (auto& r : results) {
            if (auto ep = r.second.get_exception_ptr()) {
                errMessages
                    << simulators_.at(r.first)->name() << ": "
                    << exception_ptr_msg(ep) << '\n';
                failed = true;
            } else if (r.second.get() != step_result::complete) {
                errMessages
                    << simulators_.at(r.first)->name() << ": "
                    << "Step not complete" << '\n';
                failed = true;
            }
        }
        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }
    }

    void transfer_variables()
    {
        for (const auto& c : connections_) {
            switch (c.input.type) {
                case variable_type::real:
                    transfer_real(c.output, c.input);
                    break;
                case variable_type::integer:
                    transfer_integer(c.output, c.input);
                    break;
                case variable_type::boolean:
                    transfer_boolean(c.output, c.input);
                    break;
                case variable_type::string:
                    transfer_string(c.output, c.input);
                    break;
            }
        }
    }

    void transfer_real(variable_id output, variable_id input)
    {
        simulators_[input.simulator]->set_real(
            input.index,
            simulators_[output.simulator]->get_real(output.index));
    }

    void transfer_integer(variable_id output, variable_id input)
    {
        simulators_[input.simulator]->set_integer(
            input.index,
            simulators_[output.simulator]->get_integer(output.index));
    }

    void transfer_boolean(variable_id output, variable_id input)
    {
        simulators_[input.simulator]->set_boolean(
            input.index,
            simulators_[output.simulator]->get_boolean(output.index));
    }

    void transfer_string(variable_id output, variable_id input)
    {
        simulators_[input.simulator]->set_string(
            input.index,
            simulators_[output.simulator]->get_string(output.index));
    }

    const duration stepSize_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator*> simulators_;
    std::vector<connection> connections_;
};


fixed_step_algorithm::fixed_step_algorithm(duration stepSize)
    : pimpl_(std::make_unique<impl>(stepSize))
{
}


fixed_step_algorithm::~fixed_step_algorithm() noexcept {}


fixed_step_algorithm::fixed_step_algorithm(fixed_step_algorithm&& other) noexcept
    : pimpl_(std::move(other.pimpl_))
{
}


fixed_step_algorithm& fixed_step_algorithm::operator=(
    fixed_step_algorithm&& other) noexcept
{
    pimpl_ = std::move(other.pimpl_);
    return *this;
}


void fixed_step_algorithm::add_simulator(
    simulator_index i,
    simulator* s)
{
    pimpl_->add_simulator(i, s);
}


void fixed_step_algorithm::remove_simulator(simulator_index i)
{
    pimpl_->remove_simulator(i);
}


void fixed_step_algorithm::connect_variables(
    variable_id output,
    variable_id input,
    bool inputAlreadyConnected)
{
    pimpl_->connect_variables(output, input, inputAlreadyConnected);
}


void fixed_step_algorithm::disconnect_variable(variable_id input)
{
    pimpl_->disconnect_variable(input);
}


void fixed_step_algorithm::setup(
    time_point startTime,
    std::optional<time_point> stopTime)
{
    pimpl_->setup(startTime, stopTime);
}


void fixed_step_algorithm::initialize()
{
    pimpl_->initialize();
}


duration fixed_step_algorithm::do_step(
    time_point currentT,
    std::optional<duration> maxDeltaT)
{
    return pimpl_->do_step(currentT, maxDeltaT);
}


} // namespace cse
