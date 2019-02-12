#ifdef _WIN32
#    define NOMINMAX
#endif
#include "cse/algorithm.hpp"
#include "cse/error.hpp"
#include "cse/exception.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>


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

class multi_fixed_step_algorithm::impl
{
public:
    explicit impl(duration baseStepSize)
        : baseStepSize_(baseStepSize)
    {
        CSE_INPUT_CHECK(baseStepSize.count() > 0);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(simulator_index i, simulator* s)
    {
        assert(simulators_.count(i) == 0);
        simulators_[i].sim = s;
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
        simulators_[output.simulator].sim->expose_for_getting(output.type, output.index);
        simulators_[input.simulator].sim->expose_for_setting(input.type, input.index);
        simulators_[output.simulator].outgoingConnections.push_back({output, input});
    }

    void disconnect_variable(variable_id input)
    {
        for (auto& [idx, info] : simulators_) {
            const auto it = std::find_if(
                info.outgoingConnections.begin(),
                info.outgoingConnections.end(),
                [input](const auto& c) { return c.input == input; });
            info.outgoingConnections.erase(it);
        }
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
            for (const auto& [idx, sim] : simulators_) {
                transfer_variables(sim.outgoingConnections);
            }
        }
    }

    duration do_step(time_point currentT)
    {
        for (auto& [idx, info] : simulators_) {
            if (stepCounter_ % info.step_size_multiplier == 0) {
                info.stepResult = info.sim->do_step(currentT, baseStepSize_ * info.step_size_multiplier);
            }
        }

        ++stepCounter_;

        bool failed = false;
        std::stringstream errMessages;
        for (auto& [idx, info] : simulators_) {
            if (stepCounter_ % info.step_size_multiplier == 0) {
                if (info.stepResult.valid()) {
                    if (auto ep = info.stepResult.get_exception_ptr()) {
                        errMessages
                            << info.sim->name() << ": "
                            << exception_ptr_msg(ep) << '\n';
                        failed = true;
                    } else if (info.stepResult.get() != step_result::complete) {
                        errMessages
                            << info.sim->name() << ": "
                            << "Step not complete" << '\n';
                        failed = true;
                    }
                    transfer_variables(info.outgoingConnections);
                }
            }
        }
        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }

        return baseStepSize_;
    }

    void set_simulator_stepsize_multiplier(cse::simulator_index i, int multiplier)
    {
        simulators_.at(i).step_size_multiplier = multiplier;
    }


private:
    struct connection
    {
        variable_id output;
        variable_id input;
    };

    void disconnect_simulator_variables(simulator_index i)
    {
        for (auto& [idx, info] : simulators_) {
            const auto newEnd = std::remove_if(
                info.outgoingConnections.begin(),
                info.outgoingConnections.end(),
                [i](const auto& c) {
                    return c.input.simulator == i;
                });
            info.outgoingConnections.erase(newEnd, info.outgoingConnections.end());
        }
    }

    template<typename F>
    void for_all_simulators(F f)
    {
        std::unordered_map<simulator_index, boost::fibers::future<void>> results;
        for (const auto& s : simulators_) {
            results.emplace(s.first, f(s.second.sim));
        }

        bool failed = false;
        std::stringstream errMessages;
        for (auto& r : results) {
            try {
                r.second.get();
            } catch (const std::exception& e) {
                errMessages
                    << simulators_.at(r.first).sim->name() << ": "
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

    void transfer_variables(const std::vector<connection>& connections)
    {
        for (const auto& c : connections) {
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
        simulators_[input.simulator].sim->set_real(
            input.index,
            simulators_[output.simulator].sim->get_real(output.index));
    }

    void transfer_integer(variable_id output, variable_id input)
    {
        simulators_[input.simulator].sim->set_integer(
            input.index,
            simulators_[output.simulator].sim->get_integer(output.index));
    }

    void transfer_boolean(variable_id output, variable_id input)
    {
        simulators_[input.simulator].sim->set_boolean(
            input.index,
            simulators_[output.simulator].sim->get_boolean(output.index));
    }

    void transfer_string(variable_id output, variable_id input)
    {
        simulators_[input.simulator].sim->set_string(
            input.index,
            simulators_[output.simulator].sim->get_string(output.index));
    }

    struct simulator_info
    {
        simulator* sim;
        int step_size_multiplier = 1;
        boost::fibers::future<step_result> stepResult;
        std::vector<connection> outgoingConnections;
    };

    const duration baseStepSize_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator_info> simulators_;
    int64_t stepCounter_ = 0;
};


multi_fixed_step_algorithm::multi_fixed_step_algorithm(duration stepSize)
    : pimpl_(std::make_unique<impl>(stepSize))
{
}


multi_fixed_step_algorithm::~multi_fixed_step_algorithm() noexcept {}


multi_fixed_step_algorithm::multi_fixed_step_algorithm(multi_fixed_step_algorithm&& other) noexcept
    : pimpl_(std::move(other.pimpl_))
{
}


multi_fixed_step_algorithm& multi_fixed_step_algorithm::operator=(
    multi_fixed_step_algorithm&& other) noexcept
{
    pimpl_ = std::move(other.pimpl_);
    return *this;
}


void multi_fixed_step_algorithm::add_simulator(
    simulator_index i,
    simulator* s)
{
    pimpl_->add_simulator(i, s);
}


void multi_fixed_step_algorithm::remove_simulator(simulator_index i)
{
    pimpl_->remove_simulator(i);
}


void multi_fixed_step_algorithm::connect_variables(
    variable_id output,
    variable_id input,
    bool inputAlreadyConnected)
{
    pimpl_->connect_variables(output, input, inputAlreadyConnected);
}


void multi_fixed_step_algorithm::disconnect_variable(variable_id input)
{
    pimpl_->disconnect_variable(input);
}


void multi_fixed_step_algorithm::setup(
    time_point startTime,
    std::optional<time_point> stopTime)
{
    pimpl_->setup(startTime, stopTime);
}


void multi_fixed_step_algorithm::initialize()
{
    pimpl_->initialize();
}


duration multi_fixed_step_algorithm::do_step(time_point currentT)
{
    return pimpl_->do_step(currentT);
}

void multi_fixed_step_algorithm::set_simulator_stepsize_multiplier(cse::simulator_index i, int multiplier)
{
    pimpl_->set_simulator_stepsize_multiplier(i, multiplier);
}


} // namespace cse
