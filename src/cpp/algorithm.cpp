#if defined(_WIN32) && !defined(NOMINMAX)
#    define NOMINMAX
#endif
#include "cse/algorithm.hpp"

#include "cse/error.hpp"
#include "cse/exception.hpp"

#include <algorithm>
#include <numeric>
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

class fixed_step_algorithm::impl
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
        for (auto& s : simulators_) {
            auto conns = s.second.outgoingConnections;
            const auto it = std::find_if(
                conns.begin(),
                conns.end(),
                [input](const auto& c) { return c.input == input; });
            conns.erase(it);
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
            for (const auto& s : simulators_) {
                transfer_variables(s.second.outgoingConnections);
            }
        }
    }

    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT)
    {
        for (auto& s : simulators_) {
            auto& info = s.second;
            if (stepCounter_ % info.decimationFactor == 0) {
                info.stepResult = info.sim->do_step(currentT, baseStepSize_ * info.decimationFactor);
            }
        }

        ++stepCounter_;

        std::unordered_set<simulator_index> finished;
        bool failed = false;
        std::stringstream errMessages;
        for (auto& [idx, info] : simulators_) {
            if (stepCounter_ % info.decimationFactor == 0) {
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
                finished.insert(idx);
            }
        }
        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }

        for (auto idx : finished) {
            transfer_variables(simulators_.at(idx).outgoingConnections);
        }

        return std::pair(baseStepSize_, std::move(finished));
    }

    void set_stepsize_decimation_factor(cse::simulator_index i, int factor)
    {
        CSE_INPUT_CHECK(factor > 0);
        simulators_.at(i).decimationFactor = factor;
    }


private:
    struct connection
    {
        variable_id output;
        variable_id input;
    };

    struct simulator_info
    {
        simulator* sim;
        int decimationFactor = 1;
        boost::fibers::future<step_result> stepResult;
        std::vector<connection> outgoingConnections;
    };

    void disconnect_simulator_variables(simulator_index i)
    {
        for (auto& s : simulators_) {
            auto conns = s.second.outgoingConnections;
            const auto newEnd = std::remove_if(
                conns.begin(),
                conns.end(),
                [i](const auto& c) {
                    return c.input.simulator == i;
                });
            conns.erase(newEnd, conns.end());
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
            const auto odf = simulators_[c.output.simulator].decimationFactor;
            const auto idf = simulators_[c.input.simulator].decimationFactor;
            if (stepCounter_ % std::lcm(odf, idf) == 0) {
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
                    case variable_type::enumeration:
                        CSE_PANIC();
                }
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

    const duration baseStepSize_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator_info> simulators_;
    int64_t stepCounter_ = 0;
};


fixed_step_algorithm::fixed_step_algorithm(duration baseStepSize)
    : pimpl_(std::make_unique<impl>(baseStepSize))
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


std::pair<duration, std::unordered_set<simulator_index>> fixed_step_algorithm::do_step(
    time_point currentT)
{
    return pimpl_->do_step(currentT);
}

void fixed_step_algorithm::set_stepsize_decimation_factor(cse::simulator_index simulator, int factor)
{
    pimpl_->set_stepsize_decimation_factor(simulator, factor);
}


} // namespace cse
